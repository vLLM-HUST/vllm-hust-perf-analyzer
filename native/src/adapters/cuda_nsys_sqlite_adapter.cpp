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
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
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

std::vector<std::string> missing_cuda_event_identity_columns(
    const ColumnMap& columns) {
  std::vector<std::string> out;
  for (const char* required : {"deviceId", "contextId", "streamId",
                               "eventId"}) {
    if (find_column(columns, required).empty()) {
      out.push_back(required);
    }
  }
  return out;
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
        if (!end.empty()) {
          throw std::runtime_error(
              "unsupported CUDA/Nsight CUPTI_ACTIVITY_KIND_CUDA_EVENT "
              "schema: end without start or timestamp");
        }
        const std::vector<std::string> missing_identity_columns =
            missing_cuda_event_identity_columns(columns);
        if (!missing_identity_columns.empty()) {
          throw std::runtime_error(
              "unsupported CUDA/Nsight CUPTI_ACTIVITY_KIND_CUDA_EVENT "
              "metadata schema: missing identity column(s): " +
              join(missing_identity_columns, ", "));
        }
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

struct CudaGraphLaunchEvidence {
  SourceRefId source_ref_id;
  std::uint64_t source_row_id = 0;
  std::int64_t correlation_id = -1;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

struct CudaGraphChildEvidence {
  SourceRefId source_ref_id;
  TaskId task_id;
  TraceEventId trace_event_id;
  std::uint64_t source_row_id = 0;
  std::int64_t correlation_id = -1;
  std::int64_t graph_node_id = -1;
  std::int64_t context_id = -1;
  std::uint32_t device_id = 0;
  std::uint32_t stream_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  bool compute = false;
  bool communication = false;
  std::string exact_label;
  std::string readable_label;
};

struct PreparedCudaGraphLaunch {
  const CudaGraphLaunchEvidence* launch = nullptr;
  std::vector<const CudaGraphChildEvidence*> children;
  ReplayCompositionRegionStatus failure_status =
      ReplayCompositionRegionStatus::kRecognizedCompletePattern;
  std::string identity_signature;
  std::string body_signature;
  std::string readable_body;
  std::uint32_t device_id = 0;
  std::uint32_t stream_count = 0;
  std::uint32_t compute_task_count = 0;
  std::uint32_t communication_task_count = 0;
  std::int64_t body_start_ns = 0;
  std::int64_t body_end_ns = 0;
  TaskId first_task_id;
  TaskId last_task_id;
};

SourceRefId source_ref_for_table(const NativeIr& ir,
                                 const std::string& table_name) {
  for (const SourceRefRow& source : ir.source_refs.rows()) {
    if (source.table_name == table_name) {
      return source.id;
    }
  }
  return SourceRefId::invalid();
}

std::map<std::uint64_t, TaskId> tasks_by_source_row(
    const NativeIr& ir, SourceRefId source_ref_id) {
  std::map<std::uint64_t, TaskId> out;
  if (!source_ref_id.valid()) {
    return out;
  }
  for (const TaskRow& task : ir.tasks.rows()) {
    if (task.source_ref_id != source_ref_id ||
        !task.trace_event_id.valid()) {
      continue;
    }
    const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
    out.emplace(event.source_row_id, task.id);
  }
  return out;
}

StreamId stream_for_raw_id(const NativeIr& ir, std::uint32_t device_id,
                           std::uint32_t stream_id) {
  for (const StreamRow& stream : ir.streams.rows()) {
    if (stream.device_id == device_id &&
        stream.raw_stream_id == stream_id) {
      return stream.id;
    }
  }
  return StreamId::invalid();
}

bool graph_node_activity_capability_complete(SqliteDb& db) {
  static const std::set<std::string> supported{
      "CUDA_GRAPH_NODE_EVENTS", "CUPTI_ACTIVITY_KIND_KERNEL",
      "CUPTI_ACTIVITY_KIND_MEMCPY"};
  for (const std::string& table : table_names(db)) {
    const ColumnMap columns = table_columns(db, table);
    const std::string graph_node = find_column(columns, "graphNodeId");
    if (graph_node.empty()) {
      continue;
    }
    SqliteStmt count(
        db.get(), "SELECT COUNT(*) FROM " + quote_identifier(table) +
                      " WHERE " + quote_identifier(graph_node) +
                      " IS NOT NULL");
    if (sqlite3_step(count.get()) != SQLITE_ROW) {
      throw std::runtime_error(
          "failed to inspect CUDA graph-node activity capability: " +
          std::string(sqlite3_errmsg(count.db())));
    }
    if (sqlite_i64(count.get(), 0) == 0) {
      continue;
    }
    if (supported.find(table) == supported.end()) {
      return false;
    }
    std::vector<std::string> required;
    if (table == kKernelTable) {
      required = {find_column(columns, "contextId"),
                  find_column(columns, "correlationId")};
    } else if (table == "CUPTI_ACTIVITY_KIND_MEMCPY") {
      required = {find_column(columns, "contextId"),
                  find_column(columns, "correlationId"),
                  find_column(columns, "bytes"),
                  find_column(columns, "copyKind")};
    }
    if (std::any_of(required.begin(), required.end(),
                    [](const std::string& column) {
                      return column.empty();
                    })) {
      return false;
    }
    if (!required.empty()) {
      std::string incomplete =
          quote_identifier(graph_node) + " IS NOT NULL AND (";
      for (std::size_t index = 0; index < required.size(); ++index) {
        if (index != 0) {
          incomplete += " OR ";
        }
        incomplete += quote_identifier(required[index]) + " IS NULL";
      }
      incomplete += ")";
      SqliteStmt incomplete_count(
          db.get(), "SELECT COUNT(*) FROM " + quote_identifier(table) +
                        " WHERE " + incomplete);
      if (sqlite3_step(incomplete_count.get()) != SQLITE_ROW) {
        throw std::runtime_error(
            "failed to inspect CUDA graph-node activity fields: " +
            std::string(sqlite3_errmsg(incomplete_count.db())));
      }
      if (sqlite_i64(incomplete_count.get(), 0) > 0) {
        return false;
      }
    }
  }
  return true;
}

std::vector<CudaGraphLaunchEvidence> load_cuda_graph_launch_evidence(
    SqliteDb& db, const NativeIr& ir,
    const std::unordered_map<std::int64_t, std::string>& strings) {
  constexpr const char* table = "CUPTI_ACTIVITY_KIND_RUNTIME";
  const SourceRefId source_ref = source_ref_for_table(ir, table);
  if (!source_ref.valid()) {
    return {};
  }
  const ColumnMap columns = table_columns(db, table);
  const std::string start = find_column(columns, "start");
  const std::string end = find_column(columns, "end");
  const std::string correlation = find_column(columns, "correlationId");
  const std::string name = find_column(columns, "nameId");
  if (start.empty() || end.empty() || correlation.empty() || name.empty()) {
    return {};
  }
  const std::map<std::uint64_t, TaskId> task_ids =
      tasks_by_source_row(ir, source_ref);
  SqliteStmt stmt(
      db.get(), "SELECT rowid, " + quote_identifier(start) + ", " +
                    quote_identifier(end) + ", " +
                    quote_identifier(correlation) + ", " +
                    quote_identifier(name) + " FROM " +
                    quote_identifier(table) + " WHERE " +
                    quote_identifier(correlation) + " IS NOT NULL ORDER BY " +
                    quote_identifier(start) + ", rowid");
  std::vector<CudaGraphLaunchEvidence> out;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      return out;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error("failed to load CUDA graph launch APIs: " +
                               std::string(sqlite3_errmsg(stmt.db())));
    }
    const std::string api_name = lower_ascii(resolved_name(stmt.get(), 4,
                                                            strings));
    if (api_name.find("cudagraphlaunch") == std::string::npos) {
      continue;
    }
    const std::uint64_t source_row_id =
        static_cast<std::uint64_t>(sqlite_i64(stmt.get(), 0));
    const auto task = task_ids.find(source_row_id);
    if (task == task_ids.end()) {
      continue;
    }
    out.push_back(CudaGraphLaunchEvidence{
        source_ref, source_row_id, sqlite_i64(stmt.get(), 3, -1),
        sqlite_i64(stmt.get(), 1), sqlite_i64(stmt.get(), 2)});
  }
}

