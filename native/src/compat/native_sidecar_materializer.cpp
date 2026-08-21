#include "traceloom/compat/native_sidecar_materializer.h"

#include "augmented_catalog_materializer.h"
#include "compact_grammar_projection.h"
#include "native_sidecar_packaging.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "traceloom/analysis/structural_occurrence_builder.h"
#include "traceloom/compat/anchor_cost_breakdown_rows.h"
#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/aux_attribution_rows.h"
#include "traceloom/compat/collective_tag_rows.h"
#include "traceloom/compat/evidence_role_sql_rows.h"
#include "traceloom/compat/event_reconciliation_rows.h"
#include "traceloom/compat/exact_graph_sql_rows.h"
#include "traceloom/compat/native_graph_replay_rows.h"
#include "traceloom/compat/replay_body_pattern_rows.h"
#include "traceloom/compat/replay_cost_sql_rows.h"
#include "traceloom/compat/runtime_device_rows.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/compat/symbol_normalization_rows.h"
#include "traceloom/compat/structural_projection_rows.h"
#include "traceloom/compat/structural_position_rows.h"
#include "traceloom/compat/timeline_rows.h"
#include "traceloom/pattern/grammar_engine.h"
#include "traceloom/pattern/grammar_state.h"

namespace traceloom::compat {
namespace {

namespace fs = std::filesystem;

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
using detail::RawPackagingResult;
using detail::RawSourceDatabase;
using detail::materialize_augmented_catalog;
using detail::package_sqlite_sources;
#endif

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

void emit_timing(const NativeCompatibilitySidecarOptions& options,
                 const char* name, const Stopwatch& watch) {
  if (options.timing_diagnostics) {
    std::cerr << "timing " << name << "=" << watch.elapsed_ms() << "\n";
  }
}

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

FlatAnchorBuildConfig effective_evidence_role_config(
    FlatAnchorBuildConfig config) {
  if (config.classification_rules.rules().empty()) {
    config.classification_rules = load_default_signal_classification_ruleset();
  }
  if (!config.classification_overrides.empty()) {
    config.classification_rules = override_signal_classification_ruleset(
        config.classification_rules, config.classification_overrides);
    config.classification_overrides.clear();
  }
  return config;
}

void require_matching_policy_hint(const std::string& field,
                                  const std::string& hint,
                                  const std::string& actual) {
  if (!hint.empty() && hint != actual) {
    throw std::invalid_argument(field +
                                " disagrees with evidence_role_config");
  }
}
StructuralOccurrenceGraph recover_structural_occurrence_graph(
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options,
    const std::vector<StructuralProjectionToken>& structural_tokens,
    NativeCompactGrammarProjection* compact_grammar) {
  if (compact_grammar != nullptr) {
    compact_grammar->source_token_count = structural_tokens.size();
    compact_grammar->stop_reason = "disabled";
  }
  if (!options.materialize_grammar_structural_projection ||
      structural_tokens.empty()) {
    return build_structural_occurrence_graph_from_tokens(structural_tokens);
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
    if (compact_grammar != nullptr) {
      *compact_grammar = detail::summarize_compact_grammar(
          ir, structural_tokens, grammar_state, grammar_result,
          structural_tokens.empty() ? 0 : structural_tokens.front().device_id);
    }
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
    if (!grammar_result.ok() || grammar_state.stage != GrammarStage::kDone) {
      StructuralOccurrenceGraph fallback =
          build_structural_occurrence_graph_from_tokens(structural_tokens);
      fallback.diagnostics.push_back(Diagnostic{
          DiagnosticSeverity::kWarning, "grammar_recovery_rejected",
          "recursive grammar recovery failed closed with stop reason " +
              std::string(
                  grammar_engine_stop_reason_name(grammar_result.stop_reason))});
      return fallback;
    }
    StructuralOccurrenceGraph tree =
        grammar_state.macro_defs.empty()
            ? build_structural_occurrence_graph_from_tokens(structural_tokens)
            : build_structural_occurrence_graph_from_grammar_state(
                  structural_tokens, grammar_state);
    if (grammar_result.stop_reason ==
        GrammarEngineStopReason::kSequenceTooLargeForFullPairDiscovery) {
      tree.diagnostics.push_back(Diagnostic{
          DiagnosticSeverity::kWarning,
          "grammar_partial_sequence_too_large_for_full_pair_discovery",
          "exact run folding was retained, but pair discovery was skipped "
          "because the live sequence exceeded full_discovery_cap"});
    }
    return tree;
  } catch (const std::exception& ex) {
    if (compact_grammar != nullptr) {
      compact_grammar->available = false;
      compact_grammar->stop_reason = "exception";
    }
    StructuralOccurrenceGraph fallback =
        build_structural_occurrence_graph_from_tokens(structural_tokens);
    fallback.diagnostics.push_back(Diagnostic{
        DiagnosticSeverity::kWarning, "grammar_recovery_exception",
        std::string("recursive grammar recovery failed closed: ") + ex.what()});
    return fallback;
  }
}

// Projects the structural IR tables onto a single device. The token
// table is filtered to the device with dense TokenIds and a renumbered
// sequence_index (the grammar state machine requires both). Anchor, event,
// task, communication-op, symbol, source-ref, and semantic replay tables are
// copied unchanged so original ids stay valid everywhere; per-device tokens
// reference original anchor ids, which keeps compat anchor ids and anchor
// indices consistent with the global sidecar tables. Protected intervals are
// kept only when their whole token span belongs to the device; a span that
// crosses devices fails closed because cross-device replay units are not
// supported by the structural projection.
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
        " has no structural tokens to project");
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
          "unsupported in the structural projection");
    }
    if (interval_device != device_id) {
      continue;
    }
    for (TokenId::value_type token_id = interval.first_token_id.value();
         token_id <= interval.last_token_id.value(); ++token_id) {
      if (token_devices[token_id] != interval_device) {
        throw std::invalid_argument(
            "protected interval spans devices; cross-device replay units are "
            "unsupported in the structural projection");
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

std::vector<NativeDeviceStructuralProjection>
build_native_device_structural_projections(
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options) {
  const Stopwatch tokens_watch;
  const std::vector<NativeStructuralDevicePartition> partitions =
      partition_structural_projection_tokens_by_device(
          ir, options.evidence_role_config);
  if (options.timing_diagnostics) {
    std::cerr << "timing loop_tree_tokens_ms=" << tokens_watch.elapsed_ms()
              << "\n";
    std::cerr << "timing loop_tree_device_count=" << partitions.size()
              << "\n";
  }
  std::vector<NativeDeviceStructuralProjection> device_trees;
  device_trees.reserve(partitions.size());
  for (const NativeStructuralDevicePartition& partition : partitions) {
    const Stopwatch tree_watch;
    NativeDeviceStructuralProjection device;
    device.device_id = partition.device_id;
    device.compact_grammar.device_id = partition.device_id;
    device.tokens = partition.tokens;
    if (partitions.size() == 1) {
      device.graph =
          recover_structural_occurrence_graph(ir, options, partition.tokens,
                                              &device.compact_grammar);
    } else {
      const NativeIr projection =
          project_ir_for_device(ir, partition.device_id);
      device.graph =
          recover_structural_occurrence_graph(projection, options,
                                              partition.tokens,
                                              &device.compact_grammar);
    }
    if (options.timing_diagnostics) {
      std::cerr << "timing loop_tree_structural_projection_ms_device="
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
  return build_native_loop_tree_report_data(ir, options).coverage;
}

NativeLoopTreeReportData build_native_loop_tree_report_data(
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options) {
  const std::vector<NativeDeviceStructuralProjection> device_trees =
      build_native_device_structural_projections(ir, options);
  const Stopwatch aux_watch;
  const AuxAttributionSqlRows aux_rows =
      options.materialize_aux_attribution
          ? build_aux_attribution_sql_rows(
                ir, options.evidence_role_config, options.db_idx)
          : AuxAttributionSqlRows{};
  if (options.timing_diagnostics) {
    std::cerr << "timing loop_tree_aux_rows_ms=" << aux_watch.elapsed_ms()
              << "\n";
  }
  const Stopwatch coverage_watch;
  const bool scope_node_ids = device_trees.size() > 1;
  NativeLoopTreeReportData report;
  report.compact_grammars.reserve(device_trees.size());
  for (const NativeDeviceStructuralProjection& device : device_trees) {
    NodeCoverageSqlRows device_rows = build_structural_node_coverage_sql_rows(
        device.graph, device.tokens, aux_rows, options.db_idx,
        "native_report_tree", scope_node_ids);
    report.coverage.nodes.insert(report.coverage.nodes.end(), device_rows.nodes.begin(),
                      device_rows.nodes.end());
    report.coverage.edges.insert(report.coverage.edges.end(), device_rows.edges.begin(),
                      device_rows.edges.end());
    report.coverage.loop_nodes.insert(report.coverage.loop_nodes.end(),
                           device_rows.loop_nodes.begin(),
                           device_rows.loop_nodes.end());
    report.coverage.node_anchors.insert(report.coverage.node_anchors.end(),
                             device_rows.node_anchors.begin(),
                             device_rows.node_anchors.end());
    report.coverage.anchor_primary_nodes.insert(
        report.coverage.anchor_primary_nodes.end(),
        device_rows.anchor_primary_nodes.begin(),
        device_rows.anchor_primary_nodes.end());
    report.compact_grammars.push_back(device.compact_grammar);
  }
  if (options.timing_diagnostics) {
    std::cerr << "timing loop_tree_coverage_rows_ms="
              << coverage_watch.elapsed_ms() << "\n";
  }
  return report;
}

void write_basic_native_compatibility_sidecar(
    const std::string& sqlite_path,
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options) {
  const FlatAnchorBuildConfig evidence_role_config =
      effective_evidence_role_config(options.evidence_role_config);
  const SignalClassificationPolicyMetadata& evidence_role_policy =
      evidence_role_config.classification_rules.metadata();
  require_matching_policy_hint("evidence_role_policy_id",
                               options.evidence_role_policy_id,
                               evidence_role_policy.policy_id);
  require_matching_policy_hint("evidence_role_policy_version",
                               options.evidence_role_policy_version,
                               evidence_role_policy.policy_version);
  require_matching_policy_hint("evidence_role_manifest_sha256",
                               options.evidence_role_manifest_sha256,
                               evidence_role_policy.manifest_sha256);
  std::vector<MetadataSqlRow> metadata{
      {"traceloom_schema_version", "augmented_db_v1"},
      {"native_compatibility_materializer", "basic_native_ir_v1"},
      {"source_kind", options.source_kind},
      {"input_format", options.input_format},
      {"source_path", options.source_path},
      {"input_evidence_contract", options.input_evidence_contract},
      {"input_scope", options.input_scope},
      {"input_evidence_state", options.input_evidence_state},
      {"input_missing_components", options.input_missing_components},
      {"artifact_kind", options.artifact_kind},
      {"source_embedded", options.source_embedded ? "true" : "false"},
      {"source_sha256", options.source_sha256},
      {"source_size_bytes", std::to_string(options.source_size_bytes)},
      {"trace_event_count", std::to_string(ir.trace_events.size())},
      {"runtime_call_count", std::to_string(ir.runtime_calls.size())},
      {"anchor_count", std::to_string(ir.anchors.size())},
      {"graph_template_count", std::to_string(ir.graph_templates.size())},
      {"replay_unit_count", std::to_string(ir.replay_units.size())},
      {"event_reconciliation_policy_id",
       ir.event_reconciliation.policy.policy_id},
      {"event_reconciliation_policy_version",
       ir.event_reconciliation.policy.policy_version},
      {"event_reconciliation_manifest_sha256",
       ir.event_reconciliation.policy.manifest_sha256},
      {"event_reconciliation_rule_count",
       std::to_string(ir.event_reconciliation.policy.rules.size())},
      {"event_reconciliation_decision_count",
       std::to_string(ir.event_reconciliation.decisions.size())},
      {"event_reconciliation_member_count",
       std::to_string(ir.event_reconciliation.members.size())},
      {"event_reconciliation_reconciled_count",
       std::to_string(std::count_if(
           ir.event_reconciliation.decisions.begin(),
           ir.event_reconciliation.decisions.end(),
           [](const EventReconciliationDecisionRow& decision) {
             return decision.status ==
                    EventReconciliationStatus::kReconciled;
           }))},
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
      {"evidence_role_policy_id", evidence_role_policy.policy_id},
      {"evidence_role_policy_version", evidence_role_policy.policy_version},
      {"evidence_role_manifest_sha256",
       evidence_role_policy.manifest_sha256},
  };
  const Stopwatch symbol_rows_watch;
  const SymbolNormalizationSqlRows symbol_normalization_rows =
      build_symbol_normalization_sql_rows(ir, options.db_idx);
  emit_timing(options, "sidecar_symbol_rows_ms", symbol_rows_watch);
  metadata.push_back(
      {"symbol_normalization_policy_id",
       symbol_normalization_rows.policies.front().policy_id});
  metadata.push_back(
      {"symbol_normalization_policy_version",
       symbol_normalization_rows.policies.front().policy_version});
  metadata.push_back(
      {"symbol_normalization_source_manifest",
       symbol_normalization_rows.policies.front().source_manifest});
  metadata.push_back(
      {"symbol_normalization_manifest_sha256",
       symbol_normalization_rows.policies.front().manifest_sha256});
  metadata.push_back(
      {"symbol_normalization_rule_count",
       std::to_string(symbol_normalization_rows.rules.size())});
  metadata.push_back(
      {"symbol_normalization_decision_count",
       std::to_string(symbol_normalization_rows.decisions.size())});

  {
    const Stopwatch runtime_rows_watch;
    RuntimeDeviceSqlRows runtime_rows =
        build_runtime_device_sql_rows(ir, options.db_idx);
    if (options.timing_diagnostics) {
      std::cerr << "timing runtime_device_rows_ms="
                << runtime_rows_watch.elapsed_ms() << "\n";
    }
    metadata.push_back({"device_work_count",
                        std::to_string(runtime_rows.device_works.size())});
    metadata.push_back({"runtime_device_relation_count",
                        std::to_string(runtime_rows.relations.size())});
    metadata.push_back({"anchor_runtime_relation_count",
                        std::to_string(runtime_rows.anchor_relations.size())});
    metadata.push_back({"anchor_host_interval_count",
                        std::to_string(runtime_rows.host_intervals.size())});
    metadata.push_back({"anchor_host_activity_count", "0"});
    metadata.push_back({"anchor_host_api_summary_count", "0"});
    metadata.push_back(
        {"anchor_host_activity_materialization_state", "query_time_only"});
    metadata.push_back({"anchor_host_activity_candidate_upper_bound", "0"});
    metadata.push_back({"anchor_host_activity_materialization_limit", "0"});
    const Stopwatch runtime_write_watch;
    replace_metadata_rows(sqlite_path, metadata);
    replace_runtime_device_rows(sqlite_path, runtime_rows);
    if (options.timing_diagnostics) {
      std::cerr << "timing runtime_device_write_ms="
                << runtime_write_watch.elapsed_ms() << "\n";
    }
  }
  const Stopwatch timeline_rows_watch;
  const EventSqlRows event_rows = build_timeline_sql_rows(ir, options.db_idx);
  emit_timing(options, "sidecar_timeline_rows_ms", timeline_rows_watch);
  const Stopwatch timeline_write_watch;
  replace_timeline_rows(sqlite_path,
                        split_timeline_event_sql_rows(event_rows));
  replace_event_source_rows(sqlite_path,
                            split_source_lineage_sql_rows(event_rows));
  emit_timing(options, "sidecar_timeline_write_ms", timeline_write_watch);

  const Stopwatch graph_rows_watch;
  replace_graph_replay_evidence_rows(
      sqlite_path,
      build_native_graph_replay_evidence_sql_rows(
          ir, options.source_kind, options.db_idx));
  replace_exact_graph_rows(
      sqlite_path,
      build_exact_graph_sql_rows(ir, options.source_kind, options.db_idx));
  const ReplayInternalCostMapResult replay_cost =
      build_replay_internal_cost_map(ir);
  replace_replay_cost_rows(sqlite_path, ir, replay_cost, options.db_idx);
  ReplayBodyPatternConfig replay_body_pattern_config;
  replay_body_pattern_config.worker_count = options.grammar_worker_count;
  replay_body_pattern_config.target_nodes_per_chunk =
      options.grammar_target_nodes_per_chunk;
  replay_body_pattern_config.full_discovery_cap =
      options.grammar_full_discovery_cap;
  replace_replay_body_pattern_rows(sqlite_path, ir, replay_cost,
                                   options.db_idx,
                                   replay_body_pattern_config);
  emit_timing(options, "sidecar_graph_rows_write_ms", graph_rows_watch);

  const Stopwatch anchor_rows_watch;
  const std::vector<AnchorSqlRow> anchor_rows =
      build_anchor_sequence_sql_rows(ir, options.db_idx);
  replace_anchor_rows(sqlite_path, anchor_rows);
  replace_event_reconciliation_rows(sqlite_path, ir, options.db_idx);
  replace_symbol_normalization_rows(sqlite_path, symbol_normalization_rows);
  emit_timing(options, "sidecar_anchor_rows_write_ms", anchor_rows_watch);

  const Stopwatch aux_rows_watch;
  const AuxAttributionSqlRows aux_rows =
      options.materialize_aux_attribution
          ? build_aux_attribution_sql_rows(
                ir, evidence_role_config, options.db_idx)
          : AuxAttributionSqlRows{};
  replace_aux_attribution_rows(sqlite_path, aux_rows);
  replace_anchor_cost_breakdown_rows(
      sqlite_path, build_native_anchor_cost_breakdown_sql_rows(ir, aux_rows));
  emit_timing(options, "sidecar_aux_rows_write_ms", aux_rows_watch);

  NativeCompatibilitySidecarOptions projection_options = options;
  projection_options.evidence_role_config = evidence_role_config;
  const Stopwatch structural_rows_watch;
  const std::vector<NativeDeviceStructuralProjection> device_trees =
      build_native_device_structural_projections(ir, projection_options);
  const bool scope_node_ids = device_trees.size() > 1;
  NodeCoverageSqlRows node_rows;
  SemanticTreeSqlRows semantic_rows;
  StructuralPositionSqlRows position_rows;
  for (const NativeDeviceStructuralProjection& device : device_trees) {
    const NodeCoverageSqlRows device_node_rows =
        build_structural_node_coverage_sql_rows(
            device.graph, device.tokens, aux_rows, options.db_idx,
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
        build_structural_semantic_sql_rows(
            device.graph, device.tokens, aux_rows, options.db_idx, tree_id,
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

    const StructuralPositionSqlRows device_position_rows =
        build_structural_position_sql_rows(
            device.graph, device.tokens, options.db_idx, tree_id,
            "anchor_tree", scope_node_ids);
    position_rows.refinements.insert(
        position_rows.refinements.end(),
        device_position_rows.refinements.begin(),
        device_position_rows.refinements.end());
    position_rows.occurrences.insert(position_rows.occurrences.end(),
                                     device_position_rows.occurrences.begin(),
                                     device_position_rows.occurrences.end());
    position_rows.members.insert(position_rows.members.end(),
                                 device_position_rows.members.begin(),
                                 device_position_rows.members.end());
    position_rows.edges.insert(position_rows.edges.end(),
                               device_position_rows.edges.begin(),
                               device_position_rows.edges.end());
  }
  replace_loop_tree_rows(sqlite_path, split_loop_tree_sql_rows(node_rows));
  const NodeAnchorCoverageSqlRows coverage_rows =
      split_node_anchor_coverage_sql_rows(node_rows);
  replace_node_anchor_coverage_rows(sqlite_path, coverage_rows);
  emit_timing(options, "sidecar_structural_rows_write_ms",
              structural_rows_watch);

  if (options.materialize_collective_tags) {
    const Stopwatch collective_watch;
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
    emit_timing(options, "sidecar_collective_rows_write_ms",
                collective_watch);
  }

  const Stopwatch semantic_rows_watch;
  replace_semantic_tree_catalog_rows(
      sqlite_path, split_semantic_tree_catalog_sql_rows(semantic_rows));
  replace_semantic_graph_rows(sqlite_path,
                              split_semantic_graph_sql_rows(semantic_rows));
  replace_structural_position_rows(sqlite_path, position_rows);
  if (options.materialize_structural_views) {
    const Stopwatch structural_views_watch;
    materialize_structural_compatibility_views(sqlite_path,
                                               options.timing_diagnostics);
    if (options.timing_diagnostics) {
      std::cerr << "timing structural_views_ms="
                << structural_views_watch.elapsed_ms() << "\n";
    }
  }
  emit_timing(options, "sidecar_semantic_rows_views_ms",
              semantic_rows_watch);

  const Stopwatch evidence_role_watch;
  if (options.materialize_aux_attribution) {
    replace_evidence_role_sql_rows(sqlite_path, ir, evidence_role_config,
                                   options.db_idx, aux_rows,
                                   options.timing_diagnostics);
  } else {
    replace_evidence_role_sql_rows(sqlite_path, ir, evidence_role_config,
                                   options.db_idx, false,
                                   options.timing_diagnostics);
  }
  emit_timing(options, "sidecar_evidence_role_ms", evidence_role_watch);
}

void write_queryable_database_timeline(
    const std::string& output_path,
    const std::string& source_sqlite_path,
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options) {
  write_queryable_database_timeline(
      output_path, std::vector<std::string>{source_sqlite_path}, ir, options);
}

void write_queryable_database_timeline(
    const std::string& output_path,
    const std::vector<std::string>& source_sqlite_paths,
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  if (source_sqlite_paths.empty()) {
    throw std::invalid_argument(
        "self-contained augmented DB requires at least one SQLite source");
  }
  std::vector<std::string> sources;
  sources.reserve(source_sqlite_paths.size());
  for (const std::string& source_path : source_sqlite_paths) {
    const fs::path source = fs::absolute(source_path).lexically_normal();
    if (!fs::is_regular_file(source)) {
      throw std::invalid_argument(
          "self-contained augmented DB source is not a regular SQLite file: " +
          source.string());
    }
    sources.push_back(source.string());
  }
  std::sort(sources.begin(), sources.end());
  if (std::adjacent_find(sources.begin(), sources.end()) != sources.end()) {
    throw std::invalid_argument(
        "self-contained augmented DB received duplicate SQLite sources");
  }
  const fs::path output = fs::absolute(output_path).lexically_normal();
  for (const std::string& source_path : sources) {
    const fs::path source(source_path);
    if (source == output ||
        (fs::exists(output) && fs::equivalent(source, output))) {
      throw std::invalid_argument(
          "augmented DB output must differ from every input profiler DB");
    }
  }
  if (output.has_parent_path()) {
    fs::create_directories(output.parent_path());
  }
  const std::string suffix = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const fs::path temporary = output.string() + ".tmp." + suffix;
  try {
    const Stopwatch packaging_watch;
    const RawPackagingResult packaging =
        package_sqlite_sources(sources, temporary.string());
    emit_timing(options, "augmented_packaging_ms", packaging_watch);
    NativeCompatibilitySidecarOptions augmented_options = options;
    if (augmented_options.source_path.empty()) {
      augmented_options.source_path =
          sources.size() == 1 ? sources.front() : "multiple_sqlite_sources";
    }
    if (augmented_options.collective_db_name.empty()) {
      augmented_options.collective_db_name =
          basename_or_default(augmented_options.source_path, "analysis") +
          ".traceloom.db";
    }
    augmented_options.artifact_kind = "queryable_database_timeline";
    augmented_options.source_embedded = true;
    augmented_options.source_size_bytes = 0;
    for (const RawSourceDatabase& source : packaging.sources) {
      augmented_options.source_size_bytes += source.size_bytes;
    }
    augmented_options.source_sha256 =
        packaging.sources.size() == 1 ? packaging.sources.front().sha256
                                      : std::string();
    const Stopwatch sidecar_watch;
    write_basic_native_compatibility_sidecar(
        temporary.string(), ir, augmented_options);
    emit_timing(options, "augmented_sidecar_ms", sidecar_watch);

    const Stopwatch catalog_watch;
    materialize_augmented_catalog(temporary.string(), packaging, ir);
    emit_timing(options, "augmented_catalog_ms", catalog_watch);
    std::error_code ec;
    fs::rename(temporary, output, ec);
    if (ec) {
      throw std::runtime_error("failed to publish augmented DB output: " +
                               ec.message());
    }
  } catch (...) {
    std::error_code ignored;
    fs::remove(temporary, ignored);
    throw;
  }
#else
  (void)output_path;
  (void)source_sqlite_paths;
  (void)ir;
  (void)options;
  throw std::runtime_error(
      "self-contained augmented DB requires SQLite support");
#endif
}

}  // namespace traceloom::compat
