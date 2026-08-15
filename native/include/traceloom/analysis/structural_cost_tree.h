#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/analysis/structural_occurrence_graph.h"

namespace traceloom {

struct StructuralCostLeaf {
  StructuralCostLeafId id;
  std::uint32_t token_ordinal = 0;
  std::int64_t duration_ns = 0;
  std::string source_label;
};

enum class StructuralCostItemKind {
  kStructuralOccurrence,
  kCostLeaf,
};

struct StructuralCostTreeEdge {
  StructuralNodeOccurrenceId parent_occurrence_id;
  StructuralCostItemKind child_kind = StructuralCostItemKind::kStructuralOccurrence;
  StructuralNodeOccurrenceId child_occurrence_id;
  StructuralCostLeafId child_cost_leaf_id;
  std::uint32_t edge_order = 0;
};

struct StructuralCostMetric {
  StructuralCostItemKind item_kind = StructuralCostItemKind::kStructuralOccurrence;
  StructuralNodeOccurrenceId occurrence_id;
  StructuralCostLeafId cost_leaf_id;
  std::int64_t self_duration_ns = 0;
  std::int64_t direct_child_duration_ns = 0;
  std::int64_t total_duration_ns = 0;
  std::uint32_t direct_structural_child_count = 0;
  std::uint32_t direct_cost_leaf_count = 0;
  std::uint32_t subtree_cost_leaf_count = 0;
};

struct StructuralCostTree {
  std::vector<StructuralCostTreeEdge> edges;
  std::vector<StructuralCostMetric> metrics;
  std::vector<Diagnostic> diagnostics;
};

StructuralCostTree build_structural_cost_tree(
    const StructuralOccurrenceGraph& tree,
    const std::vector<StructuralCostLeaf>& cost_leaves);

const StructuralCostMetric* find_structural_cost_metric(
    const StructuralCostTree& cost_tree,
    StructuralNodeOccurrenceId occurrence_id);

const StructuralCostMetric* find_structural_cost_leaf_metric(
    const StructuralCostTree& cost_tree,
    StructuralCostLeafId cost_leaf_id);

}  // namespace traceloom
