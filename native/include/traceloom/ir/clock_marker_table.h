#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

enum class ClockMarkerResolutionMethod {
  kPreResolved,
  kDirectOverlap,
  kOrdinalAffineFallback,
  kUnresolved,
};

std::string_view clock_marker_resolution_method_name(
    ClockMarkerResolutionMethod method);

// Resolved marker record from idle-evidence-contract section 7.2. Real v4.4
// rows retain the narrow caller record-call bracket, the outer marker bracket,
// the matched profiler-host API interval, and device TASK timestamp. A raw
// device syscnt value is never accepted as profiler nanoseconds implicitly.
struct ClockMarkerRow {
  ClockMarkerId id;
  SourceRefId source_ref_id;
  std::uint64_t source_row_id = 0;
  SymbolId marker_symbol_id;
  std::int64_t host_before_ns = 0;
  std::int64_t host_after_ns = 0;
  std::int64_t device_timestamp_ns = 0;
  std::uint64_t host_pid = 0;
  std::uint64_t host_tid = 0;
  std::uint32_t device_id = 0;
  bool has_stream_id = false;
  std::uint64_t stream_id = 0;
  bool has_connection_id = false;
  std::int64_t raw_connection_id = -1;
  SymbolId call_site_symbol_id;
  std::int64_t return_status = 0;
  bool has_profiler_host_interval = false;
  std::int64_t profiler_host_start_ns = 0;
  std::int64_t profiler_host_end_ns = 0;
  ClockMarkerResolutionMethod resolution_method =
      ClockMarkerResolutionMethod::kPreResolved;
  bool has_resolution_residual = false;
  long double resolution_residual_ns = 0.0L;
  bool has_record_host_bracket = false;
  std::int64_t record_after_ns = 0;
};

class ClockMarkerTable {
 public:
  ClockMarkerId append(SourceRefId source_ref_id,
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
                       std::int64_t return_status,
                       bool has_profiler_host_interval = false,
                       std::int64_t profiler_host_start_ns = 0,
                       std::int64_t profiler_host_end_ns = 0,
                       ClockMarkerResolutionMethod resolution_method =
                           ClockMarkerResolutionMethod::kPreResolved,
                       bool has_resolution_residual = false,
                       long double resolution_residual_ns = 0.0L,
                       bool has_record_host_bracket = false,
                       std::int64_t record_after_ns = 0);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  void reserve(std::size_t count) { rows_.reserve(count); }
  const ClockMarkerRow& row(ClockMarkerId id) const;
  const std::vector<ClockMarkerRow>& rows() const noexcept { return rows_; }

 private:
  std::vector<ClockMarkerRow> rows_;
};

}  // namespace traceloom
