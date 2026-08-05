#include "traceloom/compat/native_sidecar_materializer.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "traceloom/compat/anchor_cost_breakdown_rows.h"
#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/aux_attribution_rows.h"
#include "traceloom/compat/collective_tag_rows.h"
#include "traceloom/compat/idle_evidence_sql_rows.h"
#include "traceloom/compat/idle_explanation_rows.h"
#include "traceloom/compat/native_graph_replay_rows.h"
#include "traceloom/compat/report_tree_rows.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/compat/timeline_rows.h"
#include "traceloom/pattern/grammar_engine.h"
#include "traceloom/pattern/grammar_state.h"
#include "traceloom/report/report_tree_builder.h"

namespace traceloom::compat {
namespace {

class Stopwatch {
 public:
  Stopwatch() : start_(Clock::now()) {}

  double elapsed_ms() const {
    return std::chrono::duration<double, std::milli>(Clock::now() - start_)
        .count();
  }

 private:
  using Clock = std::chrono::steady_clock;
  Clock::time_point start_;
};

std::string basename_or_default(const std::string& path,
                                const std::string& fallback) {
  if (path.empty()) {
    return fallback;
  }
  const std::string::size_type pos = path.find_last_of("/\\");
  if (pos == std::string::npos) {
    return path.empty() ? fallback : path;
  }
  const std::string value = path.substr(pos + 1);
  return value.empty() ? fallback : value;
}

ReportTree build_sidecar_report_tree(
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options,
    const std::vector<ReportToken>& report_tokens) {
  if (!options.materialize_grammar_report_tree || report_tokens.empty()) {
    return build_report_tree_from_tokens(report_tokens);
  }

  try {
    GrammarStateConfig grammar_state_config;
    grammar_state_config.target_nodes_per_chunk =
        options.grammar_target_nodes_per_chunk;
    grammar_state_config.worker_count = options.grammar_worker_count;
    grammar_state_config.full_discovery_cap =
        options.grammar_full_discovery_cap;

    GlobalGrammarState grammar_state =
        build_initial_grammar_state(ir, grammar_state_config);
    GrammarEngineConfig grammar_engine_config;
    grammar_engine_config.full_discovery_cap =
        grammar_state.metadata.full_discovery_cap;
    const GrammarEngineResult grammar_result =
        run_grammar_state_machine(grammar_state, grammar_engine_config);
    if (options.timing_diagnostics) {
      std::cerr << "timing loop_tree_grammar_stop_reason="
                << grammar_engine_stop_reason_name(grammar_result.stop_reason)
                << "\n";
      std::cerr << "timing loop_tree_grammar_steps="
                << grammar_result.steps.size() << "\n";
      std::cerr << "timing loop_tree_grammar_live_nodes="
                << grammar_state.live_node_count << "\n";
      std::cerr << "timing loop_tree_grammar_macro_defs="
                << grammar_state.macro_defs.size() << "\n";
      if (!grammar_result.steps.empty()) {
        const GrammarEngineStep& last_step = grammar_result.steps.back();
        std::cerr << "timing loop_tree_grammar_last_before_nodes="
                  << last_step.before_live_node_count << "\n";
        std::cerr << "timing loop_tree_grammar_last_after_nodes="
                  << last_step.after_live_node_count << "\n";
        std::cerr << "timing loop_tree_grammar_last_gain="
                  << last_step.gain << "\n";
        std::cerr << "timing loop_tree_grammar_last_replace_count="
                  << last_step.replace_count << "\n";
      }
    }
    if (!grammar_result.ok() || grammar_state.stage != GrammarStage::kDone ||
        grammar_state.macro_defs.empty()) {
      return build_report_tree_from_tokens(report_tokens);
    }
    return build_report_tree_from_grammar_state(report_tokens, grammar_state);
  } catch (const std::exception&) {
    return build_report_tree_from_tokens(report_tokens);
  }
}

struct NativeReportLane {
  std::uint32_t device_id = 0;
  std::vector<ReportToken> tokens;
  ReportTree tree;
};

NativeIr build_token_only_lane_ir(const std::vector<ReportToken>& tokens) {
  NativeIr ir;
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    const ReportToken& token = tokens[index];
    ir.tokens.append(token.anchor_id, token.symbol_id, token.device_id,
                     static_cast<std::uint32_t>(index), token.start_ns,
                     token.end_ns);
  }
  return ir;
}

void mark_exact_graph_unit_tokens(const NativeIr& ir,
                                  std::vector<ReportToken>& tokens) {
  for (ReportToken& token : tokens) {
    if (token.anchor_kind != ReportAnchorKind::kGraphTemplate ||
        !token.anchor_id.valid() ||
        token.anchor_id.value() >= ir.anchors.size()) {
      continue;
    }
    const AnchorRow& anchor = ir.anchors.row(token.anchor_id);
    if (!anchor.replay_unit_id.valid() ||
        anchor.replay_unit_id.value() >= ir.replay_units.size()) {
      continue;
    }
    const ReplayUnitRow& replay = ir.replay_units.row(anchor.replay_unit_id);
    if (replay.replay_composition_region_id.valid()) {
      token.display_category = "graph_unit";
    }
  }
}

std::vector<NativeReportLane> build_sidecar_report_lanes(
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options,
    const std::vector<ReportToken>& report_tokens) {
  std::map<std::uint32_t, std::vector<ReportToken>> tokens_by_device;
  for (const ReportToken& token : report_tokens) {
    std::vector<ReportToken>& lane = tokens_by_device[token.device_id];
    lane.push_back(token);
    lane.back().ordinal = static_cast<std::uint32_t>(lane.size() - 1);
  }

  std::vector<NativeReportLane> lanes;
  lanes.reserve(tokens_by_device.size());
  const bool multiple_devices = tokens_by_device.size() > 1;
  for (auto& item : tokens_by_device) {
    NativeReportLane lane;
    lane.device_id = item.first;
    lane.tokens = std::move(item.second);
    if (multiple_devices) {
      // Grammar discovery is intentionally lane-local.  A time-interleaved
      // multi-device projection is not one execution sequence, and treating
      // it as one can invent patterns that exist on neither device.  The
      // token-only IR preserves the observed symbols and order without
      // importing workload- or parallelism-specific semantics.
      mark_exact_graph_unit_tokens(ir, lane.tokens);
      const NativeIr lane_ir = build_token_only_lane_ir(lane.tokens);
      lane.tree = build_sidecar_report_tree(lane_ir, options, lane.tokens);
    } else {
      // Preserve the exact single-device semantic ReplayUnit lowering.
      lane.tree = build_sidecar_report_tree(ir, options, lane.tokens);
    }
    lanes.push_back(std::move(lane));
  }
  return lanes;
}

std::string device_node_prefix(std::uint32_t device_id) {
  return "device" + std::to_string(device_id) + "-";
}

void namespace_node_coverage_rows(NodeCoverageSqlRows& rows,
                                  std::uint32_t device_id) {
  const std::string prefix = device_node_prefix(device_id);
  for (VizNodeSqlRow& row : rows.nodes) {
    row.node_id = prefix + row.node_id;
  }
  for (VizEdgeSqlRow& row : rows.edges) {
    row.parent_node_id = prefix + row.parent_node_id;
    row.child_node_id = prefix + row.child_node_id;
  }
  for (LoopNodeSqlRow& row : rows.loop_nodes) {
    row.node_id = prefix + row.node_id;
  }
  for (VizNodeAnchorSqlRow& row : rows.node_anchors) {
    row.node_id = prefix + row.node_id;
  }
  for (AnchorPrimaryNodeSqlRow& row : rows.anchor_primary_nodes) {
    row.node_id = prefix + row.node_id;
  }

  const std::string unit_prefix = "D" + std::to_string(device_id) + "-";
  for (StructuralUnitSqlRow& row : rows.structural_units) {
    row.unit_id = unit_prefix + row.unit_id;
    row.family_id = unit_prefix + row.family_id;
    std::size_t cursor = 0;
    while ((cursor = row.expansion_nodes.find("node-", cursor)) !=
           std::string::npos) {
      row.expansion_nodes.insert(cursor, prefix);
      cursor += prefix.size() + 5;
    }
  }
  for (StructuralUnitAnchorSqlRow& row : rows.structural_unit_anchors) {
    row.unit_id = unit_prefix + row.unit_id;
  }
}

void append_node_coverage_rows(NodeCoverageSqlRows& out,
                               NodeCoverageSqlRows rows) {
  out.nodes.insert(out.nodes.end(),
                   std::make_move_iterator(rows.nodes.begin()),
                   std::make_move_iterator(rows.nodes.end()));
  out.edges.insert(out.edges.end(),
                   std::make_move_iterator(rows.edges.begin()),
                   std::make_move_iterator(rows.edges.end()));
  out.node_anchors.insert(out.node_anchors.end(),
                          std::make_move_iterator(rows.node_anchors.begin()),
                          std::make_move_iterator(rows.node_anchors.end()));
  out.anchor_primary_nodes.insert(
      out.anchor_primary_nodes.end(),
      std::make_move_iterator(rows.anchor_primary_nodes.begin()),
      std::make_move_iterator(rows.anchor_primary_nodes.end()));
  out.loop_nodes.insert(out.loop_nodes.end(),
                        std::make_move_iterator(rows.loop_nodes.begin()),
                        std::make_move_iterator(rows.loop_nodes.end()));
  out.structural_units.insert(
      out.structural_units.end(),
      std::make_move_iterator(rows.structural_units.begin()),
      std::make_move_iterator(rows.structural_units.end()));
  out.structural_unit_anchors.insert(
      out.structural_unit_anchors.end(),
      std::make_move_iterator(rows.structural_unit_anchors.begin()),
      std::make_move_iterator(rows.structural_unit_anchors.end()));
}

NodeCoverageSqlRows build_lane_node_coverage_rows(
    const std::vector<NativeReportLane>& lanes,
    const AuxAttributionSqlRows& aux_rows,
    std::uint32_t db_idx) {
  NodeCoverageSqlRows out;
  const bool multiple_devices = lanes.size() > 1;
  for (const NativeReportLane& lane : lanes) {
    NodeCoverageSqlRows rows = build_report_tree_node_coverage_sql_rows(
        lane.tree, lane.tokens, aux_rows, db_idx);
    if (multiple_devices) {
      namespace_node_coverage_rows(rows, lane.device_id);
    }
    append_node_coverage_rows(out, std::move(rows));
  }
  return out;
}

void namespace_semantic_rows(SemanticTreeSqlRows& rows,
                             std::uint32_t device_id) {
  const std::string prefix = device_node_prefix(device_id);
  for (SemanticTreeHeaderSqlRow& row : rows.trees) {
    row.tree_id += "-device" + std::to_string(device_id);
    row.root_node_id = prefix + row.root_node_id;
  }
  for (SemanticNodeSqlRow& row : rows.nodes) {
    row.tree_id += "-device" + std::to_string(device_id);
    row.node_id = prefix + row.node_id;
    if (!row.parent_node_id.empty()) {
      row.parent_node_id = prefix + row.parent_node_id;
    }
  }
  for (SemanticEdgeSqlRow& row : rows.edges) {
    row.tree_id += "-device" + std::to_string(device_id);
    row.parent_node_id = prefix + row.parent_node_id;
    row.child_node_id = prefix + row.child_node_id;
  }
}

SemanticTreeSqlRows build_lane_semantic_rows(
    const std::vector<NativeReportLane>& lanes,
    const AuxAttributionSqlRows& aux_rows,
    std::uint32_t db_idx) {
  SemanticTreeSqlRows out;
  const bool multiple_devices = lanes.size() > 1;
  for (const NativeReportLane& lane : lanes) {
    SemanticTreeSqlRows rows = build_report_tree_semantic_sql_rows(
        lane.tree, lane.tokens, aux_rows, db_idx, "tree-1", "anchor_tree");
    if (multiple_devices) {
      namespace_semantic_rows(rows, lane.device_id);
    }
    out.trees.insert(out.trees.end(),
                     std::make_move_iterator(rows.trees.begin()),
                     std::make_move_iterator(rows.trees.end()));
    out.nodes.insert(out.nodes.end(),
                     std::make_move_iterator(rows.nodes.begin()),
                     std::make_move_iterator(rows.nodes.end()));
    out.edges.insert(out.edges.end(),
                     std::make_move_iterator(rows.edges.begin()),
                     std::make_move_iterator(rows.edges.end()));
  }
  return out;
}

}  // namespace

