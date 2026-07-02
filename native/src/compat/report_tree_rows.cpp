#include "traceloom/compat/report_tree_rows.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/aux_attribution_rows.h"
#include "traceloom/report/report_tree_builder.h"

namespace traceloom::compat {
namespace {

double ns_to_us(std::int64_t ns) {
  return static_cast<double>(ns) / 1000.0;
}

std::string symbol_value_or_empty(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

ReportAnchorKind report_anchor_kind_for_anchor_kind(AnchorKind kind) {
  switch (kind) {
    case AnchorKind::kDeviceEvent:
      return ReportAnchorKind::kExec;
    case AnchorKind::kCommunication:
      return ReportAnchorKind::kCollective;
    case AnchorKind::kGraphH:
      return ReportAnchorKind::kGraphH;
    case AnchorKind::kGraphL:
      return ReportAnchorKind::kGraphL;
    case AnchorKind::kGraphT:
      return ReportAnchorKind::kGraphT;
    case AnchorKind::kGraphReplayUnit:
      return ReportAnchorKind::kGraphTemplate;
    case AnchorKind::kSynchronization:
    case AnchorKind::kUnknown:
      return ReportAnchorKind::kUnknown;
  }
  return ReportAnchorKind::kUnknown;
}

const char* report_node_kind_name(ReportNodeKind kind) {
  switch (kind) {
    case ReportNodeKind::kSeq:
      return "seq";
    case ReportNodeKind::kRepeat:
      return "repeat";
    case ReportNodeKind::kAtom:
      return "atom";
  }
  return "unknown";
}

const char* report_node_type_name(ReportNodeKind kind) {
  switch (kind) {
    case ReportNodeKind::kSeq:
      return "Seq";
    case ReportNodeKind::kRepeat:
      return "Repeat";
    case ReportNodeKind::kAtom:
      return "Atom";
  }
  return "Unknown";
}

std::string node_id_for_def(const ReportNodeDef& def) {
  return "node-" + def.local_node_id;
}

std::uint32_t anchor_idx_for_token(const ReportToken& token) {
  if (!token.anchor_id.valid()) {
    return 0;
  }
  return token.anchor_id.value() + 1;
}

double token_duration_us(const ReportToken& token) {
  return ns_to_us(token.end_ns - token.start_ns);
}

bool is_comm_token(const ReportToken& token) {
  return token.anchor_kind == ReportAnchorKind::kCollective;
}

std::string coverage_kind_name(ReportCoverageKind kind) {
  switch (kind) {
    case ReportCoverageKind::kDirectBody:
      return "body";
    case ReportCoverageKind::kAtomLeaf:
      return "self";
  }
  return "unknown";
}

struct NodeAccum {
  std::set<std::uint32_t> token_ordinals;
  std::uint32_t occurrence_count = 0;
  std::uint32_t first_anchor_idx = 0;
  std::uint32_t last_anchor_idx = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  double compute_us = 0.0;
  double comm_us = 0.0;
  double idle_us = 0.0;
  double self_us = 0.0;
  double aux_event_count = 0.0;
  double aux_us = 0.0;
};

struct PreludeCost {
  double exec_aux_us = 0.0;
  double comm_us = 0.0;
  double idle_us = 0.0;
  double aux_event_count = 0.0;
  double aux_us = 0.0;
};

std::uint32_t primary_device_id(const std::vector<ReportToken>& tokens) {
  return tokens.empty() ? 0 : tokens.front().device_id;
}

std::string lower_ascii(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

std::unordered_set<TraceEventId::value_type> anchored_trace_event_ids(
    const NativeIr& ir) {
  std::unordered_set<TraceEventId::value_type> out;
  for (const AnchorRow& anchor : ir.anchors.rows()) {
    if (!anchor.trace_event_id.valid()) {
      continue;
    }
    if (anchor.trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument("AnchorRow trace_event_id is out of range");
    }
    out.insert(anchor.trace_event_id.value());
  }
  return out;
}

std::unordered_map<TraceEventId::value_type, const TaskRow*> tasks_by_event(
    const NativeIr& ir) {
  std::unordered_map<TraceEventId::value_type, const TaskRow*> out;
  for (const TaskRow& task : ir.tasks.rows()) {
    if (!task.trace_event_id.valid() ||
        task.trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument("TaskRow trace_event_id is out of range");
    }
    out.emplace(task.trace_event_id.value(), &task);
  }
  return out;
}

std::unordered_set<TraceEventId::value_type> communication_event_ids(
    const NativeIr& ir) {
  std::unordered_set<TraceEventId::value_type> out;
  for (const CommunicationOpRow& comm : ir.communication_ops.rows()) {
    if (!comm.trace_event_id.valid() ||
        comm.trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument(
          "CommunicationOpRow trace_event_id is out of range");
    }
    out.insert(comm.trace_event_id.value());
  }
  return out;
}

bool is_wait_event(const NativeIr& ir,
                   const TraceEventRow& event,
                   const TaskRow* task) {
  std::string blob = symbol_value_or_empty(ir, event.raw_name_symbol_id);
  if (task != nullptr) {
    blob += " " + symbol_value_or_empty(ir, task->task_type_symbol_id);
    blob += " " + symbol_value_or_empty(ir, task->op_name_symbol_id);
    blob += " " + symbol_value_or_empty(ir, task->op_type_symbol_id);
  }
  blob = lower_ascii(std::move(blob));
  return blob.find("event_wait") != std::string::npos ||
         blob.find("wait") != std::string::npos;
}

std::vector<PreludeCost> compute_prelude_costs(
    const NativeIr& ir,
    const std::vector<ReportToken>& tokens) {
  constexpr std::int64_t kMaxPreludeGapNs = 5'000'000;
  std::vector<PreludeCost> costs(tokens.size());
  if (tokens.empty()) {
    return costs;
  }

  const std::unordered_set<TraceEventId::value_type> anchored_events =
      anchored_trace_event_ids(ir);
  const std::unordered_map<TraceEventId::value_type, const TaskRow*>
      task_index = tasks_by_event(ir);
  const std::unordered_set<TraceEventId::value_type> comm_events =
      communication_event_ids(ir);

  std::vector<const TraceEventRow*> events;
  events.reserve(ir.trace_events.size());
  for (const TraceEventRow& event : ir.trace_events.rows()) {
    events.push_back(&event);
  }
  std::sort(events.begin(), events.end(),
            [](const TraceEventRow* lhs, const TraceEventRow* rhs) {
              if (lhs->start_ns != rhs->start_ns) {
                return lhs->start_ns < rhs->start_ns;
              }
              if (lhs->end_ns != rhs->end_ns) {
                return lhs->end_ns < rhs->end_ns;
              }
              return lhs->id < rhs->id;
            });

  for (std::size_t token_index = 0; token_index < tokens.size();
       ++token_index) {
    const ReportToken& token = tokens[token_index];
    const std::int64_t previous_end =
        token_index == 0 ? token.start_ns : tokens[token_index - 1].end_ns;
    const std::int64_t prelude_start = std::min(previous_end, token.start_ns);
    const std::int64_t prelude_end = token.start_ns;
    if (prelude_end <= prelude_start ||
        prelude_end - prelude_start > kMaxPreludeGapNs) {
      continue;
    }

    const auto lower =
        std::lower_bound(events.begin(), events.end(), prelude_start,
                         [](const TraceEventRow* event, std::int64_t value) {
                           return event->start_ns < value;
                         });
    const auto upper =
        std::lower_bound(events.begin(), events.end(), prelude_end,
                         [](const TraceEventRow* event, std::int64_t value) {
                           return event->start_ns < value;
                         });

    std::vector<std::pair<std::int64_t, std::int64_t>> active_intervals;
    PreludeCost cost;
    for (auto it = lower; it != upper; ++it) {
      const TraceEventRow& event = **it;
      if (event.device_id != token.device_id ||
          anchored_events.find(event.id.value()) != anchored_events.end()) {
        continue;
      }
      const std::int64_t overlap_start =
          std::max(prelude_start, event.start_ns);
      const std::int64_t overlap_end = std::min(prelude_end, event.end_ns);
      if (overlap_end <= overlap_start) {
        continue;
      }

      active_intervals.emplace_back(overlap_start, overlap_end);
      const double duration_us = ns_to_us(overlap_end - overlap_start);
      const auto task_found = task_index.find(event.id.value());
      const TaskRow* task =
          task_found == task_index.end() ? nullptr : task_found->second;
      if (comm_events.find(event.id.value()) != comm_events.end() ||
          (task != nullptr && task->comm_name_symbol_id.valid())) {
        cost.comm_us += duration_us;
        cost.aux_event_count += 1.0;
        cost.aux_us += duration_us;
      } else if (!is_wait_event(ir, event, task)) {
        cost.exec_aux_us += duration_us;
        cost.aux_event_count += 1.0;
        cost.aux_us += duration_us;
      }
    }

    std::sort(active_intervals.begin(), active_intervals.end());
    std::int64_t active_union_ns = 0;
    std::int64_t current_start = 0;
    std::int64_t current_end = 0;
    bool has_current = false;
    for (const auto& interval : active_intervals) {
      if (!has_current) {
        current_start = interval.first;
        current_end = interval.second;
        has_current = true;
        continue;
      }
      if (interval.first <= current_end) {
        current_end = std::max(current_end, interval.second);
        continue;
      }
      active_union_ns += current_end - current_start;
      current_start = interval.first;
      current_end = interval.second;
    }
    if (has_current) {
      active_union_ns += current_end - current_start;
    }

    const double gap_us = ns_to_us(prelude_end - prelude_start);
    cost.idle_us = std::max(0.0, gap_us - ns_to_us(active_union_ns));
    costs[token_index] = cost;
  }
  return costs;
}

struct AuxAttributionIndex {
  std::map<std::string, std::vector<const AuxLinkSqlRow*>> links_by_anchor;
};

AuxAttributionIndex build_aux_attribution_index(
    const AuxAttributionSqlRows& aux_rows) {
  AuxAttributionIndex out;
  for (const AuxLinkSqlRow& link : aux_rows.aux_links) {
    out.links_by_anchor[link.anchor_id].push_back(&link);
  }
  return out;
}

void add_aux_for_anchor(NodeAccum& accum,
                        const AuxAttributionIndex& aux_index,
                        AnchorId anchor_id) {
  if (!anchor_id.valid()) {
    return;
  }
  const auto found = aux_index.links_by_anchor.find(anchor_compat_id(anchor_id));
  if (found == aux_index.links_by_anchor.end()) {
    return;
  }
  for (const AuxLinkSqlRow* link : found->second) {
    accum.aux_event_count += 1.0;
    accum.aux_us += link->aux_dur_us;
  }
}

std::vector<std::uint32_t> occurrence_counts_by_def(const ReportTree& tree) {
  std::vector<std::uint32_t> out(tree.node_defs.size(), 0);
  for (const ReportNodeOccurrence& occurrence : tree.occurrences) {
    if (!occurrence.node_def_id.valid() ||
        occurrence.node_def_id.value() >= tree.node_defs.size()) {
      throw std::invalid_argument("ReportTree occurrence references bad def");
    }
    out[occurrence.node_def_id.value()] += 1;
  }
  return out;
}

std::vector<NodeAccum> accumulate_nodes(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    const AuxAttributionIndex& aux_index) {
  std::vector<NodeAccum> out(tree.node_defs.size());
  const std::vector<std::uint32_t> occurrence_counts =
      occurrence_counts_by_def(tree);
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i].occurrence_count = occurrence_counts[i];
  }

  for (const ReportNodeCoverage& coverage : tree.coverage) {
    if (!coverage.node_occurrence_id.valid() ||
        coverage.node_occurrence_id.value() >= tree.occurrences.size()) {
      throw std::invalid_argument("ReportTree coverage references bad occurrence");
    }
    if (coverage.token_end_ordinal < coverage.token_start_ordinal ||
        coverage.token_end_ordinal > tokens.size()) {
      throw std::invalid_argument("ReportTree coverage token span is out of range");
    }
    const ReportNodeOccurrence& occurrence =
        tree.occurrences[coverage.node_occurrence_id.value()];
    NodeAccum& accum = out[occurrence.node_def_id.value()];
    for (std::uint32_t token_index = coverage.token_start_ordinal;
         token_index < coverage.token_end_ordinal; ++token_index) {
      const ReportToken& token = tokens[token_index];
      const std::uint32_t anchor_idx = anchor_idx_for_token(token);
      if (accum.token_ordinals.insert(token_index).second) {
        if (token.has_prelude_cost) {
          accum.aux_event_count += token.prelude_aux_event_count;
          accum.aux_us += token.prelude_aux_us;
        } else {
          add_aux_for_anchor(accum, aux_index, token.anchor_id);
        }
        if (accum.first_anchor_idx == 0 ||
            (anchor_idx != 0 && anchor_idx < accum.first_anchor_idx)) {
          accum.first_anchor_idx = anchor_idx;
        }
        accum.last_anchor_idx = std::max(accum.last_anchor_idx, anchor_idx);
        if (accum.start_ns == 0 || token.start_ns < accum.start_ns) {
          accum.start_ns = token.start_ns;
        }
        accum.end_ns = std::max(accum.end_ns, token.end_ns);
        if (is_comm_token(token)) {
          accum.comm_us += token_duration_us(token);
        } else {
          accum.compute_us += token_duration_us(token);
        }
        accum.compute_us += token.prelude_exec_aux_us;
        accum.comm_us += token.prelude_comm_us;
        accum.idle_us += token.prelude_idle_us;
      }
      if (coverage.kind == ReportCoverageKind::kAtomLeaf) {
        accum.self_us += token_duration_us(token);
      }
    }
  }
  return out;
}

std::map<ReportNodeDefId::value_type, ReportNodeOccurrenceId>
first_occurrence_by_def(const ReportTree& tree) {
  std::map<ReportNodeDefId::value_type, ReportNodeOccurrenceId> out;
  for (const ReportNodeOccurrence& occurrence : tree.occurrences) {
    out.emplace(occurrence.node_def_id.value(), occurrence.id);
  }
  return out;
}

std::string path_for_occurrence(
    const ReportTree& tree,
    ReportNodeOccurrenceId occurrence_id) {
  std::vector<std::string> parts;
  ReportNodeOccurrenceId cursor = occurrence_id;
  while (cursor.valid()) {
    const ReportNodeOccurrence& occurrence = node_occurrence(tree, cursor);
    const ReportNodeDef& def = node_def(tree, occurrence.node_def_id);
    parts.push_back(def.local_node_id);
    cursor = occurrence.parent_occurrence_id;
  }
  std::reverse(parts.begin(), parts.end());

  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) {
      out += "/";
    }
    out += parts[i];
  }
  return out;
}

