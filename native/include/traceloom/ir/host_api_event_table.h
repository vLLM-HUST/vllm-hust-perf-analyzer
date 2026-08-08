#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

// A profiler host API interval. Times remain in the profiler host clock domain
// until ClockAlignment applies its explicit profiler-host -> caller-realtime ->
// device composition; adapters must never copy these values into a
// device-domain TraceEventRow or assume they equal caller CLOCK_REALTIME.
struct HostApiEventRow {
  HostApiEventId id;
  SourceRefId source_ref_id;
  std::uint64_t source_row_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::uint64_t raw_global_tid = 0;
  std::int64_t raw_connection_id = -1;
  SymbolId api_type_symbol_id;
  SymbolId api_name_symbol_id;
  bool has_device_id = false;
  std::uint32_t device_id = 0;
};

class HostApiEventTable {
 public:
  HostApiEventId append(SourceRefId source_ref_id,
                        std::uint64_t source_row_id,
                        std::int64_t start_ns,
                        std::int64_t end_ns,
                        std::uint64_t raw_global_tid,
                        std::int64_t raw_connection_id,
                        SymbolId api_type_symbol_id,
                        SymbolId api_name_symbol_id,
                        bool has_device_id = false,
                        std::uint32_t device_id = 0);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  void reserve(std::size_t count) { rows_.reserve(count); }
  const HostApiEventRow& row(HostApiEventId id) const;
  const std::vector<HostApiEventRow>& rows() const noexcept { return rows_; }

 private:
  std::vector<HostApiEventRow> rows_;
};

}  // namespace traceloom
