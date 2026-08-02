#include "traceloom/analysis/productive_timeline.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace traceloom {
namespace {

struct Candidate {
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::vector<ProductiveSourceLink> source_links;
  // Identity of the canonical COMMUNICATION_OP this candidate materializes
  // (invalid for task candidates). Absorption looks candidates up by this
  // id, never by interval equality.
  CommunicationOpId canonical_op_id;
};

const TraceEventRow* task_event_or_null(const NativeIr& ir,
                                        const TaskRow& task) {
  if (!task.trace_event_id.valid() ||
      task.trace_event_id.value() >= ir.trace_events.size()) {
    return nullptr;
  }
  return &ir.trace_events.row(task.trace_event_id);
}

std::string symbol_text(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

bool is_productive_role(SemanticTaskRole role) {
  return role == SemanticTaskRole::kProductiveCompute ||
         role == SemanticTaskRole::kProductiveComm ||
         role == SemanticTaskRole::kProductiveDataMove;
}

// Contract section 6.6: when communication metadata is available it must be
// compatible. A task comm_name that conflicts with every op-side name is a
// mismatch; absence of metadata on either side is not a conflict.
bool metadata_compatible(const NativeIr& ir,
                         const TaskRow& task,
                         const CommunicationOpRow& op) {
  if (!task.comm_name_symbol_id.valid()) {
    return true;
  }
  const std::string task_comm = symbol_text(ir, task.comm_name_symbol_id);
  if (op.op_name_symbol_id.valid() &&
      symbol_text(ir, op.op_name_symbol_id) == task_comm) {
    return true;
  }
  if (op.op_type_symbol_id.valid() &&
      symbol_text(ir, op.op_type_symbol_id) == task_comm) {
    return true;
  }
  if (op.linked_task_name_symbol_id.valid() &&
      symbol_text(ir, op.linked_task_name_symbol_id) == task_comm) {
    return true;
  }
  if (op.op_name_symbol_id.valid() || op.op_type_symbol_id.valid() ||
      op.linked_task_name_symbol_id.valid()) {
    return false;  // op carries names but none matches the task comm name.
  }
  return true;  // no op-side metadata: no conflict evidence.
}

bool intervals_overlap(std::int64_t a_start, std::int64_t a_end,
                       std::int64_t b_start, std::int64_t b_end) {
  return a_start < b_end && b_start < a_end;
}

bool add_task_candidate(
    const TaskRow& task,
    const TraceEventRow& event,
    std::vector<Candidate>* candidates,
    std::vector<TimelineDiagnostic>* diagnostics) {
  if (event.end_ns <= event.start_ns) {
    diagnostics->push_back(
        TimelineDiagnostic{"invalid task interval (end <= start)",
                           task.raw_global_task_id});
    return false;
  }
  candidates->push_back(
      Candidate{event.start_ns, event.end_ns,
                {ProductiveSourceLink{ProductiveSourceLink::Kind::kTask,
                                      task.trace_event_id, task.id,
                                      CommunicationOpId::invalid()}},
                CommunicationOpId::invalid()});
  return true;
}

bool add_comm_op_candidate(
    const CommunicationOpRow& op,
    const TraceEventRow& event,
    std::vector<Candidate>* candidates,
    std::vector<TimelineDiagnostic>* diagnostics) {
  if (event.end_ns <= event.start_ns) {
    diagnostics->push_back(
        TimelineDiagnostic{"invalid COMMUNICATION_OP interval (end <= start)",
                           op.raw_op_id});
    return false;
  }
  candidates->push_back(
      Candidate{event.start_ns, event.end_ns,
                {ProductiveSourceLink{ProductiveSourceLink::Kind::kCommunicationOp,
                                      op.trace_event_id, TaskId::invalid(),
                                      op.id}},
                op.id});
  return true;
}

// Communication canonicalization (contract section 6.6): COMMUNICATION_OP
// rows are the canonical productive communication intervals; a
// productive_comm TaskRow is absorbed as supporting evidence when it has an
// unambiguous same-device connectionId match, temporally compatible
// intervals, and compatible metadata. Tasks with no connectionId but a
// temporally overlapping op are kept canonical with an ambiguity
// diagnostic; nothing is silently merged.
void absorb_or_keep_comm_task(
    const NativeIr& ir,
    const TaskRow& task,
    const TraceEventRow& event,
    const std::vector<CommunicationOpId>& device_ops,
    const std::map<std::pair<std::uint32_t, std::int64_t>,
                   std::vector<CommunicationOpId>>& comm_by_device_connection,
    std::vector<TimelineDiagnostic>* diagnostics,
    std::vector<Candidate>* candidates) {
  if (task.raw_connection_id < 0) {
    // No connection id: we cannot determine whether a temporally similar op
    // is the same communication. Keep the task canonical and report the
    // ambiguity instead of guessing.
    for (const CommunicationOpId op_id : device_ops) {
      const CommunicationOpRow& op = ir.communication_ops.row(op_id);
      const TraceEventRow& op_event = ir.trace_events.row(op.trace_event_id);
      if (intervals_overlap(event.start_ns, event.end_ns, op_event.start_ns,
                            op_event.end_ns)) {
        diagnostics->push_back(
            TimelineDiagnostic{
                "no connectionId; temporally overlapping COMMUNICATION_OP "
                "exists; cannot determine whether it is the same "
                "communication; task kept canonical",
                task.raw_global_task_id});
        break;
      }
    }
    (void)add_task_candidate(task, event, candidates, diagnostics);
    return;
  }
  const auto it = comm_by_device_connection.find(
      {event.device_id, task.raw_connection_id});
  if (it == comm_by_device_connection.end()) {
    diagnostics->push_back(
        TimelineDiagnostic{
            "productive_comm task without a matching COMMUNICATION_OP row",
            task.raw_connection_id});
    (void)add_task_candidate(task, event, candidates, diagnostics);
    return;
  }
  const std::vector<CommunicationOpId>& ops = it->second;
  CommunicationOpId match;
  bool found = false;
  for (const CommunicationOpId op_id : ops) {
    const CommunicationOpRow& op = ir.communication_ops.row(op_id);
    const TraceEventRow& op_event = ir.trace_events.row(op.trace_event_id);
    if (!intervals_overlap(event.start_ns, event.end_ns, op_event.start_ns,
                           op_event.end_ns)) {
      continue;
    }
    if (!metadata_compatible(ir, task, op)) {
      diagnostics->push_back(
          TimelineDiagnostic{
              "connectionId matches a COMMUNICATION_OP row with conflicting "
              "communication metadata; task kept canonical",
              task.raw_connection_id});
      (void)add_task_candidate(task, event, candidates, diagnostics);
      return;
    }
    if (found) {
      diagnostics->push_back(
          TimelineDiagnostic{
              "ambiguous connectionId: multiple compatible COMMUNICATION_OP "
              "rows match one task",
              task.raw_connection_id});
      (void)add_task_candidate(task, event, candidates, diagnostics);
      return;
    }
    match = op_id;
    found = true;
  }
  if (!found) {
    diagnostics->push_back(
        TimelineDiagnostic{
            "connectionId matches a COMMUNICATION_OP row with incompatible "
            "timing; task kept canonical",
            task.raw_connection_id});
    (void)add_task_candidate(task, event, candidates, diagnostics);
    return;
  }
  // Absorbed: attach the task as a supporting source to the op's candidate,
  // identified by canonical op id (never by interval equality).
  const CommunicationOpRow& op = ir.communication_ops.row(match);
  for (Candidate& candidate : *candidates) {
    if (candidate.canonical_op_id == match) {
      candidate.source_links.push_back(
          ProductiveSourceLink{ProductiveSourceLink::Kind::kTask,
                               task.trace_event_id, task.id,
                               CommunicationOpId::invalid()});
      return;
    }
  }
  const TraceEventRow& op_event = ir.trace_events.row(op.trace_event_id);
  (void)add_comm_op_candidate(op, op_event, candidates, diagnostics);
  // The op candidate was just appended; attach the task link to it.
  for (Candidate& candidate : *candidates) {
    if (candidate.canonical_op_id == match) {
      candidate.source_links.push_back(
          ProductiveSourceLink{ProductiveSourceLink::Kind::kTask,
                               task.trace_event_id, task.id,
                               CommunicationOpId::invalid()});
      return;
    }
  }
}

std::vector<Candidate> merge_union(std::vector<Candidate> candidates) {
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& lhs, const Candidate& rhs) {
              return lhs.start_ns < rhs.start_ns;
            });
  std::vector<Candidate> merged;
  for (Candidate& candidate : candidates) {
    if (merged.empty() || candidate.start_ns > merged.back().end_ns) {
      merged.push_back(std::move(candidate));
      continue;
    }
    if (candidate.end_ns > merged.back().end_ns) {
      merged.back().end_ns = candidate.end_ns;
    }
    for (ProductiveSourceLink& link : candidate.source_links) {
      merged.back().source_links.push_back(std::move(link));
    }
  }
  return merged;
}

