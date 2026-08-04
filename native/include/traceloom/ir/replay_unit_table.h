#pragma once

#include <cstddef>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

struct ReplayUnitRow {
  ReplayUnitId id;
  GraphTemplateId graph_template_id;
  SourceRefId source_ref_id;
  AnchorId first_anchor_id;
  AnchorId last_anchor_id;
  TraceEventId launch_trace_event_id;
  ReplayCompositionRegionId replay_composition_region_id;
};

class ReplayUnitTable {
 public:
  ReplayUnitId append(GraphTemplateId graph_template_id,
                      SourceRefId source_ref_id,
                      AnchorId first_anchor_id,
                      AnchorId last_anchor_id,
                      TraceEventId launch_trace_event_id,
                      ReplayCompositionRegionId replay_composition_region_id =
                          ReplayCompositionRegionId::invalid());

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const ReplayUnitRow& row(ReplayUnitId id) const;
  const std::vector<ReplayUnitRow>& rows() const noexcept { return rows_; }
  void set_anchor_bounds(ReplayUnitId id,
                         AnchorId first_anchor_id,
                         AnchorId last_anchor_id);

 private:
  std::vector<ReplayUnitRow> rows_;
};

struct ReplayUnitLaunchMemberRow {
  ReplayUnitLaunchMemberId id;
  ReplayUnitId replay_unit_id;
  std::uint32_t member_order = 0;
  GraphLaunchOccurrenceId graph_launch_occurrence_id;
  ReplayCompositionSlotId replay_composition_slot_id;
};

class ReplayUnitLaunchMemberTable {
 public:
  ReplayUnitLaunchMemberId append(
      ReplayUnitId replay_unit_id,
      std::uint32_t member_order,
      GraphLaunchOccurrenceId graph_launch_occurrence_id,
      ReplayCompositionSlotId replay_composition_slot_id);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const ReplayUnitLaunchMemberRow& row(ReplayUnitLaunchMemberId id) const;
  const std::vector<ReplayUnitLaunchMemberRow>& rows() const noexcept {
    return rows_;
  }

 private:
  std::vector<ReplayUnitLaunchMemberRow> rows_;
};

}  // namespace traceloom
