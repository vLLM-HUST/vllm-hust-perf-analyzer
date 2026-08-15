#pragma once

#include "sqlite_profile_reader.h"
#include "traceloom/adapters/cuda_nsys_sqlite_adapter.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace traceloom::cuda_nsys_sqlite_detail {

// Private seam between CUDA profile ingestion and graph reconstruction. This
// is intentionally not part of the public adapter interface.
using SqliteDb = sqlite_profile_detail::ReadOnlyDatabase;
using SqliteStmt = sqlite_profile_detail::Statement;
using sqlite_profile_detail::quote_identifier;
using sqlite_profile_detail::read_text;

inline constexpr const char* kKernelTable =
    "CUPTI_ACTIVITY_KIND_KERNEL";

using ColumnMap = std::unordered_map<std::string, std::string>;
using StreamKey = std::pair<std::uint32_t, std::uint32_t>;

class Stopwatch {
 public:
  Stopwatch();
  double elapsed_ms() const;

 private:
  using Clock = std::chrono::steady_clock;
  Clock::time_point start_;
};

struct CudaLoadTiming {
  double sqlite_open_ms = 0.0;
  double inventory_ms = 0.0;
  double strings_ms = 0.0;
  double kernels_ms = 0.0;
  double auxiliary_ms = 0.0;
  double graph_trace_ms = 0.0;
};

void print_load_timing(const CudaLoadTiming& timing);
bool file_exists(const std::string& path);
std::int64_t sqlite_i64(sqlite3_stmt* stmt, int column,
                        std::int64_t fallback = 0);
std::string sqlite_text(sqlite3_stmt* stmt, int column);
std::string lower_ascii(std::string value);
std::vector<std::string> table_names(SqliteDb& db);
bool has_name(const std::vector<std::string>& names, const std::string& name);
std::uint64_t table_row_count(SqliteDb& db, const std::string& table_name);
ColumnMap table_columns(SqliteDb& db, const std::string& table_name);
std::string find_column(const ColumnMap& columns, const std::string& name);
std::string join(const std::vector<std::string>& values,
                 const std::string& separator);
CudaNsightSQLiteInventory inspect_inventory(SqliteDb& db);
std::unordered_map<std::int64_t, std::string> load_string_ids(SqliteDb& db);
std::unordered_map<std::int64_t, std::string> load_optional_enum_names(
    SqliteDb& db, const std::string& table_name);
std::string resolved_name(
    sqlite3_stmt* stmt, int column,
    const std::unordered_map<std::int64_t, std::string>& strings);
std::string resolved_sync_type(
    sqlite3_stmt* stmt, int column,
    const std::unordered_map<std::int64_t, std::string>& sync_types);
std::string choose_kernel_label(const std::string& short_name,
                                const std::string& demangled_name);
bool is_nccl_collective_kernel(const std::string& raw_name);
std::string lift_cuda_kernel_label(const std::string& raw_name);
void intern_stream(NativeIr& ir, std::map<StreamKey, StreamId>& streams,
                   SourceRefId source_ref, std::uint32_t device_id,
                   std::uint32_t stream_id);
std::uint32_t checked_u32(std::int64_t value, const std::string& field,
                          std::uint64_t source_row_id);
std::uint64_t stable_hash64(const std::string& value);

void load_kernel_rows(
    SqliteDb& db, NativeIr& ir, SourceRefId source_ref,
    const std::unordered_map<std::int64_t, std::string>& strings);
void load_auxiliary_activity_rows(
    SqliteDb& db, NativeIr& ir,
    const CudaNsightSQLiteAdapterOptions& options,
    const CudaNsightSQLiteInventory& inventory,
    const std::unordered_map<std::int64_t, std::string>& strings);
void materialize_cuda_graph_node_replays(
    SqliteDb& db, NativeIr& ir,
    const CudaNsightSQLiteAdapterOptions& options,
    const std::unordered_map<std::int64_t, std::string>& strings);
void load_graph_trace_rows(
    SqliteDb& db, NativeIr& ir,
    const CudaNsightSQLiteAdapterOptions& options,
    const CudaNsightSQLiteInventory& inventory);

}  // namespace traceloom::cuda_nsys_sqlite_detail
