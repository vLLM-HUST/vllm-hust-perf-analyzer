#include "traceloom/analysis/structural_occurrence_builder.h"
#include "traceloom/analysis/structural_cost_handoff.h"
#include "traceloom/testing/test_util.h"

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

  const SymbolId matmul(1);
  const std::vector<StructuralProjectionToken> tokens{
      token(0, matmul, "MatMul"),
      token(1, matmul, "MatMul"),
  };

  const StructuralOccurrenceGraph tree = build_structural_occurrence_graph_from_tokens(tokens);
  require(tree.node_defs.size() == 3);
  require(tree.node_defs[0].display_op == "Seq");
  require(tree.node_defs[1].display_op == "Rep x2");
  require(tree.node_defs[2].display_op == "MatMul");

  const std::vector<StructuralCostHandoffRow> rows =
      collect_structural_cost_handoff_rows(tree);
  require(rows.size() == 2);

  const StructuralNodeOccurrence& repeat_occurrence = tree.occurrences[1];
  require(structural_node_def(tree, repeat_occurrence.node_def_id).kind ==
          StructuralNodeKind::kRepeat);

  for (std::size_t i = 0; i < rows.size(); ++i) {
    const StructuralCostHandoffRow& row = rows[i];
    require(row.direct_parent_occurrence_id == repeat_occurrence.id);
    require(row.node_def_id == tree.node_defs[2].id);
    require(row.symbol_id == matmul);
    require(row.display_op == "MatMul");
    require(row.display_category == "exec");
    require(row.token_start_ordinal == i);
    require(row.token_end_ordinal == i + 1);

    const StructuralNodeOccurrence& atom_occurrence =
        structural_node_occurrence(tree, row.atom_occurrence_id);
    require(atom_occurrence.parent_occurrence_id ==
            row.direct_parent_occurrence_id);
    require(atom_occurrence.token_start_ordinal == row.token_start_ordinal);
    require(atom_occurrence.token_end_ordinal == row.token_end_ordinal);
  }

  return 0;
}
