#include "traceloom/analysis/stream_state_timeline.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace traceloom {
namespace {

// Sentinel stream id written by the ascend adapter for events that carry no
// stream metadata (COMMUNICATION_OP without a linked primary stream): the
// adapter stores 0xFFFFFFFF and appends no StreamRow. Such events are
// observable but unassignable, not damaged input. They cannot produce a
// per-stream timeline and conservatively void observed-universe completeness.
constexpr std::uint32_t kUnassignedStreamSentinel = 0xffffffffu;

// One canonical event after collection: a non-absorbed TaskRow trace event or
// a COMMUNICATION_OP trace event. Times are pre-clip; clipping happens per
// stream against the device span. source_links carries the canonical source
// plus, for communication ops, every absorbed supporting task (contract
// 6.6: the op remains one canonical event with multiple supporting sources).
struct StreamEvent {
  StreamStateSourceLink::Kind kind;
  TraceEventId trace_event_id;
  std::uint64_t stream_id = 0;  // resolved raw stream id
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  StreamState state = StreamState::kUnknown;
  std::vector<StreamStateSourceLink> source_links;
  std::int64_t diagnostic_source_row_id = -1;  // E2-style raw row id
};

// Stream identity mirrors the adapter's stream_key: (device_id, low 32 bits
// of the raw stream id). The index is built once and keep-first on
// pathological duplicate keys (hand-built IRs only; the adapter cannot
// produce them).
using StreamIndex = std::map<std::pair<std::uint32_t, std::uint32_t>, StreamId>;

StreamIndex build_stream_index(const NativeIr& ir) {
  StreamIndex index;
  for (const StreamRow& row : ir.streams.rows()) {
    index.emplace(std::make_pair(
                      row.device_id,
                      static_cast<std::uint32_t>(row.raw_stream_id &
                                                 0xffffffffu)),
                  row.id);
  }
  return index;
}

StreamId resolve_stream(const StreamIndex& index,
                        const TraceEventRow& event) {
  const auto found = index.find({event.device_id, event.stream_id});
  return found == index.end() ? StreamId::invalid() : found->second;
}

const TraceEventRow* trace_event_or_null(const NativeIr& ir,
                                         TraceEventId trace_event_id) {
  if (!trace_event_id.valid() ||
      trace_event_id.value() >= ir.trace_events.size()) {
    return nullptr;
  }
  return &ir.trace_events.row(trace_event_id);
}

StreamState role_to_state(SemanticTaskRole role) {
  switch (role) {
    case SemanticTaskRole::kProductiveCompute:
      return StreamState::kRunningCompute;
    case SemanticTaskRole::kProductiveComm:
      return StreamState::kRunningComm;
    case SemanticTaskRole::kProductiveDataMove:
      return StreamState::kRunningDataMove;
    case SemanticTaskRole::kVisibleWait:
      return StreamState::kRunningWait;
    case SemanticTaskRole::kCaptureControl:
      return StreamState::kRunningCaptureControl;
    case SemanticTaskRole::kRecord:
      return StreamState::kRunningRecord;
    case SemanticTaskRole::kRuntimeControl:
      return StreamState::kRunningRuntimeControl;
    case SemanticTaskRole::kUnknown:
      return StreamState::kUnknown;
  }
  return StreamState::kUnknown;
}

// Ascend emits zero-duration rows for synchronization/control point markers
// such as EVENT_RECORD, EVENT_WAIT, NOTIFY_RECORD, and MODEL_MAINTAINCE. They
// have no interval extent and therefore cannot appear in the interval-bearing
// stream-state table, but they are not damaged input. Productive and unknown
// rows are expected to describe intervals, so zero duration remains invalid
// for those roles.
bool is_non_interval_point_role(SemanticTaskRole role) {
  return role == SemanticTaskRole::kVisibleWait ||
         role == SemanticTaskRole::kCaptureControl ||
         role == SemanticTaskRole::kRecord ||
         role == SemanticTaskRole::kRuntimeControl;
}

// Deterministic source-link ordering (reproducible output, and link vector
// equality becomes set equality for the merge rule). trace_event_id is
// unique per event in practice, so the trailing comparisons never bind.
bool link_less(const StreamStateSourceLink& lhs,
               const StreamStateSourceLink& rhs) {
  if (lhs.kind != rhs.kind) {
    return static_cast<int>(lhs.kind) < static_cast<int>(rhs.kind);
  }
  if (lhs.trace_event_id != rhs.trace_event_id) {
    return lhs.trace_event_id < rhs.trace_event_id;
  }
  if (lhs.task_id != rhs.task_id) {
    return lhs.task_id < rhs.task_id;
  }
  if (lhs.communication_op_id != rhs.communication_op_id) {
    return lhs.communication_op_id < rhs.communication_op_id;
  }
  if (lhs.source_ref_id != rhs.source_ref_id) {
    return lhs.source_ref_id < rhs.source_ref_id;
  }
  return lhs.matched_rule_id.value_or(std::string()) <
         rhs.matched_rule_id.value_or(std::string());
}

// Identity for exact duplicates: same canonical source (kind + trace event),
// same clipped interval, same resulting state.
struct DedupeKey {
  int kind = 0;
  std::uint32_t trace_event_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  int state = 0;

