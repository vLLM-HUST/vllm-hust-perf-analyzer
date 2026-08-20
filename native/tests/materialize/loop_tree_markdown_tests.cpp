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
  options.input_evidence_contract = "ascend_full_profile_v1";
  options.input_scope = "monolithic_db_only";
  options.input_evidence_state = "evidence_incomplete";
  options.input_missing_components = "host/sqlite/runtime.db";
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
  require(markdown.find("- input_evidence_contract: "
                        "`ascend_full_profile_v1`") != std::string::npos);
  require(markdown.find("- input_scope: `monolithic_db_only`") !=
          std::string::npos);
  require(markdown.find("- input_evidence_state: `evidence_incomplete`") !=
          std::string::npos);
  require(markdown.find("- input_missing_components: "
                        "`host/sqlite/runtime.db`") != std::string::npos);
  require(markdown.find("human_view: `expanded_tree`") != std::string::npos);
  require(markdown.find("## Expanded Root") != std::string::npos);
  require(markdown.find("## Compact Grammar Summary") == std::string::npos);

  LoopTreeMarkdownOptions empty_options;
  empty_options.source_path = "profile.db";
  std::ostringstream empty_out;
  write_loop_tree_markdown(empty_out, rows, empty_options);
  require(empty_out.str().find("## ACLGraph Reconstruction") ==
          std::string::npos);

  compat::VizNodeSqlRow all_to_all_a;
  all_to_all_a.node_id = "node-1";
  all_to_all_a.view_name = "native_report_tree";
  all_to_all_a.local_node_id = "N002";
  all_to_all_a.kind = "atom";
  all_to_all_a.symbol = "AllToAll";
  all_to_all_a.label = "AllToAll";
  all_to_all_a.category = "collective";
  all_to_all_a.occurrence_count = 2;
  all_to_all_a.self_us = 7.5;
  rows.nodes.push_back(all_to_all_a);
  compat::VizNodeSqlRow all_to_all_b = all_to_all_a;
  all_to_all_b.node_id = "node-2";
  all_to_all_b.local_node_id = "N003";
  all_to_all_b.occurrence_count = 3;
  all_to_all_b.self_us = 5.0;
  rows.nodes.push_back(all_to_all_b);

  compat::NativeCompactGrammarProjection grammar;
  grammar.device_id = 0;
  grammar.available = true;
  grammar.stop_reason = "done";
  grammar.engine_step_count = 4;
  grammar.source_token_count = 5;
  compat::NativeGrammarLiveNodeSummary live;
  live.grammar_node_id = 17;
  live.symbol_id = 9;
  live.has_macro_def_id = true;
  live.macro_def_id = 3;
  live.label = "decoder repeat";
  live.source_begin_token_index = 0;
  live.source_end_token_index_exclusive = 5;
  live.first_anchor_idx = 1;
  live.last_anchor_idx = 5;
  live.span_us = 10.0;
  grammar.live_nodes.push_back(live);
  compat::NativeGrammarMacroSummary macro;
  macro.macro_def_id = 3;
  macro.symbol_id = 9;
  macro.level = "LP";
  macro.label = "decoder repeat";
  macro.rhs_labels = {"AllToAll", "MatMul"};
  macro.replace_count = 2;
  macro.gain = 3;
  macro.occurrence_count = 2;
  macro.first_anchor_idx = 1;
  macro.last_anchor_idx = 5;
  macro.inclusive_span_us = 20.0;
  grammar.macro_defs.push_back(macro);

  LoopTreeMarkdownOptions compact_options = options;
  compact_options.view = LoopTreeMarkdownView::kCompact;
  compact_options.input_format = "torch_npu_profiler";
  std::ostringstream compact_out;
  write_loop_tree_markdown(compact_out, rows, compact_options, &grammar);
  const std::string compact = compact_out.str();
  require(compact.find("human_view: `compact_grammar`") != std::string::npos);
  require(compact.find("input_format: `torch_npu_profiler`") !=
          std::string::npos);
  require(compact.find("live_grammar_nodes: `1`") != std::string::npos);
  require(compact.find("| `AllToAll` | `collective` | 2 | 5 | 12.5 |") !=
          std::string::npos);
  require(compact.find("| `G17` | `M3` | `decoder repeat` |") !=
          std::string::npos);
  require(compact.find("| `M3` | `LP` | `decoder repeat := AllToAll MatMul`") !=
          std::string::npos);
  require(compact.find("## Expanded Root") == std::string::npos);

  LoopTreeMarkdownOptions both_options = compact_options;
  both_options.view = LoopTreeMarkdownView::kBoth;
  std::ostringstream both_out;
  write_loop_tree_markdown(both_out, rows, both_options, &grammar);
  require(both_out.str().find(
              "human_view: `compact_grammar_and_expanded_tree`") !=
          std::string::npos);
  require(both_out.str().find("## Compact Grammar Summary") !=
          std::string::npos);
  require(both_out.str().find("## Expanded Root") != std::string::npos);

  return 0;
}
