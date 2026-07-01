#pragma once

#include <vector>

#include "traceloom/report/report_tree.h"

namespace traceloom {

struct ReportCostHandoffRow {
  ReportNodeOccurrenceId atom_occurrence_id;
  ReportNodeOccurrenceId direct_parent_occurrence_id;
  ReportNodeDefId node_def_id;
  SymbolId symbol_id;
  std::string display_op;
  std::string display_category;
  std::uint32_t token_start_ordinal = 0;
  std::uint32_t token_end_ordinal = 0;
};

std::vector<ReportCostHandoffRow> collect_report_cost_handoff_rows(
    const ReportTree& tree);

}  // namespace traceloom
