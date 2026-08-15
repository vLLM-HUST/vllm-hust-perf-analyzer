#include "traceloom/analysis/structural_cost_tree.h"

#include "traceloom/analysis/structural_cost_handoff.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace traceloom {

namespace {

void add_error(StructuralCostTree& out, std::string code, std::string message) {
  out.diagnostics.push_back(Diagnostic{
      DiagnosticSeverity::kError,
      std::move(code),
      std::move(message),
  });
}

bool has_errors(const StructuralCostTree& out) {
  return std::any_of(out.diagnostics.begin(), out.diagnostics.end(),
                     [](const Diagnostic& diagnostic) {
                       return diagnostic.severity == DiagnosticSeverity::kError;
                     });
}

std::size_t checked_index(StructuralNodeOccurrenceId id,
                          std::size_t size,
                          const char* what) {
  if (!id.valid() || id.value() >= size) {
    throw std::out_of_range(what);
  }
  return id.value();
}

std::size_t checked_index(StructuralCostLeafId id,
                          std::size_t size,
                          const char* what) {
  if (!id.valid() || id.value() >= size) {
    throw std::out_of_range(what);
  }
  return id.value();
}

}  // namespace

StructuralCostTree build_structural_cost_tree(
    const StructuralOccurrenceGraph& tree,
    const std::vector<StructuralCostLeaf>& cost_leaves) {
  StructuralCostTree out;

  std::vector<std::uint32_t> structural_child_counts(tree.occurrences.size(), 0);
  for (const StructuralOccurrenceEdge& edge : tree.edges) {
    if (!edge.parent_occurrence_id.valid() ||
        edge.parent_occurrence_id.value() >= tree.occurrences.size() ||
        !edge.child_occurrence_id.valid() ||
        edge.child_occurrence_id.value() >= tree.occurrences.size()) {
      add_error(out, "cost_tree_invalid_structural_edge",
                "structural graph edge references an unknown occurrence");
      continue;
    }
    structural_child_counts[edge.parent_occurrence_id.value()] += 1;
    out.edges.push_back(StructuralCostTreeEdge{
        edge.parent_occurrence_id,
        StructuralCostItemKind::kStructuralOccurrence,
        edge.child_occurrence_id,
        StructuralCostLeafId::invalid(),
        edge.edge_order,
    });
  }

  std::unordered_map<std::uint32_t, StructuralNodeOccurrenceId> atom_by_token;
  for (const StructuralCostHandoffRow& row :
       collect_structural_cost_handoff_rows(tree)) {
    if (row.token_end_ordinal <= row.token_start_ordinal) {
      add_error(out, "cost_tree_empty_atom_span",
                "atom cost handoff row has an empty token span");
      continue;
    }
    for (std::uint32_t token = row.token_start_ordinal;
         token < row.token_end_ordinal; ++token) {
      const auto inserted = atom_by_token.emplace(token, row.atom_occurrence_id);
      if (!inserted.second) {
        add_error(out, "cost_tree_duplicate_atom_owner",
                  "more than one atom occurrence owns the same token ordinal");
      }
    }
  }

  std::vector<std::vector<StructuralNodeOccurrenceId>> structural_children(
      tree.occurrences.size());
  for (const StructuralOccurrenceEdge& edge : tree.edges) {
    if (edge.parent_occurrence_id.valid() &&
        edge.parent_occurrence_id.value() < tree.occurrences.size() &&
        edge.child_occurrence_id.valid() &&
        edge.child_occurrence_id.value() < tree.occurrences.size()) {
      structural_children[edge.parent_occurrence_id.value()].push_back(
          edge.child_occurrence_id);
    }
  }

  std::vector<std::vector<StructuralCostLeafId>> cost_leaf_children(
      tree.occurrences.size());
  for (std::size_t i = 0; i < cost_leaves.size(); ++i) {
    const StructuralCostLeaf& leaf = cost_leaves[i];
    if (!leaf.id.valid() || leaf.id.value() != i) {
      add_error(out, "cost_tree_invalid_cost_leaf_id",
                "cost leaf ids must be dense and ordered");
      continue;
    }
    if (leaf.duration_ns < 0) {
      add_error(out, "cost_tree_negative_cost_leaf_duration",
                "cost leaf duration cannot be negative");
      continue;
    }
    const auto owner = atom_by_token.find(leaf.token_ordinal);
    if (owner == atom_by_token.end()) {
      add_error(out, "cost_tree_orphan_cost_leaf",
                "cost leaf token ordinal is not covered by an atom occurrence");
      continue;
    }
    const StructuralNodeOccurrenceId parent_id = owner->second;
    cost_leaf_children[parent_id.value()].push_back(leaf.id);
    out.edges.push_back(StructuralCostTreeEdge{
        parent_id,
        StructuralCostItemKind::kCostLeaf,
        StructuralNodeOccurrenceId::invalid(),
        leaf.id,
        structural_child_counts[parent_id.value()] +
            static_cast<std::uint32_t>(
                cost_leaf_children[parent_id.value()].size() - 1),
    });
  }

  if (has_errors(out)) {
    out.edges.clear();
    return out;
  }

  out.metrics.reserve(tree.occurrences.size() + cost_leaves.size());
  std::vector<StructuralCostMetric> occurrence_metrics(tree.occurrences.size());
  for (const StructuralNodeOccurrence& occurrence : tree.occurrences) {
    StructuralCostMetric& metric = occurrence_metrics[occurrence.id.value()];
    metric.item_kind = StructuralCostItemKind::kStructuralOccurrence;
    metric.occurrence_id = occurrence.id;
    metric.cost_leaf_id = StructuralCostLeafId::invalid();
    metric.direct_structural_child_count =
        static_cast<std::uint32_t>(
            structural_children[occurrence.id.value()].size());
    metric.direct_cost_leaf_count =
        static_cast<std::uint32_t>(
            cost_leaf_children[occurrence.id.value()].size());
  }

  for (const StructuralCostLeaf& leaf : cost_leaves) {
    out.metrics.push_back(StructuralCostMetric{
        StructuralCostItemKind::kCostLeaf,
        StructuralNodeOccurrenceId::invalid(),
        leaf.id,
        leaf.duration_ns,
        0,
        leaf.duration_ns,
        0,
        0,
        1,
    });
  }

  std::vector<std::uint8_t> visit_state(tree.occurrences.size(), 0);
  auto compute = [&](auto&& self, StructuralNodeOccurrenceId occurrence_id)
      -> StructuralCostMetric& {
    const std::size_t index =
        checked_index(occurrence_id, tree.occurrences.size(),
                      "StructuralNodeOccurrenceId out of range");
    if (visit_state[index] == 2) {
      return occurrence_metrics[index];
    }
    if (visit_state[index] == 1) {
      throw std::invalid_argument("structural cost tree cycle");
    }
    visit_state[index] = 1;

    StructuralCostMetric& metric = occurrence_metrics[index];
    metric.direct_child_duration_ns = 0;
    metric.total_duration_ns = metric.self_duration_ns;
    metric.subtree_cost_leaf_count = 0;

    for (StructuralNodeOccurrenceId child_id : structural_children[index]) {
      const StructuralCostMetric& child = self(self, child_id);
      metric.direct_child_duration_ns += child.total_duration_ns;
      metric.total_duration_ns += child.total_duration_ns;
      metric.subtree_cost_leaf_count += child.subtree_cost_leaf_count;
    }
    for (StructuralCostLeafId leaf_id : cost_leaf_children[index]) {
      const StructuralCostLeaf& leaf =
          cost_leaves[checked_index(leaf_id, cost_leaves.size(),
                                    "StructuralCostLeafId out of range")];
      metric.direct_child_duration_ns += leaf.duration_ns;
      metric.total_duration_ns += leaf.duration_ns;
      metric.subtree_cost_leaf_count += 1;
    }

    visit_state[index] = 2;
    return metric;
  };

  for (const StructuralNodeOccurrence& occurrence : tree.occurrences) {
    compute(compute, occurrence.id);
  }

  for (const StructuralNodeOccurrence& occurrence : tree.occurrences) {
    out.metrics.push_back(occurrence_metrics[occurrence.id.value()]);
  }

  std::sort(out.metrics.begin(), out.metrics.end(),
            [](const StructuralCostMetric& lhs, const StructuralCostMetric& rhs) {
              if (lhs.item_kind != rhs.item_kind) {
                return lhs.item_kind ==
                       StructuralCostItemKind::kStructuralOccurrence;
              }
              if (lhs.item_kind == StructuralCostItemKind::kStructuralOccurrence) {
                return lhs.occurrence_id < rhs.occurrence_id;
              }
              return lhs.cost_leaf_id < rhs.cost_leaf_id;
            });

  return out;
}

const StructuralCostMetric* find_structural_cost_metric(
    const StructuralCostTree& cost_tree,
    StructuralNodeOccurrenceId occurrence_id) {
  for (const StructuralCostMetric& metric : cost_tree.metrics) {
    if (metric.item_kind == StructuralCostItemKind::kStructuralOccurrence &&
        metric.occurrence_id == occurrence_id) {
      return &metric;
    }
  }
  return nullptr;
}

const StructuralCostMetric* find_structural_cost_leaf_metric(
    const StructuralCostTree& cost_tree,
    StructuralCostLeafId cost_leaf_id) {
  for (const StructuralCostMetric& metric : cost_tree.metrics) {
    if (metric.item_kind == StructuralCostItemKind::kCostLeaf &&
        metric.cost_leaf_id == cost_leaf_id) {
      return &metric;
    }
  }
  return nullptr;
}

}  // namespace traceloom
