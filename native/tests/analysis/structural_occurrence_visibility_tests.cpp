#include "traceloom/analysis/structural_occurrence_builder.h"
#include "traceloom/pattern/grammar_state.h"
#include "traceloom/testing/test_util.h"

#include <stdexcept>
#include <vector>

namespace {

traceloom::StructuralProjectionToken token(std::uint32_t ordinal,
                                            traceloom::SymbolId symbol,
                                            const char* op) {
  traceloom::StructuralProjectionToken out;
  out.ordinal = ordinal;
  out.symbol_id = symbol;
  out.display_op = op;
  out.display_category = "exec";
  out.anchor_kind = traceloom::StructuralAnchorKind::kExec;
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

  const std::vector<StructuralProjectionToken> macro_run_tokens{
      token(0, a, "A"), token(1, b, "B"), token(2, a, "A"),
      token(3, b, "B"), token(4, a, "A"), token(5, b, "B"),
  };
  StructuralGrammarEvidence macro_run;
  macro_run.macros.push_back(
      StructuralMacroDefinition{"M1", {a, b}, StructuralMacroVisibility::kKeepRepeat});
  macro_run.final_sequence.push_back(StructuralGrammarItem::macro("M1"));
  macro_run.final_sequence.push_back(StructuralGrammarItem::macro("M1"));
  macro_run.final_sequence.push_back(StructuralGrammarItem::macro("M1"));

  const StructuralOccurrenceGraph macro_tree =
      build_structural_occurrence_graph_from_grammar(macro_run_tokens, macro_run);
  require(macro_tree.node_defs.size() == 5);
  require(macro_tree.node_defs[0].local_node_id == "N001");
  require(macro_tree.node_defs[0].kind == StructuralNodeKind::kSeq);
  require(macro_tree.node_defs[1].local_node_id == "N002");
  require(macro_tree.node_defs[1].kind == StructuralNodeKind::kRepeat);
  require(macro_tree.node_defs[1].repeat_count == 3);
  require(macro_tree.node_defs[1].display_op == "Rep x3");
  require(macro_tree.node_defs[2].local_node_id == "N003");
  require(macro_tree.node_defs[2].kind == StructuralNodeKind::kSeq);
  require(macro_tree.node_defs[3].display_op == "A");
  require(macro_tree.node_defs[4].display_op == "B");
  require(structural_occurrence_count_for_def(macro_tree, macro_tree.node_defs[2].id) ==
          3);
  require(structural_occurrence_count_for_def(macro_tree, macro_tree.node_defs[3].id) ==
          3);
  require(structural_occurrence_count_for_def(macro_tree, macro_tree.node_defs[4].id) ==
          3);
  require(macro_tree.diagnostics.empty());
  validate_structural_occurrence_graph_or_throw(macro_tree, macro_run_tokens.size());

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
                  2, 0, ""}};
  grammar_state_run.live_node_count = 3;

  const StructuralOccurrenceGraph grammar_state_tree =
      build_structural_occurrence_graph_from_grammar_state(macro_run_tokens,
                                           grammar_state_run);
  require(grammar_state_tree.node_defs.size() == 4);
  require(grammar_state_tree.node_defs[1].kind == StructuralNodeKind::kRepeat);
  require(grammar_state_tree.node_defs[1].display_op == "Rep x3");
  require(grammar_state_tree.node_defs[1].repeat_count == 3);
  require(grammar_state_tree.node_defs[2].display_op == "A");
  require(grammar_state_tree.node_defs[3].display_op == "B");
  require(structural_occurrence_count_for_def(grammar_state_tree,
                                   grammar_state_tree.node_defs[2].id) == 3);
  require(structural_occurrence_count_for_def(grammar_state_tree,
                                   grammar_state_tree.node_defs[3].id) == 3);
  for (std::size_t index = 2; index < grammar_state_tree.node_defs.size();
       ++index) {
    require(grammar_state_tree.node_defs[index].kind == StructuralNodeKind::kAtom);
  }
  validate_structural_occurrence_graph_or_throw(grammar_state_tree, macro_run_tokens.size());

  const std::vector<StructuralProjectionToken> inline_tokens{
      token(0, a, "A"),
      token(1, b, "B"),
      token(2, c, "C"),
  };
  StructuralGrammarEvidence inline_macro;
  inline_macro.macros.push_back(
      StructuralMacroDefinition{"M1", {a, b}, StructuralMacroVisibility::kInline});
  inline_macro.final_sequence.push_back(StructuralGrammarItem::macro("M1"));
  inline_macro.final_sequence.push_back(StructuralGrammarItem::symbol(c));

  const StructuralOccurrenceGraph inline_tree =
      build_structural_occurrence_graph_from_grammar(inline_tokens, inline_macro);
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
  validate_structural_occurrence_graph_or_throw(inline_tree, inline_tokens.size());

  const SymbolId h(4);
  const SymbolId l(5);
  const SymbolId t(6);
  const std::vector<StructuralProjectionToken> semantic_tokens{
      token(0, h, "ACLH"), token(1, l, "ACLL"),
      token(2, l, "ACLL"), token(3, t, "ACLT")};
  GlobalGrammarState semantic_state;
  semantic_state.stage = GrammarStage::kDone;
  semantic_state.nodes = {GrammarNode{
      GrammarNodeId(0), SymbolId(101), MacroDefId(0), 0, 4, 0, 40,
      GrammarChunkId(0), GrammarNodeId::invalid(), GrammarNodeId::invalid(),
      true}};
  semantic_state.chunks = {GrammarChunk{
      GrammarChunkId(0), 0, 0, GrammarNodeId(0), GrammarNodeId(0), 1, 0}};
  semantic_state.macro_defs = {MacroDefRow{
      MacroDefId(0), SymbolId(101), MacroLevel::kSemantic, {h, l, l, t}, 4,
      1, 3, 0, "ReplayUnit T1"}};
  semantic_state.live_node_count = 1;
  const StructuralOccurrenceGraph semantic_tree =
      build_structural_occurrence_graph_from_grammar_state(semantic_tokens,
                                                            semantic_state);
  require(semantic_tree.node_defs.size() == 6);
  require(semantic_tree.node_defs[1].kind == StructuralNodeKind::kSeq);
  require(semantic_tree.node_defs[1].display_op == "ReplayUnit T1");
  require(semantic_tree.node_defs[1].display_category == "graph_unit");
  require(semantic_tree.node_defs[3].kind == StructuralNodeKind::kRepeat);
  require(semantic_tree.node_defs[3].display_op == "Rep x2");
  require(semantic_tree.node_defs[3].repeat_count == 2);
  require(semantic_tree.node_defs[4].display_op == "ACLL");
  require(structural_occurrence_count_for_def(semantic_tree,
                                   semantic_tree.node_defs[4].id) == 2);
  validate_structural_occurrence_graph_or_throw(semantic_tree, semantic_tokens.size());

  bool caught_mismatch = false;
  try {
    StructuralGrammarEvidence bad = macro_run;
    bad.macros[0].body_symbols = {b, a};
    (void)build_structural_occurrence_graph_from_grammar(macro_run_tokens, bad);
  } catch (const std::invalid_argument&) {
    caught_mismatch = true;
  }
  require(caught_mismatch);

  bool caught_unknown_macro = false;
  try {
    StructuralGrammarEvidence bad;
    bad.final_sequence.push_back(StructuralGrammarItem::macro("missing"));
    (void)build_structural_occurrence_graph_from_grammar(inline_tokens, bad);
  } catch (const std::invalid_argument&) {
    caught_unknown_macro = true;
  }
  require(caught_unknown_macro);

  return 0;
}
