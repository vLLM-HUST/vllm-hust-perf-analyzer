#include "ascend_sqlite_internal.h"

#include "traceloom/analysis/exact_periodic_suffix.h"
#include "traceloom/runtime/thread_pool.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace traceloom::ascend_sqlite_detail {
std::vector<std::string> split_sqlite_db_paths(const std::string& profile_dir) {
  namespace fs = std::filesystem;
  std::vector<std::string> paths;
  std::error_code ec;
  const fs::path root(profile_dir);
  if (!fs::is_directory(root, ec)) {
    return paths;
  }
  fs::recursive_directory_iterator iterator(
      root, fs::directory_options::skip_permission_denied, ec);
  const fs::recursive_directory_iterator end;
  while (!ec && iterator != end) {
    const fs::path path = iterator->path();
    if (iterator->is_regular_file(ec) && path.extension() == ".db" &&
        path.parent_path().filename() == "sqlite") {
      paths.push_back(path.string());
    }
    iterator.increment(ec);
  }
  std::sort(paths.begin(), paths.end());
  return paths;
}

std::vector<AscendSplitSQLiteTableInfo> inventory_split_profile_impl(
    const std::string& profile_dir) {
  std::vector<AscendSplitSQLiteTableInfo> inventory;
  for (const std::string& path : split_sqlite_db_paths(profile_dir)) {
    SqliteDb db(path);
    SqliteStmt tables(
        db.get(),
        "SELECT name, COALESCE(sql, '') FROM sqlite_master "
        "WHERE type = 'table' AND name NOT LIKE 'sqlite_%' ORDER BY name");
    while (true) {
      const int rc = sqlite3_step(tables.get());
      if (rc == SQLITE_DONE) {
        break;
      }
      if (rc != SQLITE_ROW) {
        throw std::runtime_error("failed to inventory split SQLite DB: " +
                                 path + ": " + sqlite3_errmsg(tables.db()));
      }
      AscendSplitSQLiteTableInfo info;
      info.db_path = path;
      info.table_name = sqlite_text(tables.get(), 0);
      info.create_sql = sqlite_text(tables.get(), 1);
      const std::string count_sql =
          "SELECT COUNT(*) FROM " + quote_identifier(info.table_name);
      SqliteStmt count(db.get(), count_sql.c_str());
      if (sqlite3_step(count.get()) == SQLITE_ROW) {
        info.row_count = sqlite_u64(count.get(), 0);
      }
      inventory.push_back(std::move(info));
    }
  }
  return inventory;
}

const AscendSplitSQLiteTableInfo* find_split_table(
    const std::vector<AscendSplitSQLiteTableInfo>& inventory,
    const std::string& table_name) {
  const auto found = std::find_if(
      inventory.begin(), inventory.end(), [&](const auto& info) {
        return info.table_name == table_name;
      });
  return found == inventory.end() ? nullptr : &*found;
}

std::vector<const AscendSplitSQLiteTableInfo*> find_split_tables(
    const std::vector<AscendSplitSQLiteTableInfo>& inventory,
    const std::string& table_name) {
  std::vector<const AscendSplitSQLiteTableInfo*> out;
  for (const auto& info : inventory) {
    if (info.table_name == table_name) {
      out.push_back(&info);
    }
  }
  return out;
}

bool split_table_has_columns(
    const AscendSplitSQLiteTableInfo* table,
    std::initializer_list<const char*> columns) {
  if (table == nullptr) {
    return false;
  }
  SqliteDb db(table->db_path);
  return table_has_columns(db, table->table_name, columns);
}

std::vector<const AscendSplitSQLiteTableInfo*> usable_split_tables(
    const std::vector<const AscendSplitSQLiteTableInfo*>& tables,
    std::initializer_list<const char*> columns) {
  std::vector<const AscendSplitSQLiteTableInfo*> out;
  for (const AscendSplitSQLiteTableInfo* table : tables) {
    if (split_table_has_columns(table, columns)) {
      out.push_back(table);
    }
  }
  return out;
}

struct SplitTaskKey {
  std::uint32_t device_id = 0;
  std::uint64_t stream_id = 0;
  std::uint64_t task_id = 0;
  std::uint64_t context_id = 0;

  bool operator==(const SplitTaskKey& other) const noexcept {
    return device_id == other.device_id && stream_id == other.stream_id &&
           task_id == other.task_id && context_id == other.context_id;
  }
};