void load_cuda_graph_kernel_children(
    SqliteDb& db, const NativeIr& ir,
    std::vector<CudaGraphChildEvidence>& out) {
  const SourceRefId source_ref = source_ref_for_table(ir, kKernelTable);
  if (!source_ref.valid()) {
    return;
  }
  const ColumnMap columns = table_columns(db, kKernelTable);
  const std::string context = find_column(columns, "contextId");
  const std::string correlation = find_column(columns, "correlationId");
  const std::string graph_node = find_column(columns, "graphNodeId");
  if (context.empty() || correlation.empty() || graph_node.empty()) {
    return;
  }
  const std::map<std::uint64_t, TaskId> task_ids =
      tasks_by_source_row(ir, source_ref);
  SqliteStmt stmt(
      db.get(), "SELECT rowid, start, end, deviceId, streamId, " +
                    quote_identifier(context) + ", " +
                    quote_identifier(correlation) + ", " +
                    quote_identifier(graph_node) + " FROM " +
                    quote_identifier(kKernelTable) + " WHERE " +
                    quote_identifier(correlation) + " IS NOT NULL AND " +
                    quote_identifier(graph_node) + " IS NOT NULL ORDER BY " +
                    "start, end, rowid");
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      return;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error("failed to load CUDA graph kernel children: " +
                               std::string(sqlite3_errmsg(stmt.db())));
    }
    const std::uint64_t source_row_id =
        static_cast<std::uint64_t>(sqlite_i64(stmt.get(), 0));
    const auto task = task_ids.find(source_row_id);
    if (task == task_ids.end()) {
      continue;
    }
    const TaskRow& task_row = ir.tasks.row(task->second);
    const std::string raw_label = task_row.op_name_symbol_id.valid()
                                      ? ir.symbols.value(
                                            task_row.op_name_symbol_id)
                                      : "cuda_kernel_" +
                                            std::to_string(source_row_id);
    const bool communication = task_row.comm_name_symbol_id.valid();
    out.push_back(CudaGraphChildEvidence{
        source_ref,
        task->second,
        task_row.trace_event_id,
        source_row_id,
        sqlite_i64(stmt.get(), 6, -1),
        sqlite_i64(stmt.get(), 7, -1),
        sqlite_i64(stmt.get(), 5, -1),
        checked_u32(sqlite_i64(stmt.get(), 3, -1), "deviceId",
                    source_row_id),
        checked_u32(sqlite_i64(stmt.get(), 4, -1), "streamId",
                    source_row_id),
        sqlite_i64(stmt.get(), 1),
        sqlite_i64(stmt.get(), 2),
        !communication,
        communication,
        std::string(communication ? "communication\t" : "kernel\t") +
            raw_label,
        raw_label});
  }
}

