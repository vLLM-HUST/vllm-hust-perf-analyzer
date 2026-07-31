#include "traceloom/compat/native_sidecar_materializer.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "traceloom/compat/anchor_cost_breakdown_rows.h"
#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/aux_attribution_rows.h"
#include "traceloom/compat/collective_tag_rows.h"
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

double ns_to_us(std::int64_t ns) {
  return static_cast<double>(ns) / 1000.0;
}

GraphReplayEvidenceSqlRows build_native_graph_replay_evidence_rows(
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options) {
  GraphReplayEvidenceSqlRows rows;
  std::uint32_t envelope_idx = 0;
  std::unordered_map<std::uint32_t, std::vector<const TraceEventRow*>>
      events_by_device;
  std::unordered_set<TraceEventId::value_type> replay_event_ids;
  for (const TraceEventRow& event : ir.trace_events.rows()) {
    events_by_device[event.device_id].push_back(&event);
  }
  for (auto& entry : events_by_device) {
    std::sort(entry.second.begin(), entry.second.end(),
              [](const TraceEventRow* lhs, const TraceEventRow* rhs) {
                if (lhs->start_ns != rhs->start_ns) {
                  return lhs->start_ns < rhs->start_ns;
                }
                if (lhs->end_ns != rhs->end_ns) {
                  return lhs->end_ns < rhs->end_ns;
                }
                return lhs->id.value() < rhs->id.value();
              });
  }
  for (const ReplayUnitRow& replay_unit : ir.replay_units.rows()) {
    if (replay_unit.launch_trace_event_id.valid()) {
      replay_event_ids.insert(replay_unit.launch_trace_event_id.value());
    }
  }

  for (const ReplayUnitRow& replay_unit : ir.replay_units.rows()) {
    if (!replay_unit.launch_trace_event_id.valid() ||
        replay_unit.launch_trace_event_id.value() >= ir.trace_events.size()) {
      continue;
    }
    const TraceEventRow& graph_event =
        ir.trace_events.row(replay_unit.launch_trace_event_id);
    const GraphTemplateRow& graph_template =
        ir.graph_templates.row(replay_unit.graph_template_id);
    const std::string graph_event_id =
        trace_event_compat_id(graph_event.id);
    const std::string graph_id =
        std::to_string(graph_template.body_sequence_hash);
    const std::string graph_exec_id =
        "native-graph-template-" +
        std::to_string(replay_unit.graph_template_id.value());

    GraphReplaySqlRow replay;
    replay.graph_event_id = graph_event_id;
    replay.db_idx = options.db_idx;
    replay.device_id = graph_event.device_id;
    replay.graph_provider = options.source_kind == "cuda_nsys_sqlite"
                                ? "cuda"
                                : options.source_kind;
    replay.graph_kind = replay.graph_provider + "_graph_replay";
    replay.graph_event_idx = replay_unit.id.value();
    replay.event_id = graph_event_id;
    replay.step_idx = graph_event.id.value();
    replay.stream_id = graph_event.stream_id;
    replay.graph_id = graph_id;
    replay.graph_exec_id = graph_exec_id;
    replay.start_ns = graph_event.start_ns;
    replay.end_ns = graph_event.end_ns;
    replay.dur_us = ns_to_us(graph_event.end_ns - graph_event.start_ns);

    const auto device_events = events_by_device.find(graph_event.device_id);
    if (device_events == events_by_device.end()) {
      rows.graph_replays.push_back(std::move(replay));
      continue;
    }
    auto child_it = std::lower_bound(
        device_events->second.begin(), device_events->second.end(),
        graph_event.start_ns,
        [](const TraceEventRow* event, std::int64_t start_ns) {
          return event->start_ns < start_ns;
        });
    for (; child_it != device_events->second.end() &&
           (*child_it)->start_ns <= graph_event.end_ns;
         ++child_it) {
      const TraceEventRow& child = **child_it;
      if (replay_event_ids.find(child.id.value()) != replay_event_ids.end() ||
          child.end_ns > graph_event.end_ns) {
        continue;
      }
      const SourceRefRow& child_source = ir.source_refs.row(child.source_ref_id);
      const bool is_kernel =
          child_source.table_name == "CUPTI_ACTIVITY_KIND_KERNEL";
      ++replay.enclosed_event_count;
      replay.enclosed_event_us += ns_to_us(child.end_ns - child.start_ns);
      if (is_kernel) {
        ++replay.enclosed_kernel_count;
        replay.enclosed_kernel_us += ns_to_us(child.end_ns - child.start_ns);
      }

      GraphEnvelopeSqlRow envelope;
      envelope.envelope_id =
          "native-graph-envelope-" + std::to_string(envelope_idx);
      envelope.db_idx = options.db_idx;
      envelope.device_id = graph_event.device_id;
      envelope.graph_provider = replay.graph_provider;
      envelope.graph_kind = replay.graph_kind;
      envelope.envelope_idx = envelope_idx++;
      envelope.graph_event_id = graph_event_id;
      envelope.child_event_id = trace_event_compat_id(child.id);
      envelope.graph_step_idx = graph_event.id.value();
      envelope.child_step_idx = child.id.value();
      envelope.relation = "contains";
      envelope.stream_relation =
          graph_event.stream_id == child.stream_id ? "same_stream"
                                                    : "cross_stream";
      envelope.graph_id = graph_id;
      envelope.graph_exec_id = graph_exec_id;
      envelope.graph_start_ns = graph_event.start_ns;
      envelope.graph_end_ns = graph_event.end_ns;
      envelope.child_start_ns = child.start_ns;
      envelope.child_end_ns = child.end_ns;
      envelope.start_offset_us =
          ns_to_us(child.start_ns - graph_event.start_ns);
      envelope.end_offset_us =
          ns_to_us(graph_event.end_ns - child.end_ns);
      envelope.child_dur_us = ns_to_us(child.end_ns - child.start_ns);
      rows.graph_envelopes.push_back(std::move(envelope));
    }
    rows.graph_replays.push_back(std::move(replay));
  }
  return rows;
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
  };

  replace_metadata_rows(sqlite_path, metadata);
  const EventSqlRows event_rows = build_timeline_sql_rows(ir, options.db_idx);
  replace_timeline_rows(sqlite_path,
                        split_timeline_event_sql_rows(event_rows));
  replace_event_source_rows(sqlite_path,
                            split_source_lineage_sql_rows(event_rows));
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
  replace_graph_replay_evidence_rows(
      sqlite_path, build_native_graph_replay_evidence_rows(ir, options));

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
