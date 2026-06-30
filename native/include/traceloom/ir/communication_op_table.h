#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

struct CommunicationOpRow {
  CommunicationOpId id;
  SourceRefId source_ref_id;
  TraceEventId trace_event_id;
  std::int64_t raw_connection_id = -1;
  std::int64_t raw_op_id = -1;
  std::uint32_t linked_task_count = 0;
  std::uint32_t linked_stream_count = 0;
  SymbolId op_name_symbol_id;
};

class CommunicationOpTable {
 public:
  CommunicationOpId append(SourceRefId source_ref_id,
                           TraceEventId trace_event_id,
                           std::int64_t raw_connection_id,
                           std::int64_t raw_op_id,
                           std::uint32_t linked_task_count,
                           std::uint32_t linked_stream_count,
                           SymbolId op_name_symbol_id);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const CommunicationOpRow& row(CommunicationOpId id) const;
  const std::vector<CommunicationOpRow>& rows() const noexcept {
    return rows_;
  }

 private:
  std::vector<CommunicationOpRow> rows_;
};

}  // namespace traceloom
