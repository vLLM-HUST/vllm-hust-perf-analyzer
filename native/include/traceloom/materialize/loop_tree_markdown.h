#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/analysis/semantic_task_classifier.h"

namespace traceloom {

struct ReconstructionStatusCount {
  std::string status;
  std::uint64_t region_count = 0;
};

struct IdleExplanationSummaryCount {
  std::string category;
  std::uint64_t slice_count = 0;
  std::uint64_t duration_ns = 0;
};

struct IdleExplanationNodeHotspot {
  std::string node_id;
  std::string label;
  std::string kind;
  std::uint64_t attributed_ns = 0;
  std::uint64_t direct_ns = 0;
  std::uint64_t correlated_ns = 0;
  std::uint64_t wait_ns = 0;
  std::uint64_t capture_control_ns = 0;
  std::uint64_t runtime_control_ns = 0;
  std::uint64_t queued_delay_ns = 0;
  std::uint64_t host_sync_ns = 0;
  std::uint64_t no_observed_work_ns = 0;
  std::uint64_t unattributed_ns = 0;
  double average_attributed_ns = 0.0;
};

struct LoopTreeMarkdownOptions {
  std::string db_label;
  std::string source_kind = "native_ir";
  std::string source_path;
  std::uint32_t db_idx = 0;
  bool has_device_id = false;
  std::uint32_t device_id = 0;
  std::uint64_t trace_event_count = 0;
  std::uint64_t anchor_count = 0;
  std::uint64_t replay_composition_region_count = 0;
  std::uint64_t recognized_replay_composition_region_count = 0;
  std::uint64_t unrecognized_replay_composition_region_count = 0;
  std::uint64_t replay_unit_count = 0;
  std::uint64_t exact_replay_unit_count = 0;
  std::vector<ReconstructionStatusCount> reconstruction_status_counts;
  bool has_semantic_operator_coverage = false;
  std::string semantic_rules_version;
  std::uint64_t unknown_task_count = 0;
  std::uint64_t unregistered_operator_occurrence_count = 0;
  std::uint64_t unique_unregistered_operator_count = 0;
  std::vector<UnregisteredOperatorSummaryRow> unregistered_operators;
  bool has_idle_explanation_summary = false;
  std::string idle_analysis_status;
  std::string idle_collection_status;
  std::string idle_attribution_rule_version;
  std::string idle_alignment_status = "uncalibrated";
  bool has_idle_clock_model_summary = false;
  std::string idle_clock_source_domain = "profiler_host";
  std::string idle_clock_intermediate_domain = "caller_clock_realtime";
  std::string idle_clock_mapping_kind = "composed_affine";
  long double idle_clock_scale = 1.0L;
  long double idle_clock_drift_ppm = 0.0L;
  long double idle_clock_profiler_to_marker_scale = 1.0L;
  long double idle_clock_profiler_to_marker_drift_ppm = 0.0L;
  std::uint64_t idle_clock_input_marker_count = 0;
  std::uint64_t idle_clock_inlier_marker_count = 0;
  std::uint64_t idle_clock_rejected_marker_count = 0;
  std::uint64_t idle_clock_fit_marker_count = 0;
  std::uint64_t idle_clock_validation_marker_count = 0;
  long double idle_clock_absolute_residual_p50_ns = 0.0L;
  long double idle_clock_absolute_residual_p95_ns = 0.0L;
  long double idle_clock_absolute_residual_max_ns = 0.0L;
  long double idle_clock_bracket_uncertainty_p95_ns = 0.0L;
  long double idle_clock_host_residual_p50_ns = 0.0L;
  long double idle_clock_host_residual_p95_ns = 0.0L;
  long double idle_clock_host_residual_max_ns = 0.0L;
  long double idle_clock_host_uncertainty_p95_ns = 0.0L;
  long double idle_clock_composed_residual_p50_ns = 0.0L;
  long double idle_clock_composed_residual_p95_ns = 0.0L;
  long double idle_clock_composed_residual_max_ns = 0.0L;
  std::uint64_t idle_clock_direct_overlap_marker_count = 0;
  std::uint64_t idle_clock_ordinal_fallback_marker_count = 0;
  std::uint64_t idle_clock_epsilon_ns = 0;
  std::uint64_t visible_productive_idle_ns = 0;
  std::uint64_t direct_explained_idle_ns = 0;
  std::uint64_t correlated_explained_idle_ns = 0;
  std::vector<IdleExplanationSummaryCount> idle_explanation_counts;
  std::uint64_t anchor_prelude_attributed_idle_ns = 0;
  std::uint64_t device_only_unassigned_idle_ns = 0;
  std::vector<IdleExplanationNodeHotspot> idle_node_hotspots;
};

void write_loop_tree_markdown(
    std::ostream& out,
    const compat::NodeCoverageSqlRows& rows,
    const LoopTreeMarkdownOptions& options = LoopTreeMarkdownOptions{});

}  // namespace traceloom
