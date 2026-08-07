#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "traceloom/analysis/clock_alignment.h"
#include "traceloom/analysis/host_api_rules.h"
#include "traceloom/analysis/productive_timeline.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom {

enum class TaskApiLinkStatus {
  kUnique,
  kOneToMany,
  kAmbiguous,
  kUnresolved,
};

std::string_view task_api_link_status_name(TaskApiLinkStatus status);

struct TaskApiLinkRow {
  HostApiEventId host_api_event_id;
  TaskId task_id;
  std::int64_t raw_connection_id = -1;
  TaskApiLinkStatus link_status = TaskApiLinkStatus::kUnresolved;
  bool has_device_id = false;
  std::uint32_t device_id = 0;
};

enum class HostEvidenceCategory {
  kQueuedVisibleTaskDelay,
  kHostSyncApiPresent,
};

std::string_view host_evidence_category_name(HostEvidenceCategory category);

enum class HostCandidateStatus {
  kPossibleOnly,
  kNonRobustDelay,
};

std::string_view host_candidate_status_name(HostCandidateStatus status);

struct HostEvidenceSourceLink {
  HostApiEventId host_api_event_id;
  TaskId task_id;
  // Official queued-delay evidence uses exact_connection_id; official host
  // sync and possible-overlap candidates use temporal_overlap.
  std::string relation;
  std::int64_t overlap_start_ns = 0;
  std::int64_t overlap_end_ns = 0;

  friend bool operator==(const HostEvidenceSourceLink& lhs,
                         const HostEvidenceSourceLink& rhs) {
    return lhs.host_api_event_id == rhs.host_api_event_id &&
           lhs.task_id == rhs.task_id && lhs.relation == rhs.relation &&
           lhs.overlap_start_ns == rhs.overlap_start_ns &&
           lhs.overlap_end_ns == rhs.overlap_end_ns;
  }
};

struct HostEvidenceInterval {
  std::uint32_t device_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  HostEvidenceCategory category = HostEvidenceCategory::kHostSyncApiPresent;
  AlignmentStatus alignment_status = AlignmentStatus::kUncalibrated;
  std::string reason;
  std::vector<HostEvidenceSourceLink> source_links;
};

struct HostIdleCandidateRow {
  std::uint32_t device_id = 0;
  std::int64_t gap_start_ns = 0;
  std::int64_t gap_end_ns = 0;
  HostEvidenceCategory category = HostEvidenceCategory::kHostSyncApiPresent;
  HostCandidateStatus candidate_status = HostCandidateStatus::kPossibleOnly;
  std::string candidate_level = "correlated";
  std::string candidate_relation;
  AlignmentStatus alignment_status = AlignmentStatus::kUncalibrated;
  std::string reason;
  std::vector<HostEvidenceSourceLink> source_links;
};

struct HostCorrelationRunResult {
  struct HostApiClassificationRow {
    HostApiEventId host_api_event_id;
    bool matched = false;
    HostApiFamily family = HostApiFamily::kHostSync;
    std::string matched_pattern;
  };

  std::vector<HostApiClassificationRow> host_api_classification;
  std::vector<TaskApiLinkRow> task_api_links;
  std::vector<HostEvidenceInterval> evidence_intervals;
  std::vector<HostIdleCandidateRow> candidates;
  std::string host_api_rules_version = "not_loaded";
  std::string host_api_rules_sha256;
  std::string correlation_rule_version = "robust_host_correlation_v1";
};

HostCorrelationRunResult build_host_correlation(
    const NativeIr& ir,
    const ProductiveTimelineRunResult& productive,
    const ClockAlignmentRunResult& alignment,
    const HostApiRuleset& ruleset);

}  // namespace traceloom