  friend bool operator<(const DedupeKey& lhs, const DedupeKey& rhs) {
    return std::tie(lhs.kind, lhs.trace_event_id, lhs.start_ns, lhs.end_ns,
                    lhs.state) <
           std::tie(rhs.kind, rhs.trace_event_id, rhs.start_ns, rhs.end_ns,
                    rhs.state);
  }
};

std::string interval_text(std::int64_t start_ns, std::int64_t end_ns) {
  return "[" + std::to_string(start_ns) + "," + std::to_string(end_ns) + ")";
}

// One boundary of a canonical event for the sweep line. (end, remove) sorts
// before (start, add) at the same timestamp, which keeps half-open
// [start, end) semantics: an event ending at t and one starting at t are
// adjacent, never overlapping.
struct SweepAction {
  std::int64_t time_ns = 0;
  int action = 0;  // 0 = remove (end), 1 = add (start)
  std::size_t event_index = 0;

  friend bool operator<(const SweepAction& lhs, const SweepAction& rhs) {
    if (lhs.time_ns != rhs.time_ns) {
      return lhs.time_ns < rhs.time_ns;
    }
    return lhs.action < rhs.action;  // removals before additions
  }
};

// Emits one segment [seg_start, seg_end) from the active canonical event
// set: zero events -> empty_observed, one event -> its state with all its
// supporting links, two or more events -> ambiguous_overlap with the union
// of all their links. ambiguous_overlap is decided by the canonical event
// count, never by the link count.
void push_segment(std::int64_t seg_start,
                  std::int64_t seg_end,
                  const std::set<std::size_t>& active,
                  const std::vector<StreamEvent>& events,
                  std::vector<StreamStateInterval>* intervals) {
  if (active.empty()) {
    intervals->push_back(
        StreamStateInterval{seg_start, seg_end, StreamState::kEmptyObserved,
                            {}});
    return;
  }
  std::vector<StreamStateSourceLink> links;
  StreamState state = StreamState::kUnknown;
  for (const std::size_t index : active) {
    if (links.empty()) {
      state = events[index].state;
    } else {
      state = StreamState::kAmbiguousOverlap;
    }
    for (const StreamStateSourceLink& link : events[index].source_links) {
      links.push_back(link);
    }
  }
  std::sort(links.begin(), links.end(), link_less);
  intervals->push_back(
      StreamStateInterval{seg_start, seg_end, state, std::move(links)});
}

// Builds the timeline for one stream over the device span: clip, dedupe,
// sweep line, merge. Returns false when no canonical event remains (stream
// is not part of the observed universe).
bool build_stream_timeline(std::int64_t span_start,
                           std::int64_t span_end,
                           std::vector<StreamEvent>* events,
                           StreamStateTimeline* timeline) {
  // Clip to the analysis span. Events fully outside (including those merely
  // touching a boundary, which clip to zero length) are silently ignored:
  // they do not participate in this analysis span.
  std::vector<StreamEvent> clipped;
  clipped.reserve(events->size());
  for (StreamEvent& event : *events) {
    if (event.end_ns <= span_start || event.start_ns >= span_end) {
      continue;
    }
    const std::int64_t clipped_start = std::max(event.start_ns, span_start);
    const std::int64_t clipped_end = std::min(event.end_ns, span_end);
    if (event.start_ns < span_start || event.end_ns > span_end) {
      timeline->diagnostics.push_back(TimelineDiagnostic{
          "event_clipped_to_span: event " + interval_text(event.start_ns,
                                                          event.end_ns) +
              " clipped to the analysis span",
          event.diagnostic_source_row_id});
    }
    event.start_ns = clipped_start;
    event.end_ns = clipped_end;
    clipped.push_back(std::move(event));
  }
  if (clipped.empty()) {
    return false;
  }

  // Exact duplicates: same stable lineage, interval, and state. Keep the
  // first and report; the analysis status stays ok. Different lineages with
  // identical intervals are NOT duplicates and proceed to the sweep.
  std::vector<StreamEvent> deduped;
  deduped.reserve(clipped.size());
  std::set<DedupeKey> seen;
  for (StreamEvent& event : clipped) {
    const DedupeKey key{static_cast<int>(event.kind),
                        event.trace_event_id.value(), event.start_ns,
                        event.end_ns, static_cast<int>(event.state)};
    if (!seen.insert(key).second) {
      timeline->diagnostics.push_back(TimelineDiagnostic{
          "exact_duplicate_event: duplicate canonical event " +
              interval_text(event.start_ns, event.end_ns) + "; kept one",
          event.diagnostic_source_row_id});
      continue;
    }
    deduped.push_back(std::move(event));
  }

  // Coincident distinct events: >= 2 distinct lineages share one interval.
  std::map<std::pair<std::int64_t, std::int64_t>, std::set<TraceEventId>>
      lineages_by_interval;
  for (const StreamEvent& event : deduped) {
    lineages_by_interval[{event.start_ns, event.end_ns}].insert(
        event.trace_event_id);
  }
  for (const auto& item : lineages_by_interval) {
    if (item.second.size() >= 2) {
      // Group-level note with no single owning row.
      timeline->diagnostics.push_back(TimelineDiagnostic{
          "coincident_distinct_events: " +
              std::to_string(item.second.size()) +
              " distinct events share interval " +
              interval_text(item.first.first, item.first.second),
          -1});
    }
  }

  // Sweep line over event boundaries, maintaining the active canonical event
  // set. O(n log n + output).
  std::vector<SweepAction> actions;
  actions.reserve(deduped.size() * 2);
  for (std::size_t index = 0; index < deduped.size(); ++index) {
    actions.push_back(SweepAction{deduped[index].start_ns, 1, index});
    actions.push_back(SweepAction{deduped[index].end_ns, 0, index});
  }
  std::sort(actions.begin(), actions.end());

  std::vector<StreamStateInterval> intervals;
  intervals.reserve(deduped.size() + 1);
  std::set<std::size_t> active;
  std::int64_t cursor = span_start;
  for (const SweepAction& action : actions) {
    if (action.time_ns > cursor) {
      push_segment(cursor, action.time_ns, active, deduped, &intervals);
      cursor = action.time_ns;
    }
    if (action.action == 0) {
      active.erase(action.event_index);
    } else {
      active.insert(action.event_index);
    }
  }
  if (cursor < span_end) {
    push_segment(cursor, span_end, active, deduped, &intervals);
  }

  // Merge adjacent segments with identical state and identical source link
  // sets (never across differing lineage: [10,20) compute from TASK:17 and
  // [20,30) compute from TASK:28 must stay separate).
  std::vector<StreamStateInterval> merged;
  merged.reserve(intervals.size());
  for (StreamStateInterval& interval : intervals) {
    if (!merged.empty() && interval.state == merged.back().state &&
        interval.source_links == merged.back().source_links) {
      merged.back().end_ns = interval.end_ns;
      continue;
    }
    merged.push_back(std::move(interval));
  }
  timeline->intervals = std::move(merged);
  return true;
}

}  // namespace

