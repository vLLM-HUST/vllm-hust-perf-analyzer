#include "traceloom/report/report_tree.h"

namespace traceloom {

const char* report_anchor_kind_name(ReportAnchorKind kind) {
  return structural_anchor_kind_name(kind);
}

const ReportNodeDef& node_def(const ReportTree& tree, ReportNodeDefId id) {
  return structural_node_def(tree, id);
}

const ReportNodeOccurrence& node_occurrence(const ReportTree& tree,
                                            ReportNodeOccurrenceId id) {
  return structural_node_occurrence(tree, id);
}

std::size_t occurrence_count_for_def(const ReportTree& tree,
                                     ReportNodeDefId id) {
  return structural_occurrence_count_for_def(tree, id);
}

}  // namespace traceloom
