#include "traceloom/report/report_tree.h"

#include <stdexcept>

namespace traceloom {

const ReportNodeDef& node_def(const ReportTree& tree, ReportNodeDefId id) {
  if (!id.valid() || id.value() >= tree.node_defs.size()) {
    throw std::out_of_range("ReportNodeDefId out of range");
  }
  return tree.node_defs[id.value()];
}

const ReportNodeOccurrence& node_occurrence(const ReportTree& tree,
                                            ReportNodeOccurrenceId id) {
  if (!id.valid() || id.value() >= tree.occurrences.size()) {
    throw std::out_of_range("ReportNodeOccurrenceId out of range");
  }
  return tree.occurrences[id.value()];
}

std::size_t occurrence_count_for_def(const ReportTree& tree,
                                     ReportNodeDefId id) {
  std::size_t count = 0;
  for (const ReportNodeOccurrence& occurrence : tree.occurrences) {
    if (occurrence.node_def_id == id) {
      ++count;
    }
  }
  return count;
}

}  // namespace traceloom