void load_cuda_graph_memcpy_children(
    SqliteDb& db, const NativeIr& ir,
    std::vector<CudaGraphChildEvidence>& out) {
  constexpr const char* table = "CUPTI_ACTIVITY_KIND_MEMCPY";
  const SourceRefId source_ref = source_ref_for_table(ir, table);
  if (!source_ref.valid()) {
    return;
  }
  const ColumnMap columns = table_columns(db, table);
  const std::string context = find_column(columns, "contextId");
  const std::string correlation = find_column(columns, "correlationId");
  const std::string graph_node = find_column(columns, "graphNodeId");
  const std::string bytes = find_column(columns, "bytes");
  const std::string copy_kind = find_column(columns, "copyKind");
  if (context.empty() || correlation.empty() || graph_node.empty() ||
      bytes.empty() || copy_kind.empty()) {
    return;
  }
  const std::map<std::uint64_t, TaskId> task_ids =
      tasks_by_source_row(ir, source_ref);
  SqliteStmt stmt(
      db.get(), "SELECT rowid, start, end, deviceId, streamId, " +
                    quote_identifier(context) + ", " +
                    quote_identifier(correlation) + ", " +
                    quote_identifier(graph_node) + ", " +
                    quote_identifier(bytes) + ", " +
                    quote_identifier(copy_kind) + " FROM " +
                    quote_identifier(table) + " WHERE " +
                    quote_identifier(correlation) + " IS NOT NULL AND " +
                    quote_identifier(graph_node) + " IS NOT NULL ORDER BY " +
                    "start, end, rowid");
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      return;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error("failed to load CUDA graph memcpy children: " +
                               std::string(sqlite3_errmsg(stmt.db())));
    }
    const std::uint64_t source_row_id =
        static_cast<std::uint64_t>(sqlite_i64(stmt.get(), 0));
    const auto task = task_ids.find(source_row_id);
    if (task == task_ids.end()) {
      continue;
    }
    const std::string label =
        "CudaMemcpy kind=" + std::to_string(sqlite_i64(stmt.get(), 9)) +
        " bytes=" + std::to_string(sqlite_i64(stmt.get(), 8));
    const TaskRow& task_row = ir.tasks.row(task->second);
    out.push_back(CudaGraphChildEvidence{
        source_ref,
        task->second,
        task_row.trace_event_id,
        source_row_id,
        sqlite_i64(stmt.get(), 6, -1),
        sqlite_i64(stmt.get(), 7, -1),
        sqlite_i64(stmt.get(), 5, -1),
        checked_u32(sqlite_i64(stmt.get(), 3, -1), "deviceId",
                    source_row_id),
        checked_u32(sqlite_i64(stmt.get(), 4, -1), "streamId",
                    source_row_id),
        sqlite_i64(stmt.get(), 1),
        sqlite_i64(stmt.get(), 2),
        false,
        false,
        "memcpy\t" + label,
        label});
  }
}

