#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

enum class ReplayCompositionIdentityPolicy : std::uint8_t {
  kCapturedGraphInstance = 0,
  kGraphConnection = 1,
  kUnavailable = 2,
  kCudaGraphNodeSet = 3,
};

enum class ReplayCompositionBoundaryPolicy : std::uint8_t {
  kExactPeriodicSuffix = 0,
  kExactOneShotLeadingComposition = 1,
  kIncompleteLaunchEvidence = 2,
  kDirectObservedGraphLaunch = 3,
};

enum class ReplayCompositionOrderPolicy : std::uint8_t {
  kDeviceExecutionOrder = 0,
  kHostSubmissionOrder = 1,
};

enum class ReplayCompositionShapePolicy : std::uint8_t {
  kUnclassified = 0,
  kHeadRepeatedLayerTail = 1,
};

enum class ReplayCompositionSlotRole : std::uint8_t {
  kUnclassified = 0,
  kHead = 1,
  kLayer = 2,
  kTail = 3,
  kGraph = 4,
  kCudaGraph = 5,
  kGeneric = 6,
};

struct ReplayCompositionCandidateRow {
  ReplayCompositionCandidateId id;
  SourceRefId source_ref_id;
  std::uint32_t device_id = 0;
  GraphLaunchOccurrenceId segment_first_launch_id;
  GraphLaunchOccurrenceId pattern_first_launch_id;
  std::uint32_t segment_launch_count = 0;
  std::uint32_t leading_launch_count = 0;
  std::uint32_t pattern_length = 0;
  std::uint32_t full_repeat_count = 0;
  std::uint32_t trailing_launch_count = 0;
  std::uint64_t pattern_sequence_hash = 0;
  ReplayCompositionIdentityPolicy identity_policy =
      ReplayCompositionIdentityPolicy::kGraphConnection;
  ReplayCompositionOrderPolicy order_policy =
      ReplayCompositionOrderPolicy::kDeviceExecutionOrder;
  ReplayCompositionShapePolicy shape_policy =
      ReplayCompositionShapePolicy::kUnclassified;
  ReplayCompositionBoundaryPolicy boundary_policy =
      ReplayCompositionBoundaryPolicy::kExactPeriodicSuffix;
};

class ReplayCompositionCandidateTable {
 public:
  ReplayCompositionCandidateId append(
      SourceRefId source_ref_id,
      std::uint32_t device_id,
      GraphLaunchOccurrenceId segment_first_launch_id,
      GraphLaunchOccurrenceId pattern_first_launch_id,
      std::uint32_t segment_launch_count,
      std::uint32_t leading_launch_count,
      std::uint32_t pattern_length,
      std::uint32_t full_repeat_count,
      std::uint32_t trailing_launch_count,
      std::uint64_t pattern_sequence_hash,
      ReplayCompositionIdentityPolicy identity_policy,
      ReplayCompositionOrderPolicy order_policy,
      ReplayCompositionShapePolicy shape_policy,
      ReplayCompositionBoundaryPolicy boundary_policy);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const ReplayCompositionCandidateRow& row(
      ReplayCompositionCandidateId id) const;
  const std::vector<ReplayCompositionCandidateRow>& rows() const noexcept {
    return rows_;
  }

 private:
  std::vector<ReplayCompositionCandidateRow> rows_;
};

struct ReplayCompositionSlotRow {
  ReplayCompositionSlotId id;
  ReplayCompositionCandidateId replay_composition_candidate_id;
  std::uint32_t slot_order = 0;
  CapturedGraphInstanceId captured_graph_instance_id;
  GraphSlotTemplateId slot_template_id;
  ReplayBodyTemplateId replay_body_template_id;
  ReplayCompositionSlotRole role =
      ReplayCompositionSlotRole::kUnclassified;
  std::int64_t raw_graph_connection_id = -1;
};