std::string repeat_context_for_occurrence(const ReportTree& tree,
                                          const ReportNodeOccurrence& occurrence) {
  std::vector<std::string> parts;
  ReportNodeOccurrenceId cursor = occurrence.id;
  while (cursor.valid()) {
    const ReportNodeOccurrence& current = node_occurrence(tree, cursor);
    const ReportNodeDef& def = node_def(tree, current.node_def_id);
    if (def.kind == ReportNodeKind::kRepeat) {
      parts.push_back(def.local_node_id + "#" +
                      std::to_string(current.repeat_iteration));
    }
    cursor = current.parent_occurrence_id;
  }
  std::reverse(parts.begin(), parts.end());

  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) {
      out += "/";
    }
    out += parts[i];
  }
  return out;
}

std::uint32_t loop_depth_for_occurrence(const ReportTree& tree,
                                        ReportNodeOccurrenceId occurrence_id) {
  std::uint32_t depth = 0;
  ReportNodeOccurrenceId cursor = occurrence_id;
  while (cursor.valid()) {
    const ReportNodeOccurrence& occurrence = node_occurrence(tree, cursor);
    const ReportNodeDef& def = node_def(tree, occurrence.node_def_id);
    if (def.kind == ReportNodeKind::kRepeat) {
      ++depth;
    }
    cursor = occurrence.parent_occurrence_id;
  }
  return depth;
}

