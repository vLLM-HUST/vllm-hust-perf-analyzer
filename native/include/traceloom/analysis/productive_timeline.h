#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "traceloom/analysis/semantic_task_classifier.h"
#include "traceloom/core/ids.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom {

// E2: canonical productive timeline and visible gaps (idle evidence
// contract sections 3, 5, 6.6). Builds, per logical (run_id, device_id) on
// a shard-merged NativeIr, the global productive union and the
// visible_productive_idle gaps within the analysis span. Gap explanation is
// out of scope (E3+).
//
// The caller MUST pass a NativeIr whose split profile shards have already
// been merged: spans and gaps are defined per logical run/device, and
// per-shard analysis would silently drop gaps at shard boundaries.

enum class AnalysisStatus {
  kOk,
  kNoProductiveSpan,
  kInvalidAnalysisSpan,
  kEmptyInput,
  kInvalidInput,
};

enum class DeviceIntervalKind {
  kProductiveActive,
  kVisibleProductiveIdle,
};

// Exact source lineage for a productive interval. SourceRefId alone only
// identifies the source table; the precise original row is
// TraceEventRow.source_row_id, reachable through trace_event_id.
struct ProductiveSourceLink {
  enum class Kind {
    kTask,
    kCommunicationOp,
  };

  Kind kind = Kind::kTask;
  TraceEventId trace_event_id;
  TaskId task_id;
  CommunicationOpId communication_op_id;
};

struct DeviceIntervalRow {
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  DeviceIntervalKind kind = DeviceIntervalKind::kVisibleProductiveIdle;
  // Exact source links of the canonical sources for productive intervals;
  // empty for gaps.
  std::vector<ProductiveSourceLink> source_links;
};

// E3 interface (contract 6.6): one entry per task absorbed into a canonical
// communication op during communication canonicalization. This is the only
// E2 -> E3 export; later stages must not re-derive communication dedup.
struct AbsorbedTaskLink {
  CommunicationOpId canonical_op_id;
  TaskId task_id;
};

struct ProductiveTimelineOptions {
  // Explicit analysis span. Both must be present for the span to apply;
  // end_ns <= start_ns yields kInvalidAnalysisSpan.
  std::optional<std::int64_t> explicit_span_start_ns;
  std::optional<std::int64_t> explicit_span_end_ns;
};

struct TimelineDiagnostic {
  std::string message;
  std::int64_t source_row_id = -1;
};

struct DeviceTimelineResult {
  std::uint32_t device_id = 0;
  AnalysisStatus status = AnalysisStatus::kOk;
  std::optional<std::int64_t> span_start_ns;
  std::optional<std::int64_t> span_end_ns;
  // Alternating, non-overlapping productive/gap intervals in time order;
  // productive union plus gap union covers the span exactly.
  std::vector<DeviceIntervalRow> intervals;
  // Communication canonicalization and input-quality notes, not errors.
  std::vector<TimelineDiagnostic> diagnostics;
  std::string semantic_rules_version;
  std::string semantic_rules_sha256;
  // E3: tasks absorbed into canonical communication ops on this device
  // (empty for non-ok devices and devices with no communication).
  std::vector<AbsorbedTaskLink> absorbed_task_links;
};

// Run-level result: carries the analysis status even when no device has any
// interval row (contract: analysis status lives in run-level metadata).
struct ProductiveTimelineRunResult {
  // kEmptyInput: no tasks and no communication ops at all.
  // kInvalidInput: damaged input rows were skipped (diagnostics on the
  //   affected devices) but analysis still ran on the valid remainder.
  // kOk otherwise; per-device statuses express no_productive_span and
  // invalid_analysis_span.
  AnalysisStatus status = AnalysisStatus::kOk;
  std::vector<DeviceTimelineResult> devices;
  std::string semantic_rules_version;
  std::string semantic_rules_sha256;
};

// classification rows must be aligned with TaskTable order; each row's
// task_id and trace_event_id are validated against its TaskRow.
ProductiveTimelineRunResult build_productive_timelines(
    const NativeIr& ir,
    const SemanticTaskClassificationResult& classification,
    const ProductiveTimelineOptions& options = {});

}  // namespace traceloom
