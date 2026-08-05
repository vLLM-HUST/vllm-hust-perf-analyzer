#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace traceloom {

template <typename Tag>
class Id {
 public:
  using value_type = std::uint32_t;

  constexpr Id() noexcept = default;
  explicit constexpr Id(value_type value) noexcept : value_(value) {}

  static constexpr Id invalid() noexcept { return Id(kInvalidValue); }

  constexpr value_type value() const noexcept { return value_; }
  constexpr bool valid() const noexcept { return value_ != kInvalidValue; }
  explicit constexpr operator bool() const noexcept { return valid(); }

  friend constexpr bool operator==(Id lhs, Id rhs) noexcept {
    return lhs.value_ == rhs.value_;
  }

  friend constexpr bool operator!=(Id lhs, Id rhs) noexcept {
    return !(lhs == rhs);
  }

  friend constexpr bool operator<(Id lhs, Id rhs) noexcept {
    return lhs.value_ < rhs.value_;
  }

 private:
  static constexpr value_type kInvalidValue =
      std::numeric_limits<value_type>::max();

  value_type value_ = kInvalidValue;
};

template <typename IdType>
constexpr IdType make_id(typename IdType::value_type value) noexcept {
  static_assert(std::is_class<IdType>::value, "IdType must be a typed id");
  return IdType(value);
}

template <typename IdType, typename SizeType>
IdType checked_next_id(SizeType size) {
  static_assert(std::is_class<IdType>::value, "IdType must be a typed id");
  using ValueType = typename IdType::value_type;
  if (size >= std::numeric_limits<ValueType>::max()) {
    throw std::overflow_error("typed id space exhausted");
  }
  return IdType(static_cast<ValueType>(size));
}

struct SourceRefIdTag {};
struct TraceEventIdTag {};
struct StreamIdTag {};
struct TaskIdTag {};
struct CommunicationOpIdTag {};
struct AnchorIdTag {};
struct TokenIdTag {};
struct ProtectedIntervalIdTag {};
struct GraphTemplateIdTag {};
struct CaptureSlotIdTag {};
struct GraphSlotTemplateIdTag {};
struct CapturedGraphInstanceIdTag {};
struct CapturedGraphStreamIdTag {};
struct GraphLaunchOccurrenceIdTag {};
struct GraphLaunchActivityIdTag {};
struct GraphLaunchActivityMemberIdTag {};
struct ReplayBodyTemplateIdTag {};
struct GraphLaunchBodyIdTag {};
struct GraphLaunchBodyMemberIdTag {};
struct ReplayCompositionCandidateIdTag {};
struct ReplayCompositionSlotIdTag {};
struct ReplayCompositionRegionIdTag {};
struct ReplayCompositionRegionMemberIdTag {};
struct ReplayUnitIdTag {};
struct ReplayUnitLaunchMemberIdTag {};
struct StringIdTag {};
struct SymbolIdTag {};
struct RoleIdTag {};
struct PatternIdTag {};
struct OccurrenceIdTag {};
struct LoopNodeIdTag {};
struct PartitionIdTag {};
struct GrammarNodeIdTag {};
struct GrammarChunkIdTag {};
struct MacroDefIdTag {};
struct ReportNodeDefIdTag {};
struct ReportNodeOccurrenceIdTag {};
struct ReportCostLeafIdTag {};

using SourceRefId = Id<SourceRefIdTag>;
using TraceEventId = Id<TraceEventIdTag>;
using StreamId = Id<StreamIdTag>;
using TaskId = Id<TaskIdTag>;
using CommunicationOpId = Id<CommunicationOpIdTag>;
using AnchorId = Id<AnchorIdTag>;
using TokenId = Id<TokenIdTag>;
using ProtectedIntervalId = Id<ProtectedIntervalIdTag>;
using GraphTemplateId = Id<GraphTemplateIdTag>;
using CaptureSlotId = Id<CaptureSlotIdTag>;
using GraphSlotTemplateId = Id<GraphSlotTemplateIdTag>;
using CapturedGraphInstanceId = Id<CapturedGraphInstanceIdTag>;
using CapturedGraphStreamId = Id<CapturedGraphStreamIdTag>;
using GraphLaunchOccurrenceId = Id<GraphLaunchOccurrenceIdTag>;
using GraphLaunchActivityId = Id<GraphLaunchActivityIdTag>;
using GraphLaunchActivityMemberId = Id<GraphLaunchActivityMemberIdTag>;
using ReplayBodyTemplateId = Id<ReplayBodyTemplateIdTag>;
using GraphLaunchBodyId = Id<GraphLaunchBodyIdTag>;
using GraphLaunchBodyMemberId = Id<GraphLaunchBodyMemberIdTag>;
using ReplayCompositionCandidateId = Id<ReplayCompositionCandidateIdTag>;
using ReplayCompositionSlotId = Id<ReplayCompositionSlotIdTag>;
using ReplayCompositionRegionId = Id<ReplayCompositionRegionIdTag>;
using ReplayCompositionRegionMemberId =
    Id<ReplayCompositionRegionMemberIdTag>;
using ReplayUnitId = Id<ReplayUnitIdTag>;
using ReplayUnitLaunchMemberId = Id<ReplayUnitLaunchMemberIdTag>;
using StringId = Id<StringIdTag>;
using SymbolId = Id<SymbolIdTag>;
using RoleId = Id<RoleIdTag>;
using PatternId = Id<PatternIdTag>;
using OccurrenceId = Id<OccurrenceIdTag>;
using LoopNodeId = Id<LoopNodeIdTag>;
using PartitionId = Id<PartitionIdTag>;
using GrammarNodeId = Id<GrammarNodeIdTag>;
using GrammarChunkId = Id<GrammarChunkIdTag>;
using MacroDefId = Id<MacroDefIdTag>;
using ReportNodeDefId = Id<ReportNodeDefIdTag>;
using ReportNodeOccurrenceId = Id<ReportNodeOccurrenceIdTag>;
using ReportCostLeafId = Id<ReportCostLeafIdTag>;

}  // namespace traceloom
