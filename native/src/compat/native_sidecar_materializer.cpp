#include "traceloom/compat/native_sidecar_materializer.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
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

// Projects the report-relevant IR tables onto a single device. The token
// table is filtered to the device with dense TokenIds and a renumbered
// sequence_index (the grammar state machine requires both). Anchor, event,
// task, communication-op, symbol, source-ref, and semantic replay tables are
// copied unchanged so original ids stay valid everywhere; per-device tokens
// reference original anchor ids, which keeps compat anchor ids and anchor
// indices consistent with the global sidecar tables. Protected intervals are
// kept only when their whole token span belongs to the device; a span that
// crosses devices fails closed because cross-device replay units are not
// supported by the structural report.
NativeIr project_ir_for_device(const NativeIr& ir, std::uint32_t device_id) {
  NativeIr out;
  out.symbols = ir.symbols;
  out.source_refs = ir.source_refs;
  out.trace_events = ir.trace_events;
  out.tasks = ir.tasks;
  out.communication_ops = ir.communication_ops;
  out.anchors = ir.anchors;
  out.graph_templates = ir.graph_templates;
  out.replay_composition_candidates = ir.replay_composition_candidates;
  out.replay_composition_regions = ir.replay_composition_regions;
  out.replay_units = ir.replay_units;

  std::vector<std::uint32_t> token_devices(ir.tokens.size(), 0);
  for (std::size_t index = 0; index < ir.tokens.size(); ++index) {
    token_devices[index] = ir.tokens.rows()[index].device_id;
  }
  std::unordered_map<TokenId::value_type, TokenId::value_type> token_remap;
  token_remap.reserve(ir.tokens.size());
  for (const TokenRow& token : ir.tokens.rows()) {
    if (token.anchor_id.value() >= ir.anchors.size()) {
      throw std::invalid_argument("TokenRow anchor_id is out of range");
    }
    const AnchorRow& anchor = ir.anchors.row(token.anchor_id);
    if (anchor.device_id != token.device_id) {
      throw std::invalid_argument(
          "TokenRow device_id disagrees with its AnchorRow device_id");
    }
    if (anchor.device_id != device_id) {
      continue;
    }
    token_remap.emplace(token.id.value(), out.tokens.size());
    out.tokens.append(token.anchor_id, token.symbol_id, device_id,
                      static_cast<std::uint32_t>(out.tokens.size()),
                      token.start_ns, token.end_ns);
  }
  if (out.tokens.empty()) {
    throw std::invalid_argument(
        "device " + std::to_string(device_id) +
        " has no report tokens to project");
  }

  for (const ProtectedIntervalRow& interval : ir.protected_intervals.rows()) {
    if (interval.first_token_id.value() > interval.last_token_id.value()) {
      throw std::invalid_argument(
          "protected interval has an inverted token span");
    }
    if (interval.last_token_id.value() >= token_devices.size()) {
      throw std::invalid_argument(
          "protected interval references an out-of-range token");
    }
    const std::uint32_t interval_device =
        token_devices[interval.first_token_id.value()];
    if (interval_device !=
        token_devices[interval.last_token_id.value()]) {
      throw std::invalid_argument(
          "protected interval spans devices; cross-device replay units are "
          "unsupported in the structural report");
    }
    if (interval_device != device_id) {
      continue;
    }
    for (TokenId::value_type token_id = interval.first_token_id.value();
         token_id <= interval.last_token_id.value(); ++token_id) {
      if (token_devices[token_id] != interval_device) {
        throw std::invalid_argument(
            "protected interval spans devices; cross-device replay units are "
            "unsupported in the structural report");
      }
    }
    const auto first_found = token_remap.find(interval.first_token_id.value());
    const auto last_found = token_remap.find(interval.last_token_id.value());
    if (first_found == token_remap.end() || last_found == token_remap.end()) {
      throw std::invalid_argument(
          "protected interval token span is not present in its device "
          "projection");
    }
    out.protected_intervals.append(
        interval.kind, interval.boundary_policy, TokenId(first_found->second),
        TokenId(last_found->second), interval.first_anchor_id,
        interval.last_anchor_id, interval.evidence_source_ref_id);
  }
  return out;
}

}  // namespace

std::vector<NativeDeviceReportTree> build_native_device_report_trees(
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options) {
  const Stopwatch tokens_watch;
  const std::vector<NativeReportDevicePartition> partitions =
      partition_report_tokens_by_device(ir);
  if (options.timing_diagnostics) {
    std::cerr << "timing loop_tree_tokens_ms=" << tokens_watch.elapsed_ms()
              << "\n";
    std::cerr << "timing loop_tree_device_count=" << partitions.size()
              << "\n";
  }
  std::vector<NativeDeviceReportTree> device_trees;
  device_trees.reserve(partitions.size());
  for (const NativeReportDevicePartition& partition : partitions) {
    const Stopwatch tree_watch;
    NativeDeviceReportTree device;
    device.device_id = partition.device_id;
    device.tokens = partition.tokens;
    if (partitions.size() == 1) {
      device.tree = build_sidecar_report_tree(ir, options, partition.tokens);
    } else {
      const NativeIr projection =
          project_ir_for_device(ir, partition.device_id);
      device.tree =
          build_sidecar_report_tree(projection, options, partition.tokens);
    }
    if (options.timing_diagnostics) {
      std::cerr << "timing loop_tree_report_tree_ms_device="
                << partition.device_id << " " << tree_watch.elapsed_ms()
                << "\n";
    }
    device_trees.push_back(std::move(device));
  }
  return device_trees;
}

