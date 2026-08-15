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
std::uint64_t stream_key(std::uint32_t device_id, std::uint64_t stream_id) {
  return (static_cast<std::uint64_t>(device_id) << 32u) ^
         (stream_id & 0xffffffffu);
}

std::uint64_t connection_key(std::uint32_t device_id,
                             std::int64_t connection_id) {
  return (static_cast<std::uint64_t>(device_id) << 32u) ^
         (static_cast<std::uint64_t>(connection_id) & 0xffffffffu);
}

bool symbol_in_set(const std::unordered_set<std::uint32_t>& symbols,
                   SymbolId symbol_id) {
  return symbol_id.valid() && symbols.find(symbol_id.value()) != symbols.end();
}

GraphTaskSymbolSets build_graph_task_symbol_sets(const SymbolTable& symbols) {
  GraphTaskSymbolSets out;
  for (std::size_t index = 0; index < symbols.size(); ++index) {
    const SymbolId symbol_id(static_cast<SymbolId::value_type>(index));
    const std::string key = normalize_key(symbols.value(symbol_id));
    if (graph_task_key(key)) {
      out.graph_control.insert(symbol_id.value());
    }
    if (key == "NOTIFY_WAIT") {
      out.notify_wait.insert(symbol_id.value());
    }
    if (key == "NOTIFY_RECORD") {
      out.notify_record.insert(symbol_id.value());
    }
    if (key == "MODEL_EXECUTE") {
      out.model_execute.insert(symbol_id.value());
    }
  }
  return out;
}

