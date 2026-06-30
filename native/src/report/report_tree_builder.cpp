#include "traceloom/report/report_tree_builder.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace traceloom {
namespace {

std::string local_node_id(std::size_t index) {
  const std::uint32_t n = static_cast<std::uint32_t>(index + 1);
  std::string digits = std::to_string(n);
  if (digits.size() < 3) {
    digits.insert(digits.begin(), 3 - digits.size(), '0');
  }
  return "N" + digits;
}

bool same_visible_token(const ReportToken& lhs, const ReportToken& rhs) {
  return lhs.symbol_id == rhs.symbol_id && lhs.display_op == rhs.display_op &&
         lhs.display_category == rhs.display_category &&
         lhs.anchor_kind == rhs.anchor_kind;
}

ReportNodeDefId append_def(ReportTree& tree,
                           ReportNodeKind kind,
                           std::string display_op,
                           std::string display_category,
                           SymbolId symbol_id,
                           std::uint32_t repeat_count,
                           std::uint32_t display_depth,
                           std::uint32_t loop_depth,
                           std::string visibility_reason) {
  const ReportNodeDefId id =
      checked_next_id<ReportNodeDefId>(tree.node_defs.size());
  ReportNodeDef def;
  def.id = id;
  def.local_node_id = local_node_id(tree.node_defs.size());
  def.kind = kind;
  def.display_op = std::move(display_op);
  def.display_category = std::move(display_category);
  def.symbol_id = symbol_id;
  def.repeat_count = repeat_count;
  def.definition_order = id.value();
  def.display_depth = display_depth;
  def.loop_depth = loop_depth;
  def.visibility_reason = std::move(visibility_reason);
  tree.node_defs.push_back(std::move(def));
  return id;
}

ReportNodeOccurrenceId append_occurrence(ReportTree& tree,
                                         ReportNodeDefId def_id,
                                         ReportNodeOccurrenceId parent_id,
                                         std::uint32_t edge_order,
                                         std::uint32_t token_start,
                                         std::uint32_t token_end,
                                         std::uint32_t repeat_iteration) {
  const ReportNodeOccurrenceId id =
      checked_next_id<ReportNodeOccurrenceId>(tree.occurrences.size());
  std::uint32_t occurrence_index = 0;
  for (const ReportNodeOccurrence& existing : tree.occurrences) {
    if (existing.node_def_id == def_id) {
      ++occurrence_index;
    }
  }

  ReportNodeOccurrence occurrence;
  occurrence.id = id;
  occurrence.node_def_id = def_id;
  occurrence.parent_occurrence_id = parent_id;
  occurrence.edge_order = edge_order;
  occurrence.occurrence_index_for_def = occurrence_index;
  occurrence.token_start_ordinal = token_start;
  occurrence.token_end_ordinal = token_end;
  occurrence.repeat_iteration = repeat_iteration;
  tree.occurrences.push_back(occurrence);

  if (parent_id.valid()) {
    tree.edges.push_back(ReportTreeEdge{parent_id, id, edge_order});
  }
  return id;
}

void append_coverage(ReportTree& tree,
                     ReportNodeOccurrenceId occurrence_id,
                     std::uint32_t token_start,
                     std::uint32_t token_end,
                     ReportCoverageKind kind) {
  tree.coverage.push_back(
      ReportNodeCoverage{occurrence_id, token_start, token_end, kind});
}

void append_atom_occurrence(ReportTree& tree,
                            const ReportToken& token,
                            ReportNodeOccurrenceId parent_id,
                            std::uint32_t edge_order,
                            std::uint32_t token_start,
                            std::uint32_t repeat_iteration,
                            ReportNodeDefId atom_def_id) {
  const ReportNodeOccurrenceId occurrence_id = append_occurrence(
      tree, atom_def_id, parent_id, edge_order, token_start, token_start + 1,
      repeat_iteration);
  append_coverage(tree, occurrence_id, token_start, token_start + 1,
                  ReportCoverageKind::kAtomLeaf);
  (void)token;
}

}  // namespace

