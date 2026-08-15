#pragma once

#include <vector>

#include "traceloom/analysis/structural_occurrence_graph.h"

namespace traceloom {

struct StructuralCostHandoffRow {
  StructuralNodeOccurrenceId atom_occurrence_id;
  StructuralNodeOccurrenceId direct_parent_occurrence_id;
  StructuralNodeDefId node_def_id;
  SymbolId symbol_id;
  std::string display_op;
  std::string display_category;
  std::uint32_t token_start_ordinal = 0;
  std::uint32_t token_end_ordinal = 0;
};

std::vector<StructuralCostHandoffRow> collect_structural_cost_handoff_rows(
    const StructuralOccurrenceGraph& tree);

}  // namespace traceloom
