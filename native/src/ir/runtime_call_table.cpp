#include "traceloom/ir/runtime_call_table.h"

#include <stdexcept>

namespace traceloom {

RuntimeCallId RuntimeCallTable::append(
    SourceRefId source_ref_id, std::uint64_t source_row_id,
    RuntimeCallProvider provider, RuntimeCallClockDomain clock_domain,
    RuntimeCallMatchPolicy match_policy, std::int64_t start_ns,
    std::int64_t end_ns, std::int64_t raw_correlation_id,
    SymbolId api_type_symbol_id, SymbolId api_name_symbol_id,
    std::int64_t raw_process_id, std::int64_t raw_thread_id,
    std::int64_t raw_global_tid, std::int64_t raw_context_id,
    bool has_device_id, std::uint32_t device_id) {
  if (!source_ref_id.valid()) {
    throw std::invalid_argument("RuntimeCallRow source_ref_id is invalid");
  }
  if (end_ns <= start_ns) {
    throw std::invalid_argument(
        "RuntimeCallRow interval must have positive duration");
  }
  const RuntimeCallId id = checked_next_id<RuntimeCallId>(rows_.size());
  rows_.push_back(RuntimeCallRow{
      id, source_ref_id, source_row_id, provider, clock_domain, match_policy,
      start_ns, end_ns, raw_correlation_id, raw_process_id, raw_thread_id,
      raw_global_tid, raw_context_id, has_device_id, device_id,
      api_type_symbol_id, api_name_symbol_id});
  return id;
}

const RuntimeCallRow& RuntimeCallTable::row(RuntimeCallId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("RuntimeCallId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
