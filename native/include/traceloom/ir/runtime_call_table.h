#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

// Provider-neutral host/runtime observation. These intervals deliberately do
// remain canonical here rather than in TraceEventTable: TraceEventRow is the
// device timeline projected into anchors and auxiliary work, whereas a runtime
// call remains in its provider host clock domain and is related to device work
// explicitly. Adapters may retain a legacy TraceEvent mirror for compatibility;
// downstream device attribution must exclude that mirror.
enum class RuntimeCallProvider : std::uint8_t {
  kUnknown = 0,
  kCuda = 1,
  kAscend = 2,
};

enum class RuntimeCallClockDomain : std::uint8_t {
  kUnknown = 0,
  kProfilerHost = 1,
};

enum class RuntimeCallMatchPolicy : std::uint8_t {
  kUnsupported = 0,
  kCudaCorrelationId = 1,
  kAscendConnectionId = 2,
};

struct RuntimeCallRow {
  RuntimeCallId id;
  SourceRefId source_ref_id;
  std::uint64_t source_row_id = 0;
  RuntimeCallProvider provider = RuntimeCallProvider::kUnknown;
  RuntimeCallClockDomain clock_domain = RuntimeCallClockDomain::kUnknown;
  RuntimeCallMatchPolicy match_policy = RuntimeCallMatchPolicy::kUnsupported;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::int64_t raw_correlation_id = -1;
  // A provider may expose only global_tid, only a thread id, or no execution
  // identity. Unknown values stay -1; they are never synthesized.
  std::int64_t raw_process_id = -1;
  std::int64_t raw_thread_id = -1;
  std::int64_t raw_global_tid = -1;
  std::int64_t raw_context_id = -1;
  bool has_device_id = false;
  std::uint32_t device_id = 0;
  SymbolId api_type_symbol_id;
  SymbolId api_name_symbol_id;
};

class RuntimeCallTable {
 public:
  RuntimeCallId append(
      SourceRefId source_ref_id, std::uint64_t source_row_id,
      RuntimeCallProvider provider, RuntimeCallClockDomain clock_domain,
      RuntimeCallMatchPolicy match_policy, std::int64_t start_ns,
      std::int64_t end_ns, std::int64_t raw_correlation_id,
      SymbolId api_type_symbol_id, SymbolId api_name_symbol_id,
      std::int64_t raw_process_id = -1, std::int64_t raw_thread_id = -1,
      std::int64_t raw_global_tid = -1, std::int64_t raw_context_id = -1,
      bool has_device_id = false, std::uint32_t device_id = 0);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  void reserve(std::size_t count) { rows_.reserve(count); }
  const RuntimeCallRow& row(RuntimeCallId id) const;
  const std::vector<RuntimeCallRow>& rows() const noexcept { return rows_; }

 private:
  std::vector<RuntimeCallRow> rows_;
};

}  // namespace traceloom