std::uint32_t display_occurrence_count(const ReportNodeDef& def,
                                       const NodeAccum& accum) {
  std::uint32_t count = accum.occurrence_count == 0 ? 1 : accum.occurrence_count;
  if (def.kind == ReportNodeKind::kRepeat && def.repeat_count > count) {
    count = def.repeat_count;
  }
  return count;
}

}  // namespace

std::vector<ReportToken> build_report_tokens_from_native_ir(
    const NativeIr& ir) {
  std::vector<ReportToken> tokens;
  tokens.reserve(ir.tokens.size());
  for (const TokenRow& token : ir.tokens.rows()) {
    if (!token.anchor_id.valid() || token.anchor_id.value() >= ir.anchors.size()) {
      throw std::invalid_argument("TokenRow anchor_id is out of range");
    }
    const AnchorRow& anchor = ir.anchors.row(token.anchor_id);

    ReportToken out;
    out.ordinal = token.sequence_index;
    out.device_id = anchor.device_id;
    out.symbol_id = token.symbol_id;
    out.display_op = symbol_value_or_empty(ir, token.symbol_id);
    out.display_category = report_anchor_kind_name(
        report_anchor_kind_for_anchor_kind(anchor.kind));
    out.anchor_kind = report_anchor_kind_for_anchor_kind(anchor.kind);
    out.anchor_id = token.anchor_id;
    out.start_ns = token.start_ns;
    out.end_ns = token.end_ns;
    tokens.push_back(std::move(out));
  }
  const std::vector<PreludeCost> prelude_costs =
      compute_prelude_costs(ir, tokens);
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    tokens[i].has_prelude_cost = true;
    tokens[i].prelude_exec_aux_us = prelude_costs[i].exec_aux_us;
    tokens[i].prelude_comm_us = prelude_costs[i].comm_us;
    tokens[i].prelude_idle_us = prelude_costs[i].idle_us;
    tokens[i].prelude_aux_event_count = prelude_costs[i].aux_event_count;
    tokens[i].prelude_aux_us = prelude_costs[i].aux_us;
  }
  return tokens;
}

