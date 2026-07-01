#include "traceloom/adapters/ascend_sqlite_adapter.h"

#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace traceloom {
namespace {

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
      throw std::runtime_error("failed to open Ascend SQLite DB: " + message);
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
  SqliteStmt(sqlite3* db, const char* sql) : db_(db) {
    sqlite3_stmt* raw = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql, -1, &raw, nullptr);
    stmt_ = raw;
    if (rc != SQLITE_OK) {
      const std::string message = sqlite3_errmsg(db_);
      throw std::runtime_error("failed to prepare Ascend SQLite inventory: " +
                               message);
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

struct ComputeInfo {
  SymbolId op_name_symbol_id = SymbolId::invalid();
  SymbolId op_type_symbol_id = SymbolId::invalid();
  SymbolId compute_task_type_symbol_id = SymbolId::invalid();
};

struct CommunicationTaskInfo {
  SymbolId comm_name_symbol_id = SymbolId::invalid();
  SymbolId task_type_symbol_id = SymbolId::invalid();
};

struct LinkedTaskStats {
  std::uint32_t linked_task_count = 0;
  std::uint32_t linked_stream_count = 0;
  std::uint64_t primary_stream_id = 0;
  bool has_primary_stream = false;
  SymbolId linked_task_name_symbol_id;
  SymbolId linked_task_type_symbol_id;
};

using StreamIndex = std::unordered_map<std::uint64_t, StreamId>;

struct TaskLink {
  std::uint64_t stream_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  SymbolId comm_name_symbol_id;
  SymbolId comm_task_type_symbol_id;
};

using TaskLinkIndex = std::unordered_map<std::uint64_t, std::vector<TaskLink>>;

std::int64_t sqlite_i64(sqlite3_stmt* stmt,
                        int column,
                        std::int64_t fallback = -1) {
  if (sqlite3_column_type(stmt, column) == SQLITE_NULL) {
    return fallback;
  }
  return sqlite3_column_int64(stmt, column);
}

std::uint32_t sqlite_u32(sqlite3_stmt* stmt, int column) {
  const std::int64_t value = sqlite_i64(stmt, column, 0);
  return value < 0 ? 0u : static_cast<std::uint32_t>(value);
}

std::uint64_t sqlite_u64(sqlite3_stmt* stmt, int column) {
  const std::int64_t value = sqlite_i64(stmt, column, 0);
  return value < 0 ? 0u : static_cast<std::uint64_t>(value);
}

std::string sqlite_text(sqlite3_stmt* stmt, int column) {
  const unsigned char* raw = sqlite3_column_text(stmt, column);
  if (raw == nullptr) {
    return "";
  }
  return reinterpret_cast<const char*>(raw);
}

std::string decode_string_id(
    const std::unordered_map<std::int64_t, std::string>& string_ids,
    std::int64_t raw_id) {
  const auto found = string_ids.find(raw_id);
  if (found != string_ids.end()) {
    return found->second;
  }
  return std::to_string(raw_id);
}

bool table_has_column(SqliteDb& db,
                      const std::string& table_name,
                      const std::string& column_name) {
  const std::string sql = "PRAGMA table_info(" + table_name + ")";
  SqliteStmt stmt(db.get(), sql.c_str());
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      if (sqlite_text(stmt.get(), 1) == column_name) {
        return true;
      }
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    throw std::runtime_error("failed to inspect Ascend SQLite table columns: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  return false;
}

std::unordered_map<std::int64_t, std::string> load_string_ids(SqliteDb& db,
                                                              NativeIr& ir) {
  static constexpr const char* kSql =
      "SELECT id, value FROM STRING_IDS ORDER BY id";
  SqliteStmt stmt(db.get(), kSql);
  std::unordered_map<std::int64_t, std::string> out;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      const std::string value = sqlite_text(stmt.get(), 1);
      ir.strings.intern(value);
      out.emplace(sqlite_i64(stmt.get(), 0), value);
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    throw std::runtime_error("failed to load STRING_IDS: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  return out;
}

SymbolId intern_optional_string_id(
    NativeIr& ir,
    const std::unordered_map<std::int64_t, std::string>& string_ids,
    sqlite3_stmt* stmt,
    int column) {
  if (sqlite3_column_type(stmt, column) == SQLITE_NULL) {
    return SymbolId::invalid();
  }
  return ir.symbols.intern(decode_string_id(string_ids, sqlite_i64(stmt, column)));
}

std::unordered_map<std::int64_t, ComputeInfo> load_compute_info(
    SqliteDb& db,
    NativeIr& ir,
    const std::unordered_map<std::int64_t, std::string>& string_ids) {
  static constexpr const char* kSql =
      "SELECT globalTaskId, name, opType, taskType "
      "FROM COMPUTE_TASK_INFO "
      "ORDER BY globalTaskId";
  SqliteStmt stmt(db.get(), kSql);
  std::unordered_map<std::int64_t, ComputeInfo> out;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      if (sqlite3_column_type(stmt.get(), 0) == SQLITE_NULL) {
        continue;
      }
      const std::int64_t global_task_id = sqlite_i64(stmt.get(), 0);
      ComputeInfo info;
      info.op_name_symbol_id =
          intern_optional_string_id(ir, string_ids, stmt.get(), 1);
      info.op_type_symbol_id =
          intern_optional_string_id(ir, string_ids, stmt.get(), 2);
      info.compute_task_type_symbol_id =
          intern_optional_string_id(ir, string_ids, stmt.get(), 3);
      out.emplace(global_task_id, info);
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    throw std::runtime_error("failed to load COMPUTE_TASK_INFO: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  return out;
}

std::unordered_map<std::int64_t, CommunicationTaskInfo>
load_communication_task_info(
    SqliteDb& db,
    NativeIr& ir,
    const std::unordered_map<std::int64_t, std::string>& string_ids,
    bool has_task_type_column) {
  const std::string sql =
      has_task_type_column
          ? "SELECT globalTaskId, MIN(name), MIN(taskType) "
            "FROM COMMUNICATION_TASK_INFO "
            "GROUP BY globalTaskId "
            "ORDER BY globalTaskId"
          : "SELECT globalTaskId, MIN(name), NULL "
            "FROM COMMUNICATION_TASK_INFO "
            "GROUP BY globalTaskId "
            "ORDER BY globalTaskId";
  SqliteStmt stmt(db.get(), sql.c_str());
  std::unordered_map<std::int64_t, CommunicationTaskInfo> out;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      if (sqlite3_column_type(stmt.get(), 0) == SQLITE_NULL) {
        continue;
      }
      CommunicationTaskInfo info;
      info.comm_name_symbol_id =
          intern_optional_string_id(ir, string_ids, stmt.get(), 1);
      info.task_type_symbol_id =
          intern_optional_string_id(ir, string_ids, stmt.get(), 2);
      out.emplace(sqlite_i64(stmt.get(), 0), info);
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    throw std::runtime_error("failed to load COMMUNICATION_TASK_INFO: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  return out;
}

std::uint64_t stream_key(std::uint32_t device_id, std::uint64_t stream_id) {
  return (static_cast<std::uint64_t>(device_id) << 32u) ^
         (stream_id & 0xffffffffu);
}

std::uint64_t connection_key(std::uint32_t device_id,
                             std::int64_t connection_id) {
  return (static_cast<std::uint64_t>(device_id) << 32u) ^
         (static_cast<std::uint64_t>(connection_id) & 0xffffffffu);
}

StreamId find_or_append_stream(StreamIndex& streams,
                               NativeIr& ir,
                               SourceRefId source_ref,
                               std::uint32_t device_id,
                               std::uint64_t raw_stream_id) {
  const std::uint64_t key = stream_key(device_id, raw_stream_id);
  const auto found = streams.find(key);
  if (found != streams.end()) {
    return found->second;
  }
  const StreamId stream = ir.streams.append(source_ref, device_id, raw_stream_id);
  streams.emplace(key, stream);
  return stream;
}

void load_task_rows(
    SqliteDb& db,
    NativeIr& ir,
    StreamIndex& streams,
    TaskLinkIndex& task_links,
    const std::unordered_map<std::int64_t, std::string>& string_ids,
    const std::unordered_map<std::int64_t, ComputeInfo>& compute_info,
    const std::unordered_map<std::int64_t, CommunicationTaskInfo>&
        communication_task_info,
    SourceRefId task_table_ref) {
  static constexpr const char* kSql =
      "SELECT rowid, startNs, endNs, deviceId, streamId, taskId, "
      "globalTaskId, connectionId, taskType "
      "FROM TASK "
      "ORDER BY deviceId, streamId, startNs, endNs, globalTaskId, taskId";
  SqliteStmt stmt(db.get(), kSql);
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      const std::uint64_t row_id = sqlite_u64(stmt.get(), 0);
      const std::int64_t start_ns = sqlite_i64(stmt.get(), 1, 0);
      const std::int64_t end_ns = sqlite_i64(stmt.get(), 2, 0);
      const std::uint32_t device_id = sqlite_u32(stmt.get(), 3);
      const std::uint64_t raw_stream_id = sqlite_u64(stmt.get(), 4);
      const std::uint64_t raw_task_id = sqlite_u64(stmt.get(), 5);
      const std::int64_t raw_global_task_id = sqlite_i64(stmt.get(), 6);
      const std::int64_t raw_connection_id = sqlite_i64(stmt.get(), 7);
      const std::int64_t raw_task_type_id = sqlite_i64(stmt.get(), 8);
      const SymbolId task_type_symbol =
          ir.symbols.intern(decode_string_id(string_ids, raw_task_type_id));
      const auto compute_found = compute_info.find(raw_global_task_id);
      const ComputeInfo compute =
          compute_found == compute_info.end() ? ComputeInfo()
                                              : compute_found->second;
      const auto comm_task_found =
          communication_task_info.find(raw_global_task_id);
      const CommunicationTaskInfo comm_task =
          comm_task_found == communication_task_info.end()
              ? CommunicationTaskInfo()
              : comm_task_found->second;
      task_links[connection_key(device_id, raw_connection_id)].push_back(
          TaskLink{raw_stream_id, start_ns, end_ns, comm_task.comm_name_symbol_id,
                   comm_task.task_type_symbol_id});

      const StreamId stream =
          find_or_append_stream(streams, ir, task_table_ref, device_id,
                                raw_stream_id);

      const TraceEventId event =
          ir.trace_events.append(task_table_ref, row_id, device_id,
                                 ir.streams.row(stream).raw_stream_id,
                                 start_ns, end_ns, task_type_symbol);
      ir.tasks.append(task_table_ref, event, raw_task_id, raw_global_task_id,
                      raw_connection_id, task_type_symbol,
                      compute.op_name_symbol_id, compute.op_type_symbol_id,
                      compute.compute_task_type_symbol_id,
                      comm_task.comm_name_symbol_id);
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    throw std::runtime_error("failed to load TASK rows: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
}

LinkedTaskStats linked_task_stats_from_index(const TaskLinkIndex& task_links,
                                             std::uint32_t device_id,
                                             std::int64_t connection_id,
                                             std::int64_t start_ns,
                                             std::int64_t end_ns) {
  LinkedTaskStats stats;
  std::unordered_map<std::uint64_t, std::uint64_t> duration_by_stream;
  std::unordered_set<std::uint64_t> streams;
  const auto found = task_links.find(connection_key(device_id, connection_id));
  if (found == task_links.end()) {
    return stats;
  }
  for (const TaskLink& task : found->second) {
    if (task.start_ns <= end_ns && task.end_ns >= start_ns) {
      ++stats.linked_task_count;
      streams.insert(task.stream_id);
      duration_by_stream[task.stream_id] +=
          static_cast<std::uint64_t>(std::max<std::int64_t>(
              0, std::min(task.end_ns, end_ns) -
                     std::max(task.start_ns, start_ns)));
      if (!stats.linked_task_name_symbol_id.valid() &&
          task.comm_name_symbol_id.valid()) {
        stats.linked_task_name_symbol_id = task.comm_name_symbol_id;
      }
      if (!stats.linked_task_type_symbol_id.valid() &&
          task.comm_task_type_symbol_id.valid()) {
        stats.linked_task_type_symbol_id = task.comm_task_type_symbol_id;
      }
    }
  }

  stats.linked_stream_count = static_cast<std::uint32_t>(streams.size());
  for (const auto& item : duration_by_stream) {
    if (!stats.has_primary_stream ||
        item.second > duration_by_stream[stats.primary_stream_id]) {
      stats.primary_stream_id = item.first;
      stats.has_primary_stream = true;
    }
  }
  return stats;
}

void load_communication_op_rows(
    SqliteDb& db,
    NativeIr& ir,
    StreamIndex& streams,
    const TaskLinkIndex& task_links,
    const std::unordered_map<std::int64_t, std::string>& string_ids,
    SourceRefId comm_table_ref,
    bool has_op_type_column) {
  const std::string sql =
      has_op_type_column
          ? "SELECT rowid, opName, opType, startNs, endNs, deviceId, "
            "connectionId, opId "
            "FROM COMMUNICATION_OP "
            "WHERE startNs IS NOT NULL AND endNs IS NOT NULL AND endNs > "
            "startNs "
            "ORDER BY deviceId, startNs, endNs, connectionId"
          : "SELECT rowid, opName, NULL AS opType, startNs, endNs, deviceId, "
            "connectionId, opId "
            "FROM COMMUNICATION_OP "
            "WHERE startNs IS NOT NULL AND endNs IS NOT NULL AND endNs > "
            "startNs "
            "ORDER BY deviceId, startNs, endNs, connectionId";
  SqliteStmt stmt(db.get(), sql.c_str());
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      const std::uint64_t row_id = sqlite_u64(stmt.get(), 0);
      const SymbolId op_name_symbol =
          intern_optional_string_id(ir, string_ids, stmt.get(), 1);
      const SymbolId op_type_symbol =
          intern_optional_string_id(ir, string_ids, stmt.get(), 2);
      const std::int64_t start_ns = sqlite_i64(stmt.get(), 3, 0);
      const std::int64_t end_ns = sqlite_i64(stmt.get(), 4, 0);
      const std::uint32_t device_id = sqlite_u32(stmt.get(), 5);
      const std::int64_t connection_id = sqlite_i64(stmt.get(), 6);
      const std::int64_t op_id = sqlite_i64(stmt.get(), 7);
      const LinkedTaskStats linked =
          linked_task_stats_from_index(task_links, device_id, connection_id,
                                       start_ns, end_ns);

      std::uint64_t raw_stream_id =
          static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
      if (linked.has_primary_stream) {
        raw_stream_id = linked.primary_stream_id;
        (void)find_or_append_stream(streams, ir, comm_table_ref, device_id,
                                    raw_stream_id);
      }

      const TraceEventId event =
          ir.trace_events.append(comm_table_ref, row_id, device_id,
                                 raw_stream_id, start_ns, end_ns,
                                 op_name_symbol);
      ir.communication_ops.append(comm_table_ref, event, connection_id, op_id,
                                  linked.linked_task_count,
                                  linked.linked_stream_count, op_name_symbol,
                                  op_type_symbol,
                                  linked.linked_task_name_symbol_id,
                                  linked.linked_task_type_symbol_id);
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    throw std::runtime_error("failed to load COMMUNICATION_OP rows: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
}

}  // namespace

AscendSQLiteAdapter::AscendSQLiteAdapter(AscendSQLiteAdapterOptions options)
    : options_(std::move(options)) {}

AscendSQLiteAdapter::AscendSQLiteAdapter(std::string db_path,
                                         std::string source_kind)
    : options_(AscendSQLiteAdapterOptions{std::move(db_path),
                                          std::move(source_kind)}) {}

NativeIr AscendSQLiteAdapter::load() const {
  if (options_.db_path.empty()) {
    throw std::invalid_argument("Ascend SQLite DB path is empty");
  }
  if (!file_exists(options_.db_path)) {
    throw std::invalid_argument("Ascend SQLite DB does not exist: " +
                                options_.db_path);
  }

  SqliteDb db(options_.db_path);
  NativeIr ir;

  static constexpr const char* kInventorySql =
      "SELECT name FROM sqlite_master "
      "WHERE type IN ('table', 'view') "
      "ORDER BY name";

  SqliteStmt stmt(db.get(), kInventorySql);
  bool saw_schema_object = false;
  std::unordered_map<std::string, SourceRefId> table_refs;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      const std::string table_name = sqlite_text(stmt.get(), 0);
      const SourceRefId source_ref = ir.source_refs.append(
          options_.source_kind, options_.db_path, table_name, 0);
      table_refs.emplace(table_name, source_ref);
      saw_schema_object = true;
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }

    const std::string message = sqlite3_errmsg(stmt.db());
    throw std::runtime_error("failed to read Ascend SQLite inventory: " +
                             message);
  }

  if (!saw_schema_object) {
    ir.source_refs.append(options_.source_kind, options_.db_path,
                          "sqlite_schema", 0);
  }

  const std::unordered_map<std::int64_t, std::string> string_ids =
      table_refs.find("STRING_IDS") == table_refs.end()
          ? std::unordered_map<std::int64_t, std::string>()
          : load_string_ids(db, ir);
  const std::unordered_map<std::int64_t, ComputeInfo> compute_info =
      table_refs.find("COMPUTE_TASK_INFO") == table_refs.end()
          ? std::unordered_map<std::int64_t, ComputeInfo>()
          : load_compute_info(db, ir, string_ids);
  const std::unordered_map<std::int64_t, CommunicationTaskInfo>
      communication_task_info =
          table_refs.find("COMMUNICATION_TASK_INFO") == table_refs.end()
              ? std::unordered_map<std::int64_t, CommunicationTaskInfo>()
              : load_communication_task_info(
                    db, ir, string_ids,
                    table_has_column(db, "COMMUNICATION_TASK_INFO",
                                     "taskType"));
  StreamIndex streams;
  TaskLinkIndex task_links;
  if (table_refs.find("TASK") != table_refs.end()) {
    load_task_rows(db, ir, streams, task_links, string_ids, compute_info,
                   communication_task_info, table_refs.at("TASK"));
  }
  if (table_refs.find("COMMUNICATION_OP") != table_refs.end()) {
    load_communication_op_rows(db, ir, streams, task_links, string_ids,
                               table_refs.at("COMMUNICATION_OP"),
                               table_has_column(db, "COMMUNICATION_OP",
                                                "opType"));
  }

  return ir;
}

}  // namespace traceloom
