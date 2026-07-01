#include "traceloom/ir/communication_op_table.h"

#include <stdexcept>

namespace traceloom {

CommunicationOpId CommunicationOpTable::append(
    SourceRefId source_ref_id,
    TraceEventId trace_event_id,
    std::int64_t raw_connection_id,
    std::int64_t raw_op_id,
    std::uint32_t linked_task_count,
    std::uint32_t linked_stream_count,
    SymbolId op_name_symbol_id,
    SymbolId op_type_symbol_id,
    SymbolId linked_task_name_symbol_id,
    SymbolId linked_task_type_symbol_id) {
  const auto id = checked_next_id<CommunicationOpId>(rows_.size());
  rows_.push_back(CommunicationOpRow{id, source_ref_id, trace_event_id,
                                     raw_connection_id, raw_op_id,
                                     linked_task_count, linked_stream_count,
                                     op_name_symbol_id, op_type_symbol_id,
                                     linked_task_name_symbol_id,
                                     linked_task_type_symbol_id});
  return id;
}

const CommunicationOpRow& CommunicationOpTable::row(
    CommunicationOpId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("CommunicationOpId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
