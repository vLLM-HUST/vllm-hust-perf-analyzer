#include "traceloom/ir/capture_slot_table.h"

#include <stdexcept>

namespace traceloom {

CaptureSlotId CaptureSlotTable::append(GraphTemplateId graph_template_id,
                                       SourceRefId source_ref_id,
                                       std::uint32_t slot_order,
                                       RoleId slot_role_id,
                                       SymbolId slot_symbol_id,
                                       TraceEventId first_event_id,
                                       TraceEventId last_event_id) {
  const auto id = checked_next_id<CaptureSlotId>(rows_.size());
  rows_.push_back(CaptureSlotRow{id, graph_template_id, source_ref_id,
                                 slot_order, slot_role_id, slot_symbol_id,
                                 first_event_id, last_event_id});
  return id;
}

const CaptureSlotRow& CaptureSlotTable::row(CaptureSlotId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("CaptureSlotId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