std::string_view stream_state_name(StreamState state) {
  switch (state) {
    case StreamState::kRunningCompute:
      return "running_compute";
    case StreamState::kRunningComm:
      return "running_comm";
    case StreamState::kRunningDataMove:
      return "running_data_move";
    case StreamState::kRunningWait:
      return "running_wait";
    case StreamState::kRunningCaptureControl:
      return "running_capture_control";
    case StreamState::kRunningRecord:
      return "running_record";
    case StreamState::kRunningRuntimeControl:
      return "running_runtime_control";
    case StreamState::kUnknown:
      return "unknown";
    case StreamState::kEmptyObserved:
      return "empty_observed";
    case StreamState::kAmbiguousOverlap:
      return "ambiguous_overlap";
  }
  return "unknown";
}

StreamStateRunResult build_stream_state_timelines(
    const NativeIr& ir,
    const SemanticTaskClassificationResult& classification,
    const ProductiveTimelineRunResult& productive) {
  // Classification alignment validation: rows must reference their own
  // TaskRow ids, never silently pair by array order alone (mirrors E2).
  if (classification.rows.size() != ir.tasks.size()) {
    throw std::invalid_argument(
        "classification_task_mismatch: classification row count does not "
        "match task count");
  }
  for (std::size_t index = 0; index < classification.rows.size(); ++index) {
    const TaskRow& task = ir.tasks.row(TaskId(index));
    if (classification.rows[index].task_id != task.id) {
      throw std::invalid_argument(
          "classification_task_mismatch: classification row is not aligned "
          "with its TaskRow");
    }
    if (classification.rows[index].trace_event_id != task.trace_event_id) {
      throw std::invalid_argument(
          "classification_event_mismatch: classification row trace event "
          "does not match its TaskRow");
    }
  }

  StreamStateRunResult run;
  if (ir.tasks.empty() && ir.communication_ops.empty()) {
    run.status = AnalysisStatus::kEmptyInput;
    return run;
  }

  // E2 communication canonicalization (contract 6.6): absorbed tasks are
  // supporting evidence of their canonical op. The op keeps its canonical
  // event and carries every absorbed task as an additional source link.
  std::set<TaskId> absorbed_tasks;
  std::map<CommunicationOpId, std::vector<TaskId>> absorbed_tasks_by_op;
  for (const DeviceTimelineResult& device : productive.devices) {
    for (const AbsorbedTaskLink& link : device.absorbed_task_links) {
      absorbed_tasks.insert(link.task_id);
      absorbed_tasks_by_op[link.canonical_op_id].push_back(link.task_id);
    }
  }

  const StreamIndex stream_index = build_stream_index(ir);

  // One validation pass over tasks and communication ops: damaged or
  // unassignable rows are skipped with diagnostics; valid events are
  // bucketed by device. Damage degrades the run to kInvalidInput; an event
  // that is merely unassignable (no stream metadata) degrades only the
  // device's scan completeness, never the run status.
  std::map<std::uint32_t, std::vector<StreamEvent>> events_by_device;
  std::map<std::uint32_t, std::vector<TimelineDiagnostic>>
      diagnostics_by_device;
  std::set<std::uint32_t> scan_incomplete_devices;
  std::vector<TimelineDiagnostic> run_diagnostics;
  bool run_invalid = false;

  const auto fail_unassignable = [&](const TraceEventRow& event,
                                     std::int64_t source_row_id) {
    // No stream metadata (0xFFFFFFFF sentinel, adapter omits the StreamRow):
    // observable but not placeable on any stream. Not corruption; the run
    // status stays ok, but this device can no longer attest scan completeness.
    diagnostics_by_device[event.device_id].push_back(TimelineDiagnostic{
        "unassigned_stream: event has no stream metadata (sentinel " +
            std::to_string(kUnassignedStreamSentinel) + ") on device " +
            std::to_string(event.device_id),
        source_row_id});
    scan_incomplete_devices.insert(event.device_id);
  };

  for (std::size_t index = 0; index < classification.rows.size(); ++index) {
    const SemanticTaskClassificationRow& row = classification.rows[index];
    const TaskRow& task = ir.tasks.row(TaskId(index));
    const TraceEventRow* event = trace_event_or_null(ir, task.trace_event_id);
    if (event == nullptr) {
      run_diagnostics.push_back(TimelineDiagnostic{
          "invalid_trace_event_reference: task has an out-of-range "
          "trace_event_id",
          task.raw_global_task_id});
      run_invalid = true;
      continue;
    }
    if (absorbed_tasks.count(task.id) != 0) {
      continue;  // supporting evidence; the canonical op event carries it.
    }
    if (event->end_ns < event->start_ns ||
        (event->end_ns == event->start_ns &&
         !is_non_interval_point_role(row.role))) {
      diagnostics_by_device[event->device_id].push_back(TimelineDiagnostic{
          "invalid_event_duration: task interval (end <= start)",
          task.raw_global_task_id});
      scan_incomplete_devices.insert(event->device_id);
      run_invalid = true;
      continue;
    }
    const bool unassigned_stream =
        event->stream_id == kUnassignedStreamSentinel;
    if (unassigned_stream) {
      // Stream assignment and interval extent are independent axes. Even a
      // legitimate zero-duration point marker cannot attest per-stream scan
      // completeness when its stream identity is unavailable.
      fail_unassignable(*event, task.raw_global_task_id);
    }
    if (event->end_ns == event->start_ns) {
      diagnostics_by_device[event->device_id].push_back(TimelineDiagnostic{
          "zero_duration_point_event_ignored: task has no interval extent",
          task.raw_global_task_id});
      continue;
    }
    if (unassigned_stream) {
      continue;  // diagnosed above; never fabricate a sentinel timeline.
    }
    const StreamId stream = resolve_stream(stream_index, *event);
    if (!stream.valid()) {
      diagnostics_by_device[event->device_id].push_back(TimelineDiagnostic{
          "unknown_stream_identity: no StreamRow for (device " +
              std::to_string(event->device_id) + ", stream " +
              std::to_string(event->stream_id) + ")",
          task.raw_global_task_id});
      scan_incomplete_devices.insert(event->device_id);
      run_invalid = true;
      continue;
    }
    const StreamRow& stream_row = ir.streams.row(stream);
    StreamEvent stream_event;
    stream_event.kind = StreamStateSourceLink::Kind::kTask;
    stream_event.trace_event_id = event->id;
    stream_event.stream_id = stream_row.raw_stream_id;
    stream_event.start_ns = event->start_ns;
    stream_event.end_ns = event->end_ns;
    stream_event.state = role_to_state(row.role);
    stream_event.source_links.push_back(StreamStateSourceLink{
        StreamStateSourceLink::Kind::kTask, event->id, task.id,
        CommunicationOpId::invalid(), event->source_ref_id,
        row.matched_rule_id, stream_event.state});
    stream_event.diagnostic_source_row_id = task.raw_global_task_id;
    events_by_device[event->device_id].push_back(std::move(stream_event));
  }

  for (const CommunicationOpRow& op : ir.communication_ops.rows()) {
    const TraceEventRow* event = trace_event_or_null(ir, op.trace_event_id);
    if (event == nullptr) {
      run_diagnostics.push_back(TimelineDiagnostic{
          "invalid_trace_event_reference: COMMUNICATION_OP has an "
          "out-of-range trace_event_id",
          op.raw_op_id});
      run_invalid = true;
      continue;
    }
    if (event->end_ns <= event->start_ns) {
      diagnostics_by_device[event->device_id].push_back(TimelineDiagnostic{
          "invalid_event_duration: COMMUNICATION_OP interval (end <= start)",
          op.raw_op_id});
      scan_incomplete_devices.insert(event->device_id);
      run_invalid = true;
      continue;
    }
    const StreamId stream = resolve_stream(stream_index, *event);
    if (!stream.valid()) {
      if (event->stream_id == kUnassignedStreamSentinel) {
        fail_unassignable(*event, op.raw_op_id);
      } else {
        diagnostics_by_device[event->device_id].push_back(TimelineDiagnostic{
            "unknown_stream_identity: no StreamRow for (device " +
                std::to_string(event->device_id) + ", stream " +
                std::to_string(event->stream_id) + ")",
            op.raw_op_id});
        scan_incomplete_devices.insert(event->device_id);
        run_invalid = true;
      }
      continue;
    }
    const StreamRow& stream_row = ir.streams.row(stream);
    StreamEvent stream_event;
    stream_event.kind = StreamStateSourceLink::Kind::kCommunicationOp;
    stream_event.trace_event_id = event->id;
    stream_event.stream_id = stream_row.raw_stream_id;
    stream_event.start_ns = event->start_ns;
    stream_event.end_ns = event->end_ns;
    stream_event.state = StreamState::kRunningComm;
    stream_event.source_links.push_back(StreamStateSourceLink{
        StreamStateSourceLink::Kind::kCommunicationOp, event->id,
        TaskId::invalid(), op.id, event->source_ref_id, std::nullopt,
        stream_event.state});
    stream_event.diagnostic_source_row_id = op.raw_op_id;
    // Absorbed tasks attach as supporting sources of the same canonical
    // event (contract 6.6): one event, full lineage.
    const auto absorbed_it = absorbed_tasks_by_op.find(op.id);
    if (absorbed_it != absorbed_tasks_by_op.end()) {
      for (const TaskId task_id : absorbed_it->second) {
        const TaskRow& task = ir.tasks.row(task_id);
        const TraceEventRow* task_event =
            trace_event_or_null(ir, task.trace_event_id);
        if (task_event == nullptr) {
          // Unreachable via E2 (absorption requires a valid event); guard
          // against malformed caller inputs.
          diagnostics_by_device[event->device_id].push_back(
              TimelineDiagnostic{
                  "invalid_trace_event_reference: absorbed task has an "
                  "out-of-range trace_event_id",
                  task.raw_global_task_id});
          scan_incomplete_devices.insert(event->device_id);
          run_invalid = true;
          continue;
        }
        if (task_event->end_ns <= task_event->start_ns) {
          // Defensive API boundary: a malformed or older E2 result may claim
          // that an invalid task was absorbed. Never attach damaged lineage
          // to the canonical op, even though the normal E2 path now rejects
          // the task before communication canonicalization.
          diagnostics_by_device[event->device_id].push_back(
              TimelineDiagnostic{
                  "invalid_event_duration: absorbed task interval "
                  "(end <= start)",
                  task.raw_global_task_id});
          scan_incomplete_devices.insert(event->device_id);
          run_invalid = true;
          continue;
        }
        const SemanticTaskClassificationRow& row =
            classification.rows[task_id.value()];
        stream_event.source_links.push_back(StreamStateSourceLink{
            StreamStateSourceLink::Kind::kTask, task.trace_event_id, task.id,
            CommunicationOpId::invalid(), task_event->source_ref_id,
            row.matched_rule_id, StreamState::kRunningComm});
      }
    }
    events_by_device[event->device_id].push_back(std::move(stream_event));
  }

  // Per device: E2 is authoritative for devices, statuses, and spans.
  for (const DeviceTimelineResult& device : productive.devices) {
    StreamStateDeviceResult result;
    result.device_id = device.device_id;
    const auto diag_it = diagnostics_by_device.find(device.device_id);
    if (diag_it != diagnostics_by_device.end()) {
      result.diagnostics = std::move(diag_it->second);
    }
    if (device.status != AnalysisStatus::kOk) {
      // kNoProductiveSpan / kInvalidAnalysisSpan: no span, no timelines, no
      // universe; scan completeness is vacuously true (nothing claimable).
      result.status = device.status;
      run.devices.push_back(std::move(result));
      continue;
    }
    if (!device.span_start_ns.has_value() || !device.span_end_ns.has_value() ||
        *device.span_end_ns <= *device.span_start_ns) {
      // Unreachable via E2; guard against malformed caller results.
      result.status = AnalysisStatus::kInvalidAnalysisSpan;
      result.diagnostics.push_back(TimelineDiagnostic{
          "invalid_analysis_span: ok E2 device result carries no valid span",
          static_cast<std::int64_t>(device.device_id)});
      run_invalid = true;
      run.devices.push_back(std::move(result));
      continue;
    }
    result.span_start_ns = device.span_start_ns;
    result.span_end_ns = device.span_end_ns;
    result.observed_universe_scan_complete =
        scan_incomplete_devices.count(device.device_id) == 0;
    const auto events_it = events_by_device.find(device.device_id);
    if (events_it != events_by_device.end()) {
      // Group by resolved stream id; streams are processed in ascending id
      // order for deterministic output.
      std::map<std::uint64_t, std::vector<StreamEvent>> by_stream;
      for (StreamEvent& stream_event : events_it->second) {
        by_stream[stream_event.stream_id].push_back(std::move(stream_event));
      }
      for (auto& item : by_stream) {
        StreamStateTimeline timeline;
        timeline.device_id = device.device_id;
        timeline.stream_id = item.first;
        timeline.span_start_ns = *device.span_start_ns;
        timeline.span_end_ns = *device.span_end_ns;
        if (build_stream_timeline(*device.span_start_ns, *device.span_end_ns,
                                  &item.second, &timeline)) {
          result.timelines.push_back(std::move(timeline));
        }
      }
    }
    result.stream_universe_size = result.timelines.size();
    run.devices.push_back(std::move(result));
  }

  // Devices that E2 never visited (their only events were damaged) are not
  // part of the E2 result; surface their damage at run level instead of
  // dropping it. Remaining entries are exactly those devices.
  for (auto& item : diagnostics_by_device) {
    for (TimelineDiagnostic& diagnostic : item.second) {
      run_diagnostics.push_back(std::move(diagnostic));
    }
  }
  run.diagnostics = std::move(run_diagnostics);
  if (run_invalid) {
    run.status = AnalysisStatus::kInvalidInput;
  }

  // Run-level aggregates: sum of the per-device universes; completeness is
  // the AND of every device's flag, additionally voided by damage that
  // cannot be attributed to a device (it still undermines absence claims).
  std::size_t universe_total = 0;
  bool all_devices_complete = true;
  for (const StreamStateDeviceResult& device : run.devices) {
    universe_total += device.stream_universe_size;
    if (!device.observed_universe_scan_complete) {
      all_devices_complete = false;
    }
  }
  run.stream_universe_size = universe_total;
  run.observed_universe_scan_complete =
      all_devices_complete && run.diagnostics.empty();
  return run;
}

}  // namespace traceloom