class ReplayCompositionSlotTable {
 public:
  ReplayCompositionSlotId append(
      ReplayCompositionCandidateId replay_composition_candidate_id,
      std::uint32_t slot_order,
      CapturedGraphInstanceId captured_graph_instance_id,
      GraphSlotTemplateId slot_template_id,
      ReplayBodyTemplateId replay_body_template_id,
      ReplayCompositionSlotRole role,
      std::int64_t raw_graph_connection_id);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const ReplayCompositionSlotRow& row(ReplayCompositionSlotId id) const;
  const std::vector<ReplayCompositionSlotRow>& rows() const noexcept {
    return rows_;
  }

 private:
  std::vector<ReplayCompositionSlotRow> rows_;
};

enum class ReplayCompositionRegionStatus : std::uint8_t {
  kRecognizedCompletePattern = 0,
  kUnrecognizedLeadingContext = 1,
  kUnrecognizedIncompleteTail = 2,
  kUnrecognizedMissingBodyEvidence = 3,
  kUnrecognizedBodyMismatch = 4,
  kUnrecognizedMissingCompletionEvidence = 5,
  kUnrecognizedMissingBodyCapability = 6,
  kUnrecognizedAmbiguousLaunchEvidence = 7,
  kUnrecognizedInsufficientRepeatEvidence = 8,
};

const char* replay_composition_identity_policy_name(
    ReplayCompositionIdentityPolicy policy) noexcept;
const char* replay_composition_boundary_policy_name(
    ReplayCompositionBoundaryPolicy policy) noexcept;
const char* replay_composition_order_policy_name(
    ReplayCompositionOrderPolicy policy) noexcept;
const char* replay_composition_shape_policy_name(
    ReplayCompositionShapePolicy policy) noexcept;
bool replay_composition_candidate_has_exact_structure(
    const ReplayCompositionCandidateRow& candidate) noexcept;
const char* replay_composition_region_status_name(
    ReplayCompositionRegionStatus status) noexcept;

struct ReplayCompositionRegionRow {
  ReplayCompositionRegionId id;
  ReplayCompositionCandidateId replay_composition_candidate_id;
  std::uint32_t region_order = 0;
  GraphLaunchOccurrenceId first_launch_id;
  GraphLaunchOccurrenceId last_launch_id;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::uint32_t observed_launch_count = 0;
  std::uint32_t expected_launch_count = 0;
  ReplayCompositionRegionStatus status =
      ReplayCompositionRegionStatus::kRecognizedCompletePattern;
};

class ReplayCompositionRegionTable {
 public:
  ReplayCompositionRegionId append(
      ReplayCompositionCandidateId replay_composition_candidate_id,
      std::uint32_t region_order,
      GraphLaunchOccurrenceId first_launch_id,
      GraphLaunchOccurrenceId last_launch_id,
      std::int64_t start_ns,
      std::int64_t end_ns,
      std::uint32_t observed_launch_count,
      std::uint32_t expected_launch_count,
      ReplayCompositionRegionStatus status);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const ReplayCompositionRegionRow& row(
      ReplayCompositionRegionId id) const;
  const std::vector<ReplayCompositionRegionRow>& rows() const noexcept {
    return rows_;
  }

 private:
  std::vector<ReplayCompositionRegionRow> rows_;
};

struct ReplayCompositionRegionMemberRow {
  ReplayCompositionRegionMemberId id;
  ReplayCompositionRegionId replay_composition_region_id;
  std::uint32_t member_order = 0;
  GraphLaunchOccurrenceId graph_launch_occurrence_id;
  std::int64_t expected_slot_order = -1;
};

class ReplayCompositionRegionMemberTable {
 public:
  ReplayCompositionRegionMemberId append(
      ReplayCompositionRegionId replay_composition_region_id,
      std::uint32_t member_order,
      GraphLaunchOccurrenceId graph_launch_occurrence_id,
      std::int64_t expected_slot_order);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const ReplayCompositionRegionMemberRow& row(
      ReplayCompositionRegionMemberId id) const;
  const std::vector<ReplayCompositionRegionMemberRow>& rows() const noexcept {
    return rows_;
  }

 private:
  std::vector<ReplayCompositionRegionMemberRow> rows_;
};

}  // namespace traceloom
