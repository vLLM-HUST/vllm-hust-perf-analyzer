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

struct DeviceIntervalRow {
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  DeviceIntervalKind kind = DeviceIntervalKind::kVisibleProductiveIdle;
  // Lineage of the canonical sources (TaskRow / CommunicationOpRow source
  // refs) for productive intervals; empty for gaps.
  std::vector<SourceRefId> source_refs;
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
  // Communication canonicalization notes (ambiguity, no-connection-id
  // fallback), not errors.
  std::vector<TimelineDiagnostic> diagnostics;
  std::string semantic_rules_version;
  std::string semantic_rules_sha256;
};

std::vector<DeviceTimelineResult> build_productive_timelines(
    const NativeIr& ir,
    const SemanticTaskClassificationResult& classification,
    const ProductiveTimelineOptions& options = {});

}  // namespace traceloom
