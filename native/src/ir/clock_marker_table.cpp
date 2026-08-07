#include "traceloom/ir/clock_marker_table.h"

#include <stdexcept>

namespace traceloom {

ClockMarkerId ClockMarkerTable::append(
    SourceRefId source_ref_id,
    std::uint64_t source_row_id,
    SymbolId marker_symbol_id,
    std::int64_t host_before_ns,
    std::int64_t host_after_ns,
    std::int64_t device_timestamp_ns,
    std::uint64_t host_pid,
    std::uint64_t host_tid,
    std::uint32_t device_id,
    bool has_stream_id,
    std::uint64_t stream_id,
    bool has_connection_id,
    std::int64_t raw_connection_id,
    SymbolId call_site_symbol_id,
    std::int64_t return_status) {
  const ClockMarkerId id = checked_next_id<ClockMarkerId>(rows_.size());
  rows_.push_back(ClockMarkerRow{
      id, source_ref_id, source_row_id, marker_symbol_id, host_before_ns,
      host_after_ns, device_timestamp_ns, host_pid, host_tid, device_id,
      has_stream_id, stream_id, has_connection_id, raw_connection_id,
      call_site_symbol_id, return_status});
  return id;
}

const ClockMarkerRow& ClockMarkerTable::row(ClockMarkerId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("ClockMarkerId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