struct SplitTaskKeyHash {
  std::size_t operator()(const SplitTaskKey& key) const noexcept {
    std::size_t hash = key.device_id;
    hash = hash * 1315423911u + static_cast<std::size_t>(key.stream_id);
    hash = hash * 1315423911u + static_cast<std::size_t>(key.task_id);
    hash = hash * 1315423911u + static_cast<std::size_t>(key.context_id);
    return hash;
  }
};

struct SplitComputeInfo {
  ComputeInfo symbols;
  std::int64_t synthetic_global_task_id = -1;
};

using SplitComputeIndex =
    std::unordered_map<SplitTaskKey, SplitComputeInfo, SplitTaskKeyHash>;

struct SplitCommunicationInfo {
  SymbolId comm_name_symbol_id;
  SymbolId task_type_symbol_id;
};

using SplitCommunicationIndex =
    std::unordered_map<SplitTaskKey, SplitCommunicationInfo, SplitTaskKeyHash>;

std::uint32_t split_device_id_from_path(const std::string& db_path);

SplitComputeIndex load_split_task_info(
    const AscendSplitSQLiteTableInfo* table,
    NativeIr& ir) {
  SplitComputeIndex out;
  if (table == nullptr) {
    return out;
  }
  SqliteDb db(table->db_path);
  SqliteStmt stmt(
      db.get(),
      "SELECT rowid, device_id, stream_id, task_id, context_id, op_name, "
      "op_type, task_type FROM TaskInfo ORDER BY rowid");
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error("failed to load split TaskInfo: " +
                               std::string(sqlite3_errmsg(stmt.db())));
    }
    SplitComputeInfo info;
    info.synthetic_global_task_id = sqlite_i64(stmt.get(), 0, 0) - 1;
    info.symbols.op_name_symbol_id =
        ir.symbols.intern(sqlite_text(stmt.get(), 5));
    info.symbols.op_type_symbol_id =
        ir.symbols.intern(sqlite_text(stmt.get(), 6));
    info.symbols.compute_task_type_symbol_id =
        ir.symbols.intern(sqlite_text(stmt.get(), 7));
    out.emplace(SplitTaskKey{sqlite_u32(stmt.get(), 1),
                             sqlite_u64(stmt.get(), 2),
                             sqlite_u64(stmt.get(), 3),
                             sqlite_u64(stmt.get(), 4)},
                info);
  }
  return out;
}

SplitCommunicationIndex load_split_communication_task_info(
    const std::vector<const AscendSplitSQLiteTableInfo*>& tables,
    NativeIr& ir) {
  SplitCommunicationIndex out;
  for (const AscendSplitSQLiteTableInfo* table : tables) {
    const std::uint32_t device_id = split_device_id_from_path(table->db_path);
    SqliteDb db(table->db_path);
    SqliteStmt stmt(
        db.get(),
        "SELECT stream_id, task_id, context_id, op_name, hccl_name "
        "FROM HCCLTaskSingleDevice ORDER BY stream_id, task_id, context_id");
    while (true) {
      const int rc = sqlite3_step(stmt.get());
      if (rc == SQLITE_DONE) {
        break;
      }
      if (rc != SQLITE_ROW) {
        throw std::runtime_error(
            "failed to load split HCCLTaskSingleDevice: " +
            std::string(sqlite3_errmsg(stmt.db())));
      }
      out.emplace(
          SplitTaskKey{device_id, sqlite_u64(stmt.get(), 0),
                       sqlite_u64(stmt.get(), 1), sqlite_u64(stmt.get(), 2)},
          SplitCommunicationInfo{
              ir.symbols.intern(sqlite_text(stmt.get(), 3)),
              ir.symbols.intern(sqlite_text(stmt.get(), 4))});
    }
  }
  return out;
}

std::unordered_map<std::int64_t, std::string> load_split_host_task_types(
    const AscendSplitSQLiteTableInfo* table) {
  std::unordered_map<std::int64_t, std::string> out;
  if (table == nullptr) {
    return out;
  }
  SqliteDb db(table->db_path);
  SqliteStmt stmt(db.get(),
                  "SELECT connection_id, task_type FROM HostTask "
                  "WHERE connection_id IS NOT NULL ORDER BY rowid");
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error("failed to load split HostTask: " +
                               std::string(sqlite3_errmsg(stmt.db())));
    }
    out.emplace(sqlite_i64(stmt.get(), 0), sqlite_text(stmt.get(), 1));
  }
  return out;
}

