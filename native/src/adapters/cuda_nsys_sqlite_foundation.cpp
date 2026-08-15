#include "cuda_nsys_sqlite_internal.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace traceloom::cuda_nsys_sqlite_detail {

constexpr const char* kStringIdsTable = "StringIds";

const std::vector<std::string>& optional_activity_table_names() {
  static const std::vector<std::string> names{
      "CUPTI_ACTIVITY_KIND_RUNTIME",
      "CUPTI_ACTIVITY_KIND_MEMCPY",
      "CUPTI_ACTIVITY_KIND_MEMSET",
      "CUPTI_ACTIVITY_KIND_SYNCHRONIZATION",
      "CUPTI_ACTIVITY_KIND_CUDA_EVENT",
      "CUPTI_ACTIVITY_KIND_GRAPH_TRACE",
  };
  return names;
}

Stopwatch::Stopwatch() : start_(Clock::now()) {}

double Stopwatch::elapsed_ms() const {
  return std::chrono::duration<double, std::milli>(Clock::now() - start_)
      .count();
}

void print_load_timing(const CudaLoadTiming& timing) {
  std::cerr << "timing load_cuda_sqlite_open_ms=" << timing.sqlite_open_ms
            << "\n";
  std::cerr << "timing load_cuda_inventory_ms=" << timing.inventory_ms
            << "\n";
  std::cerr << "timing load_cuda_strings_ms=" << timing.strings_ms << "\n";
  std::cerr << "timing load_cuda_kernels_ms=" << timing.kernels_ms << "\n";
  std::cerr << "timing load_cuda_auxiliary_ms=" << timing.auxiliary_ms << "\n";
  std::cerr << "timing load_cuda_graph_trace_ms=" << timing.graph_trace_ms
            << "\n";
}

bool file_exists(const std::string& path) {
  std::ifstream input(path);
  return input.good();
}

std::int64_t sqlite_i64(sqlite3_stmt* stmt, int column,
                        std::int64_t fallback) {
  return sqlite_profile_detail::read_i64(stmt, column, fallback);
}

std::string sqlite_text(sqlite3_stmt* stmt, int column) {
  return read_text(stmt, column);
}

