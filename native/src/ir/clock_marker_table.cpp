#include "traceloom/ir/clock_marker_table.h"

#include <stdexcept>

namespace traceloom {

std::string_view clock_marker_resolution_method_name(
    ClockMarkerResolutionMethod method) {
  switch (method) {
    case ClockMarkerResolutionMethod::kPreResolved:
      return "pre_resolved";
    case ClockMarkerResolutionMethod::kDirectOverlap:
      return "direct_overlap";
    case ClockMarkerResolutionMethod::kOrdinalAffineFallback:
      return "ordinal_affine_fallback";
    case ClockMarkerResolutionMethod::kUnresolved:
      return "unresolved";
  }
  return "unresolved";
}

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
    std::int64_t return_status,
    bool has_profiler_host_interval,
    std::int64_t profiler_host_start_ns,
    std::int64_t profiler_host_end_ns,
    ClockMarkerResolutionMethod resolution_method,
    bool has_resolution_residual,
    long double resolution_residual_ns,
    bool has_record_host_bracket,
    std::int64_t record_after_ns) {
  const ClockMarkerId id = checked_next_id<ClockMarkerId>(rows_.size());
  rows_.push_back(ClockMarkerRow{
      id, source_ref_id, source_row_id, marker_symbol_id, host_before_ns,
      host_after_ns, device_timestamp_ns, host_pid, host_tid, device_id,
      has_stream_id, stream_id, has_connection_id, raw_connection_id,
      call_site_symbol_id, return_status, has_profiler_host_interval,
      profiler_host_start_ns, profiler_host_end_ns, resolution_method,
      has_resolution_residual, resolution_residual_ns,
      has_record_host_bracket, record_after_ns});
  return id;
}

const ClockMarkerRow& ClockMarkerTable::row(ClockMarkerId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("ClockMarkerId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
