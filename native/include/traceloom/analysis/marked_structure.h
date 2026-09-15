#pragma once
#include <string>
#include <vector>
#include "traceloom/analysis/structural_occurrence_graph.h"

namespace traceloom {
struct UnitLabelRule {
  std::string label;
  std::vector<std::string> contains_any;
};
struct UnitCompositionRule {
  std::string label;
  std::vector<std::string> sequence;
};
// Explicit model knowledge, not grammar-discovered or scheduler-proven regions.
struct MarkedStructureRules {
  std::string id, begin, end;
  std::vector<UnitLabelRule> labels;
  std::vector<UnitCompositionRule> compositions;
  bool enabled() const { return !id.empty(); }
};
StructuralOccurrenceGraph build_marked_structural_graph(
    const std::vector<StructuralProjectionToken>& tokens,
    const MarkedStructureRules& rules);
}  // namespace traceloom
