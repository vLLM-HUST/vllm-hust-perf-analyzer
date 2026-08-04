#include "traceloom/ir/replay_composition_candidate_table.h"

#include <stdexcept>

namespace traceloom {

const char* replay_composition_identity_policy_name(
    ReplayCompositionIdentityPolicy policy) noexcept {
  switch (policy) {
    case ReplayCompositionIdentityPolicy::kCapturedGraphInstance:
      return "captured_graph_instance";
    case ReplayCompositionIdentityPolicy::kGraphConnection:
      return "graph_connection";
    case ReplayCompositionIdentityPolicy::kUnavailable:
      return "unavailable";
  }
  return "graph_connection";
}

const char* replay_composition_boundary_policy_name(
    ReplayCompositionBoundaryPolicy policy) noexcept {
  switch (policy) {
    case ReplayCompositionBoundaryPolicy::kExactPeriodicSuffix:
      return "exact_periodic_suffix";
    case ReplayCompositionBoundaryPolicy::kExactOneShotLeadingComposition:
      return "exact_one_shot_leading_composition";
    case ReplayCompositionBoundaryPolicy::kIncompleteLaunchEvidence:
      return "incomplete_launch_evidence";
  }
  return "exact_periodic_suffix";
}

const char* replay_composition_order_policy_name(
    ReplayCompositionOrderPolicy policy) noexcept {
  switch (policy) {
    case ReplayCompositionOrderPolicy::kDeviceExecutionOrder:
      return "device_execution_order";
    case ReplayCompositionOrderPolicy::kHostSubmissionOrder:
      return "host_submission_order";
  }
  return "device_execution_order";
}

const char* replay_composition_shape_policy_name(
    ReplayCompositionShapePolicy policy) noexcept {
  switch (policy) {
    case ReplayCompositionShapePolicy::kUnclassified:
      return "unclassified";
    case ReplayCompositionShapePolicy::kHeadRepeatedLayerTail:
      return "head_repeated_layer_tail";
  }
  return "unclassified";
}

const char* replay_composition_region_status_name(
    ReplayCompositionRegionStatus status) noexcept {
  switch (status) {
    case ReplayCompositionRegionStatus::kRecognizedCompletePattern:
      return "recognized_complete_pattern";
    case ReplayCompositionRegionStatus::kUnrecognizedLeadingContext:
      return "unrecognized_leading_context";
    case ReplayCompositionRegionStatus::kUnrecognizedIncompleteTail:
      return "unrecognized_incomplete_tail";
    case ReplayCompositionRegionStatus::kUnrecognizedMissingBodyEvidence:
      return "unrecognized_missing_body_evidence";
    case ReplayCompositionRegionStatus::kUnrecognizedBodyMismatch:
      return "unrecognized_body_mismatch";
    case ReplayCompositionRegionStatus::
        kUnrecognizedMissingCompletionEvidence:
      return "unrecognized_missing_completion_evidence";
    case ReplayCompositionRegionStatus::kUnrecognizedMissingBodyCapability:
      return "unrecognized_missing_body_capability";
  }
  return "unrecognized_incomplete_tail";
}

ReplayCompositionCandidateId ReplayCompositionCandidateTable::append(
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
    ReplayCompositionBoundaryPolicy boundary_policy) {
  const auto id = checked_next_id<ReplayCompositionCandidateId>(rows_.size());
  rows_.push_back(ReplayCompositionCandidateRow{
      id, source_ref_id, device_id, segment_first_launch_id,
      pattern_first_launch_id, segment_launch_count, leading_launch_count,
      pattern_length, full_repeat_count, trailing_launch_count,
      pattern_sequence_hash, identity_policy, order_policy, shape_policy,
      boundary_policy});
  return id;
}

const ReplayCompositionCandidateRow& ReplayCompositionCandidateTable::row(
    ReplayCompositionCandidateId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("ReplayCompositionCandidateId is out of range");
  }
  return rows_[id.value()];
}

ReplayCompositionSlotId ReplayCompositionSlotTable::append(
    ReplayCompositionCandidateId replay_composition_candidate_id,
    std::uint32_t slot_order,
    CapturedGraphInstanceId captured_graph_instance_id,
    GraphSlotTemplateId slot_template_id,
    ReplayBodyTemplateId replay_body_template_id,
    ReplayCompositionSlotRole role,
    std::int64_t raw_graph_connection_id) {
  const auto id = checked_next_id<ReplayCompositionSlotId>(rows_.size());
  rows_.push_back(ReplayCompositionSlotRow{
      id, replay_composition_candidate_id, slot_order,
      captured_graph_instance_id, slot_template_id, replay_body_template_id,
      role, raw_graph_connection_id});
  return id;
}

const ReplayCompositionSlotRow& ReplayCompositionSlotTable::row(
    ReplayCompositionSlotId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("ReplayCompositionSlotId is out of range");
  }
  return rows_[id.value()];
}

ReplayCompositionRegionId ReplayCompositionRegionTable::append(
    ReplayCompositionCandidateId replay_composition_candidate_id,
    std::uint32_t region_order,
    GraphLaunchOccurrenceId first_launch_id,
    GraphLaunchOccurrenceId last_launch_id,
    std::int64_t start_ns,
    std::int64_t end_ns,
    std::uint32_t observed_launch_count,
    std::uint32_t expected_launch_count,
    ReplayCompositionRegionStatus status) {
  const auto id = checked_next_id<ReplayCompositionRegionId>(rows_.size());
  rows_.push_back(ReplayCompositionRegionRow{
      id, replay_composition_candidate_id, region_order, first_launch_id,
      last_launch_id, start_ns, end_ns, observed_launch_count,
      expected_launch_count, status});
  return id;
}

const ReplayCompositionRegionRow& ReplayCompositionRegionTable::row(
    ReplayCompositionRegionId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("ReplayCompositionRegionId is out of range");
  }
  return rows_[id.value()];
}

ReplayCompositionRegionMemberId ReplayCompositionRegionMemberTable::append(
    ReplayCompositionRegionId replay_composition_region_id,
    std::uint32_t member_order,
    GraphLaunchOccurrenceId graph_launch_occurrence_id,
    std::int64_t expected_slot_order) {
  const auto id =
      checked_next_id<ReplayCompositionRegionMemberId>(rows_.size());
  rows_.push_back(ReplayCompositionRegionMemberRow{
      id, replay_composition_region_id, member_order,
      graph_launch_occurrence_id, expected_slot_order});
  return id;
}

const ReplayCompositionRegionMemberRow&
ReplayCompositionRegionMemberTable::row(
    ReplayCompositionRegionMemberId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range(
        "ReplayCompositionRegionMemberId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
