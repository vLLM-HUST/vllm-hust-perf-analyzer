#include "traceloom/analysis/structural_occurrence_builder.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace traceloom {

void validate_structural_occurrence_graph_or_throw(
    const StructuralOccurrenceGraph& tree, std::uint32_t token_count) {
  if (tree.node_defs.empty()) {
    throw std::invalid_argument("structural occurrence graph has no node defs");
  }
  if (tree.occurrences.empty()) {
    throw std::invalid_argument(
        "structural occurrence graph has no occurrences");
  }

  for (std::size_t index = 0; index < tree.node_defs.size(); ++index) {
    const StructuralNodeDef& def = tree.node_defs[index];
    if (!def.id.valid() || def.id.value() != index) {
      throw std::invalid_argument(
          "structural node definitions must use dense ordered ids");
    }
    if (def.kind == StructuralNodeKind::kRepeat && def.repeat_count == 0) {
      throw std::invalid_argument(
          "structural repeat definition has zero repeat count");
    }
  }

  std::size_t root_count = 0;
  StructuralNodeOccurrenceId root_id = StructuralNodeOccurrenceId::invalid();
  std::vector<std::vector<StructuralNodeOccurrenceId>> children(
      tree.occurrences.size());
  std::vector<std::vector<std::uint32_t>> occurrence_indices_by_def(
      tree.node_defs.size());
  for (std::size_t index = 0; index < tree.occurrences.size(); ++index) {
    const StructuralNodeOccurrence& occurrence = tree.occurrences[index];
    if (!occurrence.id.valid() || occurrence.id.value() != index) {
      throw std::invalid_argument(
          "structural occurrences must use dense ordered ids");
    }
    if (!occurrence.node_def_id.valid() ||
        occurrence.node_def_id.value() >= tree.node_defs.size()) {
      throw std::invalid_argument("occurrence node_def_id out of range");
    }
    if (occurrence.token_start_ordinal > occurrence.token_end_ordinal ||
        occurrence.token_end_ordinal > token_count) {
      throw std::invalid_argument("occurrence span out of range");
    }
    occurrence_indices_by_def[occurrence.node_def_id.value()].push_back(
        occurrence.occurrence_index_for_def);
    if (!occurrence.parent_occurrence_id.valid()) {
      ++root_count;
      root_id = occurrence.id;
      continue;
    }
    const StructuralNodeOccurrence& parent =
        structural_node_occurrence(tree, occurrence.parent_occurrence_id);
    if (occurrence.token_start_ordinal < parent.token_start_ordinal ||
        occurrence.token_end_ordinal > parent.token_end_ordinal) {
      throw std::invalid_argument("child span outside parent");
    }
    children[parent.id.value()].push_back(occurrence.id);
  }
  if (root_count != 1) {
    throw std::invalid_argument(
        "structural occurrence graph must have exactly one root");
  }
  for (std::size_t def_index = 0; def_index < tree.node_defs.size();
       ++def_index) {
    std::vector<std::uint32_t>& indices = occurrence_indices_by_def[def_index];
    if (indices.empty()) {
      throw std::invalid_argument(
          "structural node definition has no realized occurrence");
    }
    std::sort(indices.begin(), indices.end());
    for (std::size_t occurrence_index = 0;
         occurrence_index < indices.size(); ++occurrence_index) {
      if (indices[occurrence_index] != occurrence_index) {
        throw std::invalid_argument(
            "structural per-definition occurrence indices are not dense");
      }
    }
    if (!tree.occurrence_counts_by_def.empty() &&
        (tree.occurrence_counts_by_def.size() != tree.node_defs.size() ||
         tree.occurrence_counts_by_def[def_index] != indices.size())) {
      throw std::invalid_argument(
          "structural cached occurrence count disagrees with occurrences");
    }
  }
  const StructuralNodeOccurrence& root =
      structural_node_occurrence(tree, root_id);
  if (root.token_start_ordinal != 0 ||
      root.token_end_ordinal != token_count) {
    throw std::invalid_argument(
        "structural occurrence root does not cover all tokens");
  }

  std::unordered_set<std::uint64_t> edge_keys;
  std::vector<std::uint32_t> incoming_edge_counts(tree.occurrences.size(), 0);
  for (const StructuralOccurrenceEdge& edge : tree.edges) {
    const StructuralNodeOccurrence& parent =
        structural_node_occurrence(tree, edge.parent_occurrence_id);
    const StructuralNodeOccurrence& child =
        structural_node_occurrence(tree, edge.child_occurrence_id);
    if (child.parent_occurrence_id != parent.id) {
      throw std::invalid_argument("edge disagrees with child parent id");
    }
    const std::uint64_t key =
        (static_cast<std::uint64_t>(parent.id.value()) << 32u) |
        static_cast<std::uint64_t>(edge.edge_order);
    if (!edge_keys.insert(key).second) {
      throw std::invalid_argument("duplicate sibling edge order");
    }
    ++incoming_edge_counts[child.id.value()];
  }
  for (const StructuralNodeOccurrence& occurrence : tree.occurrences) {
    const std::uint32_t expected = occurrence.id == root_id ? 0 : 1;
    if (incoming_edge_counts[occurrence.id.value()] != expected) {
      throw std::invalid_argument(
          "structural occurrence does not have exactly one parent edge");
    }

    const StructuralNodeDef& def =
        structural_node_def(tree, occurrence.node_def_id);
    std::vector<StructuralNodeOccurrenceId>& occurrence_children =
        children[occurrence.id.value()];
    std::sort(occurrence_children.begin(), occurrence_children.end(),
              [&tree](StructuralNodeOccurrenceId lhs,
                      StructuralNodeOccurrenceId rhs) {
                const StructuralNodeOccurrence& left =
                    structural_node_occurrence(tree, lhs);
                const StructuralNodeOccurrence& right =
                    structural_node_occurrence(tree, rhs);
                if (left.token_start_ordinal != right.token_start_ordinal) {
                  return left.token_start_ordinal < right.token_start_ordinal;
                }
                return left.edge_order < right.edge_order;
              });
    if (def.kind == StructuralNodeKind::kAtom) {
      if (!occurrence_children.empty() ||
          occurrence.token_end_ordinal !=
              occurrence.token_start_ordinal + 1) {
        throw std::invalid_argument(
            "structural atom must own exactly one token and no children");
      }
      continue;
    }
    if (def.kind == StructuralNodeKind::kRepeat &&
        (occurrence.token_end_ordinal - occurrence.token_start_ordinal) %
                def.repeat_count !=
            0) {
      throw std::invalid_argument(
          "structural repeat span is not divisible by repeat count");
    }
    std::uint32_t cursor = occurrence.token_start_ordinal;
    for (StructuralNodeOccurrenceId child_id : occurrence_children) {
      const StructuralNodeOccurrence& child =
          structural_node_occurrence(tree, child_id);
      if (child.token_start_ordinal != cursor) {
        throw std::invalid_argument(
            "structural children do not exactly tile parent span");
      }
      cursor = child.token_end_ordinal;
    }
    if (cursor != occurrence.token_end_ordinal) {
      throw std::invalid_argument(
          "structural children do not exactly tile parent span");
    }
  }

  std::vector<std::uint32_t> coverage_counts(tree.occurrences.size(), 0);
  for (const StructuralNodeCoverage& row : tree.coverage) {
    if (!row.node_occurrence_id.valid() ||
        row.node_occurrence_id.value() >= tree.occurrences.size()) {
      throw std::invalid_argument("coverage occurrence id out of range");
    }
    if (row.token_start_ordinal > row.token_end_ordinal ||
        row.token_end_ordinal > token_count) {
      throw std::invalid_argument("coverage span out of range");
    }
    const StructuralNodeOccurrence& occurrence =
        structural_node_occurrence(tree, row.node_occurrence_id);
    const StructuralNodeDef& def =
        structural_node_def(tree, occurrence.node_def_id);
    if (row.token_start_ordinal != occurrence.token_start_ordinal ||
        row.token_end_ordinal != occurrence.token_end_ordinal) {
      throw std::invalid_argument("coverage span disagrees with occurrence");
    }
    const StructuralCoverageKind expected_kind =
        def.kind == StructuralNodeKind::kAtom ? StructuralCoverageKind::kAtomLeaf
                                          : StructuralCoverageKind::kDirectBody;
    if (row.kind != expected_kind) {
      throw std::invalid_argument("coverage kind disagrees with node kind");
    }
    ++coverage_counts[occurrence.id.value()];
  }
  for (std::uint32_t count : coverage_counts) {
    if (count != 1) {
      throw std::invalid_argument(
          "structural occurrence must have exactly one coverage row");
    }
  }
}

}  // namespace traceloom
