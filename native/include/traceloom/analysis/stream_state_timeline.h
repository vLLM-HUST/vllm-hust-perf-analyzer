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
// kInvalidAnalysisSpan verbatim from E2. Zero-duration non-interval roles
// (wait, capture/control, record, runtime control) are profiler point markers:
// they emit an informational device diagnostic but no interval and do not
// establish stream-universe membership. Other informational diagnostics
// (event_clipped_to_span, exact_duplicate_event, coincident_distinct_events)
// keep the status ok. Events carrying the adapter's unassigned-stream
// sentinel also keep the status ok, emit no fabricated timeline, and void
// observed-universe completeness because they could not be scanned per stream.
// Point-only and unassigned-stream semantics apply independently: a sentinel
// point marker emits no interval or universe membership but still voids
// completeness.

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
  // unknown_stream_identity) degrade the run to kInvalidInput. This vector can
  // also contain zero_duration_point_event_ignored, which is informational.
  std::vector<TimelineDiagnostic> diagnostics;
  // Observed stream universe of this device: timelines emitted on kOk
  // devices; zero on non-ok devices (no span, no universe).
  std::size_t stream_universe_size = 0;
  // False when an observed event on this device could not be placed on a
  // timeline (damaged interval, unresolvable or unassigned stream): such
  // events must not support absence claims downstream (no_observed_device_work
  // requires this flag, contract section 5). True vacuously on non-ok devices
  // (nothing observed, nothing claimable).
  bool observed_universe_scan_complete = true;
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
  // Aggregate over devices: sum of the per-device universe sizes.
  std::size_t stream_universe_size = 0;
  // Aggregate over devices: true only when every device reported scan
  // completeness AND no device-unattributable damage was seen.
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