void build_device_timeline(
    const NativeIr& ir,
    const SemanticTaskClassificationResult& classification,
    std::uint32_t device_id,
    const ProductiveTimelineOptions& options,
    bool* run_invalid,
    DeviceTimelineResult* result) {
  // Canonical communication ops on this device, indexed by connection id.
  std::map<std::pair<std::uint32_t, std::int64_t>,
           std::vector<CommunicationOpId>>
      comm_by_device_connection;
  std::vector<CommunicationOpId> device_ops;
  for (const CommunicationOpRow& op : ir.communication_ops.rows()) {
    const TraceEventRow& event = ir.trace_events.row(op.trace_event_id);
    if (event.device_id != device_id) {
      continue;
    }
    device_ops.push_back(op.id);
    if (op.raw_connection_id >= 0) {
      comm_by_device_connection[{event.device_id, op.raw_connection_id}]
          .push_back(op.id);
    }
  }

  std::vector<Candidate> candidates;
  bool device_invalid = false;
  for (const CommunicationOpId op_id : device_ops) {
    const CommunicationOpRow& op = ir.communication_ops.row(op_id);
    const TraceEventRow& event = ir.trace_events.row(op.trace_event_id);
    if (!add_comm_op_candidate(op, event, &candidates,
                               &result->diagnostics)) {
      device_invalid = true;
    }
  }

  for (std::size_t index = 0; index < classification.rows.size(); ++index) {
    const SemanticTaskClassificationRow& row = classification.rows[index];
    if (!is_productive_role(row.role)) {
      continue;
    }
    const TaskRow& task = ir.tasks.row(TaskId(index));
    const TraceEventRow* event = task_event_or_null(ir, task);
    if (event == nullptr) {
      result->diagnostics.push_back(
          TimelineDiagnostic{"task has an out-of-range trace_event_id",
                             task.raw_global_task_id});
      device_invalid = true;
      continue;
    }
    if (event->device_id != device_id) {
      continue;
    }
    if (row.role == SemanticTaskRole::kProductiveComm) {
      absorb_or_keep_comm_task(ir, task, *event, device_ops,
                               comm_by_device_connection,
                               &result->diagnostics, &candidates);
      continue;
    }
    if (!add_task_candidate(task, *event, &candidates,
                            &result->diagnostics)) {
      device_invalid = true;
    }
  }
  if (device_invalid) {
    *run_invalid = true;
  }

  const std::vector<Candidate> union_intervals = merge_union(candidates);
  result->semantic_rules_version = classification.semantic_rules_version;
  result->semantic_rules_sha256 = classification.semantic_rules_sha256;

  // Determine the analysis span.
  std::int64_t span_start = 0;
  std::int64_t span_end = 0;
  if (options.explicit_span_start_ns.has_value() ||
      options.explicit_span_end_ns.has_value()) {
    if (!options.explicit_span_start_ns.has_value() ||
        !options.explicit_span_end_ns.has_value() ||
        *options.explicit_span_end_ns <= *options.explicit_span_start_ns) {
      result->status = AnalysisStatus::kInvalidAnalysisSpan;
      return;
    }
    span_start = *options.explicit_span_start_ns;
    span_end = *options.explicit_span_end_ns;
  } else if (!union_intervals.empty()) {
    span_start = union_intervals.front().start_ns;
    span_end = union_intervals.back().end_ns;
  } else {
    result->status = AnalysisStatus::kNoProductiveSpan;
    return;
  }

  // Slice the span into productive/gap intervals. Union intervals are
  // clipped to the span; gaps are the complement.
  std::vector<DeviceIntervalRow> rows;
  std::int64_t cursor = span_start;
  for (const Candidate& interval : union_intervals) {
    const std::int64_t clipped_start = std::max(interval.start_ns, span_start);
    const std::int64_t clipped_end = std::min(interval.end_ns, span_end);
    if (clipped_end <= clipped_start) {
      continue;
    }
    if (clipped_start > cursor) {
      rows.push_back(
          DeviceIntervalRow{cursor, clipped_start,
                            DeviceIntervalKind::kVisibleProductiveIdle, {}});
    }
    rows.push_back(DeviceIntervalRow{clipped_start, clipped_end,
                                     DeviceIntervalKind::kProductiveActive,
                                     interval.source_links});
    cursor = std::max(cursor, clipped_end);
  }
  if (cursor < span_end) {
    rows.push_back(DeviceIntervalRow{cursor, span_end,
                                     DeviceIntervalKind::kVisibleProductiveIdle,
                                     {}});
  }

  result->status = AnalysisStatus::kOk;
  result->span_start_ns = span_start;
  result->span_end_ns = span_end;
  result->intervals = std::move(rows);
}

}  // namespace