void prepare_cuda_graph_body(PreparedCudaGraphLaunch& prepared) {
  if (prepared.children.empty()) {
    prepared.failure_status = ReplayCompositionRegionStatus::
        kUnrecognizedMissingBodyEvidence;
    return;
  }
  std::sort(prepared.children.begin(), prepared.children.end(),
            [](const CudaGraphChildEvidence* lhs,
               const CudaGraphChildEvidence* rhs) {
              return std::tie(lhs->start_ns, lhs->end_ns, lhs->device_id,
                              lhs->stream_id, lhs->source_row_id) <
                     std::tie(rhs->start_ns, rhs->end_ns, rhs->device_id,
                              rhs->stream_id, rhs->source_row_id);
            });
  prepared.device_id = prepared.children.front()->device_id;
  const std::int64_t context_id = prepared.children.front()->context_id;
  prepared.body_start_ns = prepared.children.front()->start_ns;
  prepared.body_end_ns = prepared.children.front()->end_ns;
  prepared.first_task_id = prepared.children.front()->task_id;
  prepared.last_task_id = prepared.children.front()->task_id;
  std::set<std::uint32_t> stream_ids;
  std::set<std::int64_t> graph_node_ids;
  for (const CudaGraphChildEvidence* child : prepared.children) {
    if (child->device_id != prepared.device_id ||
        child->context_id != context_id || child->graph_node_id < 0 ||
        !child->task_id.valid() || !child->trace_event_id.valid()) {
      prepared.failure_status = ReplayCompositionRegionStatus::
          kUnrecognizedAmbiguousLaunchEvidence;
      return;
    }
    graph_node_ids.insert(child->graph_node_id);
    stream_ids.insert(child->stream_id);
    prepared.body_start_ns =
        std::min(prepared.body_start_ns, child->start_ns);
    prepared.body_end_ns = std::max(prepared.body_end_ns, child->end_ns);
    prepared.last_task_id = child->task_id;
    if (child->communication) {
      ++prepared.communication_task_count;
    } else if (child->compute) {
      ++prepared.compute_task_count;
    }
  }
  prepared.stream_count = static_cast<std::uint32_t>(stream_ids.size());
  prepared.identity_signature =
      "cuda_graph_raw_node_set_v1\ndevice=" +
      std::to_string(prepared.device_id) + "\ncontext=" +
      std::to_string(context_id) + "\n";
  for (std::int64_t graph_node_id : graph_node_ids) {
    prepared.identity_signature += std::to_string(graph_node_id) + "\n";
  }

  std::map<std::int64_t, std::uint32_t> node_ordinals;
  for (std::int64_t graph_node_id : graph_node_ids) {
    node_ordinals.emplace(
        graph_node_id, static_cast<std::uint32_t>(node_ordinals.size()));
  }

  std::map<std::uint32_t, std::vector<const CudaGraphChildEvidence*>> lanes;
  for (const CudaGraphChildEvidence* child : prepared.children) {
    lanes[child->stream_id].push_back(child);
  }
  std::vector<std::pair<std::string, std::string>> lane_sequences;
  for (const auto& lane : lanes) {
    std::string exact;
    std::string readable;
    for (const CudaGraphChildEvidence* child : lane.second) {
      exact += child->exact_label + "\tnode=" +
               std::to_string(node_ordinals.at(child->graph_node_id)) + "\n";
      if (!readable.empty()) {
        readable += "\n";
      }
      readable += child->readable_label;
    }
    lane_sequences.emplace_back(std::move(exact), std::move(readable));
  }
  std::sort(lane_sequences.begin(), lane_sequences.end());
  prepared.body_signature =
      "cuda_graph_observed_stream_set_v1\nstream_count=" +
      std::to_string(lane_sequences.size()) + "\n";
  for (std::size_t lane = 0; lane < lane_sequences.size(); ++lane) {
    prepared.body_signature +=
        "lane_begin\n" + lane_sequences[lane].first + "lane_end\n";
    if (!prepared.readable_body.empty()) {
      prepared.readable_body += "\n";
    }
    prepared.readable_body += "lane " + std::to_string(lane) + ":\n" +
                              lane_sequences[lane].second;
  }
}

