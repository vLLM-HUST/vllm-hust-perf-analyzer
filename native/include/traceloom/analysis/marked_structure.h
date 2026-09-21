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
  // paired: explicit begin/end; end_delimited: seed/reset at begin, then
  // successive ends; cycle_end: only intervals between complete end sequences.
  std::string mode = "paired";
  std::string end_predecessor;
  std::vector<std::string> end_sequence;
  std::string cycle_label;
  // Optional independent end-delimiter unit, including its guarded predecessor.
  // Seed-only begin markers stay raw rather than becoming residual operations.
  std::string boundary_label;
  // Matching-only preprocessing; atom display and exact signatures stay intact.
  std::string name_normalization = "exact";
  std::vector<std::string> end_predecessor_any;
  bool enabled() const { return !id.empty(); }
};
StructuralOccurrenceGraph build_marked_structural_graph(
    const std::vector<StructuralProjectionToken>& tokens,
    const MarkedStructureRules& rules);
}  // namespace traceloom
