#include "traceloom/ir/replay_unit_table.h"

#include <stdexcept>

namespace traceloom {

ReplayUnitId ReplayUnitTable::append(GraphTemplateId graph_template_id,
                                     SourceRefId source_ref_id,
                                     AnchorId first_anchor_id,
                                     AnchorId last_anchor_id,
                                     TraceEventId launch_trace_event_id) {
  const auto id = checked_next_id<ReplayUnitId>(rows_.size());
  rows_.push_back(ReplayUnitRow{id, graph_template_id, source_ref_id,
                                first_anchor_id, last_anchor_id,
                                launch_trace_event_id});
  return id;
}

const ReplayUnitRow& ReplayUnitTable::row(ReplayUnitId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("ReplayUnitId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