NodeCoverageSqlRows build_report_tree_node_coverage_sql_rows(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    std::uint32_t db_idx,
    std::string view_name) {
  return build_report_tree_node_coverage_sql_rows(
      tree, tokens, AuxAttributionSqlRows{}, db_idx, std::move(view_name));
}

NodeCoverageSqlRows build_report_tree_node_coverage_sql_rows(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    const AuxAttributionSqlRows& aux_rows,
    std::uint32_t db_idx,
    std::string view_name) {
  validate_report_tree_or_throw(tree, static_cast<std::uint32_t>(tokens.size()));

  NodeCoverageSqlRows rows;
  const std::uint32_t device_id = primary_device_id(tokens);
  const AuxAttributionIndex aux_index = build_aux_attribution_index(aux_rows);
  const std::vector<NodeAccum> accum =
      accumulate_nodes(tree, tokens, aux_index);
  const auto first_occurrences = first_occurrence_by_def(tree);

  rows.nodes.reserve(tree.node_defs.size());
  for (const ReportNodeDef& def : tree.node_defs) {
    const NodeAccum& node_accum = accum[def.id.value()];
    const std::uint32_t occurrence_count =
        display_occurrence_count(def, node_accum);
    const double total_us =
        node_accum.compute_us + node_accum.comm_us + node_accum.idle_us;

    VizNodeSqlRow row;
    row.node_id = node_id_for_def(def);
    row.db_idx = db_idx;
    row.device_id = device_id;
    row.view_name = view_name;
    row.local_node_id = def.local_node_id;
    const auto occurrence_found = first_occurrences.find(def.id.value());
    row.path = occurrence_found == first_occurrences.end()
                   ? def.local_node_id
                   : path_for_occurrence(tree, occurrence_found->second);
    row.node_type = report_node_type_name(def.kind);
    row.kind = report_node_kind_name(def.kind);
    row.symbol = def.display_op;
    row.label = def.display_op;
    row.category = def.display_category;
    row.depth = def.display_depth;
    row.level = def.display_depth;
    row.repeat_label = def.kind == ReportNodeKind::kRepeat ? def.display_op : "";
    row.repeat_count = def.repeat_count;
    row.occurrence_count = occurrence_count;
    row.anchor_count = static_cast<std::uint32_t>(node_accum.token_ordinals.size());
    row.anchors_per_occurrence =
        occurrence_count == 0
            ? 0.0
            : static_cast<double>(row.anchor_count) / occurrence_count;
    row.first_anchor_idx = node_accum.first_anchor_idx;
    row.last_anchor_idx = node_accum.last_anchor_idx;
    row.compute_us = node_accum.compute_us;
    row.comm_us = node_accum.comm_us;
    row.idle_us = node_accum.idle_us;
    row.total_us = total_us;
    row.avg_compute_us = node_accum.compute_us / occurrence_count;
    row.avg_comm_us = node_accum.comm_us / occurrence_count;
    row.avg_idle_us = node_accum.idle_us / occurrence_count;
    row.avg_total_us = total_us / occurrence_count;
    row.self_us = node_accum.self_us;
    row.aux_events = node_accum.aux_event_count;
    row.aux_us = node_accum.aux_us;
    rows.nodes.push_back(std::move(row));

    if (def.kind == ReportNodeKind::kRepeat) {
      LoopNodeSqlRow loop;
      loop.node_id = node_id_for_def(def);
      loop.db_idx = db_idx;
      loop.device_id = device_id;
      loop.view_name = view_name;
      loop.loop_rank = static_cast<std::uint32_t>(rows.loop_nodes.size() + 1);
      loop.repeat_label = def.display_op;
      loop.repeat_count = def.repeat_count;
      loop.occurrence_count = occurrence_count;
      loop.anchor_count =
          static_cast<std::uint32_t>(node_accum.token_ordinals.size());
      loop.total_us = total_us;
      loop.avg_total_us = total_us / occurrence_count;
      loop.compute_us = node_accum.compute_us;
      loop.comm_us = node_accum.comm_us;
      loop.idle_us = node_accum.idle_us;
      rows.loop_nodes.push_back(std::move(loop));
    }
  }

  std::set<std::pair<std::string, std::string>> seen_edges;
  for (const ReportTreeEdge& edge : tree.edges) {
    const ReportNodeOccurrence& parent =
        node_occurrence(tree, edge.parent_occurrence_id);
    const ReportNodeOccurrence& child =
        node_occurrence(tree, edge.child_occurrence_id);
    const ReportNodeDef& parent_def = node_def(tree, parent.node_def_id);
    const ReportNodeDef& child_def = node_def(tree, child.node_def_id);
    const std::pair<std::string, std::string> key{node_id_for_def(parent_def),
                                                  node_id_for_def(child_def)};
    if (!seen_edges.insert(key).second) {
      continue;
    }
    VizEdgeSqlRow row;
    row.parent_node_id = key.first;
    row.child_node_id = key.second;
    row.db_idx = db_idx;
    row.device_id = device_id;
    row.view_name = view_name;
    row.edge_order = edge.edge_order;
    row.edge_kind = "tree";
    rows.edges.push_back(std::move(row));
  }

  std::set<std::string> primary_anchor_ids;
  for (const ReportNodeCoverage& coverage : tree.coverage) {
    const ReportNodeOccurrence& occurrence =
        node_occurrence(tree, coverage.node_occurrence_id);
    const ReportNodeDef& def = node_def(tree, occurrence.node_def_id);
    std::uint32_t anchor_order = 1;
    for (std::uint32_t token_index = coverage.token_start_ordinal;
         token_index < coverage.token_end_ordinal; ++token_index) {
      const ReportToken& token = tokens[token_index];
      if (!token.anchor_id.valid()) {
        continue;
      }
      const std::string anchor_id = anchor_compat_id(token.anchor_id);

      VizNodeAnchorSqlRow row;
      row.node_id = node_id_for_def(def);
      row.anchor_id = anchor_id;
      row.db_idx = db_idx;
      row.device_id = device_id;
      row.view_name = view_name;
      row.occurrence_idx = occurrence.occurrence_index_for_def + 1;
      row.anchor_order = anchor_order++;
      row.coverage_kind = coverage_kind_name(coverage.kind);
      row.repeat_context = repeat_context_for_occurrence(tree, occurrence);
      rows.node_anchors.push_back(std::move(row));

      if (coverage.kind == ReportCoverageKind::kAtomLeaf &&
          primary_anchor_ids.insert(anchor_id).second) {
        AnchorPrimaryNodeSqlRow primary;
        primary.anchor_id = anchor_id;
        primary.node_id = node_id_for_def(def);
        primary.db_idx = db_idx;
        primary.device_id = device_id;
        primary.view_name = view_name;
        primary.reason = "atom_leaf";
        rows.anchor_primary_nodes.push_back(std::move(primary));
      }
    }
  }

  return rows;
}

