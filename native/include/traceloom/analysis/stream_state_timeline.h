#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "traceloom/analysis/productive_timeline.h"
#include "traceloom/analysis/semantic_task_classifier.h"
#include "traceloom/core/ids.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom {

// E3: per-stream observable state timelines (idle evidence contract
// sections 2, 3.3, 8 step 5, 9). For each logical (run_id, device_id) on a
// shard-merged NativeIr, builds one timeline per observed stream: a
// continuous, mutually exclusive partition of the device's analysis span
// into StreamState intervals. Same-stream overlapping canonical events are
// split with the ambiguous_overlap state (contract section 3.3); a stream is
// "observed" when at least one canonical event intersects the span.
//
// Inputs are the E1 classification and the E2 run result built from the SAME
// IR: the analysis span, the per-device statuses, and the communication
// canonicalization (absorbed_task_links) are consumed from E2 and never
// re-derived here. Gap explanation is out of scope (E4).
//
// Status mirrors E2: run status is kOk, kEmptyInput (no tasks and no
// communication ops), or kInvalidInput (damaged rows skipped with
// diagnostics); per-device statuses express kNoProductiveSpan /
// kInvalidAnalysisSpan verbatim from E2. Informational diagnostics
// (event_clipped_to_span, exact_duplicate_event, coincident_distinct_events)
// live on the affected timeline and keep the status ok.

enum class StreamState {
  kRunningCompute,
  kRunningComm,
  kRunningDataMove,
  kRunningWait,
  kRunningCaptureControl,
  kRunningRecord,
  kRunningRuntimeControl,
  kUnknown,
  kEmptyObserved,
  kAmbiguousOverlap,
};

// Contract state names: "running_compute", "running_comm", ...,
// "empty_observed", "ambiguous_overlap".
std::string_view stream_state_name(StreamState state);

// Exact source lineage of one canonical event backing a state interval.
// SourceRefId plus trace_event_id resolve to (source_kind, source_path,
// table_name, row_id); the precise original row is TraceEventRow.source_row_id.
struct StreamStateSourceLink {
  enum class Kind {
    kTask,
    kCommunicationOp,
  };

  Kind kind = Kind::kTask;
  TraceEventId trace_event_id;
  TaskId task_id;
  CommunicationOpId communication_op_id;
  SourceRefId source_ref_id;
  // Classification rule id for task links; absent for communication ops.
  std::optional<std::string> matched_rule_id;

  friend bool operator==(const StreamStateSourceLink& lhs,
                         const StreamStateSourceLink& rhs) {
    return lhs.kind == rhs.kind &&
           lhs.trace_event_id == rhs.trace_event_id &&
           lhs.task_id == rhs.task_id &&
           lhs.communication_op_id == rhs.communication_op_id &&
           lhs.source_ref_id == rhs.source_ref_id &&
           lhs.matched_rule_id == rhs.matched_rule_id;
  }
};

struct StreamStateInterval {
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  StreamState state = StreamState::kUnknown;
  // Sources of every canonical event covering the interval; empty only for
  // kEmptyObserved, at least two for kAmbiguousOverlap.
  std::vector<StreamStateSourceLink> source_links;
};

struct StreamStateTimeline {
  std::uint32_t device_id = 0;
  std::uint64_t stream_id = 0;  // raw stream id as recorded in StreamTable
  std::int64_t span_start_ns = 0;
  std::int64_t span_end_ns = 0;
  // Mutually exclusive partition of the span: sorted, adjacent, positive
  // length, duration sum == span length.
  std::vector<StreamStateInterval> intervals;
  // Informational notes (event_clipped_to_span, exact_duplicate_event,
  // coincident_distinct_events); do not degrade the status.
  std::vector<TimelineDiagnostic> diagnostics;
};

struct StreamStateDeviceResult {
  std::uint32_t device_id = 0;
  // Mirrors the E2 device status verbatim; timelines exist only for kOk.
  AnalysisStatus status = AnalysisStatus::kOk;
  std::optional<std::int64_t> span_start_ns;
  std::optional<std::int64_t> span_end_ns;
  std::vector<StreamStateTimeline> timelines;
  // Input-damage notes (invalid_event_duration, invalid_trace_event_reference,
  // unknown_stream_identity); degrade the run to kInvalidInput.
  std::vector<TimelineDiagnostic> diagnostics;
};

// Run-level result; carries the analysis status even when no stream has any
// interval row (contract: analysis status lives in run-level metadata).
struct StreamStateRunResult {
  AnalysisStatus status = AnalysisStatus::kOk;
  std::vector<StreamStateDeviceResult> devices;
  // Damage that cannot be attributed to a device in the E2 result (e.g. an
  // out-of-range trace_event reference, or a device whose only events were
  // damaged and is therefore absent from E2).
  std::vector<TimelineDiagnostic> diagnostics;
  // Number of emitted timelines: streams with at least one canonical event
  // intersecting the analysis span, after validation and dedup.
  std::size_t stream_universe_size = 0;
  // E3 scans every observed stream it emits; collection completeness
  // attestation belongs to later stages (contract section 5).
  bool observed_universe_scan_complete = true;
};

// classification rows must be aligned with TaskTable order (validated;
// throws std::invalid_argument on mismatch); productive must be the E2
// result built from the same IR and classification.
StreamStateRunResult build_stream_state_timelines(
    const NativeIr& ir,
    const SemanticTaskClassificationResult& classification,
    const ProductiveTimelineRunResult& productive);

}  // namespace traceloom
