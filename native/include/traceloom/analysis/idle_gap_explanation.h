#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "traceloom/analysis/productive_timeline.h"
#include "traceloom/analysis/stream_state_timeline.h"
#include "traceloom/core/ids.h"

namespace traceloom {

// E4: mutually exclusive explanation slices for E2 visible gaps. The
// category order is contract semantics and is intentionally not configurable.
enum class IdleExplanationCategory {
  kBlockedByVisibleWait,
  kCaptureControlPresent,
  kRuntimeControlPresent,
  kQueuedVisibleTaskDelay,
  kHostSyncApiPresent,
  kNoObservedDeviceWork,
  kUnattributedVisibleIdle,
};

std::string_view idle_explanation_category_name(
    IdleExplanationCategory category);

enum class EvidenceLevel {
  kDirect,
  kCorrelated,
  kInferred,
  kNone,
};

std::string_view evidence_level_name(EvidenceLevel level);

enum class EvidenceRelation {
  kDeviceEventCoverage,
  kCompleteAbsenceObservation,
  kExactConnectionId,
  kTemporalOverlap,
  kPatternContext,
  kNone,
};

std::string_view evidence_relation_name(EvidenceRelation relation);

enum class AlignmentStatus {
  kNotRequired,
  kCalibrated,
  kSyntheticOnly,
  kUncalibrated,
  kInvalid,
};

std::string_view alignment_status_name(AlignmentStatus status);

enum class CollectionStatus {
  kComplete,
  kIncomplete,
  kUnknown,
  kInvalid,
};

std::string_view collection_status_name(CollectionStatus status);

// Exact or external lineage attached to one explanation slice. Task and
// communication-op links are copied from E3. Host/link records are supplied
// by the later host-evidence stage through source_key.
struct IdleExplanationSourceLink {
  enum class Kind {
    kTask,
    kCommunicationOp,
    kHostApi,
    kTaskApiLink,
  };

  Kind kind = Kind::kTask;
  TraceEventId trace_event_id;
  TaskId task_id;
  CommunicationOpId communication_op_id;
  SourceRefId source_ref_id;
  std::optional<std::string> matched_rule_id;
  // Required for kHostApi/kTaskApiLink; empty for native device links.
  std::string source_key;

  friend bool operator==(const IdleExplanationSourceLink& lhs,
                         const IdleExplanationSourceLink& rhs) {
    return lhs.kind == rhs.kind &&
           lhs.trace_event_id == rhs.trace_event_id &&
           lhs.task_id == rhs.task_id &&
           lhs.communication_op_id == rhs.communication_op_id &&
           lhs.source_ref_id == rhs.source_ref_id &&
           lhs.matched_rule_id == rhs.matched_rule_id &&
           lhs.source_key == rhs.source_key;
  }
};

// Proof produced by the host-evidence validation stage. E4 does not refit the
// clock model or repeat connection resolution, but defensively verifies that
// proof kind, category, alignment, and source shape agree before emitting an
// official correlated explanation.
enum class CorrelatedEvidenceProof {
  kInvalid,
  kUniqueExactConnectionRobustDelay,
  kRobustTemporalOverlap,
};

// A device-clock interval that has ALREADY passed the contract's upstream
// robustness gates. The host-evidence validator owns construction of the
// proof; this public handoff remains defensive against category/source-shape
// mismatches. Candidate-only/uncalibrated observations must never be passed.
struct ValidatedCorrelatedEvidenceInterval {
  std::uint32_t device_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  IdleExplanationCategory category =
      IdleExplanationCategory::kHostSyncApiPresent;
  CorrelatedEvidenceProof proof = CorrelatedEvidenceProof::kInvalid;
  AlignmentStatus alignment_status = AlignmentStatus::kInvalid;
  std::vector<IdleExplanationSourceLink> source_links;
};

// Collection completeness is an external attestation, never inferred from
// trace emptiness. All fields must pass, together with E3's per-device scan
// completeness and the absence of device-unattributable run diagnostics,
// before no_observed_device_work can be emitted.
struct CollectionCompletenessAttestation {
  CollectionStatus status = CollectionStatus::kUnknown;
  bool all_discovered_device_shards_imported = false;
  bool all_required_task_tables_readable = false;
  bool no_dropped_events_or_truncated_capture = false;
};

struct IdleGapExplanationOptions {
  CollectionCompletenessAttestation collection;
  // Run-level mapping quality. Direct device evidence is emitted with
  // not_required; correlated evidence must match this status. The residual
  // carries this value to expose why host evidence may be unavailable.
  AlignmentStatus alignment_status = AlignmentStatus::kUncalibrated;
  std::vector<ValidatedCorrelatedEvidenceInterval> correlated_evidence;
};

struct IdleExplanationRow {
  // Index in DeviceTimelineResult::intervals, not merely a gap ordinal.
  std::size_t gap_interval_index = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  IdleExplanationCategory category =
      IdleExplanationCategory::kUnattributedVisibleIdle;
  EvidenceLevel evidence_level = EvidenceLevel::kNone;
  EvidenceRelation evidence_relation = EvidenceRelation::kNone;
  AlignmentStatus alignment_status = AlignmentStatus::kNotRequired;
  CollectionStatus collection_status = CollectionStatus::kUnknown;
  std::string reason;
  // Unknown-role sources may be retained on an unattributed row for
  // diagnostics even though its evidence relation remains none.
  std::vector<IdleExplanationSourceLink> source_links;
};

struct IdleExplanationDeviceResult {
  std::uint32_t device_id = 0;
  AnalysisStatus status = AnalysisStatus::kOk;
  std::optional<std::int64_t> span_start_ns;
  std::optional<std::int64_t> span_end_ns;
  std::vector<IdleExplanationRow> rows;
};

struct IdleExplanationRunResult {
  AnalysisStatus status = AnalysisStatus::kOk;
  std::vector<IdleExplanationDeviceResult> devices;
  std::string attribution_rule_version;
};

// productive and streams must be E2/E3 results from the same logical run.
// Throws std::invalid_argument for mismatched stage outputs, malformed
// partitions, or correlated inputs outside the frozen category/evidence
// matrix.
IdleExplanationRunResult build_idle_gap_explanations(
    const ProductiveTimelineRunResult& productive,
    const StreamStateRunResult& streams,
    const IdleGapExplanationOptions& options = {});

}  // namespace traceloom