ReportTree build_report_tree_from_tokens(const std::vector<ReportToken>& tokens,
                                         ReportTreeBuildConfig config) {
  if (config.min_run_length == 0) {
    throw std::invalid_argument("ReportTreeBuildConfig min_run_length is zero");
  }

  ReportTree tree;
  const ReportNodeDefId root_def =
      append_def(tree, ReportNodeKind::kSeq, "Seq", "", SymbolId::invalid(), 0,
                 0, 0, "root");
  const ReportNodeOccurrenceId root_occurrence = append_occurrence(
      tree, root_def, ReportNodeOccurrenceId::invalid(), 0, 0,
      static_cast<std::uint32_t>(tokens.size()), 0);
  append_coverage(tree, root_occurrence, 0,
                  static_cast<std::uint32_t>(tokens.size()),
                  ReportCoverageKind::kDirectBody);

  std::uint32_t edge_order = 1;
  for (std::size_t i = 0; i < tokens.size();) {
    std::size_t run_end = i + 1;
    while (run_end < tokens.size() &&
           same_visible_token(tokens[i], tokens[run_end])) {
      ++run_end;
    }
    const std::uint32_t run_len = static_cast<std::uint32_t>(run_end - i);
    const std::uint32_t start = static_cast<std::uint32_t>(i);
    const std::uint32_t end = static_cast<std::uint32_t>(run_end);

    if (config.fold_adjacent_runs && run_len >= config.min_run_length) {
      const ReportNodeDefId repeat_def = append_def(
          tree, ReportNodeKind::kRepeat, "Rep x" + std::to_string(run_len), "",
          SymbolId::invalid(), run_len, 1, 1, "adjacent_same_symbol_run");
      const ReportNodeOccurrenceId repeat_occurrence = append_occurrence(
          tree, repeat_def, root_occurrence, edge_order++, start, end, 0);
      append_coverage(tree, repeat_occurrence, start, end,
                      ReportCoverageKind::kDirectBody);

      const ReportNodeDefId atom_def = append_def(
          tree, ReportNodeKind::kAtom, tokens[i].display_op,
          tokens[i].display_category, tokens[i].symbol_id, 0, 2, 1,
          "repeat_body_atom");
      for (std::uint32_t offset = 0; offset < run_len; ++offset) {
        append_atom_occurrence(tree, tokens[i + offset], repeat_occurrence,
                               offset + 1, start + offset, offset + 1,
                               atom_def);
      }
    } else {
      for (std::size_t j = i; j < run_end; ++j) {
        const ReportNodeDefId atom_def = append_def(
            tree, ReportNodeKind::kAtom, tokens[j].display_op,
            tokens[j].display_category, tokens[j].symbol_id, 0, 1, 0,
            "atom");
        append_atom_occurrence(tree, tokens[j], root_occurrence, edge_order++,
                               static_cast<std::uint32_t>(j), 0, atom_def);
      }
    }
    i = run_end;
  }

  validate_report_tree_or_throw(tree, static_cast<std::uint32_t>(tokens.size()));
  return tree;
}

void validate_report_tree_or_throw(const ReportTree& tree,
                                   std::uint32_t token_count) {
  if (tree.node_defs.empty()) {
    throw std::invalid_argument("report tree has no node defs");
  }
  if (tree.occurrences.empty()) {
    throw std::invalid_argument("report tree has no occurrences");
  }

  std::size_t root_count = 0;
  for (const ReportNodeOccurrence& occurrence : tree.occurrences) {
    if (!occurrence.id.valid() || occurrence.id.value() >= tree.occurrences.size()) {
      throw std::invalid_argument("occurrence id out of range");
    }
    if (!occurrence.node_def_id.valid() ||
        occurrence.node_def_id.value() >= tree.node_defs.size()) {
      throw std::invalid_argument("occurrence node_def_id out of range");
    }
    if (occurrence.token_start_ordinal > occurrence.token_end_ordinal ||
        occurrence.token_end_ordinal > token_count) {
      throw std::invalid_argument("occurrence span out of range");
    }
    if (!occurrence.parent_occurrence_id.valid()) {
      ++root_count;
      continue;
    }
    const ReportNodeOccurrence& parent =
        node_occurrence(tree, occurrence.parent_occurrence_id);
    if (occurrence.token_start_ordinal < parent.token_start_ordinal ||
        occurrence.token_end_ordinal > parent.token_end_ordinal) {
      throw std::invalid_argument("child span outside parent");
    }
  }
  if (root_count != 1) {
    throw std::invalid_argument("report tree must have exactly one root");
  }

  std::unordered_set<std::uint64_t> edge_keys;
  for (const ReportTreeEdge& edge : tree.edges) {
    const ReportNodeOccurrence& parent =
        node_occurrence(tree, edge.parent_occurrence_id);
    const ReportNodeOccurrence& child =
        node_occurrence(tree, edge.child_occurrence_id);
    if (child.parent_occurrence_id != parent.id) {
      throw std::invalid_argument("edge disagrees with child parent id");
    }
    const std::uint64_t key =
        (static_cast<std::uint64_t>(parent.id.value()) << 32u) |
        static_cast<std::uint64_t>(edge.edge_order);
    if (!edge_keys.insert(key).second) {
      throw std::invalid_argument("duplicate sibling edge order");
    }
  }

  for (const ReportNodeCoverage& row : tree.coverage) {
    if (!row.node_occurrence_id.valid() ||
        row.node_occurrence_id.value() >= tree.occurrences.size()) {
      throw std::invalid_argument("coverage occurrence id out of range");
    }
    if (row.token_start_ordinal > row.token_end_ordinal ||
        row.token_end_ordinal > token_count) {
      throw std::invalid_argument("coverage span out of range");
    }
  }
}

}  // namespace traceloom
