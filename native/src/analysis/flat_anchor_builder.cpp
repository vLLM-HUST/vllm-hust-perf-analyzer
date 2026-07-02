#include "traceloom/analysis/flat_anchor_builder.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace traceloom {
namespace {

struct AnchorCandidate {
  TraceEventId trace_event_id;
  ReplayUnitId replay_unit_id = ReplayUnitId::invalid();
  AnchorKind kind = AnchorKind::kUnknown;
  SymbolId symbol_id;
};

struct CommunicationSpan {
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

struct ReplayUnitSpan {
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

std::string symbol_text(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

bool is_matmul_backend_variant(const std::string& text) {
  return text == "MatMulV1" || text == "MatMulV2" || text == "MatMulV3" ||
         text == "BatchMatMulV1" || text == "BatchMatMulV2" ||
         text == "BatchMatMulV3";
}

SymbolId normalize_compute_symbol(NativeIr& ir, SymbolId symbol) {
  if (!symbol.valid()) {
    return symbol;
  }
  if (is_matmul_backend_variant(symbol_text(ir, symbol))) {
    return ir.symbols.intern("MatMul");
  }
  return symbol;
}

std::string lower_ascii(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

std::string normalize_task_key(std::string value) {
  for (char& ch : value) {
    if (std::isalnum(static_cast<unsigned char>(ch))) {
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    } else {
      ch = '_';
    }
  }
  while (value.find("__") != std::string::npos) {
    value.replace(value.find("__"), 2, "_");
  }
  while (!value.empty() && value.front() == '_') {
    value.erase(value.begin());
  }
  while (!value.empty() && value.back() == '_') {
    value.pop_back();
  }
  return value;
}

bool contains_any(const std::string& text,
                  const std::vector<std::string>& needles) {
  for (const std::string& needle : needles) {
    if (text.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool is_device_allreduce_label(const std::string& lower_text) {
  return contains_any(lower_text,
                      {"aiv_all_reduce", "aiv_allreduce", "aic_all_reduce",
                       "aic_allreduce", "all_reduce_bfloat16",
                       "allreduce_bfloat16"});
}

bool is_opaque_collective_instance_label(const std::string& text) {
  if (text.empty() ||
      !std::isdigit(static_cast<unsigned char>(text.front()))) {
    return false;
  }
  bool saw_underscore = false;
  for (char ch : text) {
    if (ch == '_') {
      saw_underscore = true;
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  return saw_underscore;
}

SymbolId choose_communication_symbol(NativeIr& ir,
                                     const CommunicationOpRow& comm) {
  if (!comm.op_name_symbol_id.valid() && !comm.op_type_symbol_id.valid() &&
      !comm.linked_task_name_symbol_id.valid() &&
      !comm.linked_task_type_symbol_id.valid()) {
    return SymbolId::invalid();
  }
  const std::string label = symbol_text(ir, comm.op_name_symbol_id);
  const std::string op_type = symbol_text(ir, comm.op_type_symbol_id);
  const std::string linked_task_name =
      symbol_text(ir, comm.linked_task_name_symbol_id);
  const std::string linked_task_type =
      symbol_text(ir, comm.linked_task_type_symbol_id);
  const std::string lower_blob =
      lower_ascii(label + " " + op_type + " " + linked_task_name + " " +
                  linked_task_type);
  if (contains_any(lower_blob, {"allreduce", "all_reduce"}) ||
      is_opaque_collective_instance_label(label)) {
    return ir.symbols.intern("AllReduce");
  }
  if (contains_any(lower_blob, {"reducescatter", "reduce_scatter"})) {
    return ir.symbols.intern("ReduceScatter");
  }
  if (contains_any(lower_blob, {"allgather", "all_gather"})) {
    return ir.symbols.intern("AllGather");
  }
  if (contains_any(lower_blob, {"broadcast"})) {
    return ir.symbols.intern("Broadcast");
  }
  if (contains_any(lower_blob,
                   {"alltoall", "all_to_all", "all-to-all", "all2all",
                    "a2a"})) {
    return ir.symbols.intern("AllToAll");
  }
  if (comm.op_type_symbol_id.valid()) {
    return comm.op_type_symbol_id;
  }
  if (comm.linked_task_type_symbol_id.valid()) {
    return comm.linked_task_type_symbol_id;
  }
  if (comm.linked_task_name_symbol_id.valid()) {
    return comm.linked_task_name_symbol_id;
  }
  return comm.op_name_symbol_id;
}

SymbolId choose_task_anchor_symbol(NativeIr& ir, const TaskRow& task) {
  const SymbolId symbol = choose_task_symbol(task);
  const std::string blob = lower_ascii(
      symbol_text(ir, symbol) + " " + symbol_text(ir, task.op_name_symbol_id) +
      " " + symbol_text(ir, task.op_type_symbol_id) + " " +
      symbol_text(ir, task.compute_task_type_symbol_id) + " " +
      symbol_text(ir, task.task_type_symbol_id));
  if (is_device_allreduce_label(blob)) {
    return ir.symbols.intern("AIV_AllReduce");
  }
  return normalize_compute_symbol(ir, symbol);
}

bool is_hccl_sync_label(const std::string& lower_text) {
  return contains_any(lower_text, {"hccl_aiv_sync", "hccl_aic_sync",
                                   "aiv_sync", "aic_sync"});
}

bool is_collective_label(const std::string& lower_text) {
  return lower_text.find("nccl") != std::string::npos ||
         contains_any(lower_text,
                      {"allreduce", "all_reduce", "allgather", "all_gather",
                       "reducescatter", "reduce_scatter", "broadcast",
                       "alltoall", "all_to_all", "all-to-all", "all2all",
                       "a2a"}) ||
         is_device_allreduce_label(lower_text);
}

bool is_semantic_anchor_task(const NativeIr& ir, const TaskRow& task) {
  const std::string task_type =
      normalize_task_key(symbol_text(ir, task.task_type_symbol_id));
  if (task_type == "MODEL_MAINTAINCE" || task_type == "MODEL_MAINTENANCE") {
    return false;
  }

  const std::string label = symbol_text(ir, choose_task_symbol(task));
  const std::string blob = lower_ascii(
      label + " " + symbol_text(ir, task.op_name_symbol_id) + " " +
      symbol_text(ir, task.op_type_symbol_id) + " " +
      symbol_text(ir, task.compute_task_type_symbol_id) + " " +
      symbol_text(ir, task.task_type_symbol_id));

  if (is_hccl_sync_label(blob)) {
    return false;
  }
  if (is_collective_label(blob)) {
    return true;
  }

  const std::unordered_set<std::string> control_task_types{
      "AI_CORE",       "MODEL_EXECUTE", "CAPTURE_RECORD",
      "CAPTURE_WAIT",  "EVENT_RECORD",  "EVENT_WAIT",
      "MEM_WRITE_VALUE", "MEMCPY",      "MEMCPY_ASYNC",
      "NOTIFY",        "NOTIFY_RECORD", "NOTIFY_WAIT",
      "SDMA",          "TASK_TIMEOUT_SET", "WRITE_VALUE"};
  if (control_task_types.find(task_type) != control_task_types.end()) {
    return false;
  }

  if (contains_any(blob, {"matmul", "batchmatmul", "gemm", "conv",
                          "flashattention", "fusedinferattention",
                          "pagedattention", "attention", "rmsnorm",
                          "rms_norm", "layernorm", "layer_norm", "swiglu",
                          "siluandmul", "moe", "ffn", "rotary", "rope"})) {
    return true;
  }

  return false;
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

std::unordered_map<std::uint32_t, std::vector<ReplayUnitSpan>>
replay_unit_spans_by_device(const NativeIr& ir) {
  std::unordered_map<std::uint32_t, std::vector<ReplayUnitSpan>> out;
  for (const ReplayUnitRow& replay : ir.replay_units.rows()) {
    if (!replay.launch_trace_event_id.valid()) {
      continue;
    }
    const TraceEventRow& event = ir.trace_events.row(replay.launch_trace_event_id);
    out[event.device_id].push_back(ReplayUnitSpan{event.start_ns, event.end_ns});
  }
  for (auto& item : out) {
    std::sort(item.second.begin(), item.second.end(),
              [](const ReplayUnitSpan& lhs, const ReplayUnitSpan& rhs) {
                if (lhs.start_ns != rhs.start_ns) {
                  return lhs.start_ns < rhs.start_ns;
                }
                return lhs.end_ns < rhs.end_ns;
              });
  }
  return out;
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

bool task_is_covered_by_replay_unit(
    const TraceEventRow& task_event,
    const std::unordered_map<std::uint32_t, std::vector<ReplayUnitSpan>>&
        replay_spans) {
  const auto found = replay_spans.find(task_event.device_id);
  if (found == replay_spans.end()) {
    return false;
  }
  const std::vector<ReplayUnitSpan>& spans = found->second;
  const auto first_after_start =
      std::upper_bound(spans.begin(), spans.end(), task_event.start_ns,
                       [](std::int64_t value, const ReplayUnitSpan& span) {
                         return value < span.start_ns;
                       });
  if (first_after_start == spans.begin()) {
    return false;
  }
  const ReplayUnitSpan& span = *(first_after_start - 1);
  return task_event.start_ns >= span.start_ns && task_event.end_ns <= span.end_ns;
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
  const std::unordered_map<std::uint32_t, std::vector<ReplayUnitSpan>>
      replay_spans = replay_unit_spans_by_device(ir);

  std::vector<AnchorCandidate> candidates;
  candidates.reserve(ir.tasks.size() + ir.communication_ops.size() +
                     ir.replay_units.size());
  FlatAnchorBuildStats stats;
  if (config.filter_auxiliary_task_anchors) {
    stats.projection_kind = "anchor_compute_collective_only";
  }

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
    const TraceEventRow& task_event = ir.trace_events.row(task.trace_event_id);
    if (config.skip_tasks_covered_by_replay_units &&
        task_is_covered_by_replay_unit(task_event, replay_spans)) {
      ++stats.skipped_task_events;
      continue;
    }
    if (config.filter_auxiliary_task_anchors &&
        !is_semantic_anchor_task(ir, task)) {
      ++stats.skipped_task_events;
      continue;
    }
    if (comm_event_ids.find(task.trace_event_id.value()) !=
        comm_event_ids.end()) {
      continue;
    }
    candidates.push_back(
        AnchorCandidate{task.trace_event_id, ReplayUnitId::invalid(),
                        AnchorKind::kDeviceEvent,
                        choose_task_anchor_symbol(ir, task)});
  }

  for (const ReplayUnitRow& replay : ir.replay_units.rows()) {
    if (!replay.launch_trace_event_id.valid()) {
      continue;
    }
    const TraceEventRow& event = ir.trace_events.row(replay.launch_trace_event_id);
    candidates.push_back(AnchorCandidate{
        replay.launch_trace_event_id, replay.id, AnchorKind::kGraphReplayUnit,
        event.raw_name_symbol_id});
  }

  for (const CommunicationOpRow& comm : ir.communication_ops.rows()) {
    if (!comm.trace_event_id.valid()) {
      continue;
    }
    candidates.push_back(
        AnchorCandidate{comm.trace_event_id, ReplayUnitId::invalid(),
                        AnchorKind::kCommunication,
                        choose_communication_symbol(ir, comm)});
  }

  std::sort(candidates.begin(), candidates.end(),
            [&ir](const AnchorCandidate& lhs, const AnchorCandidate& rhs) {
              return candidate_less(ir, lhs, rhs);
            });

  std::uint32_t sequence_index = 0;
  for (const AnchorCandidate& candidate : candidates) {
    const TraceEventRow& event = ir.trace_events.row(candidate.trace_event_id);
    const AnchorId anchor = ir.anchors.append(
        event.source_ref_id, candidate.trace_event_id, candidate.replay_unit_id,
        candidate.kind, candidate.symbol_id, event.device_id, event.stream_id,
        event.start_ns, event.end_ns);
    const TokenId token = ir.tokens.append(anchor, candidate.symbol_id,
                                           event.device_id, sequence_index++,
                                           event.start_ns, event.end_ns);

    if (candidate.replay_unit_id.valid()) {
      ir.replay_units.set_anchor_bounds(candidate.replay_unit_id, anchor, anchor);
      ir.protected_intervals.append(
          ProtectedIntervalKind::kGraphReplayUnit, BoundaryPolicy::kNoCross,
          token, token, anchor, anchor, event.source_ref_id);
    }

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
