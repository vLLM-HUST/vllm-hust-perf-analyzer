#include "traceloom/adapters/cuda_nsys_sqlite_adapter.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace traceloom {
namespace {

constexpr const char* kKernelTable = "CUPTI_ACTIVITY_KIND_KERNEL";
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

class Stopwatch {
 public:
  Stopwatch() : start_(Clock::now()) {}

  double elapsed_ms() const {
    return std::chrono::duration<double, std::milli>(Clock::now() - start_)
        .count();
  }

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

class SqliteDb {
 public:
  explicit SqliteDb(const std::string& path) {
    sqlite3* raw = nullptr;
    const int rc =
        sqlite3_open_v2(path.c_str(), &raw, SQLITE_OPEN_READONLY, nullptr);
    db_ = raw;
    if (rc != SQLITE_OK) {
      const std::string message =
          db_ == nullptr ? "unknown sqlite open error" : sqlite3_errmsg(db_);
      if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
      }
      throw std::runtime_error("failed to open CUDA/Nsight SQLite DB: " +
                               message);
    }
  }

  ~SqliteDb() {
    if (db_ != nullptr) {
      sqlite3_close(db_);
    }
  }

  SqliteDb(const SqliteDb&) = delete;
  SqliteDb& operator=(const SqliteDb&) = delete;

  sqlite3* get() const noexcept { return db_; }

 private:
  sqlite3* db_ = nullptr;
};

class SqliteStmt {
 public:
  SqliteStmt(sqlite3* db, const std::string& sql) : db_(db) {
    sqlite3_stmt* raw = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &raw, nullptr);
    stmt_ = raw;
    if (rc != SQLITE_OK) {
      throw std::runtime_error("failed to prepare CUDA/Nsight SQLite query: " +
                               std::string(sqlite3_errmsg(db_)) +
                               "; sql=" + sql);
    }
  }

  ~SqliteStmt() {
    if (stmt_ != nullptr) {
      sqlite3_finalize(stmt_);
    }
  }

  SqliteStmt(const SqliteStmt&) = delete;
  SqliteStmt& operator=(const SqliteStmt&) = delete;

  sqlite3_stmt* get() const noexcept { return stmt_; }
  sqlite3* db() const noexcept { return db_; }

 private:
  sqlite3* db_ = nullptr;
  sqlite3_stmt* stmt_ = nullptr;
};

bool file_exists(const std::string& path) {
  std::ifstream input(path);
  return input.good();
}

std::string sqlite_text(sqlite3_stmt* stmt, int column) {
  const unsigned char* raw = sqlite3_column_text(stmt, column);
  return raw == nullptr ? std::string()
                        : std::string(reinterpret_cast<const char*>(raw));
}

