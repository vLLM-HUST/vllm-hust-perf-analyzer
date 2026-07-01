#include "traceloom/pattern/grammar_state.h"
#include "traceloom/report/report_tree_builder.h"
#include "traceloom/testing/test_util.h"

#include <stdexcept>
#include <vector>

namespace {

traceloom::ReportToken token(std::uint32_t ordinal,
                             traceloom::SymbolId symbol,
                             const char* op) {
  traceloom::ReportToken out;
  out.ordinal = ordinal;
  out.symbol_id = symbol;
  out.display_op = op;
  out.display_category = "exec";
  out.anchor_kind = traceloom::ReportAnchorKind::kExec;
  out.start_ns = static_cast<std::int64_t>(ordinal) * 10;
  out.end_ns = out.start_ns + 5;
  return out;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  const SymbolId a(1);
  const SymbolId b(2);
  const SymbolId c(3);

  const std::vector<ReportToken> macro_run_tokens{
      token(0, a, "A"), token(1, b, "B"), token(2, a, "A"),
      token(3, b, "B"), token(4, a, "A"), token(5, b, "B"),
  };
  ReportGrammarEvidence macro_run;
  macro_run.macros.push_back(
      ReportMacroDefinition{"M1", {a, b}, ReportMacroVisibility::kKeepRepeat});
  macro_run.final_sequence.push_back(ReportGrammarItem::macro("M1"));
  macro_run.final_sequence.push_back(ReportGrammarItem::macro("M1"));
  macro_run.final_sequence.push_back(ReportGrammarItem::macro("M1"));

  const ReportTree macro_tree =
      build_report_tree_from_grammar(macro_run_tokens, macro_run);
  require(macro_tree.node_defs.size() == 5);
  require(macro_tree.node_defs[0].local_node_id == "N001");
  require(macro_tree.node_defs[0].kind == ReportNodeKind::kSeq);
  require(macro_tree.node_defs[1].local_node_id == "N002");
  require(macro_tree.node_defs[1].kind == ReportNodeKind::kRepeat);
  require(macro_tree.node_defs[1].repeat_count == 3);
  require(macro_tree.node_defs[1].display_op == "Rep x3");
  require(macro_tree.node_defs[2].local_node_id == "N003");
  require(macro_tree.node_defs[2].kind == ReportNodeKind::kSeq);
  require(macro_tree.node_defs[3].display_op == "A");
  require(macro_tree.node_defs[4].display_op == "B");
  require(occurrence_count_for_def(macro_tree, macro_tree.node_defs[2].id) ==
          3);
  require(occurrence_count_for_def(macro_tree, macro_tree.node_defs[3].id) ==
          3);
  require(occurrence_count_for_def(macro_tree, macro_tree.node_defs[4].id) ==
          3);
  require(macro_tree.diagnostics.empty());
  validate_report_tree_or_throw(macro_tree, macro_run_tokens.size());

  GlobalGrammarState grammar_state_run;
  grammar_state_run.stage = GrammarStage::kDone;
  grammar_state_run.nodes = {
      GrammarNode{GrammarNodeId(0), SymbolId(100), MacroDefId(0), 0, 2, 0, 20,
                  GrammarChunkId(0), GrammarNodeId::invalid(),
                  GrammarNodeId(1), true},
      GrammarNode{GrammarNodeId(1), SymbolId(100), MacroDefId(0), 2, 4, 20, 40,
                  GrammarChunkId(0), GrammarNodeId(0), GrammarNodeId(2),
                  true},
      GrammarNode{GrammarNodeId(2), SymbolId(100), MacroDefId(0), 4, 6, 40, 60,
                  GrammarChunkId(0), GrammarNodeId(1),
                  GrammarNodeId::invalid(), true},
  };
  grammar_state_run.chunks = {GrammarChunk{
      GrammarChunkId(0), 0, 0, GrammarNodeId(0), GrammarNodeId(2), 3, 0}};
  grammar_state_run.macro_defs = {
      MacroDefRow{MacroDefId(0), SymbolId(100), MacroLevel::kRP, {a, b}, 2, 3,
                  2, 0}};
  grammar_state_run.live_node_count = 3;

  const ReportTree grammar_state_tree =
      build_report_tree_from_grammar_state(macro_run_tokens,
                                           grammar_state_run);
  require(grammar_state_tree.node_defs.size() == 4);
  require(grammar_state_tree.node_defs[1].kind == ReportNodeKind::kRepeat);
  require(grammar_state_tree.node_defs[1].display_op == "Rep x3");
  require(grammar_state_tree.node_defs[1].repeat_count == 3);
  require(grammar_state_tree.node_defs[2].display_op == "A");
  require(grammar_state_tree.node_defs[3].display_op == "B");
  require(occurrence_count_for_def(grammar_state_tree,
                                   grammar_state_tree.node_defs[2].id) == 3);
  require(occurrence_count_for_def(grammar_state_tree,
                                   grammar_state_tree.node_defs[3].id) == 3);
  for (std::size_t index = 2; index < grammar_state_tree.node_defs.size();
       ++index) {
    require(grammar_state_tree.node_defs[index].kind == ReportNodeKind::kAtom);
  }
  validate_report_tree_or_throw(grammar_state_tree, macro_run_tokens.size());

  const std::vector<ReportToken> inline_tokens{
      token(0, a, "A"),
      token(1, b, "B"),
      token(2, c, "C"),
  };
  ReportGrammarEvidence inline_macro;
  inline_macro.macros.push_back(
      ReportMacroDefinition{"M1", {a, b}, ReportMacroVisibility::kInline});
  inline_macro.final_sequence.push_back(ReportGrammarItem::macro("M1"));
  inline_macro.final_sequence.push_back(ReportGrammarItem::symbol(c));

  const ReportTree inline_tree =
      build_report_tree_from_grammar(inline_tokens, inline_macro);
  require(inline_tree.node_defs.size() == 4);
  require(inline_tree.occurrences.size() == 4);
  require(inline_tree.edges.size() == 3);
  require(inline_tree.node_defs[0].display_op == "Seq");
  require(inline_tree.node_defs[1].display_op == "A");
  require(inline_tree.node_defs[2].display_op == "B");
  require(inline_tree.node_defs[3].display_op == "C");
  require(inline_tree.diagnostics.size() == 1);
  require(inline_tree.diagnostics[0].severity == DiagnosticSeverity::kWarning);
  require(inline_tree.diagnostics[0].code ==
          "macro_inlined_single_visible_reference");
  validate_report_tree_or_throw(inline_tree, inline_tokens.size());

  bool caught_mismatch = false;
  try {
    ReportGrammarEvidence bad = macro_run;
    bad.macros[0].body_symbols = {b, a};
    (void)build_report_tree_from_grammar(macro_run_tokens, bad);
  } catch (const std::invalid_argument&) {
    caught_mismatch = true;
  }
  require(caught_mismatch);

  bool caught_unknown_macro = false;
  try {
    ReportGrammarEvidence bad;
    bad.final_sequence.push_back(ReportGrammarItem::macro("missing"));
    (void)build_report_tree_from_grammar(inline_tokens, bad);
  } catch (const std::invalid_argument&) {
    caught_unknown_macro = true;
  }
  require(caught_unknown_macro);

  return 0;
}
