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

std::vector<std::int64_t> string_ids_for_value(
    const std::unordered_map<std::int64_t, std::string>& string_ids,
    const std::string& value) {
  std::vector<std::int64_t> out;
  for (const auto& item : string_ids) {
    if (item.second == value) {
      out.push_back(item.first);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool contains_i64(const std::vector<std::int64_t>& values,
                  std::int64_t value) {
  return std::binary_search(values.begin(), values.end(), value);
}

std::string capture_host_api_token(const std::string& name) {
  if (name.empty()) {
    return "";
  }
  if (name.size() >= std::string("GetWorkspaceSize").size() &&
      name.rfind("GetWorkspaceSize") ==
          name.size() - std::string("GetWorkspaceSize").size()) {
    return "";
  }
  if (name.rfind("aclnn", 0) == 0 ||
      name.find("Operation::Execute") != std::string::npos) {
    return name;
  }
  return "";
}

std::string join_capture_tokens(const std::vector<std::string>& tokens) {
  std::string out;
  for (const std::string& token : tokens) {
    out += token;
    out += "\n";
  }
  return out;
}

std::string build_capture_replay_unit_signature(
    const std::vector<CaptureSlotSignature>& slots,
    std::uint32_t capture_group_size) {
  if (slots.empty() || capture_group_size == 0) {
    return "";
  }
  std::string out = "aclgraph_capture_dictionary\n";
  out += "capture_group_size=" + std::to_string(capture_group_size) + "\n";
  out += "capture_slot_count=" + std::to_string(slots.size()) + "\n";
  for (std::size_t index = 0; index < slots.size(); ++index) {
    if (index % capture_group_size == 0) {
      out += "group=" + std::to_string(index / capture_group_size) + "\n";
    }
    out += "slot=" + std::to_string(slots[index].slot_index) + "\n";
    out += slots[index].host_api_signature;
  }
  return out;
}

std::vector<CaptureInterval> build_capture_intervals(
    std::vector<CaptureInterval> begin_markers,
    std::vector<CaptureInterval> end_markers) {
  if (begin_markers.empty() || end_markers.empty()) {
    return {};
  }
  auto by_start = [](const CaptureInterval& lhs,
                     const CaptureInterval& rhs) {
    if (lhs.start_ns != rhs.start_ns) {
      return lhs.start_ns < rhs.start_ns;
    }
    return lhs.end_ns < rhs.end_ns;
  };
  std::sort(begin_markers.begin(), begin_markers.end(), by_start);
  std::sort(end_markers.begin(), end_markers.end(), by_start);
  std::vector<CaptureInterval> intervals;
  intervals.reserve(std::min(begin_markers.size(), end_markers.size()));
  std::size_t begin_cursor = 0;
  for (const CaptureInterval& end_marker : end_markers) {
    if (begin_cursor >= begin_markers.size()) {
      break;
    }
    CaptureInterval interval = begin_markers[begin_cursor++];
    interval.end_ns = end_marker.end_ns;
    if (interval.end_ns > interval.start_ns) {
      intervals.push_back(interval);
    }
  }
  return intervals;
}

std::vector<CaptureSlotSignature> build_aclgraph_capture_slots(
    const std::vector<CaptureInterval>& intervals,
    std::vector<CaptureTokenCandidate> token_candidates) {
  if (intervals.empty()) {
    return {};
  }

  std::sort(token_candidates.begin(), token_candidates.end(),
            [](const CaptureTokenCandidate& lhs,
               const CaptureTokenCandidate& rhs) {
              if (lhs.start_ns != rhs.start_ns) {
                return lhs.start_ns < rhs.start_ns;
              }
              if (lhs.end_ns != rhs.end_ns) {
                return lhs.end_ns < rhs.end_ns;
              }
              return lhs.token < rhs.token;
            });

  std::vector<std::vector<std::string>> tokens(intervals.size());
  std::size_t cursor = 0;
  for (const CaptureTokenCandidate& candidate : token_candidates) {
    while (cursor < intervals.size() &&
           candidate.start_ns > intervals[cursor].end_ns) {
      ++cursor;
    }
    if (cursor >= intervals.size() ||
        candidate.start_ns < intervals[cursor].start_ns ||
        candidate.end_ns > intervals[cursor].end_ns) {
      continue;
    }
    tokens[cursor].push_back(candidate.token);
  }

  std::vector<CaptureSlotSignature> out;
  out.reserve(intervals.size());
  for (std::size_t index = 0; index < intervals.size(); ++index) {
    out.push_back(CaptureSlotSignature{
        static_cast<std::uint32_t>(index), intervals[index].start_ns,
        intervals[index].end_ns, join_capture_tokens(tokens[index])});
  }
  return out;
}

std::vector<GraphLaunchActivityView> build_graph_launch_activities(
    const std::vector<GraphLaunchView>& execute_launches,
    const std::vector<HostBlockingSyncView>& blocking_syncs) {
  struct HostEvent {
    std::int64_t raw_row_id = -1;
    std::int64_t start_ns = 0;
    std::int64_t end_ns = 0;
    const GraphLaunchView* execute = nullptr;
    const HostBlockingSyncView* boundary = nullptr;
  };

  std::map<std::uint64_t, std::vector<HostEvent>> events_by_thread;
  for (const GraphLaunchView& execute : execute_launches) {
    events_by_thread[execute.raw_global_tid].push_back(HostEvent{
        execute.raw_row_id, execute.start_ns, execute.end_ns, &execute,
        nullptr});
  }
  for (const HostBlockingSyncView& boundary : blocking_syncs) {
    events_by_thread[boundary.raw_global_tid].push_back(HostEvent{
        boundary.raw_row_id, boundary.start_ns, boundary.end_ns, nullptr,
        &boundary});
  }

  std::vector<GraphLaunchActivityView> out;
  for (auto& item : events_by_thread) {
    std::vector<HostEvent>& events = item.second;
    std::sort(events.begin(), events.end(),
              [](const HostEvent& lhs, const HostEvent& rhs) {
                if (lhs.start_ns != rhs.start_ns) {
                  return lhs.start_ns < rhs.start_ns;
                }
                if (lhs.end_ns != rhs.end_ns) {
                  return lhs.end_ns < rhs.end_ns;
                }
                return lhs.raw_row_id < rhs.raw_row_id;
              });
    GraphLaunchActivityView activity;
    activity.raw_global_tid = item.first;
    for (const HostEvent& event : events) {
      if (event.execute != nullptr) {
        if (activity.host_execute_row_ids.empty()) {
          activity.first_host_api_row_id = event.execute->raw_row_id;
          activity.start_ns = event.execute->start_ns;
        }
        activity.host_execute_row_ids.push_back(event.execute->raw_row_id);
        activity.last_host_api_row_id = event.execute->raw_row_id;
        activity.end_ns = event.execute->end_ns;
        continue;
      }
      if (activity.host_execute_row_ids.empty()) {
        continue;
      }
      activity.boundary_host_api_row_id = event.boundary->raw_row_id;
      activity.boundary_api_name = event.boundary->api_name;
      activity.boundary_policy =
          GraphLaunchActivityBoundaryPolicy::kHostBlockingSync;
      activity.end_ns = std::max(activity.end_ns, event.boundary->end_ns);
      out.push_back(std::move(activity));
      activity = GraphLaunchActivityView{};
      activity.raw_global_tid = item.first;
    }
    if (!activity.host_execute_row_ids.empty()) {
      out.push_back(std::move(activity));
    }
  }
  std::sort(out.begin(), out.end(),
            [](const GraphLaunchActivityView& lhs,
               const GraphLaunchActivityView& rhs) {
              if (lhs.start_ns != rhs.start_ns) {
                return lhs.start_ns < rhs.start_ns;
              }
              if (lhs.raw_global_tid != rhs.raw_global_tid) {
                return lhs.raw_global_tid < rhs.raw_global_tid;
              }
              return lhs.first_host_api_row_id < rhs.first_host_api_row_id;
            });
  return out;
}

AclGraphCannApiMetadata load_aclgraph_cann_api_metadata(
    SqliteDb& db,
    const std::unordered_map<std::int64_t, std::string>& string_ids) {
  AclGraphCannApiMetadata metadata;
  if (!table_has_column(db, "CANN_API", "name")) {
    return metadata;
  }
  const bool has_global_tid = table_has_column(db, "CANN_API", "globalTid");

  const std::vector<std::int64_t> execute_ids =
      string_ids_for_value(string_ids, "aclmdlRIExecuteAsync");
  const std::vector<std::int64_t> begin_ids =
      string_ids_for_value(string_ids, "aclmdlRICaptureBegin");
  const std::vector<std::int64_t> end_ids =
      string_ids_for_value(string_ids, "aclmdlRICaptureEnd");
  static constexpr const char* kBlockingSyncNames[] = {
      "aclrtSynchronizeStreamWithTimeout", "aclrtSynchronizeStream",
      "aclrtSynchronizeDeviceWithTimeout", "aclrtSynchronizeDevice"};
  std::unordered_map<std::int64_t, std::string> blocking_sync_by_id;
  for (const char* name : kBlockingSyncNames) {
    for (std::int64_t id : string_ids_for_value(string_ids, name)) {
      blocking_sync_by_id.emplace(id, name);
    }
  }
  std::unordered_map<std::int64_t, std::string> capture_token_by_id;
  for (const auto& item : string_ids) {
    const std::string token = capture_host_api_token(item.second);
    if (!token.empty()) {
      capture_token_by_id.emplace(item.first, token);
    }
  }
  if (execute_ids.empty() && begin_ids.empty() && end_ids.empty() &&
      capture_token_by_id.empty()) {
    return metadata;
  }

  std::unordered_set<std::int64_t> interesting_names;
  interesting_names.insert(execute_ids.begin(), execute_ids.end());
  interesting_names.insert(begin_ids.begin(), begin_ids.end());
  interesting_names.insert(end_ids.begin(), end_ids.end());
  for (const auto& item : blocking_sync_by_id) {
    interesting_names.insert(item.first);
  }
  for (const auto& item : capture_token_by_id) {
    interesting_names.insert(item.first);
  }
  std::string placeholders;
  for (std::size_t index = 0; index < interesting_names.size(); ++index) {
    if (index != 0) {
      placeholders += ",";
    }
    placeholders += "?";
  }
  const std::string sql =
      "SELECT rowid, startNs, endNs, connectionId, " +
      std::string(has_global_tid ? "globalTid, " : "0, ") +
      "name "
      "FROM CANN_API "
      "WHERE name IN (" +
      placeholders +
      ") AND startNs IS NOT NULL AND endNs IS NOT NULL AND endNs > startNs";
  SqliteStmt stmt(db.get(), sql.c_str());
  int bind_index = 1;
  for (std::int64_t name_id : interesting_names) {
    sqlite3_bind_int64(stmt.get(), bind_index++, name_id);
  }

  std::vector<CaptureInterval> begin_markers;
  std::vector<CaptureInterval> end_markers;
  std::vector<CaptureTokenCandidate> token_candidates;
  std::vector<HostBlockingSyncView> blocking_syncs;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      const std::int64_t row_id = sqlite_i64(stmt.get(), 0, -1);
      const std::int64_t start_ns = sqlite_i64(stmt.get(), 1, 0);
      const std::int64_t end_ns = sqlite_i64(stmt.get(), 2, 0);
      const std::int64_t connection_id = sqlite_i64(stmt.get(), 3, 0);
      const std::uint64_t global_tid = sqlite_u64(stmt.get(), 4);
      const std::int64_t name_id = sqlite_i64(stmt.get(), 5, -1);
      if (contains_i64(execute_ids, name_id)) {
        metadata.execute_launches.push_back(
            GraphLaunchView{row_id, start_ns, end_ns, connection_id,
                            global_tid});
      }
      const auto sync_found = blocking_sync_by_id.find(name_id);
      if (sync_found != blocking_sync_by_id.end()) {
        blocking_syncs.push_back(HostBlockingSyncView{
            row_id, start_ns, end_ns, global_tid, sync_found->second});
      }
      if (contains_i64(begin_ids, name_id)) {
        begin_markers.push_back(CaptureInterval{start_ns, end_ns});
      }
      if (contains_i64(end_ids, name_id)) {
        end_markers.push_back(CaptureInterval{start_ns, end_ns});
      }
      const auto token_found = capture_token_by_id.find(name_id);
      if (token_found != capture_token_by_id.end()) {
        token_candidates.push_back(
            CaptureTokenCandidate{start_ns, end_ns, token_found->second});
      }
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    throw std::runtime_error("failed to load ACLGraph CANN_API metadata: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }

  std::sort(metadata.execute_launches.begin(), metadata.execute_launches.end(),
            [](const GraphLaunchView& lhs, const GraphLaunchView& rhs) {
              if (lhs.start_ns != rhs.start_ns) {
                return lhs.start_ns < rhs.start_ns;
              }
              if (lhs.end_ns != rhs.end_ns) {
                return lhs.end_ns < rhs.end_ns;
              }
              if (lhs.connection_id != rhs.connection_id) {
                return lhs.connection_id < rhs.connection_id;
              }
              return lhs.raw_row_id < rhs.raw_row_id;
            });
  const std::vector<CaptureInterval> intervals =
      build_capture_intervals(std::move(begin_markers), std::move(end_markers));
  metadata.capture_slots =
      build_aclgraph_capture_slots(intervals, std::move(token_candidates));
  if (has_global_tid) {
    metadata.launch_activities = build_graph_launch_activities(
        metadata.execute_launches, blocking_syncs);
  }
  return metadata;
}

void load_ascend_runtime_calls(
    SqliteDb& db, NativeIr& ir,
    const std::unordered_map<std::int64_t, std::string>& string_ids,
    SourceRefId source_ref) {
  const bool has_global_tid = table_has_column(db, "CANN_API", "globalTid");
  const bool has_device_id = table_has_column(db, "CANN_API", "deviceId");
  const bool has_type = table_has_column(db, "CANN_API", "type");
  const std::string sql =
      "SELECT rowid, startNs, endNs, connectionId, name, " +
      std::string(has_global_tid ? "globalTid, " : "-1, ") +
      std::string(has_device_id ? "deviceId, " : "NULL, ") +
      std::string(has_type ? "type " : "NULL ") +
      "FROM CANN_API WHERE startNs IS NOT NULL AND endNs IS NOT NULL "
      "AND endNs > startNs ORDER BY startNs, endNs, rowid";
  SqliteStmt stmt(db.get(), sql.c_str());
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      return;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error("failed to load Ascend CANN_API runtime calls: " +
                               std::string(sqlite3_errmsg(stmt.db())));
    }
    const std::uint64_t source_row_id = sqlite_u64(stmt.get(), 0);
    const std::int64_t start_ns = sqlite_i64(stmt.get(), 1, 0);
    const std::int64_t end_ns = sqlite_i64(stmt.get(), 2, 0);
    const std::int64_t connection_id = sqlite_i64(stmt.get(), 3, -1);
    const std::string api_name =
        decode_string_id(string_ids, sqlite_i64(stmt.get(), 4, -1));
    const std::int64_t raw_global_tid = sqlite_i64(stmt.get(), 5, -1);
    std::int64_t raw_process_id = -1;
    std::int64_t raw_thread_id = -1;
    if (raw_global_tid >= 0) {
      const std::uint64_t packed =
          static_cast<std::uint64_t>(raw_global_tid);
      raw_process_id = static_cast<std::int64_t>(packed >> 32u);
      raw_thread_id = static_cast<std::int64_t>(packed & 0xffffffffULL);
    }
    const bool row_has_device_id =
        has_device_id && sqlite3_column_type(stmt.get(), 6) != SQLITE_NULL &&
        sqlite_i64(stmt.get(), 6, -1) >= 0;
    const std::uint32_t device_id =
        row_has_device_id ? sqlite_u32(stmt.get(), 6) : 0;
    const std::string api_type =
        sqlite3_column_type(stmt.get(), 7) == SQLITE_NULL
            ? std::string("cann_api")
            : decode_string_id(string_ids, sqlite_i64(stmt.get(), 7, -1));
    ir.runtime_calls.append(
        source_ref, source_row_id, RuntimeCallProvider::kAscend,
        RuntimeCallClockDomain::kProfilerHost,
        RuntimeCallMatchPolicy::kAscendConnectionId, start_ns, end_ns,
        connection_id, ir.symbols.intern(api_type),
        ir.symbols.intern(api_name), raw_process_id, raw_thread_id,
        raw_global_tid, -1, row_has_device_id, device_id);
  }
}
std::string stream_info_db_path_for_msprof(const std::string& db_path) {
  const std::filesystem::path path(db_path);
  return (path.parent_path() / "host" / "sqlite" / "stream_info.db").string();
}

bool aclgraph_capture_stream_schema_usable(
    const std::string& stream_info_path) {
  if (!file_exists(stream_info_path)) {
    return false;
  }
  SqliteDb db(stream_info_path);
  return table_has_columns(
             db, "CaptureStreamInfo", {"device_id", "model_id"}) &&
         (table_has_column(db, "CaptureStreamInfo", "stream_id") ||
          table_has_column(db, "CaptureStreamInfo", "model_stream_id"));
}

std::uint32_t infer_capture_group_size_from_stream_info(
    const std::set<std::uint64_t>& model_ids,
    const std::set<std::uint64_t>& original_stream_ids) {
  if (model_ids.empty()) {
    return 0;
  }
  if (original_stream_ids.size() > 1 &&
      model_ids.size() % original_stream_ids.size() == 0) {
    return static_cast<std::uint32_t>(model_ids.size() /
                                      original_stream_ids.size());
  }
  for (std::uint32_t candidate : {29u, 37u}) {
    if (model_ids.size() % candidate == 0) {
      return candidate;
    }
  }
  if (model_ids.size() <= 64) {
    return static_cast<std::uint32_t>(model_ids.size());
  }
  return 0;
}

AclGraphCaptureInfo load_aclgraph_capture_info(
    const std::string& stream_info_path) {
  AclGraphCaptureInfo out;
  if (!file_exists(stream_info_path)) {
    return out;
  }
  SqliteDb db(stream_info_path);
  const bool has_stream_id =
      table_has_column(db, "CaptureStreamInfo", "stream_id");
  const bool has_model_stream_id =
      table_has_column(db, "CaptureStreamInfo", "model_stream_id");
  if (!has_stream_id && !has_model_stream_id) {
    return out;
  }
  // CANN 9 exports the model-side stream as stream_id; older fixtures and
  // profiler versions used model_stream_id for the same field.
  const std::string model_stream_column =
      has_stream_id ? "stream_id" : "model_stream_id";
  const bool has_model_id =
      table_has_column(db, "CaptureStreamInfo", "model_id");
  const bool has_original_stream_id =
      table_has_column(db, "CaptureStreamInfo", "original_stream_id");
  const bool has_timestamp =
      table_has_column(db, "CaptureStreamInfo", "timestamp");
  const std::string quoted_model_stream_column =
      quote_identifier(model_stream_column);
  const std::string sql =
      "SELECT rowid, device_id, " +
      std::string(has_model_id ? "model_id, " : "-1, ") +
      std::string(has_original_stream_id ? "original_stream_id, " : "-1, ") +
      quoted_model_stream_column + ", " +
      std::string(has_timestamp ? "timestamp " : "-1 ") +
      "FROM CaptureStreamInfo ORDER BY device_id, " +
      std::string(has_timestamp ? "timestamp, " : "") +
      std::string(has_model_id ? "model_id, " : "") +
      quoted_model_stream_column + ", rowid";
  SqliteStmt stmt(db.get(), sql.c_str());
  std::set<std::uint64_t> model_ids;
  std::set<std::uint64_t> original_stream_ids;
  std::map<std::pair<std::uint32_t, std::int64_t>, CaptureModelGroupEvidence>
      model_groups;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      const std::uint64_t source_row_id = sqlite_u64(stmt.get(), 0);
      const std::uint32_t device_id = sqlite_u32(stmt.get(), 1);
      const std::int64_t model_id =
          normalize_raw_model_id(sqlite_i64(stmt.get(), 2, -1));
      const std::int64_t original_stream_id = sqlite_i64(stmt.get(), 3, -1);
      const std::uint64_t model_stream_id = sqlite_u64(stmt.get(), 4);
      const std::int64_t timestamp = sqlite_i64(stmt.get(), 5, -1);
      out.model_streams_by_device[device_id].insert(model_stream_id);
      if (model_id >= 0) {
        model_ids.insert(static_cast<std::uint64_t>(model_id));
        if (original_stream_id >= 0) {
          original_stream_ids.insert(
              static_cast<std::uint64_t>(original_stream_id));
        }
        const auto key = std::make_pair(device_id, model_id);
        auto inserted = model_groups.emplace(
            key, CaptureModelGroupEvidence{device_id, model_id, timestamp, {}});
        CaptureModelGroupEvidence& group = inserted.first->second;
        if (group.raw_timestamp < 0 ||
            (timestamp >= 0 && timestamp < group.raw_timestamp)) {
          group.raw_timestamp = timestamp;
        }
        group.streams.push_back(CaptureModelStreamEvidence{
            source_row_id, original_stream_id, model_stream_id});
      }
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    throw std::runtime_error("failed to load CaptureStreamInfo: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  out.capture_group_size =
      infer_capture_group_size_from_stream_info(model_ids, original_stream_ids);
  out.model_groups.reserve(model_groups.size());
  for (auto& item : model_groups) {
    out.model_groups.push_back(std::move(item.second));
  }
  return out;
}

CapturedGraphInstanceIndexes materialize_aclgraph_capture_instances(
    NativeIr& ir,
    const AclGraphCaptureInfo& capture_info,
    SourceRefId capture_stream_source_ref,
    SourceRefId cann_api_source_ref) {
  CapturedGraphInstanceIndexes indexes;
  if (capture_info.model_groups.empty() ||
      !capture_stream_source_ref.valid()) {
    return indexes;
  }

  std::map<std::uint32_t, std::vector<const CaptureModelGroupEvidence*>>
      groups_by_device;
  for (const CaptureModelGroupEvidence& group : capture_info.model_groups) {
    groups_by_device[group.device_id].push_back(&group);
  }
  std::map<std::string, GraphSlotTemplateId> templates_by_signature;
  for (auto& device_groups : groups_by_device) {
    std::vector<const CaptureModelGroupEvidence*>& groups =
        device_groups.second;
    std::sort(groups.begin(), groups.end(),
              [](const CaptureModelGroupEvidence* lhs,
                 const CaptureModelGroupEvidence* rhs) {
                if (lhs->raw_timestamp != rhs->raw_timestamp) {
                  if (lhs->raw_timestamp < 0) {
                    return false;
                  }
                  if (rhs->raw_timestamp < 0) {
                    return true;
                  }
                  return lhs->raw_timestamp < rhs->raw_timestamp;
                }
                return lhs->raw_model_id < rhs->raw_model_id;
              });
    bool ordinal_aligned = groups_by_device.size() == 1 &&
                           groups.size() == capture_info.capture_slots.size() &&
                           !groups.empty();
    for (std::size_t group_index = 0;
         ordinal_aligned && group_index < groups.size(); ++group_index) {
      if (groups[group_index]->raw_timestamp < 0 ||
          (group_index > 0 &&
           groups[group_index - 1]->raw_timestamp >=
               groups[group_index]->raw_timestamp)) {
        ordinal_aligned = false;
      }
    }

    for (std::size_t group_index = 0; group_index < groups.size();
         ++group_index) {
      const CaptureModelGroupEvidence& group = *groups[group_index];
      GraphSlotTemplateId slot_template_id = GraphSlotTemplateId::invalid();
      std::int64_t capture_ordinal = -1;
      CaptureAssociationPolicy association_policy =
          CaptureAssociationPolicy::kModelGroupOnly;
      if (ordinal_aligned) {
        capture_ordinal = static_cast<std::int64_t>(group_index);
        association_policy = CaptureAssociationPolicy::kCaptureOrdinalAligned;
        const std::string& signature =
            capture_info.capture_slots[group_index].host_api_signature;
        if (!signature.empty() && cann_api_source_ref.valid()) {
          const auto template_found = templates_by_signature.find(signature);
          if (template_found == templates_by_signature.end()) {
            slot_template_id = ir.graph_slot_templates.append(
                cann_api_source_ref, stable_hash64(signature),
                ir.symbols.intern(signature));
            templates_by_signature.emplace(signature, slot_template_id);
          } else {
            slot_template_id = template_found->second;
          }
        }
      }
      std::uint64_t first_source_row_id = 0;
      for (const CaptureModelStreamEvidence& stream : group.streams) {
        if (first_source_row_id == 0 ||
            stream.source_row_id < first_source_row_id) {
          first_source_row_id = stream.source_row_id;
        }
      }
      const CapturedGraphInstanceId instance_id =
          ir.captured_graph_instances.append(
              capture_stream_source_ref, first_source_row_id, group.device_id,
              group.raw_model_id, group.raw_timestamp, capture_ordinal,
              slot_template_id,
              static_cast<std::uint32_t>(group.streams.size()),
              association_policy);
      indexes.by_model_id.emplace(
          CapturedGraphInstanceKey{group.device_id, group.raw_model_id},
          instance_id);
      for (const CaptureModelStreamEvidence& stream : group.streams) {
        const CapturedGraphModelStreamKey stream_key{group.device_id,
                                                     stream.raw_model_stream_id};
        const auto inserted =
            indexes.by_model_stream.emplace(stream_key, instance_id);
        if (!inserted.second && inserted.first->second != instance_id) {
          // A stream claimed by multiple graph instances is ambiguous. Keep
          // the evidence but make it unusable for exact launch association.
          inserted.first->second = CapturedGraphInstanceId::invalid();
        }
        ir.captured_graph_streams.append(
            instance_id, capture_stream_source_ref, stream.source_row_id,
            stream.raw_original_stream_id, stream.raw_model_stream_id);
      }
    }
  }
  return indexes;
}

}  // namespace traceloom::ascend_sqlite_detail
