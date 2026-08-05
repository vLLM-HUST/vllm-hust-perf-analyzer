#include "traceloom/compat/structural_unit_rows.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "traceloom/core/sha256.h"
#include "traceloom/report/report_tree_builder.h"

namespace traceloom::compat {
namespace {

struct GraphSpan {
  ReportNodeOccurrenceId occurrence_id;
  ReportNodeDefId node_def_id;
  std::uint32_t token_begin = 0;
  std::uint32_t token_end = 0;
};

std::string anchor_compat_id(AnchorId id) {
  return id.valid() ? "anchor-" + std::to_string(id.value())
                    : std::string();
}

std::string occurrence_handle(const ReportTree& tree,
                              const ReportNodeOccurrence& occurrence) {
  const ReportNodeDef& def = node_def(tree, occurrence.node_def_id);
  return "node-" + def.local_node_id + "#" +
         std::to_string(occurrence.occurrence_index_for_def + 1);
}

std::vector<GraphSpan> graph_spans(const ReportTree& tree) {
  std::vector<GraphSpan> spans;
  for (const ReportNodeOccurrence& occurrence : tree.occurrences) {
    const ReportNodeDef& def = node_def(tree, occurrence.node_def_id);
    if (def.display_category != "graph_unit") {
      continue;
    }
    spans.push_back(GraphSpan{occurrence.id, occurrence.node_def_id,
                              occurrence.token_start_ordinal,
                              occurrence.token_end_ordinal});
  }
  std::stable_sort(spans.begin(), spans.end(),
                   [](const GraphSpan& lhs, const GraphSpan& rhs) {
                     return std::tie(lhs.token_begin, lhs.token_end,
                                     lhs.occurrence_id) <
                            std::tie(rhs.token_begin, rhs.token_end,
                                     rhs.occurrence_id);
                   });
  for (std::size_t index = 0; index < spans.size(); ++index) {
    if (spans[index].token_begin >= spans[index].token_end) {
      throw std::invalid_argument("graph unit has an empty token span");
    }
    if (index != 0 && spans[index].token_begin < spans[index - 1].token_end) {
      throw std::invalid_argument("graph unit token spans overlap");
    }
  }
  return spans;
}

std::string body_fingerprint(const std::vector<ReportToken>& tokens,
                             std::uint32_t begin,
                             std::uint32_t end,
                             const std::string& structural_identity = {}) {
  std::ostringstream canonical;
  canonical << "traceloom-structural-body-v1\n";
  if (!structural_identity.empty()) {
    canonical << "structural-identity:" << structural_identity.size() << ':'
              << structural_identity << '\n';
  }
  for (std::uint32_t index = begin; index < end; ++index) {
    const ReportToken& token = tokens[index];
    canonical << token.display_category.size() << ':'
              << token.display_category << '|'
              << token.display_op.size() << ':' << token.display_op << '|'
              << static_cast<int>(token.anchor_kind) << '\n';
  }
  return "H" + sha256_hex(canonical.str()).substr(0, 16);
}

std::vector<std::vector<ReportNodeOccurrenceId>> children_by_occurrence(
    const ReportTree& tree) {
  std::vector<std::vector<ReportNodeOccurrenceId>> children(
      tree.occurrences.size());
  for (const ReportTreeEdge& edge : tree.edges) {
    if (!edge.parent_occurrence_id.valid() ||
        edge.parent_occurrence_id.value() >= children.size()) {
      throw std::invalid_argument(
          "report edge has an invalid structural-unit parent");
    }
    children[edge.parent_occurrence_id.value()].push_back(
        edge.child_occurrence_id);
  }
  for (std::vector<ReportNodeOccurrenceId>& siblings : children) {
    std::stable_sort(
        siblings.begin(), siblings.end(),
        [&tree](ReportNodeOccurrenceId lhs_id,
                ReportNodeOccurrenceId rhs_id) {
          const ReportNodeOccurrence& lhs = node_occurrence(tree, lhs_id);
          const ReportNodeOccurrence& rhs = node_occurrence(tree, rhs_id);
          return std::tie(lhs.edge_order, lhs.token_start_ordinal, lhs.id) <
                 std::tie(rhs.edge_order, rhs.token_start_ordinal, rhs.id);
        });
  }
  return children;
}

void append_expansion_frontier(
    const ReportTree& tree,
    const std::vector<std::vector<ReportNodeOccurrenceId>>& children,
    ReportNodeOccurrenceId occurrence_id,
    std::uint32_t begin,
    std::uint32_t end,
    std::vector<std::string>& out) {
  const ReportNodeOccurrence& occurrence =
      node_occurrence(tree, occurrence_id);
  if (occurrence.token_end_ordinal <= begin ||
      occurrence.token_start_ordinal >= end) {
    return;
  }
  if (occurrence.token_start_ordinal >= begin &&
      occurrence.token_end_ordinal <= end) {
    out.push_back(occurrence_handle(tree, occurrence));
    return;
  }
  for (ReportNodeOccurrenceId child : children[occurrence_id.value()]) {
    append_expansion_frontier(tree, children, child, begin, end, out);
  }
}

std::string expansion_frontier(const ReportTree& tree,
                               std::uint32_t begin,
                               std::uint32_t end) {
  const auto root = std::find_if(
      tree.occurrences.begin(), tree.occurrences.end(),
      [](const ReportNodeOccurrence& occurrence) {
        return !occurrence.parent_occurrence_id.valid();
      });
  if (root == tree.occurrences.end()) {
    throw std::invalid_argument("structural partition requires a report root");
  }
  const auto children = children_by_occurrence(tree);
  std::vector<std::string> handles;
  for (ReportNodeOccurrenceId child : children[root->id.value()]) {
    append_expansion_frontier(tree, children, child, begin, end, handles);
  }
  std::ostringstream out;
  for (std::size_t index = 0; index < handles.size(); ++index) {
    if (index != 0) {
      out << ',';
    }
    out << handles[index];
  }
  return out.str();
}

StructuralUnitSqlRow make_unit(const ReportTree& tree,
                               const std::vector<ReportToken>& tokens,
                               std::uint32_t db_idx,
                               std::uint32_t unit_order,
                               std::uint32_t begin,
                               std::uint32_t end,
                               std::string kind,
                               std::string evidence_status,
                               std::string boundary_policy,
                               const std::string& structural_identity = {}) {
  if (begin >= end || end > tokens.size()) {
    throw std::invalid_argument("invalid structural-unit token span");
  }
  StructuralUnitSqlRow row;
  row.db_idx = db_idx;
  row.device_id = tokens[begin].device_id;
  row.unit_order = unit_order;
  row.kind = std::move(kind);
  row.run_count = 1;
  row.body_fingerprint =
      body_fingerprint(tokens, begin, end, structural_identity);
  row.token_start_ordinal = begin;
  row.token_end_ordinal = end;
  row.start_ns = tokens[begin].start_ns;
  row.end_ns = tokens[end - 1].end_ns;
  row.evidence_status = std::move(evidence_status);
  row.boundary_policy = std::move(boundary_policy);
  row.expansion_nodes = expansion_frontier(tree, begin, end);
  row.shape_signature = "unavailable";

  for (std::uint32_t index = begin; index < end; ++index) {
    const ReportToken& token = tokens[index];
    if (token.device_id != row.device_id) {
      throw std::invalid_argument(
          "structural unit spans more than one device sequence");
    }
    row.start_ns = std::min(row.start_ns, token.start_ns);
    row.end_ns = std::max(row.end_ns, token.end_ns);
    if (token.anchor_id.valid()) {
      ++row.anchor_count;
      const std::uint32_t anchor_idx = token.anchor_id.value() + 1;
      if (row.first_anchor_idx == 0 || anchor_idx < row.first_anchor_idx) {
        row.first_anchor_idx = anchor_idx;
      }
      row.last_anchor_idx = std::max(row.last_anchor_idx, anchor_idx);
    }
    const double timeline_us =
        token.has_prelude_cost
            ? token.timeline_anchor_us
            : static_cast<double>(token.end_ns - token.start_ns) / 1000.0;
    if (token.anchor_kind == ReportAnchorKind::kCollective) {
      row.comm_us += timeline_us;
    } else {
      row.compute_us += timeline_us;
    }
    if (token.has_prelude_cost) {
      row.compute_us += token.prelude_exec_aux_us;
      row.comm_us += token.prelude_comm_us;
      row.idle_us += token.prelude_idle_us;
      row.aux_events += token.prelude_aux_event_count;
      row.aux_us += token.prelude_aux_us;
    }
  }
  row.span_us = static_cast<double>(row.end_ns - row.start_ns) / 1000.0;
  row.total_us = row.compute_us + row.comm_us + row.idle_us;
  return row;
}

bool can_fold(const StructuralUnitSqlRow& lhs,
              const StructuralUnitSqlRow& rhs) {
  return lhs.kind == rhs.kind &&
         lhs.evidence_status == rhs.evidence_status &&
         lhs.boundary_policy == rhs.boundary_policy &&
         lhs.body_fingerprint == rhs.body_fingerprint &&
         lhs.token_end_ordinal == rhs.token_start_ordinal;
}

void fold_into(StructuralUnitSqlRow& lhs,
               const StructuralUnitSqlRow& rhs) {
  lhs.run_count += rhs.run_count;
  lhs.token_end_ordinal = rhs.token_end_ordinal;
  lhs.last_anchor_idx = std::max(lhs.last_anchor_idx, rhs.last_anchor_idx);
  lhs.anchor_count += rhs.anchor_count;
  lhs.start_ns = std::min(lhs.start_ns, rhs.start_ns);
  lhs.end_ns = std::max(lhs.end_ns, rhs.end_ns);
  lhs.span_us = static_cast<double>(lhs.end_ns - lhs.start_ns) / 1000.0;
  lhs.compute_us += rhs.compute_us;
  lhs.comm_us += rhs.comm_us;
  lhs.idle_us += rhs.idle_us;
  lhs.total_us += rhs.total_us;
  lhs.aux_events += rhs.aux_events;
  lhs.aux_us += rhs.aux_us;
  if (!rhs.expansion_nodes.empty()) {
    if (!lhs.expansion_nodes.empty()) {
      lhs.expansion_nodes += ',';
    }
    lhs.expansion_nodes += rhs.expansion_nodes;
  }
}

void assign_ids(std::vector<StructuralUnitSqlRow>& rows) {
  std::map<std::pair<std::string, std::string>, std::string> family_by_key;
  std::map<std::string, std::uint32_t> next_family_by_kind;
  std::map<std::string, std::uint32_t> next_unit_by_kind;
  for (StructuralUnitSqlRow& row : rows) {
    const char prefix = row.kind == "graph_unit"
                            ? 'G'
                            : (row.kind == "structural_unit" ? 'U' : 'X');
    const std::string prefix_text(1, prefix);
    const auto key = std::make_pair(row.kind, row.body_fingerprint);
    const auto found = family_by_key.find(key);
    if (found == family_by_key.end()) {
      const std::uint32_t family_index = ++next_family_by_kind[row.kind];
      row.family_id = prefix_text + "F" + std::to_string(family_index);
      family_by_key.emplace(key, row.family_id);
    } else {
      row.family_id = found->second;
    }
    const std::uint32_t unit_index = ++next_unit_by_kind[row.kind];
    row.unit_id = prefix_text + std::to_string(unit_index);
  }
}

}  // namespace

StructuralUnitSqlRows build_structural_unit_sql_rows(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    std::uint32_t db_idx) {
  StructuralUnitSqlRows out;
  if (tokens.empty()) {
    return out;
  }
  validate_report_tree_or_throw(tree,
                                static_cast<std::uint32_t>(tokens.size()));
  const std::vector<GraphSpan> graphs = graph_spans(tree);
  if (graphs.empty()) {
    return out;
  }

  std::vector<StructuralUnitSqlRow> unfolded;
  std::uint32_t cursor = 0;
  for (const GraphSpan& graph : graphs) {
    if (cursor < graph.token_begin) {
      const bool open_prefix = cursor == 0;
      unfolded.push_back(make_unit(
          tree, tokens, db_idx, static_cast<std::uint32_t>(unfolded.size()),
          cursor, graph.token_begin,
          open_prefix ? "unrecognized" : "structural_unit",
          open_prefix ? "unrecognized_open_prefix" : "complete",
          open_prefix ? "open_trace_prefix"
                      : "bounded_by_adjacent_exact_graph_units"));
    }
    unfolded.push_back(make_unit(
        tree, tokens, db_idx, static_cast<std::uint32_t>(unfolded.size()),
        graph.token_begin, graph.token_end, "graph_unit", "exact",
        "direct_exact_graph_unit",
        node_def(tree, graph.node_def_id).display_category + "\n" +
            node_def(tree, graph.node_def_id).display_op));
    cursor = graph.token_end;
  }
  if (cursor < tokens.size()) {
    unfolded.push_back(make_unit(
        tree, tokens, db_idx, static_cast<std::uint32_t>(unfolded.size()),
        cursor, static_cast<std::uint32_t>(tokens.size()), "unrecognized",
        "unrecognized_open_suffix", "open_trace_suffix"));
  }

  std::vector<std::uint32_t> coverage(tokens.size(), 0);
  for (const StructuralUnitSqlRow& row : unfolded) {
    for (std::uint32_t index = row.token_start_ordinal;
         index < row.token_end_ordinal; ++index) {
      ++coverage[index];
    }
  }
  if (!std::all_of(coverage.begin(), coverage.end(),
                   [](std::uint32_t count) { return count == 1; })) {
    throw std::invalid_argument(
        "structural units do not partition every token exactly once");
  }

  for (StructuralUnitSqlRow& row : unfolded) {
    if (!out.units.empty() && can_fold(out.units.back(), row)) {
      fold_into(out.units.back(), row);
    } else {
      out.units.push_back(std::move(row));
    }
  }
  assign_ids(out.units);
  for (std::size_t unit_index = 0; unit_index < out.units.size();
       ++unit_index) {
    StructuralUnitSqlRow& unit = out.units[unit_index];
    unit.unit_order = static_cast<std::uint32_t>(unit_index);
    std::uint32_t anchor_order = 0;
    for (std::uint32_t token_index = unit.token_start_ordinal;
         token_index < unit.token_end_ordinal; ++token_index) {
      const ReportToken& token = tokens[token_index];
      if (!token.anchor_id.valid()) {
        continue;
      }
      out.unit_anchors.push_back(StructuralUnitAnchorSqlRow{
          unit.unit_id, anchor_compat_id(token.anchor_id), db_idx,
          unit.device_id, anchor_order++, "observed_member"});
    }
  }
  return out;
}

}  // namespace traceloom::compat
