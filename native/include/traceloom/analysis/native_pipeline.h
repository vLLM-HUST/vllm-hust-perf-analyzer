#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/cost/cost_summary_lite.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/pattern/candidate.h"
#include "traceloom/pattern/candidate_scan.h"
#include "traceloom/pattern/pattern_candidate_table.h"
#include "traceloom/pattern/pattern_candidate_summary.h"
#include "traceloom/sequence/partition_plan.h"

namespace traceloom {

enum class NativePipelineAnchorMode {
  kBuildFlatAnchors,
  kUseExistingAnchorsAndTokens,
};

struct NativePipelineOptions {
  std::size_t thread_count = 1;
  PartitionPlanConfig partition_config{4096, 3};
  CandidateScanConfig candidate_scan_config{2, 3};
  FlatAnchorBuildConfig anchor_config;
  NativePipelineAnchorMode anchor_mode =
      NativePipelineAnchorMode::kBuildFlatAnchors;
};

struct NativePipelineStats {
  std::size_t source_ref_count = 0;
  std::size_t trace_event_count = 0;
  std::size_t task_count = 0;
  std::size_t communication_op_count = 0;
  std::size_t graph_slot_template_count = 0;
  std::size_t captured_graph_instance_count = 0;
  std::size_t captured_graph_stream_count = 0;
  std::size_t graph_launch_occurrence_count = 0;
  std::size_t graph_launch_instance_linked_count = 0;
  std::size_t graph_launch_completion_adjacent_count = 0;
  std::size_t graph_launch_ordered_fallback_count = 0;
  std::size_t graph_launch_cuda_correlation_count = 0;
  std::size_t graph_launch_unmatched_count = 0;
  std::size_t replay_body_template_count = 0;
  std::size_t graph_launch_body_count = 0;
  std::size_t graph_launch_activity_count = 0;
  std::size_t graph_launch_activity_member_count = 0;
  std::size_t graph_launch_activity_host_sync_count = 0;
  std::size_t graph_launch_activity_thread_tail_count = 0;
  std::size_t graph_launch_activity_unmatched_host_execute_count = 0;
  std::size_t replay_composition_candidate_count = 0;
  std::size_t replay_composition_body_confirmed_count = 0;
  std::size_t replay_composition_slot_count = 0;
  std::size_t replay_composition_region_count = 0;
  std::size_t replay_composition_region_member_count = 0;
  std::size_t replay_composition_recognized_region_count = 0;
  std::size_t replay_composition_unrecognized_region_count = 0;
  std::size_t graph_template_count = 0;
  std::size_t replay_unit_count = 0;
  std::size_t exact_replay_unit_count = 0;
  std::size_t replay_unit_launch_member_count = 0;
  std::size_t anchor_count = 0;
  std::size_t token_count = 0;
  std::size_t protected_interval_count = 0;
  std::size_t candidate_occurrence_count = 0;
  std::size_t candidate_distinct_count = 0;
  std::size_t candidate_diagnostic_count = 0;
};

struct NativePipelineTiming {
  double build_anchor_tokens_ms = 0.0;
  double build_protected_sequence_ms = 0.0;
  double build_boundary_index_ms = 0.0;
  double partition_plan_ms = 0.0;
  double candidate_scan_map_ms = 0.0;
  double pattern_candidate_table_ms = 0.0;
  double candidate_reduce_ms = 0.0;
  double pattern_candidate_summary_ms = 0.0;
  double cost_summary_lite_ms = 0.0;
};

struct NativePipelineMemory {
  std::size_t trace_event_bytes = 0;
  std::size_t anchor_bytes = 0;
  std::size_t token_bytes = 0;
  std::size_t candidate_occurrence_bytes = 0;
  std::size_t pattern_mining_diagnostic_bytes = 0;
  std::size_t pattern_candidate_summary_bytes = 0;
};

struct NativePipelineResult {
  FlatAnchorBuildStats anchor_stats;
  NativePipelineStats stats;
  NativePipelineTiming timing;
  NativePipelineMemory memory;
  PatternCandidateTable pattern_candidate_table;
  PatternMiningDiagnostics pattern_mining_diagnostics;
  std::vector<CandidateSummaryRow> reduced_candidates;
  PatternCandidateSummaryTable pattern_candidate_summary;
  CostSummaryLite cost_summary_lite;
};

NativePipelineResult run_native_pipeline(NativeIr& ir,
                                         const NativePipelineOptions& options);

}  // namespace traceloom