AclGraphCannApiMetadata load_split_aclgraph_api_metadata(
    const AscendSplitSQLiteTableInfo* table) {
  AclGraphCannApiMetadata metadata;
  if (table == nullptr) {
    return metadata;
  }
  SqliteDb db(table->db_path);
  const bool has_thread_id = table_has_column(db, "ApiData", "thread_id");
  const std::string sql =
      "SELECT rowid, start, end, connection_id, id, " +
      std::string(has_thread_id ? "thread_id " : "0 ") +
      "FROM ApiData "
      "WHERE start IS NOT NULL AND end IS NOT NULL AND end > start "
      "ORDER BY start, end, rowid";
  SqliteStmt stmt(db.get(), sql.c_str());
  std::vector<CaptureInterval> begin_markers;
  std::vector<CaptureInterval> end_markers;
  std::vector<CaptureTokenCandidate> token_candidates;
  std::vector<HostBlockingSyncView> blocking_syncs;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error("failed to load split ApiData: " +
                               std::string(sqlite3_errmsg(stmt.db())));
    }
    const std::int64_t row_id = sqlite_i64(stmt.get(), 0, -1);
    const std::int64_t start_ns = sqlite_i64(stmt.get(), 1, 0) * 10;
    const std::int64_t end_ns = sqlite_i64(stmt.get(), 2, 0) * 10;
    const std::int64_t connection_id = sqlite_i64(stmt.get(), 3, 0);
    const std::string name = sqlite_text(stmt.get(), 4);
    const std::uint64_t global_tid = sqlite_u64(stmt.get(), 5);
    if (name == "aclmdlRIExecuteAsync") {
      metadata.execute_launches.push_back(
          GraphLaunchView{row_id, start_ns, end_ns, connection_id,
                          global_tid});
    } else if (name == "aclmdlRICaptureBegin") {
      begin_markers.push_back(CaptureInterval{start_ns, end_ns});
    } else if (name == "aclmdlRICaptureEnd") {
      end_markers.push_back(CaptureInterval{start_ns, end_ns});
    }
    if (name == "aclrtSynchronizeStreamWithTimeout" ||
        name == "aclrtSynchronizeStream" ||
        name == "aclrtSynchronizeDeviceWithTimeout" ||
        name == "aclrtSynchronizeDevice") {
      blocking_syncs.push_back(HostBlockingSyncView{
          row_id, start_ns, end_ns, global_tid, name});
    }
    const std::string token = capture_host_api_token(name);
    if (!token.empty()) {
      token_candidates.push_back(
          CaptureTokenCandidate{start_ns, end_ns, token});
    }
  }
  const std::vector<CaptureInterval> intervals =
      build_capture_intervals(std::move(begin_markers), std::move(end_markers));
  metadata.capture_slots =
      build_aclgraph_capture_slots(intervals, std::move(token_candidates));
  if (has_thread_id) {
    metadata.launch_activities = build_graph_launch_activities(
        metadata.execute_launches, blocking_syncs);
  }
  return metadata;
}