void materialize_cuda_graph_node_replays(
    SqliteDb& db, NativeIr& ir, const CudaNsightSQLiteAdapterOptions& options,
    const std::unordered_map<std::int64_t, std::string>& strings) {
  const std::vector<std::string> tables = table_names(db);
  if (!has_name(tables, "CUDA_GRAPH_NODE_EVENTS") ||
      table_row_count(db, "CUDA_GRAPH_NODE_EVENTS") == 0) {
    return;
  }
  const bool body_capability_complete =
      graph_node_activity_capability_complete(db);
  const std::vector<CudaGraphLaunchEvidence> launches =
      load_cuda_graph_launch_evidence(db, ir, strings);
  if (launches.empty()) {
    return;
  }
  std::vector<CudaGraphChildEvidence> children;
  load_cuda_graph_kernel_children(db, ir, children);
  load_cuda_graph_memcpy_children(db, ir, children);

  std::map<std::int64_t, std::vector<const CudaGraphLaunchEvidence*>>
      launches_by_correlation;
  std::map<std::int64_t, std::vector<const CudaGraphChildEvidence*>>
      children_by_correlation;
  for (const CudaGraphLaunchEvidence& launch : launches) {
    launches_by_correlation[launch.correlation_id].push_back(&launch);
  }
  for (const CudaGraphChildEvidence& child : children) {
    children_by_correlation[child.correlation_id].push_back(&child);
  }

  std::vector<PreparedCudaGraphLaunch> prepared_launches;
  prepared_launches.reserve(launches.size());
  for (const CudaGraphLaunchEvidence& launch : launches) {
    PreparedCudaGraphLaunch prepared;
    prepared.launch = &launch;
    if (!body_capability_complete) {
      prepared.failure_status = ReplayCompositionRegionStatus::
          kUnrecognizedMissingBodyCapability;
    } else if (launches_by_correlation[launch.correlation_id].size() != 1) {
      prepared.failure_status = ReplayCompositionRegionStatus::
          kUnrecognizedAmbiguousLaunchEvidence;
    } else {
      prepared.children = children_by_correlation[launch.correlation_id];
      prepare_cuda_graph_body(prepared);
    }
    prepared_launches.push_back(std::move(prepared));
  }

  std::map<std::string, std::size_t> repeat_counts;
  std::map<std::string, std::set<std::string>> bodies_by_identity;
  for (const PreparedCudaGraphLaunch& prepared : prepared_launches) {
    if (prepared.failure_status ==
        ReplayCompositionRegionStatus::kRecognizedCompletePattern) {
      ++repeat_counts[prepared.identity_signature + prepared.body_signature];
      bodies_by_identity[prepared.identity_signature].insert(
          prepared.body_signature);
    }
  }
  static constexpr std::size_t kMinimumExactBodyRepeats = 2;
  std::map<std::string, ReplayBodyTemplateId> body_templates;
  std::map<std::string, GraphTemplateId> graph_templates;
  SourceRefId replay_source_ref = SourceRefId::invalid();

  for (PreparedCudaGraphLaunch& prepared : prepared_launches) {
    const CudaGraphLaunchEvidence& launch = *prepared.launch;
    const bool has_body = !prepared.body_signature.empty();
    if (prepared.failure_status ==
        ReplayCompositionRegionStatus::kRecognizedCompletePattern) {
      if (bodies_by_identity[prepared.identity_signature].size() != 1) {
        prepared.failure_status =
            ReplayCompositionRegionStatus::kUnrecognizedBodyMismatch;
      } else if (repeat_counts[prepared.identity_signature +
                               prepared.body_signature] <
                 kMinimumExactBodyRepeats) {
        prepared.failure_status = ReplayCompositionRegionStatus::
            kUnrecognizedInsufficientRepeatEvidence;
      }
    }

    ReplayBodyTemplateId body_template = ReplayBodyTemplateId::invalid();
    if (has_body) {
      const auto found = body_templates.find(prepared.body_signature);
      if (found == body_templates.end()) {
        body_template = ir.replay_body_templates.append(
            prepared.children.front()->source_ref_id,
            stable_hash64(prepared.body_signature),
            ir.symbols.intern(prepared.readable_body),
            prepared.compute_task_count, prepared.communication_task_count,
            prepared.stream_count,
            ReplayBodyTopologyPolicy::kObservedStreamSetUnordered);
        body_templates.emplace(prepared.body_signature, body_template);
      } else {
        body_template = found->second;
      }
    }

    const bool direct_correlation = has_body &&
        launches_by_correlation[launch.correlation_id].size() == 1;
    const std::uint32_t device_id =
        has_body ? prepared.device_id : 0;
    const StreamId execute_stream =
        has_body ? stream_for_raw_id(ir, device_id,
                                     prepared.children.front()->stream_id)
                 : StreamId::invalid();
    const std::int64_t start_ns =
        has_body ? prepared.body_start_ns : launch.start_ns;
    const std::int64_t end_ns =
        has_body ? prepared.body_end_ns : launch.end_ns;
    const GraphLaunchOccurrenceId occurrence =
        ir.graph_launch_occurrences.append(
            launch.source_ref_id, launch.source_ref_id, device_id,
            static_cast<std::int64_t>(launch.source_row_id),
            launch.correlation_id, -1, -1, execute_stream,
            StreamId::invalid(), CapturedGraphInstanceId::invalid(),
            TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), start_ns,
            end_ns, -1,
            direct_correlation
                ? GraphLaunchMatchPolicy::kCudaRuntimeCorrelation
                : GraphLaunchMatchPolicy::kUnmatched,
            has_body
                ? GraphLaunchInstanceAssociationPolicy::kCudaGraphNodeSet
                : GraphLaunchInstanceAssociationPolicy::kNone);
    if (has_body) {
      ir.graph_launch_bodies.append(
          occurrence, body_template, prepared.first_task_id,
          prepared.last_task_id, prepared.compute_task_count,
          prepared.communication_task_count, prepared.stream_count);
    }

    const bool recognized =
        prepared.failure_status ==
        ReplayCompositionRegionStatus::kRecognizedCompletePattern;
    const ReplayCompositionCandidateId candidate =
        ir.replay_composition_candidates.append(
            launch.source_ref_id, device_id, occurrence, occurrence, 1, 0,
            has_body ? 1 : 0, recognized ? 1 : 0, 0,
            stable_hash64(prepared.identity_signature +
                          prepared.body_signature),
            has_body ? ReplayCompositionIdentityPolicy::kCudaGraphNodeSet
                     : ReplayCompositionIdentityPolicy::kUnavailable,
            ReplayCompositionOrderPolicy::kHostSubmissionOrder,
            recognized ? ReplayCompositionShapePolicy::kSingleGraph
                       : ReplayCompositionShapePolicy::kUnclassified,
            ReplayCompositionBoundaryPolicy::kDirectObservedGraphLaunch);
    ReplayCompositionSlotId slot = ReplayCompositionSlotId::invalid();
    if (has_body) {
      slot = ir.replay_composition_slots.append(
          candidate, 0, CapturedGraphInstanceId::invalid(),
          GraphSlotTemplateId::invalid(), body_template,
          ReplayCompositionSlotRole::kCudaGraph, -1);
    }
    const ReplayCompositionRegionId region =
        ir.replay_composition_regions.append(
            candidate, 0, occurrence, occurrence, start_ns, end_ns, 1, 1,
            prepared.failure_status);
    ir.replay_composition_region_members.append(
        region, 0, occurrence, has_body ? 0 : -1);
    if (!recognized) {
      continue;
    }

    if (!replay_source_ref.valid()) {
      replay_source_ref = ir.source_refs.append(
          options.source_kind, options.db_path, "CUDA_GRAPH_REPLAY_UNIT", 0);
    }
    GraphTemplateId graph_template = GraphTemplateId::invalid();
    const auto graph_found = graph_templates.find(prepared.body_signature);
    if (graph_found == graph_templates.end()) {
      graph_template = ir.graph_templates.append(
          replay_source_ref, stable_hash64(prepared.body_signature), 1);
      graph_templates.emplace(prepared.body_signature, graph_template);
    } else {
      graph_template = graph_found->second;
    }
    const std::uint32_t raw_stream_id =
        prepared.children.front()->stream_id;
    const std::string symbol =
        "CUDAGraph ExactT" + std::to_string(graph_template.value() + 1);
    const TraceEventId event = ir.trace_events.append(
        replay_source_ref, region.value() + 1, device_id, raw_stream_id,
        start_ns, end_ns, ir.symbols.intern(symbol));
    const ReplayUnitId unit = ir.replay_units.append(
        graph_template, replay_source_ref, AnchorId::invalid(),
        AnchorId::invalid(), event, region);
    ir.replay_unit_launch_members.append(unit, 0, occurrence, slot);
  }
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
    const bool collective = is_nccl_collective_kernel(raw_label);
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
  materialize_cuda_graph_node_replays(db, ir, options_, strings);
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
