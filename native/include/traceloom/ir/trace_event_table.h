#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

struct TraceEventRow {
  TraceEventId id;
  SourceRefId source_ref_id;
  std::uint64_t source_row_id = 0;
  std::uint32_t device_id = 0;
  std::uint32_t stream_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  SymbolId raw_name_symbol_id;
};

class TraceEventTable {
 public:
  TraceEventId append(SourceRefId source_ref_id,
                      std::uint64_t source_row_id,
                      std::uint32_t device_id,
                      std::uint32_t stream_id,
                      std::int64_t start_ns,
                      std::int64_t end_ns,
                      SymbolId raw_name_symbol_id);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const TraceEventRow& row(TraceEventId id) const;
  const std::vector<TraceEventRow>& rows() const noexcept { return rows_; }

 private:
  std::vector<TraceEventRow> rows_;
};

}  // namespace traceloom
