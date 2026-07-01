#include "traceloom/analysis/flat_anchor_builder.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace traceloom {
namespace {

struct AnchorCandidate {
  TraceEventId trace_event_id;
  AnchorKind kind = AnchorKind::kUnknown;
  SymbolId symbol_id;
};

struct CommunicationSpan {
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

SymbolId choose_task_symbol(const TaskRow& task) {
  if (task.op_type_symbol_id.valid()) {
    return task.op_type_symbol_id;
  }
  if (task.op_name_symbol_id.valid()) {
    return task.op_name_symbol_id;
  }
  if (task.comm_name_symbol_id.valid()) {
    return task.comm_name_symbol_id;
  }
  return task.task_type_symbol_id;
}

void validate_task_trace_event_refs(const TaskTable& tasks,
                                    std::size_t trace_event_count) {
  for (const TaskRow& task : tasks.rows()) {
    if (!task.trace_event_id.valid() ||
        task.trace_event_id.value() >= trace_event_count) {
      throw std::invalid_argument("TaskRow trace_event_id is out of range");
    }
  }
}

std::unordered_set<TraceEventId::value_type> communication_trace_event_ids(
    const CommunicationOpTable& communication_ops) {
  std::unordered_set<TraceEventId::value_type> out;
  for (const CommunicationOpRow& row : communication_ops.rows()) {
    if (row.trace_event_id.valid()) {
      out.insert(row.trace_event_id.value());
    }
  }
  return out;
}

std::uint64_t connection_key(std::uint32_t device_id,
                             std::int64_t connection_id) {
  return (static_cast<std::uint64_t>(device_id) << 32u) ^
         (static_cast<std::uint64_t>(connection_id) & 0xffffffffu);
}

std::unordered_map<std::uint64_t, std::vector<CommunicationSpan>>
communication_spans_by_connection(const NativeIr& ir) {
  std::unordered_map<std::uint64_t, std::vector<CommunicationSpan>> out;
  for (const CommunicationOpRow& comm : ir.communication_ops.rows()) {
    if (!comm.trace_event_id.valid()) {
      continue;
    }
    const TraceEventRow& event = ir.trace_events.row(comm.trace_event_id);
    out[connection_key(event.device_id, comm.raw_connection_id)].push_back(
        CommunicationSpan{event.start_ns, event.end_ns});
  }
  return out;
}

bool task_type_is_skipped(const NativeIr& ir,
                          const TaskRow& task,
                          const FlatAnchorBuildConfig& config) {
  if (!task.task_type_symbol_id.valid()) {
    return false;
  }
  const std::string& task_type = ir.symbols.value(task.task_type_symbol_id);
  return std::find(config.skipped_task_type_symbols.begin(),
                   config.skipped_task_type_symbols.end(),
                   task_type) != config.skipped_task_type_symbols.end();
}

bool task_is_covered_by_communication_op(
    const NativeIr& ir,
    const TaskRow& task,
    const std::unordered_map<std::uint64_t, std::vector<CommunicationSpan>>&
        comm_spans) {
  if (!task.trace_event_id.valid() || task.raw_connection_id < 0) {
    return false;
  }
  const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
  const auto found =
      comm_spans.find(connection_key(event.device_id, task.raw_connection_id));
  if (found == comm_spans.end()) {
    return false;
  }
  for (const CommunicationSpan& span : found->second) {
    if (event.start_ns <= span.end_ns && event.end_ns >= span.start_ns) {
      return true;
    }
  }
  return false;
}

bool candidate_less(const NativeIr& ir,
                    const AnchorCandidate& lhs,
                    const AnchorCandidate& rhs) {
  const TraceEventRow& a = ir.trace_events.row(lhs.trace_event_id);
  const TraceEventRow& b = ir.trace_events.row(rhs.trace_event_id);
  if (a.device_id != b.device_id) {
    return a.device_id < b.device_id;
  }
  if (a.start_ns != b.start_ns) {
    return a.start_ns < b.start_ns;
  }
  if (a.end_ns != b.end_ns) {
    return a.end_ns < b.end_ns;
  }
  if (a.stream_id != b.stream_id) {
    return a.stream_id < b.stream_id;
  }
  const auto source_kind_order = [](AnchorKind kind) {
    switch (kind) {
      case AnchorKind::kCommunication:
        return 0;
      case AnchorKind::kGraphReplayUnit:
        return 1;
      case AnchorKind::kDeviceEvent:
        return 2;
      case AnchorKind::kSynchronization:
        return 3;
      case AnchorKind::kGraphH:
      case AnchorKind::kGraphL:
      case AnchorKind::kGraphT:
        return 4;
      case AnchorKind::kUnknown:
        return 5;
    }
    return 6;
  };
  const int lhs_order = source_kind_order(lhs.kind);
  const int rhs_order = source_kind_order(rhs.kind);
  if (lhs_order != rhs_order) {
    return lhs_order < rhs_order;
  }
  if (lhs.symbol_id != rhs.symbol_id) {
    return lhs.symbol_id < rhs.symbol_id;
  }
  if (a.source_ref_id != b.source_ref_id) {
    return a.source_ref_id < b.source_ref_id;
  }
  if (a.source_row_id != b.source_row_id) {
    return a.source_row_id < b.source_row_id;
  }
  return lhs.trace_event_id.value() < rhs.trace_event_id.value();
}

}  // namespace

FlatAnchorBuildStats build_flat_anchors(NativeIr& ir,
                                        FlatAnchorBuildConfig config) {
  if (!ir.anchors.empty() || !ir.tokens.empty()) {
    throw std::invalid_argument(
        "build_flat_anchors expects empty AnchorTable and TokenTable");
  }

  validate_task_trace_event_refs(ir.tasks, ir.trace_events.size());
  const std::unordered_set<TraceEventId::value_type> comm_event_ids =
      communication_trace_event_ids(ir.communication_ops);
  const std::unordered_map<std::uint64_t, std::vector<CommunicationSpan>>
      comm_spans = communication_spans_by_connection(ir);

  std::vector<AnchorCandidate> candidates;
  candidates.reserve(ir.tasks.size() + ir.communication_ops.size());
  FlatAnchorBuildStats stats;

  for (const TaskRow& task : ir.tasks.rows()) {
    if (!task.trace_event_id.valid()) {
      continue;
    }
    if (task_type_is_skipped(ir, task, config)) {
      ++stats.skipped_task_events;
      continue;
    }
    if (config.skip_tasks_covered_by_communication_ops &&
        task_is_covered_by_communication_op(ir, task, comm_spans)) {
      ++stats.skipped_task_events;
      continue;
    }
    if (comm_event_ids.find(task.trace_event_id.value()) !=
        comm_event_ids.end()) {
      continue;
    }
    candidates.push_back(
        AnchorCandidate{task.trace_event_id, AnchorKind::kDeviceEvent,
                        choose_task_symbol(task)});
  }

  for (const CommunicationOpRow& comm : ir.communication_ops.rows()) {
    if (!comm.trace_event_id.valid()) {
      continue;
    }
    candidates.push_back(
        AnchorCandidate{comm.trace_event_id, AnchorKind::kCommunication,
                        comm.op_name_symbol_id});
  }

  std::sort(candidates.begin(), candidates.end(),
            [&ir](const AnchorCandidate& lhs, const AnchorCandidate& rhs) {
              return candidate_less(ir, lhs, rhs);
            });

  std::uint32_t sequence_index = 0;
  for (const AnchorCandidate& candidate : candidates) {
    const TraceEventRow& event = ir.trace_events.row(candidate.trace_event_id);
    const AnchorId anchor = ir.anchors.append(
        event.source_ref_id, candidate.trace_event_id, ReplayUnitId::invalid(),
        candidate.kind, candidate.symbol_id, event.device_id, event.stream_id,
        event.start_ns, event.end_ns);
    ir.tokens.append(anchor, candidate.symbol_id, event.device_id,
                     sequence_index++, event.start_ns, event.end_ns);

    if (candidate.kind == AnchorKind::kCommunication) {
      ++stats.communication_anchors;
    } else {
      ++stats.device_event_anchors;
    }
  }
  stats.tokens = ir.tokens.size();
  return stats;
}

}  // namespace traceloom
