#include "traceloom/ir/trace_event_table.h"

#include <stdexcept>

namespace traceloom {

TraceEventId TraceEventTable::append(SourceRefId source_ref_id,
                                     std::uint64_t source_row_id,
                                     std::uint32_t device_id,
                                     std::uint32_t stream_id,
                                     std::int64_t start_ns,
                                     std::int64_t end_ns,
                                     SymbolId raw_name_symbol_id) {
  const auto id = checked_next_id<TraceEventId>(rows_.size());
  rows_.push_back(TraceEventRow{id, source_ref_id, source_row_id, device_id,
                                stream_id, start_ns, end_ns,
                                raw_name_symbol_id});
  return id;
}

const TraceEventRow& TraceEventTable::row(TraceEventId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("TraceEventId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