void load_split_ascend_runtime_calls(
    const AscendSplitSQLiteTableInfo* table, SourceRefId source_ref,
    NativeIr& ir) {
  if (table == nullptr || !source_ref.valid()) {
    return;
  }
  SqliteDb db(table->db_path);
  const bool has_thread_id = table_has_column(db, "ApiData", "thread_id");
  const bool has_process_id = table_has_column(db, "ApiData", "process_id");
  const bool has_device_id = table_has_column(db, "ApiData", "device_id");
  const bool has_context_id = table_has_column(db, "ApiData", "context_id");
  const bool has_type = table_has_column(db, "ApiData", "type");
  const std::string sql =
      "SELECT rowid, start, end, connection_id, id, " +
      std::string(has_thread_id ? "thread_id, " : "-1, ") +
      std::string(has_process_id ? "process_id, " : "-1, ") +
      std::string(has_device_id ? "device_id, " : "NULL, ") +
      std::string(has_context_id ? "context_id, " : "-1, ") +
      std::string(has_type ? "type " : "NULL ") +
      "FROM ApiData WHERE start IS NOT NULL AND end IS NOT NULL "
      "AND end > start ORDER BY start, end, rowid";
  SqliteStmt stmt(db.get(), sql.c_str());
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      return;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error("failed to load split ApiData runtime calls: " +
                               std::string(sqlite3_errmsg(stmt.db())));
    }
    // Split profiler timestamps use 10 ns ticks, matching the existing
    // ApiData graph-evidence loader.
    const std::int64_t start_ns = sqlite_i64(stmt.get(), 1, 0) * 10;
    const std::int64_t end_ns = sqlite_i64(stmt.get(), 2, 0) * 10;
    const std::int64_t raw_thread_id = sqlite_i64(stmt.get(), 5, -1);
    const std::int64_t raw_process_id = sqlite_i64(stmt.get(), 6, -1);
    const bool row_has_device_id =
        has_device_id && sqlite3_column_type(stmt.get(), 7) != SQLITE_NULL &&
        sqlite_i64(stmt.get(), 7, -1) >= 0;
    const std::string api_type =
        sqlite3_column_type(stmt.get(), 9) == SQLITE_NULL
            ? std::string("cann_api")
            : sqlite_text(stmt.get(), 9);
    ir.runtime_calls.append(
        source_ref, sqlite_u64(stmt.get(), 0), RuntimeCallProvider::kAscend,
        RuntimeCallClockDomain::kProfilerHost,
        RuntimeCallMatchPolicy::kAscendConnectionId, start_ns, end_ns,
        sqlite_i64(stmt.get(), 3, -1), ir.symbols.intern(api_type),
        ir.symbols.intern(sqlite_text(stmt.get(), 4)), raw_process_id,
        raw_thread_id, -1, sqlite_i64(stmt.get(), 8, -1), row_has_device_id,
        row_has_device_id ? sqlite_u32(stmt.get(), 7) : 0);
  }
}

struct SplitRawTask {
  RawTaskRow row;
  SourceRefId source_ref;
  SymbolId task_type_symbol;
  SplitComputeInfo compute;
  SplitCommunicationInfo communication;
};

struct SplitCommunicationEnvelope {
  std::int64_t start_ns = std::numeric_limits<std::int64_t>::max();
  std::int64_t end_ns = std::numeric_limits<std::int64_t>::min();
  std::uint32_t linked_task_count = 0;
  std::map<std::uint64_t, std::uint64_t> duration_by_stream;
  SymbolId linked_task_name_symbol_id;
  SymbolId linked_task_type_symbol_id;
  bool task_name_consistent = true;
};

using SplitCommunicationEnvelopeIndex =
    std::unordered_map<std::uint64_t, SplitCommunicationEnvelope>;

struct SplitCommunicationOpEvidence {
  SourceRefId source_ref;
  std::uint64_t source_row_id = 0;
  std::uint32_t device_id = 0;
  std::int64_t connection_id = -1;
  std::int64_t raw_op_id = -1;
  SymbolId op_name_symbol_id;
  SymbolId op_type_symbol_id;
};

std::uint32_t split_device_id_from_path(const std::string& db_path) {
  const std::string directory = std::filesystem::path(db_path)
                                    .parent_path()
                                    .parent_path()
                                    .filename()
                                    .string();
  static const std::string prefix = "device_";
  if (directory.rfind(prefix, 0) != 0) {
    return 0;
  }
  try {
    return static_cast<std::uint32_t>(
        std::stoul(directory.substr(prefix.size())));
  } catch (const std::exception&) {
    return 0;
  }
}

SplitCommunicationEnvelopeIndex build_split_communication_envelopes(
    const std::vector<SplitRawTask>& raw_tasks) {
  SplitCommunicationEnvelopeIndex out;
  for (const SplitRawTask& raw : raw_tasks) {
    if (raw.row.raw_connection_id < 0 ||
        !raw.communication.comm_name_symbol_id.valid()) {
      continue;
    }
    SplitCommunicationEnvelope& envelope =
        out[connection_key(raw.row.device_id, raw.row.raw_connection_id)];
    envelope.start_ns = std::min(envelope.start_ns, raw.row.start_ns);
    envelope.end_ns = std::max(envelope.end_ns, raw.row.end_ns);
    ++envelope.linked_task_count;
    envelope.duration_by_stream[raw.row.raw_stream_id] +=
        static_cast<std::uint64_t>(
            std::max<std::int64_t>(0, raw.row.end_ns - raw.row.start_ns));
    if (!envelope.linked_task_name_symbol_id.valid()) {
      envelope.linked_task_name_symbol_id =
          raw.communication.comm_name_symbol_id;
    } else if (envelope.linked_task_name_symbol_id !=
               raw.communication.comm_name_symbol_id) {
      envelope.task_name_consistent = false;
    }
    if (!envelope.linked_task_type_symbol_id.valid()) {
      envelope.linked_task_type_symbol_id =
          raw.communication.task_type_symbol_id;
    }
  }
  return out;
}

