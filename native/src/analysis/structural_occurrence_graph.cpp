#include "traceloom/analysis/structural_occurrence_graph.h"

#include <stdexcept>

namespace traceloom {

const char* structural_anchor_kind_name(StructuralAnchorKind kind) {
  switch (kind) {
    case StructuralAnchorKind::kExec:
      return "exec";
    case StructuralAnchorKind::kGraphH:
      return "graph_h";
    case StructuralAnchorKind::kGraphL:
      return "graph_l";
    case StructuralAnchorKind::kGraphT:
      return "graph_t";
    case StructuralAnchorKind::kGraphTemplate:
      return "graph_template";
    case StructuralAnchorKind::kGraphLaunchActivity:
      return "graph_launch_activity";
    case StructuralAnchorKind::kCollective:
      return "collective";
    case StructuralAnchorKind::kUnknown:
      return "unknown";
  }
  return "unknown";
}

const StructuralNodeDef& structural_node_def(
    const StructuralOccurrenceGraph& graph, StructuralNodeDefId id) {
  if (!id.valid() || id.value() >= graph.node_defs.size()) {
    throw std::out_of_range("StructuralNodeDefId out of range");
  }
  return graph.node_defs[id.value()];
}

const StructuralNodeOccurrence& structural_node_occurrence(
    const StructuralOccurrenceGraph& graph, StructuralNodeOccurrenceId id) {
  if (!id.valid() || id.value() >= graph.occurrences.size()) {
    throw std::out_of_range("StructuralNodeOccurrenceId out of range");
  }
  return graph.occurrences[id.value()];
}

std::size_t structural_occurrence_count_for_def(
    const StructuralOccurrenceGraph& graph, StructuralNodeDefId id) {
  if (id.valid() && id.value() < graph.occurrence_counts_by_def.size()) {
    return graph.occurrence_counts_by_def[id.value()];
  }
  std::size_t count = 0;
  for (const StructuralNodeOccurrence& occurrence : graph.occurrences) {
    if (occurrence.node_def_id == id) {
      ++count;
    }
  }
  return count;
}

}  // namespace traceloom