std::int64_t sqlite_i64(sqlite3_stmt* stmt, int column,
                        std::int64_t fallback = 0) {
  if (sqlite3_column_type(stmt, column) == SQLITE_NULL) {
    return fallback;
  }
  return sqlite3_column_int64(stmt, column);
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

std::string quote_identifier(const std::string& value) {
  std::string out = "\"";
  for (const char ch : value) {
    out += ch;
    if (ch == '"') {
      out += '"';
    }
  }
  out += '"';
  return out;
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

using ColumnMap = std::unordered_map<std::string, std::string>;

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

bool is_collective_kernel(const std::string& raw_name) {
  const std::string low = lower_ascii(raw_name);
  return low.find("nccl") != std::string::npos ||
         contains_any(low, {"allreduce", "all_reduce", "allgather",
                            "all_gather", "reducescatter", "reduce_scatter",
                            "alltoall", "all_to_all", "broadcast"});
}

std::string lift_cuda_kernel_label(const std::string& raw_name) {
  const std::string low = lower_ascii(raw_name);
  if (is_collective_kernel(raw_name)) {
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

using StreamKey = std::pair<std::uint32_t, std::uint32_t>;

void intern_stream(NativeIr& ir, std::map<StreamKey, StreamId>& streams,
                   SourceRefId source_ref, std::uint32_t device_id,
                   std::uint32_t stream_id) {
  const StreamKey key{device_id, stream_id};
  if (streams.find(key) == streams.end()) {
    streams.emplace(key,
                    ir.streams.append(source_ref, device_id, stream_id));
  }
}

std::uint32_t checked_u32(std::int64_t value, const std::string& field,
                          std::uint64_t source_row_id) {
  if (value < 0 ||
      static_cast<std::uint64_t>(value) >
          std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error("unsupported CUDA/Nsight " + field +
                             " at ordered kernel row " +
                             std::to_string(source_row_id) +
                             ": expected a non-negative 32-bit integer");
  }
  return static_cast<std::uint32_t>(value);
}

std::string first_column(const ColumnMap& columns,
                         std::initializer_list<const char*> names) {
  for (const char* name : names) {
    const std::string found = find_column(columns, name);
    if (!found.empty()) {
      return found;
    }
  }
  return {};
}

std::string select_or_null(const std::string& column) {
  return column.empty() ? std::string("NULL") : quote_identifier(column);
}

struct AuxiliaryActivitySpec {
  const char* table;
  const char* task_type;
  const char* fallback_label;
};

const std::vector<AuxiliaryActivitySpec>& auxiliary_activity_specs() {
  static const std::vector<AuxiliaryActivitySpec> specs{
      {"CUPTI_ACTIVITY_KIND_RUNTIME", "CUDA_RUNTIME_AUX", "CudaRuntime"},
      {"CUPTI_ACTIVITY_KIND_MEMCPY", "CUDA_MEMCPY_AUX", "CudaMemcpy"},
      {"CUPTI_ACTIVITY_KIND_MEMSET", "CUDA_MEMSET_AUX", "CudaMemset"},
      {"CUPTI_ACTIVITY_KIND_SYNCHRONIZATION", "CUDA_SYNC_AUX", "CudaSync"},
      {"CUPTI_ACTIVITY_KIND_CUDA_EVENT", "CUDA_EVENT_AUX", "CudaEvent"},
  };
  return specs;
}

void load_auxiliary_activity_rows(
    SqliteDb& db, NativeIr& ir, const CudaNsightSQLiteAdapterOptions& options,
    const CudaNsightSQLiteInventory& inventory,
    const std::unordered_map<std::int64_t, std::string>& strings) {
  for (const AuxiliaryActivitySpec& spec : auxiliary_activity_specs()) {
    if (!has_name(inventory.present_activity_tables, spec.table) ||
        table_row_count(db, spec.table) == 0) {
      continue;
    }
    const ColumnMap columns = table_columns(db, spec.table);
    const std::string start = first_column(columns, {"start", "timestamp"});
    const std::string end = find_column(columns, "end");
    if (start.empty()) {
      // Recent Nsight exports can use CUPTI_ACTIVITY_KIND_CUDA_EVENT as an
      // identity-only lookup table (eventId, device/context/stream) without a
      // device timestamp. It remains useful inventory evidence, but cannot be
      // materialized as a timeline event. Do not let that optional metadata
      // prevent timed kernel, synchronization, or graph evidence from loading.
      if (std::string(spec.table) == "CUPTI_ACTIVITY_KIND_CUDA_EVENT") {
        if (options.timing_diagnostics) {
          std::cerr << "cuda_nsys_metadata_only_table=" << spec.table << "\n";
        }
        continue;
      }
      throw std::runtime_error("unsupported CUDA/Nsight " +
                               std::string(spec.table) +
                               " schema: missing start or timestamp");
    }
    const std::string device = first_column(columns, {"deviceId", "device"});
    const std::string stream = first_column(columns, {"streamId", "stream"});
    const std::string correlation = find_column(columns, "correlationId");
    const std::string name = first_column(
        columns, {"nameId", "shortName", "copyKind", "syncType", "eventId"});
    const std::string sql =
        "SELECT rowid, " + quote_identifier(start) + ", " +
        select_or_null(end) + ", " + select_or_null(device) + ", " +
        select_or_null(stream) + ", " + select_or_null(correlation) + ", " +
        select_or_null(name) + " FROM " + quote_identifier(spec.table) +
        " WHERE " + quote_identifier(start) + " IS NOT NULL ORDER BY " +
        quote_identifier(start) + ", rowid";

    const SourceRefId source_ref = ir.source_refs.append(
        options.source_kind, options.db_path, spec.table, 0);
    std::map<StreamKey, StreamId> streams;
    SqliteStmt stmt(db.get(), sql);
    while (true) {
      const int rc = sqlite3_step(stmt.get());
      if (rc == SQLITE_DONE) {
        break;
      }
      if (rc != SQLITE_ROW) {
        throw std::runtime_error("failed to load CUDA/Nsight " +
                                 std::string(spec.table) + ": " +
                                 sqlite3_errmsg(stmt.db()));
      }
      const std::uint64_t source_row_id =
          static_cast<std::uint64_t>(sqlite_i64(stmt.get(), 0));
      const std::int64_t start_ns = sqlite_i64(stmt.get(), 1);
      std::int64_t end_ns = sqlite_i64(stmt.get(), 2, start_ns + 1);
      if (end_ns <= start_ns) {
        end_ns = start_ns + 1;
      }
      const std::uint32_t device_id = static_cast<std::uint32_t>(
          std::max<std::int64_t>(0, sqlite_i64(stmt.get(), 3, 0)));
      const std::uint32_t stream_id = static_cast<std::uint32_t>(
          std::max<std::int64_t>(0, sqlite_i64(stmt.get(), 4, 0)));
      const std::int64_t correlation_id = sqlite_i64(stmt.get(), 5, -1);
      std::string label = resolved_name(stmt.get(), 6, strings);
      if (label.empty()) {
        label = std::string(spec.fallback_label) + "_" +
                std::to_string(source_row_id);
      }
      intern_stream(ir, streams, source_ref, device_id, stream_id);
      const SymbolId label_symbol = ir.symbols.intern(label);
      const SymbolId task_type_symbol = ir.symbols.intern(spec.task_type);
      const TraceEventId event = ir.trace_events.append(
          source_ref, source_row_id, device_id, stream_id, start_ns, end_ns,
          label_symbol);
      const std::uint64_t raw_task_id =
          correlation_id < 0 ? source_row_id
                             : static_cast<std::uint64_t>(correlation_id);
      ir.tasks.append(source_ref, event, raw_task_id, correlation_id,
                      correlation_id, task_type_symbol, label_symbol,
                      task_type_symbol, task_type_symbol, SymbolId::invalid());
    }
  }
}

std::uint64_t stable_hash64(const std::string& value) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (unsigned char ch : value) {
    hash ^= static_cast<std::uint64_t>(ch);
    hash *= 1099511628211ULL;
  }
  return hash;
}

void load_graph_trace_rows(
    SqliteDb& db, NativeIr& ir, const CudaNsightSQLiteAdapterOptions& options,
    const CudaNsightSQLiteInventory& inventory) {
  constexpr const char* table = "CUPTI_ACTIVITY_KIND_GRAPH_TRACE";
  if (!has_name(inventory.present_activity_tables, table) ||
      table_row_count(db, table) == 0) {
    return;
  }
  const ColumnMap columns = table_columns(db, table);
  const std::string start = first_column(columns, {"start", "timestamp"});
  const std::string end = find_column(columns, "end");
  const std::string device = first_column(columns, {"deviceId", "device"});
  const std::string stream = first_column(columns, {"streamId", "stream"});
  const std::string graph = first_column(
      columns, {"executableGraphId", "graphExecId", "graphId", "graphNodeId"});
  if (start.empty()) {
    throw std::runtime_error(
        "unsupported CUDA/Nsight graph trace schema: missing start or timestamp");
  }
  const std::string sql =
      "SELECT rowid, " + quote_identifier(start) + ", " +
      select_or_null(end) + ", " + select_or_null(device) + ", " +
      select_or_null(stream) + ", " + select_or_null(graph) + " FROM " +
      quote_identifier(table) + " WHERE " + quote_identifier(start) +
      " IS NOT NULL ORDER BY " + quote_identifier(start) + ", rowid";
  const SourceRefId source_ref =
      ir.source_refs.append(options.source_kind, options.db_path, table, 0);
  std::map<std::string, GraphTemplateId> templates;
  std::map<StreamKey, StreamId> streams;
  SqliteStmt stmt(db.get(), sql);
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      return;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error("failed to load CUDA/Nsight graph trace rows: " +
                               std::string(sqlite3_errmsg(stmt.db())));
    }
    const std::uint64_t source_row_id =
        static_cast<std::uint64_t>(sqlite_i64(stmt.get(), 0));
    const std::int64_t start_ns = sqlite_i64(stmt.get(), 1);
    std::int64_t end_ns = sqlite_i64(stmt.get(), 2, start_ns + 1);
    if (end_ns <= start_ns) {
      end_ns = start_ns + 1;
    }
    const std::uint32_t device_id = static_cast<std::uint32_t>(
        std::max<std::int64_t>(0, sqlite_i64(stmt.get(), 3, 0)));
    const std::uint32_t stream_id = static_cast<std::uint32_t>(
        std::max<std::int64_t>(0, sqlite_i64(stmt.get(), 4, 0)));
    const std::int64_t raw_graph_id = sqlite_i64(stmt.get(), 5, -1);
    const std::string graph_key =
        raw_graph_id < 0 ? "row-" + std::to_string(source_row_id)
                         : std::to_string(raw_graph_id);
    auto found = templates.find(graph_key);
    GraphTemplateId graph_template;
    if (found == templates.end()) {
      graph_template =
          ir.graph_templates.append(source_ref, stable_hash64(graph_key), 0);
      templates.emplace(graph_key, graph_template);
    } else {
      graph_template = found->second;
    }
    intern_stream(ir, streams, source_ref, device_id, stream_id);
    const std::string label = "CudaGraphReplay T" +
                              std::to_string(graph_template.value() + 1);
    const SymbolId symbol = ir.symbols.intern(label);
    const TraceEventId event = ir.trace_events.append(
        source_ref, source_row_id, device_id, stream_id, start_ns, end_ns,
        symbol);
    ir.replay_units.append(graph_template, source_ref, AnchorId::invalid(),
                           AnchorId::invalid(), event);
  }
}

void load_kernel_rows(
    SqliteDb& db, NativeIr& ir, SourceRefId source_ref,
    const std::unordered_map<std::int64_t, std::string>& strings) {
  const ColumnMap columns = table_columns(db, kKernelTable);
  const std::string start = find_column(columns, "start");
  const std::string end = find_column(columns, "end");
  const std::string device = find_column(columns, "deviceId");
  const std::string stream = find_column(columns, "streamId");
  const std::string correlation = find_column(columns, "correlationId");
  const std::string demangled = find_column(columns, "demangledName");
  const std::string short_name = find_column(columns, "shortName");

  const auto select_or_null = [](const std::string& column) {
    return column.empty() ? std::string("NULL") : quote_identifier(column);
  };
  const std::string sql =
      "SELECT rowid, " + quote_identifier(start) + ", " +
      quote_identifier(end) + ", " + quote_identifier(device) + ", " +
      quote_identifier(stream) + ", " + select_or_null(correlation) + ", " +
      select_or_null(demangled) + ", " + select_or_null(short_name) + " FROM " +
      quote_identifier(kKernelTable) + " WHERE " + quote_identifier(start) +
      " IS NOT NULL AND " + quote_identifier(end) + " IS NOT NULL AND " +
      quote_identifier(end) + " > " + quote_identifier(start) + " ORDER BY " +
      quote_identifier(start) + ", " + quote_identifier(end) + ", " +
      quote_identifier(device) + ", " + quote_identifier(stream) + ", " +
      select_or_null(correlation) + ", " + select_or_null(short_name) + ", " +
      select_or_null(demangled) + ", rowid";

  SqliteStmt stmt(db.get(), sql);
  std::map<StreamKey, StreamId> streams;
  std::uint64_t ordered_row_id = 0;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      return;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error("failed to load CUDA/Nsight kernel rows: " +
                               std::string(sqlite3_errmsg(stmt.db())));
    }

    ++ordered_row_id;
    const std::int64_t raw_source_row_id = sqlite_i64(stmt.get(), 0, -1);
    if (raw_source_row_id < 0) {
      throw std::runtime_error(
          "unsupported CUDA/Nsight kernel rowid at ordered kernel row " +
          std::to_string(ordered_row_id) + ": expected a non-negative integer");
    }
    const std::uint64_t source_row_id =
        static_cast<std::uint64_t>(raw_source_row_id);
    const std::int64_t start_ns = sqlite_i64(stmt.get(), 1);
    const std::int64_t end_ns = sqlite_i64(stmt.get(), 2);
    const std::uint32_t device_id =
        checked_u32(sqlite_i64(stmt.get(), 3, -1), "deviceId", ordered_row_id);
    const std::uint32_t stream_id =
        checked_u32(sqlite_i64(stmt.get(), 4, -1), "streamId", ordered_row_id);
    const std::int64_t correlation_id = sqlite_i64(stmt.get(), 5, -1);
    const std::int64_t normalized_correlation_id =
        correlation_id < 0 ? -1 : correlation_id;
    const std::string demangled_name = resolved_name(stmt.get(), 6, strings);
    const std::string short_label = resolved_name(stmt.get(), 7, strings);
    std::string raw_label =
        choose_kernel_label(short_label, demangled_name);
    if (raw_label.empty()) {
      raw_label = "cuda_kernel_" + std::to_string(source_row_id);
    }
    const std::string lifted_label = lift_cuda_kernel_label(raw_label);
    const bool auxiliary = lifted_label.rfind("CudaAux:", 0) == 0;
    const bool collective = is_collective_kernel(raw_label);
    const std::string task_type =
        auxiliary ? "CUDA_KERNEL_AUX"
                  : (collective ? "CUDA_COLLECTIVE_KERNEL" : "CUDA_KERNEL");

    intern_stream(ir, streams, source_ref, device_id, stream_id);
    const SymbolId raw_symbol = ir.symbols.intern(raw_label);
    const TraceEventId event = ir.trace_events.append(
        source_ref, source_row_id, device_id, stream_id, start_ns, end_ns,
        raw_symbol);
    const SymbolId task_type_symbol = ir.symbols.intern(task_type);
    const SymbolId op_type_symbol = ir.symbols.intern(lifted_label);
    const std::uint64_t raw_task_id =
        correlation_id < 0 ? source_row_id
                           : static_cast<std::uint64_t>(correlation_id);
    const std::int64_t raw_global_task_id =
        correlation_id < 0 ? static_cast<std::int64_t>(source_row_id)
                           : correlation_id;
    const SymbolId compute_task_type_symbol =
        collective ? SymbolId::invalid() : task_type_symbol;
    const SymbolId comm_name_symbol =
        collective ? raw_symbol : SymbolId::invalid();
    const SymbolId communication_task_type_symbol =
        collective ? task_type_symbol : SymbolId::invalid();
    ir.tasks.append(source_ref, event, raw_task_id, raw_global_task_id,
                    normalized_correlation_id, task_type_symbol, raw_symbol,
                    op_type_symbol, compute_task_type_symbol, comm_name_symbol,
                    -1, communication_task_type_symbol);
    if (collective) {
      ir.communication_ops.append(
          source_ref, event, normalized_correlation_id, raw_source_row_id, 1, 1,
          raw_symbol, op_type_symbol, raw_symbol, task_type_symbol);
    }
  }
}

}  // namespace

