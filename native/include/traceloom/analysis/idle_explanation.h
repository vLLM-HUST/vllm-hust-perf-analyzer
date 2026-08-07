#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "traceloom/analysis/host_correlation.h"
#include "traceloom/analysis/productive_timeline.h"
#include "traceloom/analysis/stream_state_timeline.h"

namespace traceloom {

// E4: conservative explanations for visible productive idle. Device evidence
// retains priority; optional host evidence has already passed calibrated
// robust-window gating and is expressed in the device clock domain.

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

enum class IdleEvidenceLevel {
  kDirect,
  kCorrelated,
  kNone,
};

std::string_view idle_evidence_level_name(IdleEvidenceLevel level);

enum class IdleEvidenceRelation {
  kDeviceEventCoverage,
  kCompleteAbsenceObservation,
  kExactConnectionId,
  kTemporalOverlap,
  kNone,
};

std::string_view idle_evidence_relation_name(IdleEvidenceRelation relation);

enum class CollectionStatus {
  kComplete,
  kIncomplete,
  kUnknown,
  kInvalid,
};

std::string_view collection_status_name(CollectionStatus status);

struct IdleExplanationSourceLink {
  std::uint64_t stream_id = 0;
  StreamState state = StreamState::kUnknown;
  StreamStateSourceLink source;

  friend bool operator==(const IdleExplanationSourceLink& lhs,
                         const IdleExplanationSourceLink& rhs) {
    return lhs.stream_id == rhs.stream_id && lhs.state == rhs.state &&
           lhs.source == rhs.source;
  }
};

struct IdleExplanationRow {
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  IdleExplanationCategory category =
      IdleExplanationCategory::kUnattributedVisibleIdle;
  IdleEvidenceLevel evidence_level = IdleEvidenceLevel::kNone;
  IdleEvidenceRelation evidence_relation = IdleEvidenceRelation::kNone;
  // Device-only evidence never needs cross-clock alignment.
  std::string alignment_status = "not_required";
  std::string reason;
  // Sources supporting the selected direct category. For unattributed slices,
  // unknown/ambiguous visible sources are retained as diagnostic lineage even
  // though they do not constitute evidence for an explanation claim.
  std::vector<IdleExplanationSourceLink> source_links;
  // Host links are kept separate from stream-state lineage because their
  // source interval is in the host domain and their overlap extent is the
  // mapped robust window in the device domain.
  std::vector<HostEvidenceSourceLink> host_source_links;
};

struct IdleExplanationDeviceResult {
  std::uint32_t device_id = 0;
  AnalysisStatus status = AnalysisStatus::kOk;
  CollectionStatus collection_status = CollectionStatus::kUnknown;
  std::vector<IdleExplanationRow> explanations;
  std::vector<TimelineDiagnostic> diagnostics;
};

struct IdleExplanationRunResult {
  AnalysisStatus status = AnalysisStatus::kOk;
  CollectionStatus collection_status = CollectionStatus::kUnknown;
  std::vector<IdleExplanationDeviceResult> devices;
  std::string attribution_rule_version = "device_projection_v1";
};

struct IdleExplanationOptions {
  // Must be externally attested. Trace content alone cannot upgrade this to
  // complete. kUnknown is therefore the safe default for real captures.
  CollectionStatus collection_status = CollectionStatus::kUnknown;
};

// productive and streams must be outputs of E2/E3 for the same IR and span.
// Structural disagreement, or productive E3 coverage inside an E2 visible
// gap, is a caller error and throws std::invalid_argument.
IdleExplanationRunResult build_idle_explanations(
    const ProductiveTimelineRunResult& productive,
    const StreamStateRunResult& streams,
    const IdleExplanationOptions& options = {},
    const HostCorrelationRunResult* host_correlation = nullptr);

}  // namespace traceloom