NodeCoverageSqlRows build_native_loop_tree_node_coverage_rows(
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options) {
  const Stopwatch tokens_watch;
  const std::vector<ReportToken> report_tokens =
      build_report_tokens_from_native_ir(ir);
  if (options.timing_diagnostics) {
    std::cerr << "timing loop_tree_tokens_ms=" << tokens_watch.elapsed_ms()
              << "\n";
  }
  const Stopwatch aux_watch;
  const AuxAttributionSqlRows aux_rows =
      options.materialize_aux_attribution
          ? build_aux_attribution_sql_rows(ir, options.db_idx)
          : AuxAttributionSqlRows{};
  if (options.timing_diagnostics) {
    std::cerr << "timing loop_tree_aux_rows_ms=" << aux_watch.elapsed_ms()
              << "\n";
  }
  const Stopwatch tree_watch;
  const std::vector<NativeReportLane> report_lanes =
      build_sidecar_report_lanes(ir, options, report_tokens);
  if (options.timing_diagnostics) {
    std::cerr << "timing loop_tree_report_tree_ms="
              << tree_watch.elapsed_ms() << "\n";
  }
  const Stopwatch coverage_watch;
  NodeCoverageSqlRows rows =
      build_lane_node_coverage_rows(report_lanes, aux_rows, options.db_idx);
  if (options.timing_diagnostics) {
    std::cerr << "timing loop_tree_coverage_rows_ms="
              << coverage_watch.elapsed_ms() << "\n";
  }
  return rows;
}