std::string lower_ascii(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

bool contains_any(const std::string& text,
                  std::initializer_list<const char*> needles) {
  for (const char* needle : needles) {
    if (text.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> table_names(SqliteDb& db) {
  SqliteStmt stmt(db.get(),
                  "SELECT name FROM sqlite_master WHERE type='table' "
                  "ORDER BY name");
  std::vector<std::string> out;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      out.push_back(sqlite_text(stmt.get(), 0));
      continue;
    }
    if (rc == SQLITE_DONE) {
      return out;
    }
    throw std::runtime_error("failed to read CUDA/Nsight SQLite inventory: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
}

bool has_name(const std::vector<std::string>& names, const std::string& name) {
  return std::find(names.begin(), names.end(), name) != names.end();
}

std::uint64_t table_row_count(SqliteDb& db, const std::string& table_name) {
  SqliteStmt stmt(db.get(), "SELECT COUNT(*) FROM " +
                                quote_identifier(table_name));
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_ROW) {
    throw std::runtime_error("failed to count CUDA/Nsight table " +
                             table_name + ": " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  const std::int64_t count = sqlite_i64(stmt.get(), 0);
  return count < 0 ? 0 : static_cast<std::uint64_t>(count);
}

ColumnMap table_columns(SqliteDb& db, const std::string& table_name) {
  SqliteStmt stmt(db.get(),
                  "PRAGMA table_info(" + quote_identifier(table_name) + ")");
  ColumnMap out;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      const std::string actual_name = sqlite_text(stmt.get(), 1);
      out.emplace(lower_ascii(actual_name), actual_name);
      continue;
    }
    if (rc == SQLITE_DONE) {
      return out;
    }
    throw std::runtime_error("failed to inspect CUDA/Nsight table " +
                             table_name + ": " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
}

std::string find_column(const ColumnMap& columns, const std::string& name) {
  const auto found = columns.find(lower_ascii(name));
  return found == columns.end() ? std::string() : found->second;
}

std::vector<std::string> missing_required_kernel_columns(
    const ColumnMap& columns) {
  std::vector<std::string> out;
  for (const char* required : {"start", "end", "deviceId", "streamId"}) {
    if (find_column(columns, required).empty()) {
      out.push_back(required);
    }
  }
  return out;
}

std::string join(const std::vector<std::string>& values,
                 const std::string& separator) {
  std::ostringstream out;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) {
      out << separator;
    }
    out << values[index];
  }
  return out.str();
}

CudaNsightSQLiteInventory inspect_inventory(SqliteDb& db) {
  CudaNsightSQLiteInventory inventory;
  const std::vector<std::string> tables = table_names(db);
  inventory.has_kernel_table = has_name(tables, kKernelTable);
  inventory.has_string_ids_table = has_name(tables, kStringIdsTable);
  if (inventory.has_kernel_table) {
    inventory.kernel_row_count = table_row_count(db, kKernelTable);
    inventory.missing_required_kernel_columns =
        missing_required_kernel_columns(table_columns(db, kKernelTable));
  }
  for (const std::string& name : optional_activity_table_names()) {
    if (has_name(tables, name)) {
      inventory.present_activity_tables.push_back(name);
    }
  }
  return inventory;
}

std::unordered_map<std::int64_t, std::string> load_string_ids(SqliteDb& db) {
  const ColumnMap columns = table_columns(db, kStringIdsTable);
  const std::string id_column = find_column(columns, "id");
  const std::string value_column = find_column(columns, "value");
  std::vector<std::string> missing;
  if (id_column.empty()) {
    missing.push_back("id");
  }
  if (value_column.empty()) {
    missing.push_back("value");
  }
  if (!missing.empty()) {
    throw std::runtime_error(
        "unsupported CUDA/Nsight StringIds schema: missing required "
        "column(s): " +
        join(missing, ", "));
  }

  SqliteStmt stmt(db.get(), "SELECT " + quote_identifier(id_column) + ", " +
                                quote_identifier(value_column) + " FROM " +
                                quote_identifier(kStringIdsTable) +
                                " ORDER BY " + quote_identifier(id_column));
  std::unordered_map<std::int64_t, std::string> out;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      if (sqlite3_column_type(stmt.get(), 0) != SQLITE_NULL) {
        out.emplace(sqlite_i64(stmt.get(), 0), sqlite_text(stmt.get(), 1));
      }
      continue;
    }
    if (rc == SQLITE_DONE) {
      return out;
    }
    throw std::runtime_error("failed to load CUDA/Nsight StringIds: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
}

std::unordered_map<std::int64_t, std::string> load_optional_enum_names(
    SqliteDb& db, const std::string& table) {
  const std::vector<std::string> tables = table_names(db);
  if (!has_name(tables, table)) {
    return {};
  }
  const ColumnMap columns = table_columns(db, table);
  const std::string id_column = find_column(columns, "id");
  const std::string name_column = find_column(columns, "name");
  if (id_column.empty() || name_column.empty()) {
    throw std::runtime_error("unsupported CUDA/Nsight " + table +
                             " schema: expected id and name columns");
  }
  SqliteStmt stmt(db.get(), "SELECT " + quote_identifier(id_column) + ", " +
                                quote_identifier(name_column) + " FROM " +
                                quote_identifier(table) + " ORDER BY " +
                                quote_identifier(id_column));
  std::unordered_map<std::int64_t, std::string> out;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      if (sqlite3_column_type(stmt.get(), 0) != SQLITE_NULL) {
        out.emplace(sqlite_i64(stmt.get(), 0), sqlite_text(stmt.get(), 1));
      }
      continue;
    }
    if (rc == SQLITE_DONE) {
      return out;
    }
    throw std::runtime_error("failed to load CUDA/Nsight " + table + ": " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
}

std::string resolved_name(
    sqlite3_stmt* stmt, int column,
    const std::unordered_map<std::int64_t, std::string>& strings) {
  const int type = sqlite3_column_type(stmt, column);
  if (type == SQLITE_TEXT) {
    return sqlite_text(stmt, column);
  }
  if (type == SQLITE_INTEGER) {
    const auto found = strings.find(sqlite_i64(stmt, column, -1));
    if (found != strings.end()) {
      return found->second;
    }
  }
  return {};
}

std::string resolved_sync_type(
    sqlite3_stmt* stmt, int column,
    const std::unordered_map<std::int64_t, std::string>& sync_types) {
  const int type = sqlite3_column_type(stmt, column);
  if (type == SQLITE_TEXT) {
    return sqlite_text(stmt, column);
  }
  if (type != SQLITE_INTEGER) {
    return {};
  }
  const std::int64_t raw_type = sqlite_i64(stmt, column, -1);
  const auto found = sync_types.find(raw_type);
  if (found == sync_types.end()) {
    return "CUDA_SYNC_TYPE_" + std::to_string(raw_type);
  }
  constexpr const char* prefix = "CUPTI_ACTIVITY_SYNCHRONIZATION_TYPE_";
  if (found->second.rfind(prefix, 0) == 0) {
    return found->second.substr(std::char_traits<char>::length(prefix));
  }
  return found->second;
}

std::string choose_kernel_label(const std::string& short_name,
                                const std::string& demangled_name) {
  const std::string short_lower = lower_ascii(short_name);
  if ((short_lower == "kernel" || short_lower == "kernel1" ||
       short_lower == "kernel2" || short_lower == "kernel3") &&
      !demangled_name.empty()) {
    return demangled_name;
  }
  return short_name.empty() ? demangled_name : short_name;
}

bool is_nccl_collective_kernel(const std::string& raw_name) {
  const std::string low = lower_ascii(raw_name);
  return low.find("nccl") != std::string::npos &&
         contains_any(low, {"allreduce", "all_reduce", "allgather",
                            "all_gather", "reducescatter", "reduce_scatter",
                            "alltoall", "all_to_all", "broadcast"});
}

std::string lift_cuda_kernel_label(const std::string& raw_name) {
  const std::string low = lower_ascii(raw_name);
  if (is_nccl_collective_kernel(raw_name)) {
    return raw_name;
  }
  if (contains_any(low,
                   {"flash_attn", "flashattention", "flash_fwd", "_flash_fwd"}) ||
      (low.find("flash") != std::string::npos &&
       low.find("attention") != std::string::npos)) {
    return "CudaFlashAttention";
  }
  if (low.find("paged_attention") != std::string::npos ||
      low.find("attention") != std::string::npos) {
    return "CudaAttention";
  }
  if (contains_any(low,
                   {"sgemm", "dgemm", "hgemm", "gemm", "cutlass", "matmul"})) {
    return "CudaMatMul";
  }
  if (contains_any(low, {"rmsnorm", "rms_norm"})) {
    return "CudaRmsNorm";
  }
  if (contains_any(low, {"layer_norm", "layernorm"})) {
    return "CudaLayerNorm";
  }
  if (contains_any(low, {"rotary", "rope"})) {
    return "CudaRope";
  }
  if (contains_any(low,
                   {"silu", "swiglu", "gelu", "act_and_mul"})) {
    return "CudaActivation";
  }
  if (contains_any(low,
                   {"reshape_and_cache", "slot_mapping", "kv_cache"})) {
    return "CudaKVCache";
  }
  if (contains_any(low, {"topk", "topp", "sampling", "sampler"})) {
    return "CudaSampling";
  }
  if (low.find("softmax") != std::string::npos) {
    return "CudaSoftmax";
  }
  if (contains_any(low, {"splitkreduce", "reduce_kernel"})) {
    return "CudaReduce";
  }
  if (contains_any(low, {"distribution_", "random", "curand"})) {
    return "CudaAux:Random";
  }
  if (contains_any(low, {"vectorized_elementwise_kernel", "elementwise"})) {
    return "CudaAux:Pointwise";
  }
  if (contains_any(low, {"memset", "fill", "zero"})) {
    return "CudaAux:Fill";
  }
  if (contains_any(low,
                   {"copy", "catarraybatchedcopy", "tensorcopy"})) {
    return "CudaAux:Copy";
  }
  if (contains_any(low, {"index", "gather", "scatter"})) {
    return "CudaAux:Index";
  }
  return raw_name;
}

}  // namespace traceloom::cuda_nsys_sqlite_detail
