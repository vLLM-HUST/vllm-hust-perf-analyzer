#include "traceloom/ir/host_api_event_table.h"

#include <stdexcept>

namespace traceloom {

HostApiEventId HostApiEventTable::append(
    SourceRefId source_ref_id,
    std::uint64_t source_row_id,
    std::int64_t start_ns,
    std::int64_t end_ns,
    std::uint64_t raw_global_tid,
    std::int64_t raw_connection_id,
    SymbolId api_type_symbol_id,
    SymbolId api_name_symbol_id,
    bool has_device_id,
    std::uint32_t device_id) {
  const HostApiEventId id = checked_next_id<HostApiEventId>(rows_.size());
  rows_.push_back(HostApiEventRow{
      id, source_ref_id, source_row_id, start_ns, end_ns, raw_global_tid,
      raw_connection_id, api_type_symbol_id, api_name_symbol_id,
      has_device_id, device_id});
  return id;
}

const HostApiEventRow& HostApiEventTable::row(HostApiEventId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("HostApiEventId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
