#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/report/report_tree.h"

namespace traceloom {

struct ReportCostLeaf {
  ReportCostLeafId id;
  std::uint32_t token_ordinal = 0;
  std::int64_t duration_ns = 0;
  std::string source_label;
};

enum class ReportCostItemKind {
  kStructuralOccurrence,
  kCostLeaf,
};

struct ReportCostTreeEdge {
  ReportNodeOccurrenceId parent_occurrence_id;
  ReportCostItemKind child_kind = ReportCostItemKind::kStructuralOccurrence;
  ReportNodeOccurrenceId child_occurrence_id;
  ReportCostLeafId child_cost_leaf_id;
  std::uint32_t edge_order = 0;
};

struct ReportCostMetric {
  ReportCostItemKind item_kind = ReportCostItemKind::kStructuralOccurrence;
  ReportNodeOccurrenceId occurrence_id;
  ReportCostLeafId cost_leaf_id;
  std::int64_t self_duration_ns = 0;
  std::int64_t direct_child_duration_ns = 0;
  std::int64_t total_duration_ns = 0;
  std::uint32_t direct_structural_child_count = 0;
  std::uint32_t direct_cost_leaf_count = 0;
  std::uint32_t subtree_cost_leaf_count = 0;
};

struct ReportCostTree {
  std::vector<ReportCostTreeEdge> edges;
  std::vector<ReportCostMetric> metrics;
  std::vector<Diagnostic> diagnostics;
};

ReportCostTree build_report_cost_tree(
    const ReportTree& tree,
    const std::vector<ReportCostLeaf>& cost_leaves);

const ReportCostMetric* find_report_cost_metric(
    const ReportCostTree& cost_tree,
    ReportNodeOccurrenceId occurrence_id);

const ReportCostMetric* find_report_cost_leaf_metric(
    const ReportCostTree& cost_tree,
    ReportCostLeafId cost_leaf_id);

}  // namespace traceloom
