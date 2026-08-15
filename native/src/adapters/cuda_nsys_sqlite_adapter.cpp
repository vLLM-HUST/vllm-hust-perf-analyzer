#include "traceloom/adapters/cuda_nsys_sqlite_adapter.h"

#include "cuda_nsys_sqlite_internal.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace traceloom {
namespace cuda_nsys_sqlite_detail {

constexpr const char* kSyncTypeTable = "ENUM_CUPTI_SYNC_TYPE";

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
  const bool has_sync_activity =
      has_name(inventory.present_activity_tables,
               "CUPTI_ACTIVITY_KIND_SYNCHRONIZATION") &&
      table_row_count(db, "CUPTI_ACTIVITY_KIND_SYNCHRONIZATION") > 0;
  const std::unordered_map<std::int64_t, std::string> sync_types =
      has_sync_activity ? load_optional_enum_names(db, kSyncTypeTable)
                        : std::unordered_map<std::int64_t, std::string>{};
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
    const std::string global_tid = find_column(columns, "globalTid");
    const std::string process_id =
        first_column(columns, {"processId", "pid"});
    const std::string thread_id = first_column(columns, {"threadId", "tid"});
    const std::string context_id = find_column(columns, "contextId");
    const std::string sql =
        "SELECT rowid, " + quote_identifier(start) + ", " +
        select_or_null(end) + ", " + select_or_null(device) + ", " +
        select_or_null(stream) + ", " + select_or_null(correlation) + ", " +
        select_or_null(name) + ", " + select_or_null(global_tid) + ", " +
        select_or_null(process_id) + ", " + select_or_null(thread_id) + ", " +
        select_or_null(context_id) + " FROM " + quote_identifier(spec.table) +
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
      const bool row_has_device_id =
          !device.empty() && sqlite3_column_type(stmt.get(), 3) != SQLITE_NULL &&
          sqlite_i64(stmt.get(), 3, -1) >= 0;
      const std::uint32_t stream_id = static_cast<std::uint32_t>(
          std::max<std::int64_t>(0, sqlite_i64(stmt.get(), 4, 0)));
      const std::int64_t correlation_id = sqlite_i64(stmt.get(), 5, -1);
      std::string label =
          std::string(spec.table) == "CUPTI_ACTIVITY_KIND_SYNCHRONIZATION"
              ? resolved_sync_type(stmt.get(), 6, sync_types)
              : resolved_name(stmt.get(), 6, strings);
      if (label.empty()) {
        label = std::string(spec.fallback_label) + "_" +
                std::to_string(source_row_id);
      }
      const std::int64_t raw_global_tid = sqlite_i64(stmt.get(), 7, -1);
      std::int64_t raw_process_id = sqlite_i64(stmt.get(), 8, -1);
      std::int64_t raw_thread_id = sqlite_i64(stmt.get(), 9, -1);
      const std::int64_t raw_context_id = sqlite_i64(stmt.get(), 10, -1);
      if (raw_global_tid >= 0) {
        const std::uint64_t packed =
            static_cast<std::uint64_t>(raw_global_tid);
        if (raw_process_id < 0 && (packed >> 32u) != 0) {
          raw_process_id = static_cast<std::int64_t>(packed >> 32u);
        }
        if (raw_thread_id < 0) {
          raw_thread_id = static_cast<std::int64_t>(packed & 0xffffffffULL);
        }
      }
      intern_stream(ir, streams, source_ref, device_id, stream_id);
      const SymbolId label_symbol = ir.symbols.intern(label);
      const SymbolId task_type_symbol = ir.symbols.intern(spec.task_type);
      if (std::string(spec.table) == "CUPTI_ACTIVITY_KIND_RUNTIME") {
        ir.runtime_calls.append(
            source_ref, source_row_id, RuntimeCallProvider::kCuda,
            RuntimeCallClockDomain::kProfilerHost,
            RuntimeCallMatchPolicy::kCudaCorrelationId, start_ns, end_ns,
            correlation_id, ir.symbols.intern("cuda_runtime"), label_symbol,
            raw_process_id, raw_thread_id, raw_global_tid, raw_context_id,
            row_has_device_id, device_id);
      }
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

}  // namespace cuda_nsys_sqlite_detail

namespace detail = cuda_nsys_sqlite_detail;

CudaNsightSQLiteInventory inspect_cuda_nsys_sqlite_profile(
    const std::string& db_path) {
  if (db_path.empty()) {
    throw std::invalid_argument("CUDA/Nsight SQLite DB path is empty");
  }
  if (!detail::file_exists(db_path)) {
    throw std::invalid_argument("CUDA/Nsight SQLite DB does not exist: " +
                                db_path);
  }
  detail::SqliteDb db(db_path);
  return detail::inspect_inventory(db);
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
  if (!detail::file_exists(options_.db_path)) {
    throw std::invalid_argument("CUDA/Nsight SQLite DB does not exist: " +
                                options_.db_path);
  }

  detail::CudaLoadTiming timing;
  const detail::Stopwatch open_watch;
  detail::SqliteDb db(options_.db_path);
  timing.sqlite_open_ms = open_watch.elapsed_ms();

  const detail::Stopwatch inventory_watch;
  const CudaNsightSQLiteInventory inventory = detail::inspect_inventory(db);
  timing.inventory_ms = inventory_watch.elapsed_ms();
  if (!inventory.has_kernel_table) {
    throw std::runtime_error(
        "unsupported CUDA/Nsight SQLite export: required table " +
        std::string(detail::kKernelTable) + " is missing");
  }
  if (!inventory.missing_required_kernel_columns.empty()) {
    throw std::runtime_error(
        "unsupported CUDA/Nsight kernel schema: missing required column(s): " +
        detail::join(inventory.missing_required_kernel_columns, ", "));
  }

  NativeIr ir;
  const SourceRefId kernel_source_ref = ir.source_refs.append(
      options_.source_kind, options_.db_path, detail::kKernelTable, 0);

  std::unordered_map<std::int64_t, std::string> strings;
  const detail::Stopwatch strings_watch;
  if (inventory.has_string_ids_table) {
    strings = detail::load_string_ids(db);
  }
  timing.strings_ms = strings_watch.elapsed_ms();

  const detail::Stopwatch kernels_watch;
  detail::load_kernel_rows(db, ir, kernel_source_ref, strings);
  timing.kernels_ms = kernels_watch.elapsed_ms();

  const detail::Stopwatch auxiliary_watch;
  detail::load_auxiliary_activity_rows(db, ir, options_, inventory, strings);
  timing.auxiliary_ms = auxiliary_watch.elapsed_ms();

  const detail::Stopwatch graph_trace_watch;
  detail::materialize_cuda_graph_node_replays(db, ir, options_, strings);
  detail::load_graph_trace_rows(db, ir, options_, inventory);
  timing.graph_trace_ms = graph_trace_watch.elapsed_ms();

  if (options_.timing_diagnostics) {
    detail::print_load_timing(timing);
    for (const std::string& table : inventory.present_activity_tables) {
      std::cerr << "cuda_nsys_activity_table=" << table << "\n";
    }
  }
  return ir;
}

}  // namespace traceloom
