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
  std::vector<SourceRefId> source_refs;
};

const TraceEventRow& task_event(const NativeIr& ir, const TaskRow& task) {
  if (!task.trace_event_id.valid() ||
      task.trace_event_id.value() >= ir.trace_events.size()) {
    throw std::invalid_argument("TaskRow trace_event_id is out of range");
  }
  return ir.trace_events.row(task.trace_event_id);
}

bool is_productive_role(SemanticTaskRole role) {
  return role == SemanticTaskRole::kProductiveCompute ||
         role == SemanticTaskRole::kProductiveComm ||
         role == SemanticTaskRole::kProductiveDataMove;
}

// Communication canonicalization (contract section 6.6): COMMUNICATION_OP
// rows are the canonical productive communication intervals; a
// productive_comm TaskRow is absorbed as supporting evidence when it has an
// unambiguous same-device connectionId match with temporally compatible
// intervals. Tasks with no connectionId, or with an ambiguous match, become
// canonical themselves (with a diagnostic); nothing is silently merged.
bool try_absorb_comm_task(
    const NativeIr& ir,
    const TaskRow& task,
    const TraceEventRow& event,
    const std::map<std::pair<std::uint32_t, std::int64_t>,
                   std::vector<CommunicationOpId>>& comm_by_device_connection,
    std::vector<TimelineDiagnostic>* diagnostics,
    std::vector<Candidate>* candidates) {
  if (task.raw_connection_id < 0) {
    candidates->push_back(
        Candidate{event.start_ns, event.end_ns, {task.source_ref_id}});
    return true;
  }
  const auto it = comm_by_device_connection.find(
      {event.device_id, task.raw_connection_id});
  if (it == comm_by_device_connection.end()) {
    // No matching COMMUNICATION_OP row: the task is canonical, with a note
    // that no op counterpart exists.
    diagnostics->push_back(
        TimelineDiagnostic{"productive_comm task without a matching "
                           "COMMUNICATION_OP row",
                           task.raw_connection_id});
    candidates->push_back(
        Candidate{event.start_ns, event.end_ns, {task.source_ref_id}});
    return true;
  }
  const std::vector<CommunicationOpId>& ops = it->second;
  CommunicationOpId match;
  bool found = false;
  for (const CommunicationOpId op_id : ops) {
    const CommunicationOpRow& op = ir.communication_ops.row(op_id);
    const TraceEventRow& op_event = ir.trace_events.row(op.trace_event_id);
    if (op_event.start_ns < event.end_ns && op_event.end_ns > event.start_ns) {
      if (found) {
        // More than one temporally compatible op for one connection id:
        // ambiguous, do not absorb.
        diagnostics->push_back(
            TimelineDiagnostic{
                "ambiguous connectionId: multiple COMMUNICATION_OP rows "
                "match one task",
                task.raw_connection_id});
        candidates->push_back(
            Candidate{event.start_ns, event.end_ns, {task.source_ref_id}});
        return true;
      }
      match = op_id;
      found = true;
    }
  }
  if (!found) {
    diagnostics->push_back(
        TimelineDiagnostic{
            "connectionId matches a COMMUNICATION_OP row with incompatible "
            "timing; task kept canonical",
            task.raw_connection_id});
    candidates->push_back(
        Candidate{event.start_ns, event.end_ns, {task.source_ref_id}});
    return true;
  }
  // Absorbed: the op is the canonical interval; keep the task as a
  // supporting source ref.
  const CommunicationOpRow& op = ir.communication_ops.row(match);
  const TraceEventRow& op_event = ir.trace_events.row(op.trace_event_id);
  for (Candidate& candidate : *candidates) {
    if (candidate.start_ns == op_event.start_ns &&
        candidate.end_ns == op_event.end_ns) {
      candidate.source_refs.push_back(task.source_ref_id);
      return true;
    }
  }
  candidates->push_back(
      Candidate{op_event.start_ns, op_event.end_ns,
                {op.source_ref_id, task.source_ref_id}});
  return true;
}