std::vector<SplitCommunicationOpEvidence> load_split_communication_op_evidence(
    const std::vector<const AscendSplitSQLiteTableInfo*>& tables,
    const std::unordered_map<std::string, SourceRefId>& table_refs,
    NativeIr& ir,
    bool device_id_from_path) {
  std::vector<SplitCommunicationOpEvidence> out;
  for (const AscendSplitSQLiteTableInfo* table : tables) {
    SqliteDb db(table->db_path);
    const bool has_index_id =
        table_has_column(db, table->table_name, "index_id");
    const std::string sql =
        device_id_from_path
            ? "SELECT rowid, " +
                  std::string(has_index_id ? "index_id" : "-1") +
                  ", op_name, op_type, connection_id FROM " +
                  quote_identifier(table->table_name) + " ORDER BY rowid"
            : "SELECT rowid, " +
                  std::string(has_index_id ? "index_id" : "-1") +
                  ", op_name, op_type, connection_id, device_id FROM " +
                  quote_identifier(table->table_name) + " ORDER BY rowid";
    SqliteStmt stmt(db.get(), sql.c_str());
    while (true) {
      const int rc = sqlite3_step(stmt.get());
      if (rc == SQLITE_DONE) {
        break;
      }
      if (rc != SQLITE_ROW) {
        throw std::runtime_error("failed to load split " +
                                 table->table_name + ": " +
                                 sqlite3_errmsg(stmt.db()));
      }
      const std::uint64_t row_id = sqlite_u64(stmt.get(), 0);
      const std::int64_t index_id = sqlite_i64(stmt.get(), 1, -1);
      out.push_back(SplitCommunicationOpEvidence{
          table_refs.at(table->db_path + "\n" + table->table_name), row_id,
          device_id_from_path ? split_device_id_from_path(table->db_path)
                              : sqlite_u32(stmt.get(), 5),
          sqlite_i64(stmt.get(), 4, -1),
          index_id >= 0 ? index_id : static_cast<std::int64_t>(row_id),
          ir.symbols.intern(sqlite_text(stmt.get(), 2)),
          ir.symbols.intern(sqlite_text(stmt.get(), 3))});
    }
  }
  return out;
}

