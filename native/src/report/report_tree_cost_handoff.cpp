#include "traceloom/report/report_tree_cost_handoff.h"

#include <stdexcept>

namespace traceloom {

std::vector<ReportCostHandoffRow> collect_report_cost_handoff_rows(
    const ReportTree& tree) {
  std::vector<ReportCostHandoffRow> rows;
  for (const ReportNodeOccurrence& occurrence : tree.occurrences) {
    const ReportNodeDef& def = node_def(tree, occurrence.node_def_id);
    if (def.kind != ReportNodeKind::kAtom) {
      continue;
    }
    if (!occurrence.parent_occurrence_id.valid()) {
      throw std::invalid_argument(
          "atom occurrence cannot be a cost handoff root");
    }
    rows.push_back(ReportCostHandoffRow{
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