std::unordered_set<std::uint64_t> flatten_model_stream_keys(
    const std::unordered_map<std::uint32_t, std::unordered_set<std::uint64_t>>&
        model_streams_by_device) {
  std::unordered_set<std::uint64_t> out;
  std::size_t stream_count = 0;
  for (const auto& item : model_streams_by_device) {
    stream_count += item.second.size();
  }
  out.reserve(stream_count);
  for (const auto& item : model_streams_by_device) {
    for (std::uint64_t raw_stream_id : item.second) {
      out.insert(stream_key(item.first, raw_stream_id));
    }
  }
  return out;
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

bool raw_task_row_less(const RawTaskRow& lhs, const RawTaskRow& rhs) {
  if (lhs.device_id != rhs.device_id) {
    return lhs.device_id < rhs.device_id;
  }
  if (lhs.raw_stream_id != rhs.raw_stream_id) {
    return lhs.raw_stream_id < rhs.raw_stream_id;
  }
  if (lhs.start_ns != rhs.start_ns) {
    return lhs.start_ns < rhs.start_ns;
  }
  if (lhs.end_ns != rhs.end_ns) {
    return lhs.end_ns < rhs.end_ns;
  }
  if (lhs.raw_global_task_id != rhs.raw_global_task_id) {
    return lhs.raw_global_task_id < rhs.raw_global_task_id;
  }
  if (lhs.raw_task_id != rhs.raw_task_id) {
    return lhs.raw_task_id < rhs.raw_task_id;
  }
  return lhs.row_id < rhs.row_id;
}

std::vector<RowidRange> task_rowid_ranges(SqliteDb& db,
                                          std::size_t thread_count) {
  SqliteStmt stmt(
      db.get(),
      "SELECT MIN(rowid), MAX(rowid), COUNT(*) FROM TASK");
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_ROW) {
    if (rc == SQLITE_DONE) {
      return {};
    }
    throw std::runtime_error("failed to inspect TASK rowid ranges: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  if (sqlite3_column_type(stmt.get(), 0) == SQLITE_NULL ||
      sqlite3_column_type(stmt.get(), 1) == SQLITE_NULL) {
    return {};
  }
  const std::int64_t min_rowid = sqlite_i64(stmt.get(), 0, 0);
  const std::int64_t max_rowid = sqlite_i64(stmt.get(), 1, 0);
  const std::uint64_t row_count = sqlite_u64(stmt.get(), 2);
  if (row_count == 0 || max_rowid < min_rowid) {
    return {};
  }
  const std::uint64_t desired_chunks =
      static_cast<std::uint64_t>(std::max<std::size_t>(1, thread_count)) * 4u;
  const std::size_t target_chunks = static_cast<std::size_t>(
      std::max<std::uint64_t>(1, std::min(row_count, desired_chunks)));
  const std::uint64_t span =
      static_cast<std::uint64_t>(max_rowid - min_rowid) + 1u;
  std::vector<RowidRange> ranges;
  ranges.reserve(target_chunks);
  for (std::size_t index = 0; index < target_chunks; ++index) {
    const std::int64_t first =
        min_rowid + static_cast<std::int64_t>((span * index) / target_chunks);
    const std::int64_t next =
        min_rowid +
        static_cast<std::int64_t>((span * (index + 1u)) / target_chunks);
    const std::int64_t last = next - 1;
    if (first <= last) {
      ranges.push_back(RowidRange{first, last});
    }
  }
  return ranges;
}

std::vector<RawTaskRow> read_task_raw_rows(SqliteDb& db,
                                           const RowidRange* range,
                                           bool ordered,
                                           bool has_model_id,
                                           bool has_context_id) {
  std::string sql =
      "SELECT rowid, startNs, endNs, deviceId, streamId, taskId, "
      "globalTaskId, connectionId, " +
      std::string(has_context_id ? "contextId, " : "-1, ") +
      "taskType, " +
      std::string(has_model_id ? "modelId " : "-1 ") +
      "FROM TASK ";
  if (range != nullptr) {
    sql += "WHERE rowid BETWEEN ? AND ? ";
  }
  if (ordered) {
    sql +=
        "ORDER BY deviceId, streamId, startNs, endNs, globalTaskId, taskId";
  }
  SqliteStmt stmt(db.get(), sql.c_str());
  if (range != nullptr) {
    sqlite3_bind_int64(stmt.get(), 1, range->first);
    sqlite3_bind_int64(stmt.get(), 2, range->last);
  }

  std::vector<RawTaskRow> raw_rows;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      raw_rows.push_back(RawTaskRow{
          sqlite_u64(stmt.get(), 0), sqlite_i64(stmt.get(), 1, 0),
          sqlite_i64(stmt.get(), 2, 0), sqlite_u32(stmt.get(), 3),
          sqlite_u64(stmt.get(), 4), sqlite_u64(stmt.get(), 5),
          sqlite_i64(stmt.get(), 6), sqlite_i64(stmt.get(), 7),
          sqlite_i64(stmt.get(), 8), sqlite_i64(stmt.get(), 9),
          normalize_raw_model_id(sqlite_i64(stmt.get(), 10, -1))});
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    throw std::runtime_error("failed to load TASK rows: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  return raw_rows;
}

std::vector<RawTaskRow> load_task_raw_rows(SqliteDb& db,
                                           const std::string& db_path,
                                           std::size_t thread_count) {
  static constexpr std::size_t kMaxTaskReaderThreads = 8;
  const std::size_t reader_count =
      std::min(std::max<std::size_t>(1, thread_count), kMaxTaskReaderThreads);
  const bool has_model_id = table_has_column(db, "TASK", "modelId");
  const bool has_context_id = table_has_column(db, "TASK", "contextId");
  if (reader_count <= 1) {
    return read_task_raw_rows(db, nullptr, true, has_model_id,
                              has_context_id);
  }
  const std::vector<RowidRange> ranges = task_rowid_ranges(db, reader_count);
  if (ranges.size() <= 1) {
    return read_task_raw_rows(db, nullptr, true, has_model_id,
                              has_context_id);
  }

  std::vector<std::vector<RawTaskRow>> chunks(ranges.size());
  ThreadPool pool(reader_count);
  pool.parallel_for(ranges.size(), [&](std::size_t index) {
    SqliteDb worker_db(db_path);
    chunks[index] =
        read_task_raw_rows(worker_db, &ranges[index], false, has_model_id,
                           has_context_id);
  });

  std::size_t total_rows = 0;
  for (const std::vector<RawTaskRow>& chunk : chunks) {
    total_rows += chunk.size();
  }
  std::vector<RawTaskRow> raw_rows;
  raw_rows.reserve(total_rows);
  for (std::vector<RawTaskRow>& chunk : chunks) {
    raw_rows.insert(raw_rows.end(), std::make_move_iterator(chunk.begin()),
                    std::make_move_iterator(chunk.end()));
  }
  std::sort(raw_rows.begin(), raw_rows.end(), raw_task_row_less);
  return raw_rows;
}

void load_task_rows(
    SqliteDb& db,
    const std::string& db_path,
    std::size_t thread_count,
    NativeIr& ir,
    StreamIndex& streams,
    TaskLinkIndex& task_links,
    const std::unordered_map<std::int64_t, std::string>& string_ids,
    const std::unordered_map<std::int64_t, ComputeInfo>& compute_info,
    const std::unordered_map<std::int64_t, CommunicationTaskInfo>&
        communication_task_info,
    SourceRefId task_table_ref) {
  std::vector<RawTaskRow> raw_rows =
      load_task_raw_rows(db, db_path, thread_count);

  ir.trace_events.reserve(ir.trace_events.size() + raw_rows.size());
  ir.tasks.reserve(ir.tasks.size() + raw_rows.size());

  std::unordered_map<std::int64_t, SymbolId> task_type_symbols;
  for (const RawTaskRow& row : raw_rows) {
    auto task_type_found = task_type_symbols.find(row.raw_task_type_id);
    if (task_type_found == task_type_symbols.end()) {
      task_type_found =
          task_type_symbols
              .emplace(row.raw_task_type_id,
                       ir.symbols.intern(decode_string_id(
                           string_ids, row.raw_task_type_id)))
              .first;
    }
    const SymbolId task_type_symbol = task_type_found->second;
    const auto compute_found = compute_info.find(row.raw_global_task_id);
    const ComputeInfo compute =
        compute_found == compute_info.end() ? ComputeInfo()
                                            : compute_found->second;
    const auto comm_task_found =
        communication_task_info.find(row.raw_global_task_id);
    const CommunicationTaskInfo comm_task =
        comm_task_found == communication_task_info.end()
            ? CommunicationTaskInfo()
            : comm_task_found->second;
    task_links[connection_key(row.device_id, row.raw_connection_id)].push_back(
        TaskLink{row.raw_stream_id, row.start_ns, row.end_ns,
                 comm_task.comm_name_symbol_id,
                 comm_task.task_type_symbol_id});

    const StreamId stream =
        find_or_append_stream(streams, ir, task_table_ref, row.device_id,
                              row.raw_stream_id);

    const TraceEventId event = ir.trace_events.append(
        task_table_ref, row.row_id, row.device_id,
        ir.streams.row(stream).raw_stream_id, row.start_ns, row.end_ns,
        task_type_symbol);
    ir.tasks.append(task_table_ref, event, row.raw_task_id,
                    row.raw_global_task_id, row.raw_connection_id,
                    task_type_symbol, compute.op_name_symbol_id,
                    compute.op_type_symbol_id,
                    compute.compute_task_type_symbol_id,
                    comm_task.comm_name_symbol_id, row.raw_model_id,
                    comm_task.task_type_symbol_id, row.raw_context_id);
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
        item.second > duration_by_stream[stats.primary_stream_id] ||
        (item.second == duration_by_stream[stats.primary_stream_id] &&
         item.first < stats.primary_stream_id)) {
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

}  // namespace traceloom::ascend_sqlite_detail
