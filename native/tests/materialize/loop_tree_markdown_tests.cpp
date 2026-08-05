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
  options.idle_attribution_rule_version = "device_projection_v1";
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
