#include "traceloom/materialize/loop_tree_markdown.h"
#include "traceloom/testing/test_util.h"

#include <sstream>
#include <string>

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  compat::NodeCoverageSqlRows rows;
  compat::VizNodeSqlRow root;
  root.node_id = "node-0";
  root.view_name = "native_report_tree";
  root.local_node_id = "N001";
  root.path = "N001";
  root.node_type = "sequence";
  root.kind = "sequence";
  root.label = "Seq";
  root.occurrence_count = 1;
  root.total_us = 10.0;
  root.avg_total_us = 10.0;
  rows.nodes.push_back(root);

  LoopTreeMarkdownOptions options;
  options.source_kind = "ascend_sqlite_hot_path";
  options.source_path = "profile.db";
  options.has_device_id = true;
  options.device_id = 0;
  options.replay_composition_region_count = 3;
  options.recognized_replay_composition_region_count = 1;
  options.unrecognized_replay_composition_region_count = 2;
  options.replay_unit_count = 1;
  options.exact_replay_unit_count = 1;
  options.reconstruction_status_counts = {
      {"recognized_complete_pattern", 1},
      {"unrecognized_missing_body_capability", 2},
  };
  options.has_idle_explanation_summary = true;
  options.idle_analysis_status = "ok";
  options.idle_collection_status = "unknown";
  options.idle_attribution_rule_version = "host_device_projection_v2";
  options.idle_alignment_status = "calibrated";
  options.has_idle_clock_model_summary = true;
  options.idle_clock_source_domain = "profiler_host";
  options.idle_clock_intermediate_domain = "caller_clock_realtime";
  options.idle_clock_mapping_kind = "composed_affine";
  options.idle_clock_profiler_caller_observation_kind =
      "record_api_midpoint_to_record_bracket_midpoint";
  options.idle_clock_marker_device_observation_kind =
      "record_sync_bracket_midpoint_to_task_start";
  options.idle_clock_scale = 1.000001234567L;
  options.idle_clock_offset_ns = 123.5L;
  options.idle_clock_intercept_ns = -456.25L;
  options.idle_clock_drift_ppm = 1.234567L;
  options.idle_clock_profiler_to_marker_scale = 0.999998L;
  options.idle_clock_profiler_to_marker_drift_ppm = -2.0L;
  options.idle_clock_input_marker_count = 21;
  options.idle_clock_inlier_marker_count = 20;
  options.idle_clock_rejected_marker_count = 1;
  options.idle_clock_fit_marker_count = 16;
  options.idle_clock_validation_marker_count = 4;
  options.idle_clock_absolute_residual_p50_ns = 101.25L;
  options.idle_clock_absolute_residual_p95_ns = 202.5L;
  options.idle_clock_absolute_residual_max_ns = 303.75L;
  options.idle_clock_bracket_uncertainty_p95_ns = 404.125L;
  options.idle_clock_host_residual_p50_ns = 11.25L;
  options.idle_clock_host_residual_p95_ns = 22.5L;
  options.idle_clock_host_residual_max_ns = 33.75L;
  options.idle_clock_host_uncertainty_p95_ns = 22.75L;
  options.idle_clock_profiler_caller_bracket_uncertainty_p95_ns = 7.25L;
  options.idle_clock_composed_residual_p50_ns = 44.25L;
  options.idle_clock_composed_residual_p95_ns = 55.5L;
  options.idle_clock_composed_residual_max_ns = 66.75L;
  options.idle_clock_direct_overlap_marker_count = 5;
  options.idle_clock_ordinal_fallback_marker_count = 15;
  options.idle_clock_epsilon_ns = 630;
  options.visible_productive_idle_ns = 10000;
  options.direct_explained_idle_ns = 4000;
  options.idle_explanation_counts = {
      {"blocked_by_visible_wait", 2, 4000},
      {"unattributed_visible_idle", 1, 6000},
  };
  options.anchor_prelude_attributed_idle_ns = 8000;
  options.device_only_unassigned_idle_ns = 2000;
  IdleExplanationNodeHotspot hotspot;
  hotspot.node_id = "node-0";
  hotspot.label = "Seq";
  hotspot.kind = "sequence";
  hotspot.attributed_ns = 8000;
  hotspot.direct_ns = 4000;
  hotspot.wait_ns = 4000;
  hotspot.unattributed_ns = 4000;
  hotspot.average_attributed_ns = 8000.0;
  options.idle_node_hotspots = {hotspot};

  std::ostringstream out;
  write_loop_tree_markdown(out, rows, options);
  const std::string markdown = out.str();
  require(markdown.find("## ACLGraph Reconstruction") != std::string::npos);
  require(markdown.find("- regions: `3` (`1` recognized, `2` unrecognized)") !=
          std::string::npos);
  require(markdown.find("- replay_units: `1` (`1` exact, `0` legacy)") !=
          std::string::npos);
  require(markdown.find("| `unrecognized_missing_body_capability` | 2 |") !=
          std::string::npos);
  require(markdown.find("## Visible Productive Idle Evidence") !=
          std::string::npos);
  require(markdown.find("- collection_status: `unknown`") !=
          std::string::npos);
  require(markdown.find("### Host→Device Clock Calibration") !=
          std::string::npos);
  require(markdown.find("- source_clock_domain: `profiler_host`") !=
          std::string::npos);
  require(markdown.find(
              "- intermediate_clock_domain: `caller_clock_realtime`") !=
          std::string::npos);
  require(markdown.find(
              "- profiler_caller_observation_kind: "
              "`record_api_midpoint_to_record_bracket_midpoint`") !=
          std::string::npos);
  require(markdown.find("- offset_ns: `123.500000`") != std::string::npos &&
              markdown.find("- intercept_ns: `-456.250000`") !=
                  std::string::npos);
  require(markdown.find("- absolute_residual_p50_ns: `101.250000`") !=
          std::string::npos);
  require(markdown.find("- absolute_residual_p95_ns: `202.500000`") !=
          std::string::npos);
  require(markdown.find("- absolute_residual_max_ns: `303.750000`") !=
          std::string::npos);
  require(markdown.find("- rejected_marker_count: `1`") !=
          std::string::npos);
  require(markdown.find(
              "- host_clock_absolute_residual_p95_ns: `22.500000`") !=
          std::string::npos);
  require(markdown.find(
              "- profiler_to_caller_bracket_uncertainty_p95_ns: `7.250000`") !=
          std::string::npos);
  require(markdown.find(
              "- composed_absolute_residual_p95_ns: `55.500000`") !=
          std::string::npos);
  require(markdown.find("- ordinal_affine_fallback_marker_count: `15`") !=
          std::string::npos);
  require(markdown.find("- epsilon_ns: `630`") != std::string::npos);
  require(markdown.find("- directly_explained_us: `4` (`40%`)") !=
          std::string::npos);
  require(markdown.find(
              "| `blocked_by_visible_wait` | 2 | 4000 | 4 | 40% |") !=
          std::string::npos);
  require(markdown.find("### Anchor-Prelude Attribution") !=
          std::string::npos);
  require(markdown.find(
              "- attributed_visible_productive_idle_us: `8` (`80%`)") !=
          std::string::npos);
  require(markdown.find("| `node-0` Seq | `sequence` | 8 | 8 | 4 | 0.00 |") !=
          std::string::npos);

  LoopTreeMarkdownOptions empty_options;
  empty_options.source_path = "profile.db";
  std::ostringstream empty_out;
  write_loop_tree_markdown(empty_out, rows, empty_options);
  require(empty_out.str().find("## ACLGraph Reconstruction") ==
          std::string::npos);
  require(empty_out.str().find("## Visible Productive Idle Evidence") ==
          std::string::npos);

  return 0;
}