ProductiveTimelineRunResult build_productive_timelines(
    const NativeIr& ir,
    const SemanticTaskClassificationResult& classification,
    const ProductiveTimelineOptions& options) {
  if (classification.rows.size() != ir.tasks.size()) {
    throw std::invalid_argument(
        "classification row count does not match task count");
  }
  // Validate the classification alignment: rows must reference their own
  // TaskRow ids, never silently pair by array order alone.
  for (std::size_t index = 0; index < classification.rows.size(); ++index) {
    const TaskRow& task = ir.tasks.row(TaskId(index));
    if (classification.rows[index].task_id != task.id ||
        classification.rows[index].trace_event_id != task.trace_event_id) {
      throw std::invalid_argument(
          "classification row is not aligned with its TaskRow");
    }
  }

  ProductiveTimelineRunResult run;
  run.semantic_rules_version = classification.semantic_rules_version;
  run.semantic_rules_sha256 = classification.semantic_rules_sha256;
  if (ir.tasks.empty() && ir.communication_ops.empty()) {
    run.status = AnalysisStatus::kEmptyInput;
    return run;
  }

  std::vector<std::uint32_t> device_ids;
  for (const TaskRow& task : ir.tasks.rows()) {
    const TraceEventRow* event = task_event_or_null(ir, task);
    if (event != nullptr &&
        std::find(device_ids.begin(), device_ids.end(), event->device_id) ==
            device_ids.end()) {
      device_ids.push_back(event->device_id);
    }
  }
  for (const CommunicationOpRow& op : ir.communication_ops.rows()) {
    if (op.trace_event_id.valid() &&
        op.trace_event_id.value() < ir.trace_events.size()) {
      const std::uint32_t device_id =
          ir.trace_events.row(op.trace_event_id).device_id;
      if (std::find(device_ids.begin(), device_ids.end(), device_id) ==
          device_ids.end()) {
        device_ids.push_back(device_id);
      }
    }
  }

  bool run_invalid = false;
  for (const std::uint32_t device_id : device_ids) {
    DeviceTimelineResult result;
    result.device_id = device_id;
    build_device_timeline(ir, classification, device_id, options,
                          &run_invalid, &result);
    run.devices.push_back(std::move(result));
  }
  if (run_invalid) {
    run.status = AnalysisStatus::kInvalidInput;
  }
  return run;
}

}  // namespace traceloom