NodeCoverageSqlRows build_native_report_tree_node_coverage_sql_rows(
    const NativeIr& ir,
    std::uint32_t db_idx,
    std::string view_name) {
  const std::vector<ReportToken> tokens = build_report_tokens_from_native_ir(ir);
  const ReportTree tree = build_report_tree_from_tokens(tokens);
  const AuxAttributionSqlRows aux_rows =
      build_aux_attribution_sql_rows(ir, db_idx);
  return build_report_tree_node_coverage_sql_rows(
      tree, tokens, aux_rows, db_idx, std::move(view_name));
}

LoopTreeSqlRows split_loop_tree_sql_rows(const NodeCoverageSqlRows& rows) {
  LoopTreeSqlRows out;
  out.nodes = rows.nodes;
  out.edges = rows.edges;
  out.loop_nodes = rows.loop_nodes;
  return out;
}

NodeAnchorCoverageSqlRows split_node_anchor_coverage_sql_rows(
    const NodeCoverageSqlRows& rows) {
  NodeAnchorCoverageSqlRows out;
  out.node_anchors = rows.node_anchors;
  out.anchor_primary_nodes = rows.anchor_primary_nodes;
  return out;
}

SemanticTreeSqlRows build_report_tree_semantic_sql_rows(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    std::uint32_t db_idx,
    std::string tree_id,
    std::string view_name) {
  return build_report_tree_semantic_sql_rows(
      tree, tokens, AuxAttributionSqlRows{}, db_idx, std::move(tree_id),
      std::move(view_name));
}

