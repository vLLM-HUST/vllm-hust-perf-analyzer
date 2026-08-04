#include "traceloom/analysis/native_pipeline.h"

#include <chrono>
#include <utility>

#include "traceloom/cost/cost_summary_lite.h"
#include "traceloom/pattern/candidate_reduce.h"
#include "traceloom/pattern/pattern_candidate_table.h"
#include "traceloom/pattern/pattern_candidate_summary.h"
#include "traceloom/sequence/boundary_index.h"
#include "traceloom/sequence/protected_sequence.h"

namespace traceloom {
namespace {

class Stopwatch {
 public:
  Stopwatch() : start_(Clock::now()) {}

  double elapsed_ms() const {
    const auto elapsed = Clock::now() - start_;
    return std::chrono::duration<double, std::milli>(elapsed).count();
  }

 private:
  using Clock = std::chrono::steady_clock;
  Clock::time_point start_;
};

template <typename Fn>
double time_stage(Fn&& fn) {
  const Stopwatch stopwatch;
  fn();
  return stopwatch.elapsed_ms();
}

NativePipelineMemory estimate_memory(
    const NativeIr& ir,
    const PatternCandidateTable& pattern_candidate_table,
    const PatternMiningDiagnostics& pattern_mining_diagnostics,
    const PatternCandidateSummaryTable& pattern_candidate_summary) {
  NativePipelineMemory memory;
  memory.trace_event_bytes = ir.trace_events.size() * sizeof(TraceEventRow);
  memory.anchor_bytes = ir.anchors.size() * sizeof(AnchorRow);
  memory.token_bytes = ir.tokens.size() * sizeof(TokenRow);
  memory.candidate_occurrence_bytes =
      pattern_candidate_table.rows.size() * sizeof(CandidateOccurrence);
  memory.pattern_mining_diagnostic_bytes =
      pattern_mining_diagnostics.rows.size() * sizeof(CandidateDiagnostic);
  memory.pattern_candidate_summary_bytes =
      pattern_candidate_summary.rows.size() *
      sizeof(PatternCandidateSummaryRow);
  return memory;
}

NativePipelineStats collect_stats(
    const NativeIr& ir,
    const PatternCandidateTable& pattern_candidate_table,
    const PatternMiningDiagnostics& pattern_mining_diagnostics,
    const std::vector<CandidateSummaryRow>& reduced_candidates) {
  NativePipelineStats stats;
  stats.source_ref_count = ir.source_refs.size();
  stats.trace_event_count = ir.trace_events.size();
  stats.task_count = ir.tasks.size();
  stats.communication_op_count = ir.communication_ops.size();
  stats.graph_slot_template_count = ir.graph_slot_templates.size();
  stats.captured_graph_instance_count = ir.captured_graph_instances.size();
  stats.captured_graph_stream_count = ir.captured_graph_streams.size();
  stats.graph_launch_occurrence_count = ir.graph_launch_occurrences.size();
  for (const GraphLaunchOccurrenceRow& launch :
       ir.graph_launch_occurrences.rows()) {
    if (launch.captured_graph_instance_id.valid()) {
      ++stats.graph_launch_instance_linked_count;
    }
    switch (launch.match_policy) {
      case GraphLaunchMatchPolicy::kNotifyCompletionAdjacent:
        ++stats.graph_launch_completion_adjacent_count;
        break;
      case GraphLaunchMatchPolicy::kNotifyOrderedFallback:
        ++stats.graph_launch_ordered_fallback_count;
        break;
      case GraphLaunchMatchPolicy::kUnmatched:
        ++stats.graph_launch_unmatched_count;
        break;
    }
  }
  stats.replay_body_template_count = ir.replay_body_templates.size();
  stats.graph_launch_body_count = ir.graph_launch_bodies.size();
  stats.graph_launch_activity_count = ir.graph_launch_activities.size();
  stats.graph_launch_activity_member_count =
      ir.graph_launch_activity_members.size();
  for (const GraphLaunchActivityRow& activity :
       ir.graph_launch_activities.rows()) {
    switch (activity.boundary_policy) {
      case GraphLaunchActivityBoundaryPolicy::kHostBlockingSync:
        ++stats.graph_launch_activity_host_sync_count;
        break;
      case GraphLaunchActivityBoundaryPolicy::kHostThreadTail:
        ++stats.graph_launch_activity_thread_tail_count;
        break;
    }
    if (activity.host_execute_count > activity.matched_launch_count) {
      stats.graph_launch_activity_unmatched_host_execute_count +=
          activity.host_execute_count - activity.matched_launch_count;
    }
  }
  stats.replay_composition_candidate_count =
      ir.replay_composition_candidates.size();
  for (const ReplayCompositionCandidateRow& candidate :
       ir.replay_composition_candidates.rows()) {
    if (candidate.shape_policy ==
            ReplayCompositionShapePolicy::kHeadRepeatedLayerTail ||
        candidate.shape_policy == ReplayCompositionShapePolicy::kSingleGraph) {
      ++stats.replay_composition_body_confirmed_count;
    }
  }
  stats.replay_composition_slot_count = ir.replay_composition_slots.size();
  stats.replay_composition_region_count =
      ir.replay_composition_regions.size();
  stats.replay_composition_region_member_count =
      ir.replay_composition_region_members.size();
  for (const ReplayCompositionRegionRow& region :
       ir.replay_composition_regions.rows()) {
    if (region.status ==
        ReplayCompositionRegionStatus::kRecognizedCompletePattern) {
      ++stats.replay_composition_recognized_region_count;
    } else {
      ++stats.replay_composition_unrecognized_region_count;
    }
  }
  stats.graph_template_count = ir.graph_templates.size();
  stats.replay_unit_count = ir.replay_units.size();
  for (const ReplayUnitRow& replay_unit : ir.replay_units.rows()) {
    if (replay_unit.replay_composition_region_id.valid()) {
      ++stats.exact_replay_unit_count;
    }
  }
  stats.replay_unit_launch_member_count =
      ir.replay_unit_launch_members.size();
  stats.anchor_count = ir.anchors.size();
  stats.token_count = ir.tokens.size();
  stats.protected_interval_count = ir.protected_intervals.size();
  stats.candidate_occurrence_count = pattern_candidate_table.rows.size();
  stats.candidate_distinct_count = reduced_candidates.size();
  stats.candidate_diagnostic_count = pattern_mining_diagnostics.rows.size();
  return stats;
}

FlatAnchorBuildStats summarize_existing_anchors_and_tokens(
    const NativeIr& ir) {
  FlatAnchorBuildStats stats;
  stats.tokens = ir.tokens.size();
  for (const AnchorRow& row : ir.anchors.rows()) {
    switch (row.kind) {
      case AnchorKind::kDeviceEvent:
        ++stats.device_event_anchors;
        break;
      case AnchorKind::kGraphReplayUnit:
      case AnchorKind::kGraphH:
      case AnchorKind::kGraphL:
      case AnchorKind::kGraphT:
        ++stats.device_event_anchors;
        break;
      case AnchorKind::kCommunication:
        ++stats.communication_anchors;
        break;
      case AnchorKind::kSynchronization:
      case AnchorKind::kUnknown:
        break;
    }
  }
  return stats;
}

}  // namespace

NativePipelineResult run_native_pipeline(NativeIr& ir,
                                         const NativePipelineOptions& options) {
  NativePipelineResult result;

  result.timing.build_anchor_tokens_ms = time_stage([&]() {
    switch (options.anchor_mode) {
      case NativePipelineAnchorMode::kBuildFlatAnchors:
        result.anchor_stats = build_flat_anchors(ir, options.anchor_config);
        break;
      case NativePipelineAnchorMode::kUseExistingAnchorsAndTokens:
        if (ir.anchors.empty() || ir.tokens.empty()) {
          throw std::invalid_argument(
              "existing anchor/token mode requires non-empty AnchorTable and "
              "TokenTable");
        }
        result.anchor_stats = summarize_existing_anchors_and_tokens(ir);
        break;
    }
  });

  ProtectedSequence sequence;
  result.timing.build_protected_sequence_ms = time_stage(
      [&]() { sequence = ProtectedSequence::from_token_table(ir.tokens); });

  BoundaryIndex boundaries;
  result.timing.build_boundary_index_ms = time_stage([&]() {
    boundaries = BoundaryIndex::build(sequence, ir.protected_intervals);
  });

  PartitionPlan plan;
  result.timing.partition_plan_ms = time_stage([&]() {
    plan = PartitionPlan::build(sequence.size(), options.partition_config);
  });

  CandidateScanResult scan_result;
  result.timing.candidate_scan_map_ms = time_stage([&]() {
    scan_result = scan_candidate_partitions_with_diagnostics(
        sequence, boundaries, plan, options.candidate_scan_config,
        options.thread_count);
  });

  result.timing.pattern_candidate_table_ms = time_stage([&]() {
    result.pattern_candidate_table =
        build_pattern_candidate_table(std::move(scan_result.occurrences));
    result.pattern_mining_diagnostics =
        build_pattern_mining_diagnostics(std::move(scan_result.diagnostics));
  });

  result.timing.candidate_reduce_ms = time_stage([&]() {
    result.reduced_candidates =
        reduce_candidates(result.pattern_candidate_table.rows);
  });

  result.timing.pattern_candidate_summary_ms = time_stage([&]() {
    result.pattern_candidate_summary =
        build_pattern_candidate_summary(result.reduced_candidates);
  });

  result.timing.cost_summary_lite_ms = time_stage(
      [&]() { result.cost_summary_lite = summarize_anchor_costs(ir.anchors); });

  result.stats = collect_stats(ir, result.pattern_candidate_table,
                               result.pattern_mining_diagnostics,
                               result.reduced_candidates);
  result.memory = estimate_memory(ir, result.pattern_candidate_table,
                                  result.pattern_mining_diagnostics,
                                  result.pattern_candidate_summary);
  return result;
}

}  // namespace traceloom
