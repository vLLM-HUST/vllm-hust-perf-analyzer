#include "traceloom/ir/replay_unit_table.h"

#include <stdexcept>

namespace traceloom {

ReplayUnitId ReplayUnitTable::append(GraphTemplateId graph_template_id,
                                     SourceRefId source_ref_id,
                                     AnchorId first_anchor_id,
                                     AnchorId last_anchor_id,
                                     TraceEventId launch_trace_event_id,
                                     ReplayCompositionRegionId
                                         replay_composition_region_id) {
  const auto id = checked_next_id<ReplayUnitId>(rows_.size());
  rows_.push_back(ReplayUnitRow{id, graph_template_id, source_ref_id,
                                first_anchor_id, last_anchor_id,
                                launch_trace_event_id,
                                replay_composition_region_id});
  return id;
}

const ReplayUnitRow& ReplayUnitTable::row(ReplayUnitId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("ReplayUnitId is out of range");
  }
  return rows_[id.value()];
}

void ReplayUnitTable::set_anchor_bounds(ReplayUnitId id,
                                        AnchorId first_anchor_id,
                                        AnchorId last_anchor_id) {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("ReplayUnitId is out of range");
  }
  rows_[id.value()].first_anchor_id = first_anchor_id;
  rows_[id.value()].last_anchor_id = last_anchor_id;
}

ReplayUnitLaunchMemberId ReplayUnitLaunchMemberTable::append(
    ReplayUnitId replay_unit_id,
    std::uint32_t member_order,
    GraphLaunchOccurrenceId graph_launch_occurrence_id,
    ReplayCompositionSlotId replay_composition_slot_id) {
  const auto id = checked_next_id<ReplayUnitLaunchMemberId>(rows_.size());
  rows_.push_back(ReplayUnitLaunchMemberRow{
      id, replay_unit_id, member_order, graph_launch_occurrence_id,
      replay_composition_slot_id});
  return id;
}

const ReplayUnitLaunchMemberRow& ReplayUnitLaunchMemberTable::row(
    ReplayUnitLaunchMemberId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("ReplayUnitLaunchMemberId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