NodeCoverageSqlRows build_native_loop_tree_node_coverage_rows(
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options) {
  const std::vector<NativeDeviceReportTree> device_trees =
      build_native_device_report_trees(ir, options);
  const Stopwatch aux_watch;
  const AuxAttributionSqlRows aux_rows =
      options.materialize_aux_attribution
          ? build_aux_attribution_sql_rows(ir, options.db_idx)
          : AuxAttributionSqlRows{};
  if (options.timing_diagnostics) {
    std::cerr << "timing loop_tree_aux_rows_ms=" << aux_watch.elapsed_ms()
              << "\n";
  }
  const Stopwatch coverage_watch;
  const bool scope_node_ids = device_trees.size() > 1;
  NodeCoverageSqlRows rows;
  for (const NativeDeviceReportTree& device : device_trees) {
    NodeCoverageSqlRows device_rows = build_report_tree_node_coverage_sql_rows(
        device.tree, device.tokens, aux_rows, options.db_idx,
        "native_report_tree", scope_node_ids);
    rows.nodes.insert(rows.nodes.end(), device_rows.nodes.begin(),
                      device_rows.nodes.end());
    rows.edges.insert(rows.edges.end(), device_rows.edges.begin(),
                      device_rows.edges.end());
    rows.loop_nodes.insert(rows.loop_nodes.end(),
                           device_rows.loop_nodes.begin(),
                           device_rows.loop_nodes.end());
    rows.node_anchors.insert(rows.node_anchors.end(),
                             device_rows.node_anchors.begin(),
                             device_rows.node_anchors.end());
    rows.anchor_primary_nodes.insert(
        rows.anchor_primary_nodes.end(),
        device_rows.anchor_primary_nodes.begin(),
        device_rows.anchor_primary_nodes.end());
  }
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
  const std::vector<NativeDeviceReportTree> device_trees =
      build_native_device_report_trees(ir, options);
  const bool scope_node_ids = device_trees.size() > 1;
  NodeCoverageSqlRows node_rows;
  SemanticTreeSqlRows semantic_rows;
  for (const NativeDeviceReportTree& device : device_trees) {
    const NodeCoverageSqlRows device_node_rows =
        build_report_tree_node_coverage_sql_rows(
            device.tree, device.tokens, aux_rows, options.db_idx,
            "native_report_tree", scope_node_ids);
    node_rows.nodes.insert(node_rows.nodes.end(),
                           device_node_rows.nodes.begin(),
                           device_node_rows.nodes.end());
    node_rows.edges.insert(node_rows.edges.end(),
                           device_node_rows.edges.begin(),
                           device_node_rows.edges.end());
    node_rows.loop_nodes.insert(node_rows.loop_nodes.end(),
                                device_node_rows.loop_nodes.begin(),
                                device_node_rows.loop_nodes.end());
    node_rows.node_anchors.insert(node_rows.node_anchors.end(),
                                  device_node_rows.node_anchors.begin(),
                                  device_node_rows.node_anchors.end());
    node_rows.anchor_primary_nodes.insert(
        node_rows.anchor_primary_nodes.end(),
        device_node_rows.anchor_primary_nodes.begin(),
        device_node_rows.anchor_primary_nodes.end());

    const std::string tree_id =
        device_trees.size() == 1
            ? "native-report-tree"
            : "native-report-tree-d" + std::to_string(device.device_id);
    const SemanticTreeSqlRows device_semantic_rows =
        build_report_tree_semantic_sql_rows(
            device.tree, device.tokens, aux_rows, options.db_idx, tree_id,
            "anchor_tree", scope_node_ids);
    semantic_rows.trees.insert(semantic_rows.trees.end(),
                               device_semantic_rows.trees.begin(),
                               device_semantic_rows.trees.end());
    semantic_rows.nodes.insert(semantic_rows.nodes.end(),
                               device_semantic_rows.nodes.begin(),
                               device_semantic_rows.nodes.end());
    semantic_rows.edges.insert(semantic_rows.edges.end(),
                               device_semantic_rows.edges.begin(),
                               device_semantic_rows.edges.end());
  }
  replace_loop_tree_rows(sqlite_path, split_loop_tree_sql_rows(node_rows));
  const NodeAnchorCoverageSqlRows coverage_rows =
      split_node_anchor_coverage_sql_rows(node_rows);
  replace_node_anchor_coverage_rows(sqlite_path, coverage_rows);
  if (idle_evidence != nullptr) {
    const IdleExplanationAttributionRows attribution =
        build_idle_explanation_attribution_rows(
            build_report_tokens_from_native_ir(ir),
            idle_evidence->idle_explanations, node_rows, options.db_idx);
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
    const CollectiveTagSqlRows collective_rows =
        build_collective_tag_sql_rows({member}, tag_options);
    replace_collective_global_link_rows(sqlite_path,
                                        collective_rows.local_links);
  }

  replace_semantic_tree_catalog_rows(
      sqlite_path, split_semantic_tree_catalog_sql_rows(semantic_rows));
  replace_semantic_graph_rows(sqlite_path,
                              split_semantic_graph_sql_rows(semantic_rows));
  if (options.materialize_report_views) {
    materialize_report_compatibility_views(sqlite_path);
  }
}

}  // namespace traceloom::compat