SemanticTreeSqlRows build_report_tree_semantic_sql_rows(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    const AuxAttributionSqlRows& aux_rows,
    std::uint32_t db_idx,
    std::string tree_id,
    std::string view_name) {
  validate_report_tree_or_throw(tree, static_cast<std::uint32_t>(tokens.size()));

  SemanticTreeSqlRows rows;
  const std::uint32_t device_id = primary_device_id(tokens);
  const AuxAttributionIndex aux_index = build_aux_attribution_index(aux_rows);
  const std::vector<NodeAccum> accum =
      accumulate_nodes(tree, tokens, aux_index);
  const auto first_occurrences = first_occurrence_by_def(tree);

  SemanticTreeHeaderSqlRow header;
  header.tree_id = tree_id;
  header.db_idx = db_idx;
  header.device_id = device_id;
  header.view_name = view_name;
  header.tree_kind = "semantic";
  header.stem = "native_report_tree";
  header.schema_version = "compat-v1";
  header.semantic_projection = "native_report_tree";
  header.macro_discovery = "native_report_tree";
  header.readable_macro_mode = "native_report_tree";
  header.auxiliary_attribution =
      aux_rows.aux_links.empty() ? "none" : "native_aux_attribution";
  if (!tree.node_defs.empty()) {
    header.root_node_id = node_id_for_def(tree.node_defs.front());
  }
  rows.trees.push_back(header);

  rows.nodes.reserve(tree.node_defs.size());
  for (const ReportNodeDef& def : tree.node_defs) {
    const NodeAccum& node_accum = accum[def.id.value()];
    const std::uint32_t occurrence_count =
        display_occurrence_count(def, node_accum);
    const double total_us =
        node_accum.compute_us + node_accum.comm_us + node_accum.idle_us;
    const auto occurrence_found = first_occurrences.find(def.id.value());

    SemanticNodeSqlRow row;
    row.node_id = node_id_for_def(def);
    row.tree_id = tree_id;
    row.db_idx = db_idx;
    row.device_id = device_id;
    row.view_name = view_name;
    row.tree_kind = "semantic";
    row.local_node_id = def.local_node_id;
    row.preorder_idx = def.definition_order;
    row.path = occurrence_found == first_occurrences.end()
                   ? def.local_node_id
                   : path_for_occurrence(tree, occurrence_found->second);
    row.depth = def.display_depth;
    row.display_depth = def.display_depth;
    row.node_type = report_node_type_name(def.kind);
    row.semantic_kind = report_node_kind_name(def.kind);
    row.symbol = def.display_op;
    row.label = def.display_op;
    row.category = def.display_category;
    row.repeat_count = def.repeat_count;
    row.occurrence_count = occurrence_count;
    row.anchor_count =
        static_cast<std::uint32_t>(node_accum.token_ordinals.size());
    row.first_anchor_idx = node_accum.first_anchor_idx;
    row.last_anchor_idx = node_accum.last_anchor_idx;
    row.start_ns = node_accum.start_ns;
    row.end_ns = node_accum.end_ns;
    row.compute_us = node_accum.compute_us;
    row.comm_us = node_accum.comm_us;
    row.idle_us = node_accum.idle_us;
    row.total_us = total_us;
    row.avg_compute_us = node_accum.compute_us / occurrence_count;
    row.avg_comm_us = node_accum.comm_us / occurrence_count;
    row.avg_idle_us = node_accum.idle_us / occurrence_count;
    row.avg_total_us = total_us / occurrence_count;
    row.self_us = node_accum.self_us;
    row.aux_event_count = node_accum.aux_event_count;
    row.aux_us = node_accum.aux_us;

    if (occurrence_found != first_occurrences.end()) {
      const ReportNodeOccurrence& occurrence =
          node_occurrence(tree, occurrence_found->second);
      row.sibling_order = occurrence.edge_order;
      row.loop_depth = loop_depth_for_occurrence(tree, occurrence.id);
      if (occurrence.parent_occurrence_id.valid()) {
        const ReportNodeOccurrence& parent =
            node_occurrence(tree, occurrence.parent_occurrence_id);
        const ReportNodeDef& parent_def = node_def(tree, parent.node_def_id);
        row.parent_node_id = node_id_for_def(parent_def);
        row.parent_local_node_id = parent_def.local_node_id;
      }
    }
    rows.nodes.push_back(std::move(row));
  }

  std::set<std::pair<std::string, std::string>> seen_edges;
  for (const ReportTreeEdge& edge : tree.edges) {
    const ReportNodeOccurrence& parent =
        node_occurrence(tree, edge.parent_occurrence_id);
    const ReportNodeOccurrence& child =
        node_occurrence(tree, edge.child_occurrence_id);
    const ReportNodeDef& parent_def = node_def(tree, parent.node_def_id);
    const ReportNodeDef& child_def = node_def(tree, child.node_def_id);
    const std::pair<std::string, std::string> key{node_id_for_def(parent_def),
                                                  node_id_for_def(child_def)};
    if (!seen_edges.insert(key).second) {
      continue;
    }
    SemanticEdgeSqlRow row;
    row.parent_node_id = key.first;
    row.child_node_id = key.second;
    row.tree_id = tree_id;
    row.db_idx = db_idx;
    row.device_id = device_id;
    row.view_name = view_name;
    row.tree_kind = "semantic";
    row.edge_order = edge.edge_order;
    row.edge_kind = "child";
    rows.edges.push_back(std::move(row));
  }

  return rows;
}

SemanticTreeSqlRows build_native_report_tree_semantic_sql_rows(
    const NativeIr& ir,
    std::uint32_t db_idx,
    std::string tree_id,
    std::string view_name) {
  const std::vector<ReportToken> tokens = build_report_tokens_from_native_ir(ir);
  const ReportTree tree = build_report_tree_from_tokens(tokens);
  const AuxAttributionSqlRows aux_rows =
      build_aux_attribution_sql_rows(ir, db_idx);
  return build_report_tree_semantic_sql_rows(
      tree, tokens, aux_rows, db_idx, std::move(tree_id), std::move(view_name));
}

std::vector<SemanticTreeHeaderSqlRow> split_semantic_tree_catalog_sql_rows(
    const SemanticTreeSqlRows& rows) {
  return rows.trees;
}

SemanticGraphSqlRows split_semantic_graph_sql_rows(
    const SemanticTreeSqlRows& rows) {
  SemanticGraphSqlRows out;
  out.nodes = rows.nodes;
  out.edges = rows.edges;
  return out;
}

}  // namespace traceloom::compat