void materialize_split_communication_ops(
    const std::vector<AscendSplitSQLiteTableInfo>& inventory,
    const std::unordered_map<std::string, SourceRefId>& table_refs,
    const SplitCommunicationEnvelopeIndex& envelopes,
    NativeIr& ir,
    StreamIndex& streams) {
  // Split HCCLOP begin/end values may use a host-side clock domain that is not
  // directly comparable with AscendTask. Treat the op row as identity
  // evidence and derive observable device geometry only from its linked task
  // group, matching the monolithic COMMUNICATION_OP materialization.
  std::vector<const AscendSplitSQLiteTableInfo*> op_tables =
      usable_split_tables(find_split_tables(inventory, "HCCLOP"),
                          {"device_id", "op_name", "op_type",
                           "connection_id"});
  op_tables.erase(
      std::remove_if(op_tables.begin(), op_tables.end(),
                     [](const AscendSplitSQLiteTableInfo* table) {
                       return table->row_count == 0;
                     }),
      op_tables.end());
  bool device_id_from_path = false;
  if (op_tables.empty()) {
    op_tables = usable_split_tables(
        find_split_tables(inventory, "HCCLOpSingleDevice"),
        {"op_name", "op_type", "connection_id"});
    device_id_from_path = true;
  }
  std::vector<SplitCommunicationOpEvidence> evidence =
      load_split_communication_op_evidence(op_tables, table_refs, ir,
                                           device_id_from_path);
  std::unordered_map<std::uint64_t, std::uint32_t> evidence_count_by_key;
  for (const SplitCommunicationOpEvidence& row : evidence) {
    ++evidence_count_by_key[connection_key(row.device_id, row.connection_id)];
  }
  std::sort(evidence.begin(), evidence.end(), [&](const auto& lhs,
                                                  const auto& rhs) {
    const auto lhs_envelope =
        envelopes.find(connection_key(lhs.device_id, lhs.connection_id));
    const auto rhs_envelope =
        envelopes.find(connection_key(rhs.device_id, rhs.connection_id));
    const std::int64_t lhs_start =
        lhs_envelope == envelopes.end()
            ? std::numeric_limits<std::int64_t>::max()
            : lhs_envelope->second.start_ns;
    const std::int64_t rhs_start =
        rhs_envelope == envelopes.end()
            ? std::numeric_limits<std::int64_t>::max()
            : rhs_envelope->second.start_ns;
    if (lhs.device_id != rhs.device_id) {
      return lhs.device_id < rhs.device_id;
    }
    if (lhs_start != rhs_start) {
      return lhs_start < rhs_start;
    }
    return lhs.source_row_id < rhs.source_row_id;
  });

  for (const SplitCommunicationOpEvidence& row : evidence) {
    const std::uint64_t key = connection_key(row.device_id, row.connection_id);
    const auto envelope_found = envelopes.find(key);
    if (row.connection_id < 0 || envelope_found == envelopes.end() ||
        evidence_count_by_key[key] != 1) {
      continue;
    }
    const SplitCommunicationEnvelope& envelope = envelope_found->second;
    if (envelope.linked_task_count == 0 ||
        !envelope.task_name_consistent ||
        envelope.start_ns == std::numeric_limits<std::int64_t>::max() ||
        envelope.end_ns <= envelope.start_ns) {
      continue;
    }
    std::uint64_t primary_stream_id =
        std::numeric_limits<std::uint32_t>::max();
    std::uint64_t primary_duration = 0;
    bool has_primary_stream = false;
    for (const auto& item : envelope.duration_by_stream) {
      if (!has_primary_stream || item.second > primary_duration) {
        primary_stream_id = item.first;
        primary_duration = item.second;
        has_primary_stream = true;
      }
    }
    (void)find_or_append_stream(streams, ir, row.source_ref, row.device_id,
                                primary_stream_id);
    const TraceEventId event = ir.trace_events.append(
        row.source_ref, row.source_row_id, row.device_id, primary_stream_id,
        envelope.start_ns, envelope.end_ns, row.op_name_symbol_id);
    ir.communication_ops.append(
        row.source_ref, event, row.connection_id, row.raw_op_id,
        envelope.linked_task_count,
        static_cast<std::uint32_t>(envelope.duration_by_stream.size()),
        row.op_name_symbol_id, row.op_type_symbol_id,
        envelope.linked_task_name_symbol_id,
        envelope.linked_task_type_symbol_id);
  }
}

