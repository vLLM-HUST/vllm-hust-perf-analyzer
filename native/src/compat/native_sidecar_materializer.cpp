#include "traceloom/compat/native_sidecar_materializer.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "traceloom/compat/anchor_cost_breakdown_rows.h"
#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/aux_attribution_rows.h"
#include "traceloom/compat/collective_tag_rows.h"
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
  const ReportTree report_tree =
      build_sidecar_report_tree(ir, options, report_tokens);
  if (options.timing_diagnostics) {
    std::cerr << "timing loop_tree_report_tree_ms="
              << tree_watch.elapsed_ms() << "\n";
  }
  const Stopwatch coverage_watch;
  NodeCoverageSqlRows rows = build_report_tree_node_coverage_sql_rows(
      report_tree, report_tokens, aux_rows, options.db_idx);
  if (options.timing_diagnostics) {
    std::cerr << "timing loop_tree_coverage_rows_ms="
              << coverage_watch.elapsed_ms() << "\n";
  }
  return rows;
}

void write_basic_native_compatibility_sidecar(
    const std::string& sqlite_path,
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options) {
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
  const ReportTree report_tree =
      build_sidecar_report_tree(ir, options, report_tokens);
  const NodeCoverageSqlRows node_rows =
      build_report_tree_node_coverage_sql_rows(report_tree, report_tokens,
                                               aux_rows, options.db_idx);
  replace_loop_tree_rows(sqlite_path, split_loop_tree_sql_rows(node_rows));
  const NodeAnchorCoverageSqlRows coverage_rows =
      split_node_anchor_coverage_sql_rows(node_rows);
  replace_node_anchor_coverage_rows(sqlite_path, coverage_rows);
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

  const SemanticTreeSqlRows semantic_rows = build_report_tree_semantic_sql_rows(
      report_tree, report_tokens, aux_rows, options.db_idx);
  replace_semantic_tree_catalog_rows(
      sqlite_path, split_semantic_tree_catalog_sql_rows(semantic_rows));
  replace_semantic_graph_rows(sqlite_path,
                              split_semantic_graph_sql_rows(semantic_rows));
  if (options.materialize_report_views) {
    materialize_report_compatibility_views(sqlite_path);
  }
}

}  // namespace traceloom::compat