CudaNsightSQLiteInventory inspect_cuda_nsys_sqlite_profile(
    const std::string& db_path) {
  if (db_path.empty()) {
    throw std::invalid_argument("CUDA/Nsight SQLite DB path is empty");
  }
  if (!file_exists(db_path)) {
    throw std::invalid_argument("CUDA/Nsight SQLite DB does not exist: " +
                                db_path);
  }
  SqliteDb db(db_path);
  return inspect_inventory(db);
}

bool looks_like_cuda_nsys_sqlite_profile(const std::string& db_path) {
  try {
    const CudaNsightSQLiteInventory inventory =
        inspect_cuda_nsys_sqlite_profile(db_path);
    return inventory.has_kernel_table && inventory.kernel_row_count > 0;
  } catch (const std::exception&) {
    return false;
  }
}

CudaNsightSQLiteAdapter::CudaNsightSQLiteAdapter(
    CudaNsightSQLiteAdapterOptions options)
    : options_(std::move(options)) {}

CudaNsightSQLiteAdapter::CudaNsightSQLiteAdapter(std::string db_path,
                                                 std::string source_kind)
    : options_(CudaNsightSQLiteAdapterOptions{std::move(db_path),
                                              std::move(source_kind)}) {}

NativeIr CudaNsightSQLiteAdapter::load() const {
  if (options_.db_path.empty()) {
    throw std::invalid_argument("CUDA/Nsight SQLite DB path is empty");
  }
  if (!file_exists(options_.db_path)) {
    throw std::invalid_argument("CUDA/Nsight SQLite DB does not exist: " +
                                options_.db_path);
  }

  CudaLoadTiming timing;
  const Stopwatch open_watch;
  SqliteDb db(options_.db_path);
  timing.sqlite_open_ms = open_watch.elapsed_ms();

  const Stopwatch inventory_watch;
  const CudaNsightSQLiteInventory inventory = inspect_inventory(db);
  timing.inventory_ms = inventory_watch.elapsed_ms();
  if (!inventory.has_kernel_table) {
    throw std::runtime_error(
        "unsupported CUDA/Nsight SQLite export: required table " +
        std::string(kKernelTable) + " is missing");
  }
  if (!inventory.missing_required_kernel_columns.empty()) {
    throw std::runtime_error(
        "unsupported CUDA/Nsight kernel schema: missing required column(s): " +
        join(inventory.missing_required_kernel_columns, ", "));
  }

  NativeIr ir;
  const SourceRefId kernel_source_ref = ir.source_refs.append(
      options_.source_kind, options_.db_path, kKernelTable, 0);

  std::unordered_map<std::int64_t, std::string> strings;
  const Stopwatch strings_watch;
  if (inventory.has_string_ids_table) {
    strings = load_string_ids(db);
  }
  timing.strings_ms = strings_watch.elapsed_ms();

  const Stopwatch kernels_watch;
  load_kernel_rows(db, ir, kernel_source_ref, strings);
  timing.kernels_ms = kernels_watch.elapsed_ms();

  const Stopwatch auxiliary_watch;
  load_auxiliary_activity_rows(db, ir, options_, inventory, strings);
  timing.auxiliary_ms = auxiliary_watch.elapsed_ms();

  const Stopwatch graph_trace_watch;
  load_graph_trace_rows(db, ir, options_, inventory);
  timing.graph_trace_ms = graph_trace_watch.elapsed_ms();

  if (options_.timing_diagnostics) {
    print_load_timing(timing);
    for (const std::string& table : inventory.present_activity_tables) {
      std::cerr << "cuda_nsys_activity_table=" << table << "\n";
    }
  }
  return ir;
}

}  // namespace traceloom
