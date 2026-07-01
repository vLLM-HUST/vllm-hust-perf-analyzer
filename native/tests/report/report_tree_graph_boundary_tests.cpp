#include "traceloom/report/report_tree_builder.h"
#include "traceloom/testing/test_util.h"

#include <stdexcept>
#include <vector>

namespace {

traceloom::ReportToken graph_token(std::uint32_t ordinal,
                                   traceloom::SymbolId symbol,
                                   const char* op,
                                   traceloom::ReportAnchorKind kind) {
  traceloom::ReportToken out;
  out.ordinal = ordinal;
  out.symbol_id = symbol;
  out.display_op = op;
  out.display_category = "graph";
  out.anchor_kind = kind;
  out.launch_activity_id = "launch_1";
  out.start_ns = static_cast<std::int64_t>(ordinal) * 10;
  out.end_ns = out.start_ns + 5;
  return out;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  const SymbolId h(1);
  const SymbolId l(2);
  const SymbolId t(3);
  const std::vector<ReportToken> hlt_tokens{
      graph_token(0, h, "ACLGraphType H", ReportAnchorKind::kGraphH),
      graph_token(1, l, "ACLGraphType L", ReportAnchorKind::kGraphL),
      graph_token(2, t, "ACLGraphType T", ReportAnchorKind::kGraphT),
      graph_token(3, h, "ACLGraphType H", ReportAnchorKind::kGraphH),
      graph_token(4, l, "ACLGraphType L", ReportAnchorKind::kGraphL),
      graph_token(5, t, "ACLGraphType T", ReportAnchorKind::kGraphT),
  };

  ReportGrammarEvidence grammar;
  grammar.macros.push_back(
      ReportMacroDefinition{"M1", {h, l, t}, ReportMacroVisibility::kKeepRepeat});
  grammar.final_sequence.push_back(ReportGrammarItem::macro("M1"));
  grammar.final_sequence.push_back(ReportGrammarItem::macro("M1"));

  ReportGraphReplayEvidence exact_graph;
  exact_graph.tiling_status = ReportGraphTilingStatus::kExact;
  const ReportTree tree =
      build_report_tree_from_grammar(hlt_tokens, grammar, exact_graph);

  require(tree.diagnostics.empty());
  require(tree.node_defs.size() == 6);
  require(tree.node_defs[0].local_node_id == "N001");
  require(tree.node_defs[1].kind == ReportNodeKind::kRepeat);
  require(tree.node_defs[1].repeat_count == 2);
  require(tree.node_defs[2].kind == ReportNodeKind::kSeq);
  require(tree.node_defs[3].display_op == "ACLGraphType H");
  require(tree.node_defs[3].display_category == "graph");
  require(tree.node_defs[4].display_op == "ACLGraphType L");
  require(tree.node_defs[5].display_op == "ACLGraphType T");
  require(occurrence_count_for_def(tree, tree.node_defs[3].id) == 2);
  require(occurrence_count_for_def(tree, tree.node_defs[4].id) == 2);
  require(occurrence_count_for_def(tree, tree.node_defs[5].id) == 2);

  for (const ReportNodeDef& def : tree.node_defs) {
    require(def.display_op != "graph_launch_activity");
    require(def.display_op != "launch_1");
  }

  ReportGraphReplayEvidence ambiguous_graph;
  ambiguous_graph.tiling_status = ReportGraphTilingStatus::kAmbiguous;
  ambiguous_graph.diagnostic_code = "graph_replay_tiling_ambiguous";
  const ReportTree failed =
      build_report_tree_from_grammar(hlt_tokens, grammar, ambiguous_graph);
  require(failed.node_defs.empty());
  require(failed.occurrences.empty());
  require(failed.edges.empty());
  require(failed.coverage.empty());
  require(failed.diagnostics.size() == 1);
  require(failed.diagnostics[0].severity == DiagnosticSeverity::kError);
  require(failed.diagnostics[0].code == "graph_replay_tiling_ambiguous");

  bool caught_launch_anchor = false;
  try {
    std::vector<ReportToken> bad_tokens = hlt_tokens;
    bad_tokens[0].anchor_kind = ReportAnchorKind::kGraphLaunchActivity;
    (void)build_report_tree_from_grammar(bad_tokens, grammar, exact_graph);
  } catch (const std::invalid_argument&) {
    caught_launch_anchor = true;
  }
  require(caught_launch_anchor);

  return 0;
}