std::vector<Candidate> merge_union(std::vector<Candidate> candidates) {
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& lhs, const Candidate& rhs) {
              return lhs.start_ns < rhs.start_ns;
            });
  std::vector<Candidate> merged;
  for (Candidate& candidate : candidates) {
    if (candidate.end_ns <= candidate.start_ns) {
      continue;  // zero/negative intervals are dropped by construction.
    }
    if (merged.empty() || candidate.start_ns > merged.back().end_ns) {
      merged.push_back(std::move(candidate));
      continue;
    }
    if (candidate.end_ns > merged.back().end_ns) {
      merged.back().end_ns = candidate.end_ns;
    }
    for (SourceRefId ref : candidate.source_refs) {
      merged.back().source_refs.push_back(ref);
    }
  }
  return merged;
}

void build_device_timeline(
    const NativeIr& ir,
    const SemanticTaskClassificationResult& classification,
    std::uint32_t device_id,
    const ProductiveTimelineOptions& options,
    DeviceTimelineResult* result) {
  // Collect per-device candidates from canonical comm ops and from tasks.
  std::map<std::pair<std::uint32_t, std::int64_t>,
           std::vector<CommunicationOpId>>
      comm_by_device_connection;
  for (const CommunicationOpRow& op : ir.communication_ops.rows()) {
    const TraceEventRow& event = ir.trace_events.row(op.trace_event_id);
    if (event.device_id != device_id || op.raw_connection_id < 0) {
      continue;
    }
    comm_by_device_connection[{event.device_id, op.raw_connection_id}]
        .push_back(op.id);
  }

  std::vector<Candidate> candidates;
  // All COMMUNICATION_OP rows on this device are canonical communication.
  for (const CommunicationOpRow& op : ir.communication_ops.rows()) {
    const TraceEventRow& event = ir.trace_events.row(op.trace_event_id);
    if (event.device_id == device_id) {
      candidates.push_back(
          Candidate{event.start_ns, event.end_ns, {op.source_ref_id}});
    }
  }

  for (std::size_t index = 0; index < classification.rows.size(); ++index) {
    const SemanticTaskClassificationRow& row = classification.rows[index];
    if (!is_productive_role(row.role)) {
      continue;
    }
    const TaskRow& task = ir.tasks.row(TaskId(index));
    const TraceEventRow& event = task_event(ir, task);
    if (event.device_id != device_id) {
      continue;
    }
    if (row.role == SemanticTaskRole::kProductiveComm) {
      try_absorb_comm_task(ir, task, event, comm_by_device_connection,
                           &result->diagnostics, &candidates);
      continue;
    }
    candidates.push_back(
        Candidate{event.start_ns, event.end_ns, {task.source_ref_id}});
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
                                     interval.source_refs});
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

std::vector<DeviceTimelineResult> build_productive_timelines(
    const NativeIr& ir,
    const SemanticTaskClassificationResult& classification,
    const ProductiveTimelineOptions& options) {
  if (classification.rows.size() != ir.tasks.size()) {
    throw std::invalid_argument(
        "classification row count does not match task count");
  }
  if (ir.trace_events.empty()) {
    return {};
  }

  std::vector<std::uint32_t> device_ids;
  for (const TraceEventRow& event : ir.trace_events.rows()) {
    if (std::find(device_ids.begin(), device_ids.end(), event.device_id) ==
        device_ids.end()) {
      device_ids.push_back(event.device_id);
    }
  }

  std::vector<DeviceTimelineResult> results;
  for (const std::uint32_t device_id : device_ids) {
    DeviceTimelineResult result;
    result.device_id = device_id;
    build_device_timeline(ir, classification, device_id, options, &result);
    results.push_back(std::move(result));
  }
  return results;
}

}  // namespace traceloom