void write_basic_native_compatibility_sidecar(
    const std::string& sqlite_path,
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options,
    const IdleEvidencePipelineResult* idle_evidence) {
  std::vector<MetadataSqlRow> metadata{
      {"traceloom_schema_version", "augmented_db_v1"},
      {"native_compatibility_materializer", "basic_native_ir_v1"},
      {"source_kind", options.source_kind},
      {"source_path", options.source_path},
      {"trace_event_count", std::to_string(ir.trace_events.size())},
      {"anchor_count", std::to_string(ir.anchors.size())},
      {"graph_template_count", std::to_string(ir.graph_templates.size())},
      {"replay_unit_count", std::to_string(ir.replay_units.size())},
      {"replay_composition_region_count",
       std::to_string(ir.replay_composition_regions.size())},
      {"unrecognized_replay_composition_region_count",
       std::to_string(std::count_if(
           ir.replay_composition_regions.rows().begin(),
           ir.replay_composition_regions.rows().end(),
           [](const ReplayCompositionRegionRow& region) {
             return region.status != ReplayCompositionRegionStatus::
                                         kRecognizedCompletePattern;
           }))},
  };

  replace_metadata_rows(sqlite_path, metadata);
  const EventSqlRows event_rows = build_timeline_sql_rows(ir, options.db_idx);
  replace_timeline_rows(sqlite_path,
                        split_timeline_event_sql_rows(event_rows));
  replace_event_source_rows(sqlite_path,
                            split_source_lineage_sql_rows(event_rows));
  replace_graph_replay_evidence_rows(
      sqlite_path,
      build_native_graph_replay_evidence_sql_rows(
          ir, options.source_kind, options.db_idx));
  const std::vector<AnchorSqlRow> anchor_rows =
      build_anchor_sequence_sql_rows(ir, options.db_idx);
  replace_anchor_rows(sqlite_path, anchor_rows);
  const AuxAttributionSqlRows aux_rows =
      options.materialize_aux_attribution
          ? build_aux_attribution_sql_rows(ir, options.db_idx)
          : AuxAttributionSqlRows{};
  replace_aux_attribution_rows(sqlite_path, aux_rows);
  replace_anchor_cost_breakdown_rows(
      sqlite_path, build_native_anchor_cost_breakdown_sql_rows(ir, aux_rows));
  const std::vector<ReportToken> report_tokens =
      build_report_tokens_from_native_ir(ir);
  const std::vector<NativeReportLane> report_lanes =
      build_sidecar_report_lanes(ir, options, report_tokens);
  const NodeCoverageSqlRows node_rows =
      build_lane_node_coverage_rows(report_lanes, aux_rows, options.db_idx);
  replace_loop_tree_rows(sqlite_path, split_loop_tree_sql_rows(node_rows));
  const NodeAnchorCoverageSqlRows coverage_rows =
      split_node_anchor_coverage_sql_rows(node_rows);
  replace_node_anchor_coverage_rows(sqlite_path, coverage_rows);
  if (idle_evidence != nullptr) {
    const IdleExplanationAttributionRows attribution =
        build_idle_explanation_attribution_rows(
            report_tokens, idle_evidence->idle_explanations, node_rows,
            options.db_idx);
    IdleEvidenceSqlRowOptions idle_options;
    idle_options.db_idx = options.db_idx;
    idle_options.source_kind = options.source_kind;
    idle_options.source_path = options.source_path;
    replace_idle_evidence_rows(
        sqlite_path,
        build_idle_evidence_sql_rows(ir, *idle_evidence, attribution,
                                     idle_options));
  } else {
    // Replacement semantics matter when an existing sidecar is regenerated
    // for a provider whose idle taxonomy is not enabled.
    replace_idle_evidence_rows(sqlite_path, IdleEvidenceSqlRows{});
  }
  if (options.materialize_collective_tags) {
    CollectiveTagMemberInput member;
    member.db_name = options.collective_db_name.empty()
                         ? basename_or_default(sqlite_path, "native_sidecar.db")
                         : options.collective_db_name;
    member.db_idx = options.db_idx;
    member.events = split_timeline_event_sql_rows(event_rows);
    member.anchors = anchor_rows;
    member.loop_tree = split_loop_tree_sql_rows(node_rows);
    member.node_anchor_coverage = coverage_rows;

    CollectiveTagOptions tag_options;
    tag_options.run_name =
        options.collective_run_name.empty()
            ? basename_or_default(options.source_path, "traceloom_run")
            : options.collective_run_name;
    tag_options.expected_world_size = options.collective_expected_world_size;
    CollectiveTagSqlRows collective_rows =
        build_collective_tag_sql_rows({member}, tag_options);
    CollectiveTagSqlRows graph_body_collective_rows =
        build_graph_body_collective_tag_sql_rows(
            ir, member.db_name, options.db_idx, tag_options);
    collective_rows.local_links.insert(
        collective_rows.local_links.end(),
        std::make_move_iterator(
            graph_body_collective_rows.local_links.begin()),
        std::make_move_iterator(
            graph_body_collective_rows.local_links.end()));
    std::stable_sort(
        collective_rows.local_links.begin(), collective_rows.local_links.end(),
        [](const CollectiveGlobalLinkSqlRow& lhs,
           const CollectiveGlobalLinkSqlRow& rhs) {
          return std::make_tuple(lhs.candidate_collective_key, lhs.device_id,
                                 lhs.start_ns, lhs.event_id) <
                 std::make_tuple(rhs.candidate_collective_key, rhs.device_id,
                                 rhs.start_ns, rhs.event_id);
        });
    replace_collective_global_link_rows(sqlite_path,
                                        collective_rows.local_links);
  }

  const SemanticTreeSqlRows semantic_rows =
      build_lane_semantic_rows(report_lanes, aux_rows, options.db_idx);
  replace_semantic_tree_catalog_rows(
      sqlite_path, split_semantic_tree_catalog_sql_rows(semantic_rows));
  replace_semantic_graph_rows(sqlite_path,
                              split_semantic_graph_sql_rows(semantic_rows));
  if (options.materialize_report_views) {
    materialize_report_compatibility_views(sqlite_path);
  }
}

}  // namespace traceloom::compat
