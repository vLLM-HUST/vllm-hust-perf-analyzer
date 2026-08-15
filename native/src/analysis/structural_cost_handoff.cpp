#include "traceloom/analysis/structural_cost_handoff.h"

#include <stdexcept>

namespace traceloom {

std::vector<StructuralCostHandoffRow> collect_structural_cost_handoff_rows(
    const StructuralOccurrenceGraph& tree) {
  std::vector<StructuralCostHandoffRow> rows;
  for (const StructuralNodeOccurrence& occurrence : tree.occurrences) {
    const StructuralNodeDef& def =
        structural_node_def(tree, occurrence.node_def_id);
    if (def.kind != StructuralNodeKind::kAtom) {
      continue;
    }
    if (!occurrence.parent_occurrence_id.valid()) {
      throw std::invalid_argument(
          "atom occurrence cannot be a cost handoff root");
    }
    rows.push_back(StructuralCostHandoffRow{
        occurrence.id,
        occurrence.parent_occurrence_id,
        occurrence.node_def_id,
        def.symbol_id,
        def.display_op,
        def.display_category,
        occurrence.token_start_ordinal,
        occurrence.token_end_ordinal,
    });
  }
  return rows;
}

}  // namespace traceloom