NativeIr load_split_profile(const AscendSQLiteAdapterOptions& options) {
  const std::vector<AscendSplitSQLiteTableInfo> inventory =
      inventory_split_profile_impl(options.db_path);
  const std::vector<const AscendSplitSQLiteTableInfo*> task_tables =
      find_split_tables(inventory, "AscendTask");
  if (task_tables.empty()) {
    throw std::invalid_argument(
        "split SQLite profile is missing required "
        "device_*/sqlite/ascend_task.db:AscendTask table: " +
        options.db_path);
  }
  for (const AscendSplitSQLiteTableInfo* task_table : task_tables) {
    if (!split_table_has_columns(
            task_table,
            {"start_time", "duration", "device_task_type", "stream_id",
             "task_id", "context_id", "connection_id", "host_task_type"})) {
      throw std::invalid_argument(
          "split SQLite profile has an incompatible AscendTask schema: " +
          task_table->db_path);
    }
  }

  std::vector<std::string> missing_optional;
  for (const char* table_name : {"ApiData", "HostTask", "TaskInfo"}) {
    if (find_split_table(inventory, table_name) == nullptr) {
      missing_optional.push_back(table_name);
    }
  }
  std::cerr << "warning: using Ascend split SQLite fallback: "
            << options.db_path;
  if (!missing_optional.empty()) {
    std::cerr << " (missing optional tables:";
    for (const std::string& table_name : missing_optional) {
      std::cerr << " " << table_name;
    }
    std::cerr << ")";
  }
  std::cerr << '\n';

  NativeIr ir;
  std::unordered_map<std::string, SourceRefId> table_refs;
  for (const auto& info : inventory) {
    const SourceRefId source_ref = ir.source_refs.append(
        options.source_kind, info.db_path, info.table_name, 0);
    table_refs.emplace(info.db_path + "\n" + info.table_name, source_ref);
    if (options.timing_diagnostics) {
      std::cerr << "split_inventory db=" << info.db_path
                << " table=" << info.table_name
                << " rows=" << info.row_count << '\n';
    }
  }

  const AscendSplitSQLiteTableInfo* task_info_table =
      find_split_table(inventory, "TaskInfo");
  const bool task_info_usable = split_table_has_columns(
      task_info_table,
      {"device_id", "stream_id", "task_id", "context_id", "op_name",
       "op_type", "task_type"});
  const SplitComputeIndex compute_info =
      load_split_task_info(task_info_usable ? task_info_table : nullptr, ir);
  const std::vector<const AscendSplitSQLiteTableInfo*> communication_tables =
      find_split_tables(inventory, "HCCLTaskSingleDevice");
  const std::vector<const AscendSplitSQLiteTableInfo*>
      usable_communication_tables = usable_split_tables(
          communication_tables,
          {"stream_id", "task_id", "context_id", "op_name", "hccl_name"});
  const bool communication_info_usable =
      !communication_tables.empty() &&
      usable_communication_tables.size() == communication_tables.size();
  const SplitCommunicationIndex communication_info =
      load_split_communication_task_info(
          usable_communication_tables, ir);
  const AscendSplitSQLiteTableInfo* host_task_table =
      find_split_table(inventory, "HostTask");
  const auto host_task_types = load_split_host_task_types(
      split_table_has_columns(host_task_table, {"connection_id", "task_type"})
          ? host_task_table
          : nullptr);
  std::vector<SplitRawTask> raw_tasks;
  for (const AscendSplitSQLiteTableInfo* table : task_tables) {
    const std::uint32_t table_device_id =
        split_device_id_from_path(table->db_path);
    const SourceRefId source_ref =
        table_refs.at(table->db_path + "\n" + table->table_name);
    SqliteDb db(table->db_path);
    const bool has_model_id =
        table_has_column(db, "AscendTask", "model_id");
    const std::string task_sql =
        "SELECT rowid, start_time, duration, device_task_type, stream_id, "
        "task_id, context_id, connection_id, host_task_type, " +
        std::string(has_model_id ? "model_id " : "-1 ") +
        "FROM AscendTask WHERE start_time >= 0 AND duration >= 0 "
        "ORDER BY start_time, stream_id, task_id, context_id, rowid";
    SqliteStmt stmt(db.get(), task_sql.c_str());
    while (true) {
      const int rc = sqlite3_step(stmt.get());
      if (rc == SQLITE_DONE) {
        break;
      }
      if (rc != SQLITE_ROW) {
        throw std::runtime_error("failed to load split AscendTask: " +
                                 std::string(sqlite3_errmsg(stmt.db())));
      }
      const std::int64_t start_ns = sqlite_i64(stmt.get(), 1, 0);
      const std::int64_t duration_ns = sqlite_i64(stmt.get(), 2, 0);
      const std::string device_task_type = sqlite_text(stmt.get(), 3);
      const std::uint64_t stream_id = sqlite_u64(stmt.get(), 4);
      const std::uint64_t task_id = sqlite_u64(stmt.get(), 5);
      const std::uint64_t context_id = sqlite_u64(stmt.get(), 6);
      const std::int64_t connection_id = sqlite_i64(stmt.get(), 7);
      std::string host_task_type = sqlite_text(stmt.get(), 8);
      const std::int64_t model_id =
          normalize_raw_model_id(sqlite_i64(stmt.get(), 9, -1));
      const auto host_found = host_task_types.find(connection_id);
      if (host_task_type.empty() && host_found != host_task_types.end()) {
        host_task_type = host_found->second;
      }
      if (host_task_type.empty()) {
        host_task_type = device_task_type;
      }
      const SplitTaskKey key{table_device_id, stream_id, task_id, context_id};
      SplitComputeInfo compute;
      auto compute_found = compute_info.find(key);
      if (compute_found != compute_info.end()) {
        compute = compute_found->second;
      }
      SplitCommunicationInfo communication;
      auto communication_found = communication_info.find(key);
      if (communication_found != communication_info.end()) {
        communication = communication_found->second;
      }
      const std::string normalized_task_type =
          communication.comm_name_symbol_id.valid() &&
                  !device_task_type.empty() && device_task_type != "UNKNOWN"
              ? device_task_type
              : host_task_type;
      raw_tasks.push_back(SplitRawTask{
          RawTaskRow{sqlite_u64(stmt.get(), 0), start_ns,
                     start_ns + duration_ns, table_device_id, stream_id, task_id,
                     compute.synthetic_global_task_id, connection_id,
                     static_cast<std::int64_t>(context_id), -1, model_id},
          source_ref, ir.symbols.intern(normalized_task_type), compute,
          communication});
    }
  }
  std::sort(raw_tasks.begin(), raw_tasks.end(),
            [](const SplitRawTask& lhs, const SplitRawTask& rhs) {
              return raw_task_row_less(lhs.row, rhs.row);
            });
  const SplitCommunicationEnvelopeIndex communication_envelopes =
      build_split_communication_envelopes(raw_tasks);

  StreamIndex streams;
  for (const SplitRawTask& raw : raw_tasks) {
    const StreamId stream = find_or_append_stream(
        streams, ir, raw.source_ref, raw.row.device_id, raw.row.raw_stream_id);
    const TraceEventId event = ir.trace_events.append(
        raw.source_ref, raw.row.row_id, raw.row.device_id,
        ir.streams.row(stream).raw_stream_id, raw.row.start_ns, raw.row.end_ns,
        raw.task_type_symbol);
    ir.tasks.append(raw.source_ref, event, raw.row.raw_task_id,
                    raw.row.raw_global_task_id, raw.row.raw_connection_id,
                    raw.task_type_symbol, raw.compute.symbols.op_name_symbol_id,
                    raw.compute.symbols.op_type_symbol_id,
                    raw.compute.symbols.compute_task_type_symbol_id,
                    raw.communication.comm_name_symbol_id,
                    raw.row.raw_model_id,
                    raw.communication.task_type_symbol_id,
                    raw.row.raw_context_id);
  }
  materialize_split_communication_ops(inventory, table_refs,
                                      communication_envelopes, ir, streams);

  const AscendSplitSQLiteTableInfo* raw_api_table =
      find_split_table(inventory, "ApiData");
  const AscendSplitSQLiteTableInfo* api_table =
      split_table_has_columns(
          raw_api_table, {"start", "end", "connection_id", "id"})
          ? raw_api_table
          : nullptr;
  if (api_table != nullptr) {
    load_split_ascend_runtime_calls(
        api_table,
        table_refs.at(api_table->db_path + "\n" + api_table->table_name), ir);
  }
  const AclGraphCannApiMetadata cann_api_metadata =
      load_split_aclgraph_api_metadata(api_table);
  const AscendSplitSQLiteTableInfo* capture_stream_table =
      find_split_table(inventory, "CaptureStreamInfo");
  const bool capture_stream_usable =
      capture_stream_table != nullptr &&
      aclgraph_capture_stream_schema_usable(capture_stream_table->db_path);
  AclGraphCaptureInfo capture_info;
  if (capture_stream_usable) {
    capture_info =
        load_aclgraph_capture_info(capture_stream_table->db_path);
  }
  capture_info.capture_slots = cann_api_metadata.capture_slots;
  capture_info.replay_unit_signature = build_capture_replay_unit_signature(
      capture_info.capture_slots, capture_info.capture_group_size);

  const auto source_ref_for = [&](const AscendSplitSQLiteTableInfo* table) {
    return table == nullptr
               ? SourceRefId::invalid()
               : table_refs.at(table->db_path + "\n" + table->table_name);
  };
  const SourceRefId api_source_ref = source_ref_for(api_table);
  CapturedGraphInstanceIndexes captured_graph_instances;
  if (!capture_info.model_groups.empty()) {
    captured_graph_instances = materialize_aclgraph_capture_instances(
        ir, capture_info, source_ref_for(capture_stream_table),
        api_source_ref);
  }
  materialize_aclgraph_launch_occurrences(
      ir, streams, captured_graph_instances,
      cann_api_metadata.execute_launches, api_source_ref);
  const std::set<GraphLaunchOccurrenceId> missing_body_capability_launches =
      materialize_graph_launch_bodies(
          ir, task_info_usable, communication_info_usable);
  if (api_source_ref.valid()) {
    materialize_graph_launch_activities(
        ir, cann_api_metadata.launch_activities, api_source_ref);
  }
  materialize_replay_composition_candidates(
      ir, missing_body_capability_launches);
  (void)materialize_exact_aclgraph_replay_units(
      ir, options.source_kind, options.db_path);
  return ir;
}

}  // namespace traceloom::ascend_sqlite_detail
