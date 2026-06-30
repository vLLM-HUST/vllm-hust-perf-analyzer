#include "traceloom/ir/anchor_table.h"

#include <stdexcept>

namespace traceloom {

AnchorId AnchorTable::append(SourceRefId source_ref_id,
                             TraceEventId trace_event_id,
                             ReplayUnitId replay_unit_id,
                             AnchorKind kind,
                             SymbolId symbol_id,
                             std::uint32_t device_id,
                             std::uint32_t stream_id,
                             std::int64_t start_ns,
                             std::int64_t end_ns) {
  const auto id = checked_next_id<AnchorId>(rows_.size());
  rows_.push_back(AnchorRow{id, source_ref_id, trace_event_id, replay_unit_id,
                            kind, symbol_id, device_id, stream_id, start_ns,
                            end_ns});
  return id;
}

const AnchorRow& AnchorTable::row(AnchorId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("AnchorId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
