#include "traceloom/adapters/ascend_sqlite_adapter.h"

#include "traceloom/analysis/exact_periodic_suffix.h"
#include "traceloom/runtime/thread_pool.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
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

namespace traceloom {
namespace {

class Stopwatch {
 public:
  Stopwatch() : start_(Clock::now()) {}

  double elapsed_ms() const {
    const auto elapsed = Clock::now() - start_;
    return std::chrono::duration<double, std::milli>(elapsed).count();
  }

 private:
  using Clock = std::chrono::steady_clock;
  Clock::time_point start_;
};

template <typename Fn>
double time_stage(Fn&& fn) {
  const Stopwatch stopwatch;
  fn();
  return stopwatch.elapsed_ms();
}

struct AscendLoadTiming {
  double sqlite_open_ms = 0.0;
  double inventory_ms = 0.0;
  double string_ids_ms = 0.0;
  double compute_info_ms = 0.0;
  double cann_api_metadata_ms = 0.0;
  double communication_task_info_ms = 0.0;
  double task_rows_ms = 0.0;
  double communication_op_rows_ms = 0.0;
  double stream_info_capture_ms = 0.0;
  double cann_api_capture_slots_ms = 0.0;
  double aclgraph_capture_instances_ms = 0.0;
  double aclgraph_launch_occurrences_ms = 0.0;
  double aclgraph_launch_bodies_ms = 0.0;
  double aclgraph_launch_activities_ms = 0.0;
  double replay_composition_candidates_ms = 0.0;
  double aclgraph_replay_units_ms = 0.0;
};

void print_load_timing(const AscendLoadTiming& timing) {
  std::cerr << "timing load_sqlite_open_ms=" << timing.sqlite_open_ms << "\n";
  std::cerr << "timing load_inventory_ms=" << timing.inventory_ms << "\n";
  std::cerr << "timing load_string_ids_ms=" << timing.string_ids_ms << "\n";
  std::cerr << "timing load_compute_info_ms=" << timing.compute_info_ms
            << "\n";
  std::cerr << "timing load_cann_api_metadata_ms="
            << timing.cann_api_metadata_ms << "\n";
  std::cerr << "timing load_communication_task_info_ms="
            << timing.communication_task_info_ms << "\n";
  std::cerr << "timing load_task_rows_ms=" << timing.task_rows_ms << "\n";
  std::cerr << "timing load_communication_op_rows_ms="
            << timing.communication_op_rows_ms << "\n";
  std::cerr << "timing load_stream_info_capture_ms="
            << timing.stream_info_capture_ms << "\n";
  std::cerr << "timing load_cann_api_capture_slots_ms="
            << timing.cann_api_capture_slots_ms << "\n";
  std::cerr << "timing load_aclgraph_capture_instances_ms="
            << timing.aclgraph_capture_instances_ms << "\n";
  std::cerr << "timing load_aclgraph_launch_occurrences_ms="
            << timing.aclgraph_launch_occurrences_ms << "\n";
  std::cerr << "timing load_aclgraph_launch_bodies_ms="
            << timing.aclgraph_launch_bodies_ms << "\n";
  std::cerr << "timing load_aclgraph_launch_activities_ms="
            << timing.aclgraph_launch_activities_ms << "\n";
  std::cerr << "timing load_replay_composition_candidates_ms="
            << timing.replay_composition_candidates_ms << "\n";
  std::cerr << "timing load_aclgraph_replay_units_ms="
            << timing.aclgraph_replay_units_ms << "\n";
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

std::string quote_identifier(const std::string& value) {
  std::string out = "\"";
  for (char ch : value) {
    if (ch == '\"') {
      out += "\"\"";
    } else {
      out += ch;
    }
  }
  out += "\"";
  return out;
}

bool sqlite_table_has_rows(const std::string& path,
                           const std::string& table_name) {
  try {
    SqliteDb db(path);
    const std::string sql = "SELECT 1 FROM " + quote_identifier(table_name) +
                            " LIMIT 1";
    SqliteStmt stmt(db.get(), sql.c_str());
    return sqlite3_step(stmt.get()) == SQLITE_ROW;
  } catch (const std::exception&) {
    return false;
  }
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
using CapturedGraphInstanceKey = std::pair<std::uint32_t, std::int64_t>;
using CapturedGraphInstanceIndex =
    std::map<CapturedGraphInstanceKey, CapturedGraphInstanceId>;
using CapturedGraphModelStreamKey =
    std::pair<std::uint32_t, std::uint64_t>;

struct CapturedGraphInstanceIndexes {
  CapturedGraphInstanceIndex by_model_id;
  std::map<CapturedGraphModelStreamKey, CapturedGraphInstanceId>
      by_model_stream;
};

struct TaskLink {
  std::uint64_t stream_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  SymbolId comm_name_symbol_id;
  SymbolId comm_task_type_symbol_id;
};

using TaskLinkIndex = std::unordered_map<std::uint64_t, std::vector<TaskLink>>;

struct RawTaskRow {
  std::uint64_t row_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::uint32_t device_id = 0;
  std::uint64_t raw_stream_id = 0;
  std::uint64_t raw_task_id = 0;
  std::int64_t raw_global_task_id = -1;
  std::int64_t raw_connection_id = -1;
  std::int64_t raw_task_type_id = -1;
  std::int64_t raw_model_id = -1;
};

struct RowidRange {
  std::int64_t first = 0;
  std::int64_t last = -1;
};

struct GraphTaskView {
  const TaskRow* task = nullptr;
  const TraceEventRow* event = nullptr;
};

struct GraphReplayUnitView {
  std::vector<GraphTaskView> rows;
  bool has_window = false;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::string template_signature;
};

GraphReplayUnitView replay_unit_for_rows(std::vector<GraphTaskView> rows) {
  GraphReplayUnitView unit;
  unit.rows = std::move(rows);
  return unit;
}

struct ReplayUnitWindow {
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

struct GraphLaunchView {
  std::int64_t raw_row_id = -1;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::int64_t connection_id = 0;
  std::uint64_t raw_global_tid = 0;
};

struct HostBlockingSyncView {
  std::int64_t raw_row_id = -1;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::uint64_t raw_global_tid = 0;
  std::string api_name;
};

struct GraphLaunchActivityView {
  std::uint64_t raw_global_tid = 0;
  std::vector<std::int64_t> host_execute_row_ids;
  std::int64_t first_host_api_row_id = -1;
  std::int64_t last_host_api_row_id = -1;
  std::int64_t boundary_host_api_row_id = -1;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::string boundary_api_name;
  GraphLaunchActivityBoundaryPolicy boundary_policy =
      GraphLaunchActivityBoundaryPolicy::kHostThreadTail;
};

struct CaptureSlotSignature {
  std::uint32_t slot_index = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::string host_api_signature;
};

struct CaptureModelStreamEvidence {
  std::uint64_t source_row_id = 0;
  std::int64_t raw_original_stream_id = -1;
  std::uint64_t raw_model_stream_id = 0;
};

struct CaptureModelGroupEvidence {
  std::uint32_t device_id = 0;
  std::int64_t raw_model_id = -1;
  std::int64_t raw_timestamp = -1;
  std::vector<CaptureModelStreamEvidence> streams;
};

struct CaptureInterval {
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

struct CaptureTokenCandidate {
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::string token;
};

struct AclGraphCannApiMetadata {
  std::vector<GraphLaunchView> execute_launches;
  std::vector<GraphLaunchActivityView> launch_activities;
  std::vector<CaptureSlotSignature> capture_slots;
};

struct AclGraphCaptureInfo {
  std::unordered_map<std::uint32_t, std::unordered_set<std::uint64_t>>
      model_streams_by_device;
  std::uint32_t capture_group_size = 0;
  std::vector<CaptureModelGroupEvidence> model_groups;
  std::vector<CaptureSlotSignature> capture_slots;
  std::string replay_unit_signature;
};

struct GraphTaskSymbolSets {
  std::unordered_set<std::uint32_t> graph_control;
  std::unordered_set<std::uint32_t> notify_wait;
  std::unordered_set<std::uint32_t> notify_record;
  std::unordered_set<std::uint32_t> model_execute;
};

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

std::int64_t normalize_raw_model_id(std::int64_t value) {
  if (value < 0 ||
      value == static_cast<std::int64_t>(
                   std::numeric_limits<std::uint32_t>::max())) {
    return -1;
  }
  return value;
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

bool table_has_columns(SqliteDb& db,
                       const std::string& table_name,
                       std::initializer_list<const char*> columns) {
  return std::all_of(columns.begin(), columns.end(), [&](const char* column) {
    return table_has_column(db, table_name, column);
  });
}

std::string lower_ascii(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

std::string normalize_key(std::string value) {
  for (char& ch : value) {
    if (std::isalnum(static_cast<unsigned char>(ch))) {
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    } else {
      ch = '_';
    }
  }
  while (value.find("__") != std::string::npos) {
    value.replace(value.find("__"), 2, "_");
  }
  while (!value.empty() && value.front() == '_') {
    value.erase(value.begin());
  }
  while (!value.empty() && value.back() == '_') {
    value.pop_back();
  }
  return value;
}

bool graph_task_key(const std::string& key) {
  static const std::unordered_set<std::string> kKeys{
      "MODEL_EXECUTE", "MODEL_MAINTAINCE", "MODEL_MAINTENANCE",
      "NOTIFY_WAIT", "NOTIFY_RECORD"};
  return kKeys.find(key) != kKeys.end();
}

bool graph_body_excluded_task_key(const std::string& key) {
  static const std::unordered_set<std::string> kKeys{
      "AI_CORE",        "CAPTURE_RECORD", "CAPTURE_WAIT",
      "EVENT_RECORD",   "MEM_WRITE_VALUE", "MEMCPY",
      "MEMCPY_ASYNC",   "MODEL_EXECUTE",  "MODEL_MAINTAINCE",
      "MODEL_MAINTENANCE", "NOTIFY",      "NOTIFY_RECORD",
      "NOTIFY_WAIT",    "SDMA",           "WRITE_VALUE"};
  return kKeys.find(key) != kKeys.end();
}

bool graph_body_infrastructure_task_key(const std::string& key) {
  static const std::unordered_set<std::string> kKeys{
      "CAPTURE_RECORD",    "CAPTURE_WAIT",      "EVENT_RECORD",
      "EVENT_WAIT",       "MEM_WRITE_VALUE",   "MODEL_MAINTAINCE",
      "MODEL_MAINTENANCE", "PROFILING_DISABLE", "PROFILING_ENABLE",
      "TASK_TIMEOUT_SET"};
  return kKeys.find(key) != kKeys.end();
}

std::string symbol_value_or_empty(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

std::string graph_body_family(const std::string& label) {
  const std::string low = lower_ascii(label);
  if (low.find("matmul") != std::string::npos ||
      low.find("gemm") != std::string::npos) {
    return "matmul";
  }
  if (low.find("rmsnorm") != std::string::npos ||
      low.find("rms_norm") != std::string::npos ||
      low.find("layernorm") != std::string::npos ||
      low.find("layer_norm") != std::string::npos) {
    return "norm";
  }
  if (low.find("pagedattention") != std::string::npos ||
      low.find("paged_attention") != std::string::npos ||
      low.find("attention") != std::string::npos) {
    return "attention";
  }
  if (low.find("swiglu") != std::string::npos) {
    return "swiglu";
  }
  if (low.find("rope") != std::string::npos ||
      low.find("rotary") != std::string::npos) {
    return "rope";
  }
  if (low.find("cast") != std::string::npos) {
    return "cast";
  }
  if (low.find("fill") != std::string::npos ||
      low.find("zeroslike") != std::string::npos ||
      low.find("oneslike") != std::string::npos) {
    return "fill";
  }
  if (low.find("shape") != std::string::npos ||
      low.find("reshape") != std::string::npos ||
      low.find("transpose") != std::string::npos ||
      low.find("slice") != std::string::npos ||
      low.find("tile") != std::string::npos ||
      low.find("broadcastto") != std::string::npos ||
      low.find("expand") != std::string::npos) {
    return "shape";
  }
  if (low.find("index") != std::string::npos ||
      low.find("gather") != std::string::npos ||
      low.find("scatter") != std::string::npos) {
    return "index";
  }
  if (low.find("range") != std::string::npos ||
      low.find("arange") != std::string::npos) {
    return "range";
  }
  if (low.find("div") != std::string::npos ||
      low.find("reciprocal") != std::string::npos) {
    return "div";
  }
  if (low.find("pow") != std::string::npos) {
    return "pow";
  }
  if (low.find("trig") != std::string::npos ||
      low.find("cos") != std::string::npos ||
      low.find("sin") != std::string::npos) {
    return "trig";
  }
  if (low.find("concat") != std::string::npos || low == "cat") {
    return "concat";
  }
  if (low.find("quant") != std::string::npos) {
    return "quant";
  }
  if (low.find("compare") != std::string::npos ||
      low.find("greaterequal") != std::string::npos ||
      low.find("less") != std::string::npos ||
      low.find("logical") != std::string::npos ||
      low.find("bitwise") != std::string::npos) {
    return "compare";
  }
  if (low.find("elemwise") != std::string::npos || low == "add" ||
      low == "sub" || low == "mul") {
    return "elemwise";
  }
  if (low.find("copy") != std::string::npos ||
      low.find("memcpy") != std::string::npos ||
      low.find("tensor move") != std::string::npos) {
    return "data_move";
  }
  return "";
}

std::string canonical_graph_body_label(const std::string& family,
                                       const std::string& fallback) {
  if (family == "matmul") {
    return "MatMul";
  }
  if (family == "norm") {
    return "RmsNorm";
  }
  if (family == "attention") {
    return "Attention";
  }
  if (family == "swiglu") {
    return "SwiGlu";
  }
  if (family == "rope") {
    return "Rope";
  }
  if (family == "index") {
    return "Index";
  }
  if (family == "shape") {
    return "Shape";
  }
  if (family == "cast") {
    return "Cast";
  }
  return fallback.empty() ? family : fallback;
}

std::string graph_body_token(const NativeIr& ir, const TaskRow& task) {
  const std::string task_label = symbol_value_or_empty(ir, task.task_type_symbol_id);
  const std::string task_key = normalize_key(task_label);
  if (graph_body_excluded_task_key(task_key)) {
    return "";
  }
  std::string raw_op = symbol_value_or_empty(ir, task.op_type_symbol_id);
  if (raw_op.empty()) {
    raw_op = symbol_value_or_empty(ir, task.op_name_symbol_id);
  }
  if (raw_op.empty()) {
    raw_op = task_label;
  }
  const std::string family = graph_body_family(raw_op);
  if (family.empty() || family == "data_move") {
    return "";
  }
  return family + "|" + canonical_graph_body_label(family, raw_op);
}

std::uint64_t stable_hash64(const std::string& text) {
  std::uint64_t hash = 1469598103934665603ull;
  for (unsigned char ch : text) {
    hash ^= ch;
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string body_signature(const NativeIr& ir,
                           const std::vector<GraphTaskView>& rows) {
  std::map<std::string, std::uint32_t> counts;
  for (const GraphTaskView& row : rows) {
    const std::string token = graph_body_token(ir, *row.task);
    if (!token.empty()) {
      ++counts[token];
    }
  }
  std::string out;
  for (const auto& item : counts) {
    out += item.first + ":" + std::to_string(item.second) + "\n";
  }
  return out;
}

std::uint32_t infer_replay_unit_count(
    const std::map<std::string, std::uint32_t>& body_counts,
    const std::map<std::string, std::uint32_t>& control_counts,
    std::uint32_t capture_group_size) {
  (void)body_counts;
  const auto model_execute = control_counts.find("MODEL_EXECUTE");
  if (capture_group_size > 1 && model_execute != control_counts.end() &&
      model_execute->second > 0) {
    return std::max<std::uint32_t>(
        1, (model_execute->second + capture_group_size / 2) /
               capture_group_size);
  }
  const auto notify_wait = control_counts.find("NOTIFY_WAIT");
  if (capture_group_size > 1 && notify_wait != control_counts.end() &&
      notify_wait->second > 0) {
    return std::max<std::uint32_t>(
        1, (notify_wait->second + capture_group_size / 2) /
               capture_group_size);
  }
  if (model_execute != control_counts.end() && model_execute->second > 0) {
    return model_execute->second;
  }
  if (notify_wait != control_counts.end() && notify_wait->second > 0) {
    if (notify_wait->second % 29 == 0) {
      return std::max<std::uint32_t>(1, notify_wait->second / 29);
    }
    return notify_wait->second;
  }
  return 1;
}

std::vector<std::int64_t> valid_inner_boundaries(
    std::vector<std::int64_t> boundaries) {
  std::sort(boundaries.begin(), boundaries.end());
  std::vector<std::int64_t> out;
  std::int64_t last = -1;
  for (std::int64_t value : boundaries) {
    if (value <= 0 || value <= last) {
      return {};
    }
    out.push_back(value);
    last = value;
  }
  return out;
}

std::vector<GraphReplayUnitView> split_rows_by_boundaries(
    const std::vector<GraphTaskView>& rows,
    const std::vector<std::int64_t>& boundaries) {
  std::vector<GraphReplayUnitView> out(boundaries.size() + 1);
  std::size_t cursor = 0;
  for (const GraphTaskView& row : rows) {
    while (cursor < boundaries.size() &&
           row.event->start_ns >= boundaries[cursor]) {
      ++cursor;
    }
    out[cursor].rows.push_back(row);
  }
  for (const GraphReplayUnitView& unit : out) {
    if (unit.rows.empty()) {
      return {replay_unit_for_rows(rows)};
    }
  }
  return out;
}

std::vector<GraphReplayUnitView> split_rows_by_execute_waves(
    const std::vector<GraphTaskView>& rows,
    const std::vector<GraphLaunchView>& launches,
    std::uint32_t capture_group_size) {
  if (rows.empty() || capture_group_size == 0 ||
      launches.size() < capture_group_size ||
      launches.size() % capture_group_size != 0) {
    return {};
  }

  std::vector<GraphLaunchView> ordered_launches = launches;
  std::sort(ordered_launches.begin(), ordered_launches.end(),
            [](const GraphLaunchView& lhs, const GraphLaunchView& rhs) {
              if (lhs.start_ns != rhs.start_ns) {
                return lhs.start_ns < rhs.start_ns;
              }
              if (lhs.end_ns != rhs.end_ns) {
                return lhs.end_ns < rhs.end_ns;
              }
              return lhs.connection_id < rhs.connection_id;
            });

  const std::size_t group_size = static_cast<std::size_t>(capture_group_size);
  const std::size_t wave_count = ordered_launches.size() / group_size;
  if (wave_count <= 1) {
    return {};
  }

  std::vector<std::int64_t> boundaries;
  boundaries.reserve(wave_count - 1);
  for (std::size_t wave = 1; wave < wave_count; ++wave) {
    boundaries.push_back(ordered_launches[wave * group_size].start_ns);
  }
  boundaries = valid_inner_boundaries(std::move(boundaries));
  if (boundaries.size() + 1 != wave_count) {
    return {};
  }

  std::vector<GraphReplayUnitView> waves(wave_count);
  std::size_t cursor = 0;
  for (const GraphTaskView& row : rows) {
    while (cursor < boundaries.size() &&
           row.event->start_ns >= boundaries[cursor]) {
      ++cursor;
    }
    waves[cursor].rows.push_back(row);
  }

  std::vector<GraphReplayUnitView> out;
  out.reserve(wave_count);
  for (std::size_t wave = 0; wave < wave_count; ++wave) {
    GraphReplayUnitView& unit = waves[wave];
    if (unit.rows.empty()) {
      continue;
    }
    // Host launches identify wave membership. The replay interval itself is a
    // device-side TASK envelope; including the next launch or the host API
    // would turn scheduler gaps and launch overhead into graph execution cost.
    unit.has_window = true;
    unit.start_ns = unit.rows.front().event->start_ns;
    unit.end_ns = unit.rows.front().event->end_ns;
    for (const GraphTaskView& row : unit.rows) {
      unit.start_ns = std::min(unit.start_ns, row.event->start_ns);
      unit.end_ns = std::max(unit.end_ns, row.event->end_ns);
    }
    if (unit.end_ns <= unit.start_ns) {
      unit.has_window = false;
    }
    out.push_back(std::move(unit));
  }
  return out;
}

std::vector<GraphLaunchView> device_backed_execute_launches(
    const std::vector<GraphLaunchView>& launches,
    const std::vector<GraphTaskView>& model_executes,
    std::uint32_t capture_group_size) {
  if (launches.empty() || model_executes.empty() || capture_group_size == 0) {
    return launches;
  }
  std::unordered_set<std::int64_t> device_connections;
  device_connections.reserve(model_executes.size());
  for (const GraphTaskView& model_execute : model_executes) {
    if (model_execute.task->raw_connection_id >= 0) {
      device_connections.insert(model_execute.task->raw_connection_id);
    }
  }
  std::vector<GraphLaunchView> matched;
  matched.reserve(launches.size());
  for (const GraphLaunchView& launch : launches) {
    if (device_connections.find(launch.connection_id) !=
        device_connections.end()) {
      matched.push_back(launch);
    }
  }
  // Some profiles retain trailing aclmdlRIExecuteAsync host calls without a
  // corresponding device MODEL_EXECUTE. Prefer correlation-backed launches
  // only when they still form complete capture groups.
  if (matched.size() >= capture_group_size &&
      matched.size() % capture_group_size == 0) {
    return matched;
  }
  return launches;
}

std::vector<std::int64_t> control_boundaries(
    const std::vector<GraphTaskView>& controls,
    std::uint32_t expected_count) {
  if (expected_count <= 1 || controls.size() < expected_count) {
    return {};
  }
  const std::size_t wave_size = controls.size() / expected_count;
  if (wave_size == 0) {
    return {};
  }
  std::vector<std::int64_t> boundaries;
  for (std::uint32_t unit = 1; unit < expected_count; ++unit) {
    const std::size_t index = static_cast<std::size_t>(unit) * wave_size;
    if (index >= controls.size()) {
      return {};
    }
    boundaries.push_back(controls[index].event->start_ns);
  }
  return valid_inner_boundaries(std::move(boundaries));
}

std::vector<GraphReplayUnitView> split_activity(
    const NativeIr& ir,
    const std::vector<GraphTaskView>& rows,
    const std::vector<GraphTaskView>& notify_waits,
    const std::vector<GraphTaskView>& model_execs,
    std::uint32_t capture_group_size) {
  std::map<std::string, std::uint32_t> body_counts;
  for (const GraphTaskView& row : rows) {
    const std::string token = graph_body_token(ir, *row.task);
    if (!token.empty()) {
      ++body_counts[token];
    }
  }
  std::map<std::string, std::uint32_t> control_counts;
  control_counts["NOTIFY_WAIT"] = static_cast<std::uint32_t>(notify_waits.size());
  control_counts["MODEL_EXECUTE"] = static_cast<std::uint32_t>(model_execs.size());
  const std::uint32_t expected_count =
      infer_replay_unit_count(body_counts, control_counts, capture_group_size);
  if (expected_count <= 1 || rows.size() <= 1) {
    return {replay_unit_for_rows(rows)};
  }
  (void)ir;
  std::vector<std::int64_t> boundaries =
      control_boundaries(model_execs, expected_count);
  if (boundaries.empty()) {
    boundaries = control_boundaries(notify_waits, expected_count);
  }
  if (boundaries.empty()) {
    return {replay_unit_for_rows(rows)};
  }
  return split_rows_by_boundaries(rows, boundaries);
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
                                           bool has_model_id) {
  std::string sql =
      "SELECT rowid, startNs, endNs, deviceId, streamId, taskId, "
      "globalTaskId, connectionId, taskType, " +
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
          sqlite_i64(stmt.get(), 8),
          normalize_raw_model_id(sqlite_i64(stmt.get(), 9, -1))});
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
  if (reader_count <= 1) {
    return read_task_raw_rows(db, nullptr, true, has_model_id);
  }
  const std::vector<RowidRange> ranges = task_rowid_ranges(db, reader_count);
  if (ranges.size() <= 1) {
    return read_task_raw_rows(db, nullptr, true, has_model_id);
  }

  std::vector<std::vector<RawTaskRow>> chunks(ranges.size());
  ThreadPool pool(reader_count);
  pool.parallel_for(ranges.size(), [&](std::size_t index) {
    SqliteDb worker_db(db_path);
    chunks[index] =
        read_task_raw_rows(worker_db, &ranges[index], false, has_model_id);
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
                    comm_task.task_type_symbol_id);
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

bool event_overlaps(const TraceEventRow& event,
                    std::int64_t start_ns,
                    std::int64_t end_ns) {
  return event.start_ns <= end_ns && event.end_ns >= start_ns;
}

std::vector<GraphTaskView> controls_with_symbol_set(
    const std::vector<GraphTaskView>& controls,
    const std::unordered_set<std::uint32_t>& task_type_symbols) {
  std::vector<GraphTaskView> out;
  for (const GraphTaskView& row : controls) {
    if (symbol_in_set(task_type_symbols, row.task->task_type_symbol_id)) {
      out.push_back(row);
    }
  }
  return out;
}

std::vector<GraphTaskView> controls_in_interval_from_sorted(
    const std::vector<GraphTaskView>& controls,
    std::int64_t start_ns,
    std::int64_t end_ns,
    std::size_t& cursor) {
  while (cursor < controls.size() && controls[cursor].event->end_ns < start_ns) {
    ++cursor;
  }
  std::vector<GraphTaskView> out;
  std::size_t scan = cursor;
  while (scan < controls.size() && controls[scan].event->start_ns <= end_ns) {
    if (event_overlaps(*controls[scan].event, start_ns, end_ns)) {
      out.push_back(controls[scan]);
    }
    ++scan;
  }
  return out;
}

std::uint64_t absolute_timestamp_delta(std::int64_t lhs, std::int64_t rhs) {
  static constexpr std::uint64_t kSignBit = std::uint64_t{1} << 63u;
  const std::uint64_t ordered_lhs = static_cast<std::uint64_t>(lhs) ^ kSignBit;
  const std::uint64_t ordered_rhs = static_cast<std::uint64_t>(rhs) ^ kSignBit;
  return ordered_lhs >= ordered_rhs ? ordered_lhs - ordered_rhs
                                    : ordered_rhs - ordered_lhs;
}

void materialize_aclgraph_launch_occurrences(
    NativeIr& ir,
    const StreamIndex& streams,
    const CapturedGraphInstanceIndexes& captured_graph_instances,
    const std::vector<GraphLaunchView>& host_execute_launches,
    SourceRefId host_api_source_ref) {
  const GraphTaskSymbolSets graph_symbols =
      build_graph_task_symbol_sets(ir.symbols);
  std::vector<GraphTaskView> model_executes;
  std::vector<GraphTaskView> notify_waits;
  std::vector<GraphTaskView> notify_records;
  for (const TaskRow& task : ir.tasks.rows()) {
    if (!task.trace_event_id.valid()) {
      continue;
    }
    const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
    const GraphTaskView view{&task, &event};
    if (symbol_in_set(graph_symbols.model_execute,
                      task.task_type_symbol_id)) {
      model_executes.push_back(view);
    } else if (symbol_in_set(graph_symbols.notify_wait,
                             task.task_type_symbol_id)) {
      notify_waits.push_back(view);
    } else if (symbol_in_set(graph_symbols.notify_record,
                             task.task_type_symbol_id)) {
      notify_records.push_back(view);
    }
  }
  if (model_executes.empty()) {
    return;
  }

  const auto graph_task_less = [](const GraphTaskView& lhs,
                                  const GraphTaskView& rhs) {
    if (lhs.event->start_ns != rhs.event->start_ns) {
      return lhs.event->start_ns < rhs.event->start_ns;
    }
    if (lhs.event->end_ns != rhs.event->end_ns) {
      return lhs.event->end_ns < rhs.event->end_ns;
    }
    if (lhs.event->device_id != rhs.event->device_id) {
      return lhs.event->device_id < rhs.event->device_id;
    }
    return lhs.task->id < rhs.task->id;
  };
  std::sort(model_executes.begin(), model_executes.end(), graph_task_less);
  std::sort(notify_waits.begin(), notify_waits.end(), graph_task_less);
  std::sort(notify_records.begin(), notify_records.end(), graph_task_less);

  struct WorkingLaunch {
    GraphTaskView execute;
    const GraphLaunchView* host_execute = nullptr;
    const GraphTaskView* wait = nullptr;
    const GraphTaskView* record = nullptr;
    GraphLaunchMatchPolicy policy = GraphLaunchMatchPolicy::kUnmatched;
  };
  std::vector<WorkingLaunch> launches;
  launches.reserve(model_executes.size());

  std::unordered_map<std::int64_t, std::vector<const GraphLaunchView*>>
      host_by_connection;
  for (const GraphLaunchView& host : host_execute_launches) {
    host_by_connection[host.connection_id].push_back(&host);
  }
  std::vector<bool> wait_claimed(notify_waits.size(), false);
  for (const GraphTaskView& execute : model_executes) {
    WorkingLaunch launch;
    launch.execute = execute;
    const auto host_found =
        host_by_connection.find(execute.task->raw_connection_id);
    if (host_found != host_by_connection.end()) {
      for (const GraphLaunchView* candidate : host_found->second) {
        if (launch.host_execute == nullptr ||
            absolute_timestamp_delta(candidate->end_ns,
                                     execute.event->start_ns) <
                absolute_timestamp_delta(launch.host_execute->end_ns,
                                         execute.event->start_ns)) {
          launch.host_execute = candidate;
        }
      }
    }

    std::size_t best_wait = notify_waits.size();
    std::uint64_t best_delta = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t index = 0; index < notify_waits.size(); ++index) {
      if (wait_claimed[index]) {
        continue;
      }
      const GraphTaskView& candidate = notify_waits[index];
      if (candidate.event->device_id != execute.event->device_id ||
          candidate.task->raw_connection_id !=
              execute.task->raw_connection_id) {
        continue;
      }
      const std::uint64_t delta = absolute_timestamp_delta(
          candidate.event->start_ns, execute.event->start_ns);
      if (delta < best_delta) {
        best_wait = index;
        best_delta = delta;
      }
    }
    if (best_wait != notify_waits.size()) {
      wait_claimed[best_wait] = true;
      launch.wait = &notify_waits[best_wait];
    }
    launches.push_back(launch);
  }

  struct CompletionCandidate {
    std::size_t launch_index = 0;
    std::size_t record_index = 0;
    std::uint64_t absolute_delta_ns = 0;
  };
  static constexpr std::uint64_t kCompletionAdjacencyThresholdNs = 10'000;
  std::vector<CompletionCandidate> candidates;
  for (std::size_t launch_index = 0; launch_index < launches.size();
       ++launch_index) {
    const WorkingLaunch& launch = launches[launch_index];
    if (launch.wait == nullptr) {
      continue;
    }
    for (std::size_t record_index = 0; record_index < notify_records.size();
         ++record_index) {
      const GraphTaskView& record = notify_records[record_index];
      if (record.event->device_id != launch.execute.event->device_id) {
        continue;
      }
      const std::uint64_t delta = absolute_timestamp_delta(
          launch.wait->event->end_ns, record.event->end_ns);
      if (delta <= kCompletionAdjacencyThresholdNs) {
        candidates.push_back(
            CompletionCandidate{launch_index, record_index, delta});
      }
    }
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const CompletionCandidate& lhs,
               const CompletionCandidate& rhs) {
              if (lhs.absolute_delta_ns != rhs.absolute_delta_ns) {
                return lhs.absolute_delta_ns < rhs.absolute_delta_ns;
              }
              if (lhs.launch_index != rhs.launch_index) {
                return lhs.launch_index < rhs.launch_index;
              }
              return lhs.record_index < rhs.record_index;
            });
  std::vector<bool> record_claimed(notify_records.size(), false);
  for (const CompletionCandidate& candidate : candidates) {
    WorkingLaunch& launch = launches[candidate.launch_index];
    if (launch.record != nullptr || record_claimed[candidate.record_index]) {
      continue;
    }
    launch.record = &notify_records[candidate.record_index];
    launch.policy = GraphLaunchMatchPolicy::kNotifyCompletionAdjacent;
    record_claimed[candidate.record_index] = true;
  }

  std::map<std::uint32_t, std::vector<std::size_t>> unmatched_by_device;
  std::map<std::uint32_t, std::vector<std::size_t>> records_by_device;
  for (std::size_t index = 0; index < launches.size(); ++index) {
    const WorkingLaunch& launch = launches[index];
    if (launch.wait != nullptr && launch.record == nullptr) {
      unmatched_by_device[launch.execute.event->device_id].push_back(index);
    }
  }
  for (std::size_t index = 0; index < notify_records.size(); ++index) {
    if (!record_claimed[index]) {
      records_by_device[notify_records[index].event->device_id].push_back(
          index);
    }
  }
  for (const auto& item : unmatched_by_device) {
    const auto records_found = records_by_device.find(item.first);
    if (records_found == records_by_device.end() ||
        item.second.size() != records_found->second.size()) {
      continue;
    }
    for (std::size_t index = 0; index < item.second.size(); ++index) {
      WorkingLaunch& launch = launches[item.second[index]];
      const std::size_t record_index = records_found->second[index];
      launch.record = &notify_records[record_index];
      launch.policy = GraphLaunchMatchPolicy::kNotifyOrderedFallback;
      record_claimed[record_index] = true;
    }
  }

  ir.graph_launch_occurrences.reserve(launches.size());
  for (const WorkingLaunch& launch : launches) {
    const TraceEventRow& execute_event = *launch.execute.event;
    const auto execute_stream =
        streams.find(stream_key(execute_event.device_id,
                                execute_event.stream_id));
    StreamId model_stream_id = StreamId::invalid();
    CapturedGraphInstanceId captured_graph_instance_id =
        CapturedGraphInstanceId::invalid();
    GraphLaunchInstanceAssociationPolicy instance_association_policy =
        GraphLaunchInstanceAssociationPolicy::kNone;
    TaskId wait_task_id = TaskId::invalid();
    TaskId record_task_id = TaskId::invalid();
    std::int64_t raw_graph_connection_id = -1;
    std::int64_t raw_model_id = -1;
    std::int64_t end_ns = execute_event.end_ns;
    std::int64_t wait_record_delta_ns = -1;
    if (launch.wait != nullptr) {
      wait_task_id = launch.wait->task->id;
      end_ns = std::max(end_ns, launch.wait->event->end_ns);
    }
    if (launch.record != nullptr) {
      record_task_id = launch.record->task->id;
      raw_graph_connection_id = launch.record->task->raw_connection_id;
      raw_model_id = launch.record->task->raw_model_id;
      if (raw_model_id >= 0) {
        const auto instance = captured_graph_instances.by_model_id.find(
            CapturedGraphInstanceKey{execute_event.device_id, raw_model_id});
        if (instance != captured_graph_instances.by_model_id.end()) {
          captured_graph_instance_id = instance->second;
          instance_association_policy =
              GraphLaunchInstanceAssociationPolicy::kRecordModelId;
        }
      }
      end_ns = std::max(end_ns, launch.record->event->end_ns);
      wait_record_delta_ns =
          launch.wait->event->end_ns - launch.record->event->end_ns;
      const auto model_stream = streams.find(stream_key(
          launch.record->event->device_id, launch.record->event->stream_id));
      if (model_stream != streams.end()) {
        model_stream_id = model_stream->second;
      }
      if (!captured_graph_instance_id.valid()) {
        const auto instance = captured_graph_instances.by_model_stream.find(
            CapturedGraphModelStreamKey{launch.record->event->device_id,
                                        launch.record->event->stream_id});
        if (instance != captured_graph_instances.by_model_stream.end() &&
            instance->second.valid()) {
          captured_graph_instance_id = instance->second;
          instance_association_policy =
              GraphLaunchInstanceAssociationPolicy::kRecordModelStream;
        }
      }
    }
    ir.graph_launch_occurrences.append(
        launch.execute.task->source_ref_id, host_api_source_ref,
        execute_event.device_id,
        launch.host_execute == nullptr ? -1 : launch.host_execute->raw_row_id,
        launch.execute.task->raw_connection_id, raw_graph_connection_id,
        raw_model_id,
        execute_stream == streams.end() ? StreamId::invalid()
                                        : execute_stream->second,
        model_stream_id, captured_graph_instance_id, launch.execute.task->id,
        wait_task_id, record_task_id, execute_event.start_ns, end_ns,
        wait_record_delta_ns, launch.policy, instance_association_policy);
  }
}

void materialize_graph_launch_activities(
    NativeIr& ir,
    const std::vector<GraphLaunchActivityView>& activities,
    SourceRefId host_api_source_ref) {
  if (!host_api_source_ref.valid()) {
    return;
  }
  std::unordered_map<std::int64_t,
                     std::vector<GraphLaunchOccurrenceId>>
      launches_by_host_row;
  for (const GraphLaunchOccurrenceRow& launch :
       ir.graph_launch_occurrences.rows()) {
    if (launch.raw_host_api_row_id >= 0) {
      launches_by_host_row[launch.raw_host_api_row_id].push_back(launch.id);
    }
  }

  for (const GraphLaunchActivityView& activity : activities) {
    std::uint32_t matched_launch_count = 0;
    for (std::int64_t row_id : activity.host_execute_row_ids) {
      const auto found = launches_by_host_row.find(row_id);
      if (found != launches_by_host_row.end()) {
        matched_launch_count +=
            static_cast<std::uint32_t>(found->second.size());
      }
    }
    const GraphLaunchActivityId activity_id =
        ir.graph_launch_activities.append(
            host_api_source_ref, activity.raw_global_tid,
            activity.first_host_api_row_id, activity.last_host_api_row_id,
            activity.boundary_host_api_row_id,
            activity.boundary_api_name.empty()
                ? SymbolId::invalid()
                : ir.symbols.intern(activity.boundary_api_name),
            activity.start_ns, activity.end_ns,
            static_cast<std::uint32_t>(
                activity.host_execute_row_ids.size()),
            matched_launch_count, activity.boundary_policy);
    for (std::size_t order = 0;
         order < activity.host_execute_row_ids.size(); ++order) {
      const auto found =
          launches_by_host_row.find(activity.host_execute_row_ids[order]);
      if (found == launches_by_host_row.end()) {
        continue;
      }
      for (GraphLaunchOccurrenceId launch_id : found->second) {
        ir.graph_launch_activity_members.append(
            activity_id, launch_id, static_cast<std::uint32_t>(order));
      }
    }
  }
}

std::set<GraphLaunchOccurrenceId> materialize_graph_launch_bodies(
    NativeIr& ir,
    bool compute_identity_source,
    bool communication_identity_source) {
  std::unordered_map<std::uint64_t, std::vector<GraphTaskView>>
      tasks_by_stream;
  std::unordered_map<std::uint64_t, std::vector<GraphTaskView>>
      normalized_tasks_by_stream;
  for (const TaskRow& task : ir.tasks.rows()) {
    if (!task.trace_event_id.valid()) {
      continue;
    }
    const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
    tasks_by_stream[stream_key(event.device_id, event.stream_id)].push_back(
        GraphTaskView{&task, &event});
    if (!task.op_type_symbol_id.valid() && !task.op_name_symbol_id.valid() &&
        !task.comm_name_symbol_id.valid()) {
      continue;
    }
    normalized_tasks_by_stream[stream_key(event.device_id, event.stream_id)]
        .push_back(GraphTaskView{&task, &event});
  }
  const auto task_order = [](const GraphTaskView& lhs,
                             const GraphTaskView& rhs) {
    if (lhs.event->start_ns != rhs.event->start_ns) {
      return lhs.event->start_ns < rhs.event->start_ns;
    }
    if (lhs.event->end_ns != rhs.event->end_ns) {
      return lhs.event->end_ns < rhs.event->end_ns;
    }
    return lhs.task->id < rhs.task->id;
  };
  for (auto& item : normalized_tasks_by_stream) {
    std::sort(item.second.begin(), item.second.end(), task_order);
  }
  for (auto& item : tasks_by_stream) {
    std::sort(item.second.begin(), item.second.end(), task_order);
  }

  struct StreamBody {
    std::uint64_t raw_stream_id = 0;
    std::vector<const GraphTaskView*> tasks;
    std::string exact_sequence;
    std::string readable_sequence;
  };

  std::map<std::string, ReplayBodyTemplateId> templates_by_topology;
  std::set<GraphLaunchOccurrenceId> missing_body_capability_launches;
  for (const GraphLaunchOccurrenceRow& launch :
       ir.graph_launch_occurrences.rows()) {
    if (launch.raw_graph_connection_id >= 0 &&
        !launch.captured_graph_instance_id.valid()) {
      missing_body_capability_launches.insert(launch.id);
    }
    if (!launch.model_stream_id.valid()) {
      continue;
    }

    ReplayBodyTopologyPolicy topology_policy =
        ReplayBodyTopologyPolicy::kSingleModelStream;
    std::vector<std::uint64_t> body_stream_ids;
    if (launch.captured_graph_instance_id.valid()) {
      if (launch.captured_graph_instance_id.value() >=
          ir.captured_graph_instances.size()) {
        throw std::invalid_argument(
            "graph launch references invalid captured graph instance");
      }
      const CapturedGraphInstanceRow& instance =
          ir.captured_graph_instances.row(launch.captured_graph_instance_id);
      for (const CapturedGraphStreamRow& stream :
           ir.captured_graph_streams.rows()) {
        if (stream.captured_graph_instance_id == instance.id) {
          body_stream_ids.push_back(stream.raw_model_stream_id);
        }
      }
      std::sort(body_stream_ids.begin(), body_stream_ids.end());
      body_stream_ids.erase(
          std::unique(body_stream_ids.begin(), body_stream_ids.end()),
          body_stream_ids.end());
      if (body_stream_ids.empty() ||
          body_stream_ids.size() != instance.model_stream_count) {
        continue;
      }
      const StreamRow& launch_model_stream =
          ir.streams.row(launch.model_stream_id);
      if (!std::binary_search(body_stream_ids.begin(), body_stream_ids.end(),
                              launch_model_stream.raw_stream_id)) {
        continue;
      }
      topology_policy =
          ReplayBodyTopologyPolicy::kCapturedStreamSetUnordered;
    } else {
      body_stream_ids.push_back(
          ir.streams.row(launch.model_stream_id).raw_stream_id);
    }

    std::vector<StreamBody> stream_bodies;
    stream_bodies.reserve(body_stream_ids.size());
    std::vector<const GraphTaskView*> body_tasks;
    bool launch_identity_coverage = true;
    for (std::uint64_t raw_stream_id : body_stream_ids) {
      StreamBody stream_body;
      stream_body.raw_stream_id = raw_stream_id;
      if (!compute_identity_source || !communication_identity_source) {
        const auto all_tasks = tasks_by_stream.find(
            stream_key(launch.device_id, raw_stream_id));
        if (all_tasks != tasks_by_stream.end()) {
          auto task = std::lower_bound(
              all_tasks->second.begin(), all_tasks->second.end(),
              launch.start_ns,
              [](const GraphTaskView& row, std::int64_t start_ns) {
                return row.event->start_ns < start_ns;
              });
          for (; task != all_tasks->second.end() &&
                 task->event->start_ns <= launch.end_ns;
               ++task) {
            if (task->event->end_ns > launch.end_ns ||
                (launch.raw_model_id >= 0 &&
                 task->task->raw_model_id != launch.raw_model_id) ||
                task->task->id == launch.model_execute_task_id ||
                task->task->id == launch.notify_wait_task_id ||
                task->task->id == launch.notify_record_task_id ||
                task->task->op_type_symbol_id.valid() ||
                task->task->op_name_symbol_id.valid() ||
                task->task->comm_name_symbol_id.valid()) {
              continue;
            }
            const std::string task_key = normalize_key(symbol_value_or_empty(
                ir, task->task->task_type_symbol_id));
            if (!graph_body_infrastructure_task_key(task_key)) {
              launch_identity_coverage = false;
              break;
            }
          }
        }
      }
      const auto found = normalized_tasks_by_stream.find(
          stream_key(launch.device_id, raw_stream_id));
      if (found != normalized_tasks_by_stream.end()) {
        const std::vector<GraphTaskView>& tasks = found->second;
        auto task = std::lower_bound(
            tasks.begin(), tasks.end(), launch.start_ns,
            [](const GraphTaskView& row, std::int64_t start_ns) {
              return row.event->start_ns < start_ns;
            });
        for (; task != tasks.end() && task->event->start_ns <= launch.end_ns;
             ++task) {
          if (task->event->end_ns > launch.end_ns ||
              (launch.raw_model_id >= 0 &&
               task->task->raw_model_id != launch.raw_model_id)) {
            continue;
          }
          stream_body.tasks.push_back(&*task);
          body_tasks.push_back(&*task);
        }
      }
      for (const GraphTaskView* row : stream_body.tasks) {
        const TaskRow& task = *row->task;
        const bool is_communication =
            !task.op_type_symbol_id.valid() &&
            !task.op_name_symbol_id.valid() &&
            task.comm_name_symbol_id.valid();
        std::string op;
        if (is_communication) {
          op = ir.symbols.value(task.comm_name_symbol_id);
          const std::size_t suffix = op.find("__");
          if (suffix != std::string::npos) {
            op.resize(suffix);
          }
          op = "comm:" + op;
          if (task.communication_task_type_symbol_id.valid()) {
            op += "/" +
                  ir.symbols.value(task.communication_task_type_symbol_id);
          }
        } else {
          op = task.op_type_symbol_id.valid()
                   ? ir.symbols.value(task.op_type_symbol_id)
                   : ir.symbols.value(task.op_name_symbol_id);
        }
        if (!stream_body.readable_sequence.empty()) {
          stream_body.readable_sequence += "\n";
        }
        stream_body.readable_sequence += op;
        stream_body.exact_sequence += is_communication ? "communication\t"
                                                       : "compute\t";
        stream_body.exact_sequence += op;
        stream_body.exact_sequence += "\t";
        if (task.compute_task_type_symbol_id.valid()) {
          stream_body.exact_sequence +=
              ir.symbols.value(task.compute_task_type_symbol_id);
        } else if (task.communication_task_type_symbol_id.valid()) {
          stream_body.exact_sequence +=
              ir.symbols.value(task.communication_task_type_symbol_id);
        }
        stream_body.exact_sequence += "\t";
        if (task.task_type_symbol_id.valid()) {
          stream_body.exact_sequence +=
              ir.symbols.value(task.task_type_symbol_id);
        }
        stream_body.exact_sequence += "\n";
      }
      stream_bodies.push_back(std::move(stream_body));
    }
    if (!launch_identity_coverage) {
      missing_body_capability_launches.insert(launch.id);
      continue;
    }
    if (body_tasks.empty()) {
      continue;
    }

    std::sort(stream_bodies.begin(), stream_bodies.end(),
              [](const StreamBody& lhs, const StreamBody& rhs) {
                if (lhs.exact_sequence != rhs.exact_sequence) {
                  return lhs.exact_sequence < rhs.exact_sequence;
                }
                return lhs.raw_stream_id < rhs.raw_stream_id;
              });
    std::string exact_topology =
        topology_policy ==
                ReplayBodyTopologyPolicy::kCapturedStreamSetUnordered
            ? "captured_stream_set_unordered\n"
            : "single_model_stream\n";
    exact_topology += "stream_count=" +
                      std::to_string(stream_bodies.size()) + "\n";
    std::string readable_topology;
    for (std::size_t lane = 0; lane < stream_bodies.size(); ++lane) {
      exact_topology += "lane_begin\n";
      exact_topology += stream_bodies[lane].exact_sequence;
      exact_topology += "lane_end\n";
      if (stream_bodies.size() == 1) {
        readable_topology = stream_bodies[lane].readable_sequence;
        continue;
      }
      if (!readable_topology.empty()) {
        readable_topology += "\n";
      }
      readable_topology += "lane " + std::to_string(lane) + ":\n";
      readable_topology += stream_bodies[lane].readable_sequence.empty()
                               ? "<no normalized compute>"
                               : stream_bodies[lane].readable_sequence;
    }
    std::sort(body_tasks.begin(), body_tasks.end(),
              [&](const GraphTaskView* lhs, const GraphTaskView* rhs) {
                return task_order(*lhs, *rhs);
              });
    const std::uint32_t communication_task_count =
        static_cast<std::uint32_t>(std::count_if(
            body_tasks.begin(), body_tasks.end(),
            [](const GraphTaskView* row) {
              return !row->task->op_type_symbol_id.valid() &&
                     !row->task->op_name_symbol_id.valid() &&
                     row->task->comm_name_symbol_id.valid();
            }));
    const std::uint32_t compute_task_count =
        static_cast<std::uint32_t>(body_tasks.size()) -
        communication_task_count;

    ReplayBodyTemplateId template_id = ReplayBodyTemplateId::invalid();
    const auto existing = templates_by_topology.find(exact_topology);
    if (existing == templates_by_topology.end()) {
      template_id = ir.replay_body_templates.append(
          body_tasks.front()->task->source_ref_id,
          stable_hash64(exact_topology), ir.symbols.intern(readable_topology),
          compute_task_count, communication_task_count,
          static_cast<std::uint32_t>(stream_bodies.size()), topology_policy);
      templates_by_topology.emplace(std::move(exact_topology), template_id);
    } else {
      template_id = existing->second;
    }
    ir.graph_launch_bodies.append(
        launch.id, template_id, body_tasks.front()->task->id,
        body_tasks.back()->task->id,
        compute_task_count, communication_task_count,
        static_cast<std::uint32_t>(stream_bodies.size()));
  }
  return missing_body_capability_launches;
}

ReplayBodyTemplateId replay_body_template_for_launch(
    const NativeIr& ir,
    GraphLaunchOccurrenceId launch_id) {
  for (const GraphLaunchBodyRow& body : ir.graph_launch_bodies.rows()) {
    if (body.graph_launch_occurrence_id == launch_id) {
      return body.replay_body_template_id;
    }
  }
  return ReplayBodyTemplateId::invalid();
}

void materialize_replay_composition_segment(
    NativeIr& ir,
    const std::vector<const GraphLaunchOccurrenceRow*>& segment,
    ReplayCompositionOrderPolicy order_policy,
    const std::set<GraphLaunchOccurrenceId>&
        missing_body_capability_launches) {
  if (segment.empty()) {
    return;
  }
  bool all_instances = true;
  std::vector<std::int64_t> identities;
  identities.reserve(segment.size());
  for (const GraphLaunchOccurrenceRow* launch : segment) {
    if (!launch->captured_graph_instance_id.valid()) {
      all_instances = false;
      break;
    }
    identities.push_back(
        static_cast<std::int64_t>(launch->captured_graph_instance_id.value()));
  }
  ReplayCompositionIdentityPolicy identity_policy =
      ReplayCompositionIdentityPolicy::kCapturedGraphInstance;
  if (!all_instances) {
    identity_policy = ReplayCompositionIdentityPolicy::kGraphConnection;
    identities.clear();
    for (const GraphLaunchOccurrenceRow* launch : segment) {
      if (launch->raw_graph_connection_id < 0) {
        return;
      }
      identities.push_back(launch->raw_graph_connection_id);
    }
  }

  const ExactPeriodicSuffixCandidate periodic =
      find_exact_periodic_suffix(identities);
  if (periodic.period == 0) {
    return;
  }
  std::string hash_input =
      identity_policy == ReplayCompositionIdentityPolicy::kCapturedGraphInstance
          ? "captured_graph_instance\n"
          : "graph_connection\n";
  hash_input +=
      order_policy == ReplayCompositionOrderPolicy::kHostSubmissionOrder
          ? "host_submission_order\n"
          : "device_execution_order\n";
  for (std::size_t index = 0; index < periodic.period; ++index) {
    hash_input += std::to_string(identities[periodic.start + index]);
    hash_input += "\n";
  }
  const GraphLaunchOccurrenceRow& first_pattern =
      *segment[periodic.start];
  std::vector<ReplayBodyTemplateId> body_templates;
  body_templates.reserve(periodic.period);
  for (std::size_t index = 0; index < periodic.period; ++index) {
    body_templates.push_back(replay_body_template_for_launch(
        ir, segment[periodic.start + index]->id));
  }
  ReplayCompositionShapePolicy shape_policy =
      ReplayCompositionShapePolicy::kUnclassified;
  if (body_templates.size() >= 3 && body_templates.front().valid() &&
      body_templates.back().valid() && body_templates[1].valid() &&
      body_templates.front() != body_templates[1] &&
      body_templates.back() != body_templates[1] &&
      body_templates.front() != body_templates.back() &&
      std::all_of(body_templates.begin() + 1, body_templates.end() - 1,
                  [&](ReplayBodyTemplateId id) {
                    return id == body_templates[1];
                  })) {
    shape_policy = ReplayCompositionShapePolicy::kHeadRepeatedLayerTail;
  }
  const auto range_has_body_capability = [&](std::size_t begin,
                                             std::size_t count) {
    for (std::size_t offset = 0; offset < count; ++offset) {
      if (missing_body_capability_launches.find(segment[begin + offset]->id) !=
          missing_body_capability_launches.end()) {
        return false;
      }
    }
    return true;
  };

  // A one-shot prefill cannot prove periodicity by repetition.  It can still
  // be recognized exactly when it occupies precisely one decode-sized leading
  // composition and independently has an H + L* + T body shape.  Requiring
  // the already-confirmed periodic suffix to have the same high-level shape
  // prevents arbitrary leading context from being promoted by this rule.
  bool recognized_one_shot_leading = false;
  if (range_has_body_capability(0, periodic.start + periodic.period) &&
      shape_policy ==
          ReplayCompositionShapePolicy::kHeadRepeatedLayerTail &&
      periodic.start == periodic.period && periodic.start >= 3) {
    std::vector<ReplayBodyTemplateId> leading_bodies;
    leading_bodies.reserve(periodic.start);
    for (std::size_t index = 0; index < periodic.start; ++index) {
      leading_bodies.push_back(
          replay_body_template_for_launch(ir, segment[index]->id));
    }
    const bool leading_hlt =
        leading_bodies.front().valid() && leading_bodies.back().valid() &&
        leading_bodies[1].valid() &&
        leading_bodies.front() != leading_bodies[1] &&
        leading_bodies.back() != leading_bodies[1] &&
        leading_bodies.front() != leading_bodies.back() &&
        std::all_of(leading_bodies.begin() + 1, leading_bodies.end() - 1,
                    [&](ReplayBodyTemplateId id) {
                      return id == leading_bodies[1];
                    });
    if (leading_hlt) {
      std::string leading_hash_input =
          identity_policy ==
                  ReplayCompositionIdentityPolicy::kCapturedGraphInstance
              ? "captured_graph_instance\n"
              : "graph_connection\n";
      leading_hash_input +=
          order_policy == ReplayCompositionOrderPolicy::kHostSubmissionOrder
              ? "host_submission_order\n"
              : "device_execution_order\n";
      leading_hash_input += "exact_one_shot_leading_composition\n";
      for (std::size_t index = 0; index < periodic.start; ++index) {
        leading_hash_input += std::to_string(identities[index]);
        leading_hash_input += "\n";
      }
      const ReplayCompositionCandidateId leading_candidate =
          ir.replay_composition_candidates.append(
              segment.front()->source_ref_id, segment.front()->device_id,
              segment.front()->id, segment.front()->id,
              static_cast<std::uint32_t>(periodic.start), 0,
              static_cast<std::uint32_t>(periodic.start), 1, 0,
              stable_hash64(leading_hash_input), identity_policy, order_policy,
              ReplayCompositionShapePolicy::kHeadRepeatedLayerTail,
              ReplayCompositionBoundaryPolicy::
                  kExactOneShotLeadingComposition);
      for (std::size_t index = 0; index < periodic.start; ++index) {
        const GraphLaunchOccurrenceRow& launch = *segment[index];
        GraphSlotTemplateId slot_template_id =
            GraphSlotTemplateId::invalid();
        if (launch.captured_graph_instance_id.valid()) {
          slot_template_id =
              ir.captured_graph_instances
                  .row(launch.captured_graph_instance_id)
                  .slot_template_id;
        }
        const ReplayCompositionSlotRole role =
            index == 0
                ? ReplayCompositionSlotRole::kHead
                : (index + 1 == periodic.start
                       ? ReplayCompositionSlotRole::kTail
                       : ReplayCompositionSlotRole::kLayer);
        ir.replay_composition_slots.append(
            leading_candidate, static_cast<std::uint32_t>(index),
            launch.captured_graph_instance_id, slot_template_id,
            leading_bodies[index], role, launch.raw_graph_connection_id);
      }
      std::int64_t leading_start_ns = segment.front()->start_ns;
      std::int64_t leading_end_ns = segment.front()->end_ns;
      for (std::size_t index = 1; index < periodic.start; ++index) {
        leading_start_ns =
            std::min(leading_start_ns, segment[index]->start_ns);
        leading_end_ns = std::max(leading_end_ns, segment[index]->end_ns);
      }
      const ReplayCompositionRegionId leading_region =
          ir.replay_composition_regions.append(
              leading_candidate, 0, segment.front()->id,
              segment[periodic.start - 1]->id, leading_start_ns,
              leading_end_ns, static_cast<std::uint32_t>(periodic.start),
              static_cast<std::uint32_t>(periodic.start),
              ReplayCompositionRegionStatus::kRecognizedCompletePattern);
      for (std::size_t index = 0; index < periodic.start; ++index) {
        ir.replay_composition_region_members.append(
            leading_region, static_cast<std::uint32_t>(index),
            segment[index]->id, static_cast<std::int64_t>(index));
      }
      recognized_one_shot_leading = true;
    }
  }

  const ReplayCompositionCandidateId candidate_id =
      ir.replay_composition_candidates.append(
          first_pattern.source_ref_id, first_pattern.device_id,
          segment.front()->id, first_pattern.id,
          static_cast<std::uint32_t>(segment.size()),
          static_cast<std::uint32_t>(periodic.start),
          static_cast<std::uint32_t>(periodic.period),
          static_cast<std::uint32_t>(periodic.full_repeats),
          static_cast<std::uint32_t>(periodic.trailing),
          stable_hash64(hash_input), identity_policy, order_policy,
          shape_policy,
          ReplayCompositionBoundaryPolicy::kExactPeriodicSuffix);
  for (std::size_t index = 0; index < periodic.period; ++index) {
    const GraphLaunchOccurrenceRow& launch =
        *segment[periodic.start + index];
    GraphSlotTemplateId slot_template_id = GraphSlotTemplateId::invalid();
    if (launch.captured_graph_instance_id.valid()) {
      slot_template_id =
          ir.captured_graph_instances
              .row(launch.captured_graph_instance_id)
              .slot_template_id;
    }
    ReplayCompositionSlotRole role = ReplayCompositionSlotRole::kUnclassified;
    if (shape_policy ==
        ReplayCompositionShapePolicy::kHeadRepeatedLayerTail) {
      role = index == 0
                 ? ReplayCompositionSlotRole::kHead
                 : (index + 1 == periodic.period
                        ? ReplayCompositionSlotRole::kTail
                        : ReplayCompositionSlotRole::kLayer);
    }
    ir.replay_composition_slots.append(
        candidate_id, static_cast<std::uint32_t>(index),
        launch.captured_graph_instance_id, slot_template_id,
        body_templates[index], role,
        launch.raw_graph_connection_id);
  }

  const auto append_region = [&](std::uint32_t region_order,
                                 std::size_t begin,
                                 std::size_t count,
                                 std::uint32_t expected_launch_count,
                                 ReplayCompositionRegionStatus status,
                                 bool has_expected_slots) {
    std::int64_t start_ns = segment[begin]->start_ns;
    std::int64_t end_ns = segment[begin]->end_ns;
    for (std::size_t offset = 1; offset < count; ++offset) {
      start_ns = std::min(start_ns, segment[begin + offset]->start_ns);
      end_ns = std::max(end_ns, segment[begin + offset]->end_ns);
    }
    const ReplayCompositionRegionId region_id =
        ir.replay_composition_regions.append(
            candidate_id, region_order, segment[begin]->id,
            segment[begin + count - 1]->id, start_ns, end_ns,
            static_cast<std::uint32_t>(count), expected_launch_count, status);
    for (std::size_t offset = 0; offset < count; ++offset) {
      ir.replay_composition_region_members.append(
          region_id, static_cast<std::uint32_t>(offset),
          segment[begin + offset]->id,
          has_expected_slots ? static_cast<std::int64_t>(offset) : -1);
    }
  };
  const auto body_status = [&](std::size_t begin, std::size_t count,
                               ReplayCompositionRegionStatus matched_status) {
    if (!range_has_body_capability(begin, count)) {
      return ReplayCompositionRegionStatus::
          kUnrecognizedMissingBodyCapability;
    }
    for (std::size_t offset = 0; offset < count; ++offset) {
      const ReplayBodyTemplateId expected = body_templates[offset];
      const ReplayBodyTemplateId observed = replay_body_template_for_launch(
          ir, segment[begin + offset]->id);
      if (!expected.valid() || !observed.valid()) {
        return ReplayCompositionRegionStatus::
            kUnrecognizedMissingBodyEvidence;
      }
      if (expected != observed) {
        return ReplayCompositionRegionStatus::kUnrecognizedBodyMismatch;
      }
    }
    return matched_status;
  };

  std::uint32_t region_order = 0;
  if (periodic.start > 0 && !recognized_one_shot_leading) {
    append_region(region_order++, 0, periodic.start, 0,
                  range_has_body_capability(0, periodic.start)
                      ? ReplayCompositionRegionStatus::
                            kUnrecognizedLeadingContext
                      : ReplayCompositionRegionStatus::
                            kUnrecognizedMissingBodyCapability,
                  false);
  }
  for (std::size_t repeat = 0; repeat < periodic.full_repeats; ++repeat) {
    const std::size_t begin = periodic.start + repeat * periodic.period;
    append_region(
        region_order++, begin, periodic.period,
        static_cast<std::uint32_t>(periodic.period),
        body_status(
            begin, periodic.period,
            ReplayCompositionRegionStatus::kRecognizedCompletePattern),
        true);
  }
  if (periodic.trailing > 0) {
    const std::size_t begin =
        periodic.start + periodic.full_repeats * periodic.period;
    append_region(
        region_order++, begin, periodic.trailing,
        static_cast<std::uint32_t>(periodic.period),
        body_status(
            begin, periodic.trailing,
            ReplayCompositionRegionStatus::kUnrecognizedIncompleteTail),
        true);
  }
}

void materialize_incomplete_launch_segment(
    NativeIr& ir,
    const std::vector<const GraphLaunchOccurrenceRow*>& segment,
    ReplayCompositionOrderPolicy order_policy) {
  if (segment.empty()) {
    return;
  }
  std::string hash_input = "incomplete_launch_evidence\n";
  hash_input +=
      order_policy == ReplayCompositionOrderPolicy::kHostSubmissionOrder
          ? "host_submission_order\n"
          : "device_execution_order\n";
  std::int64_t start_ns = segment.front()->start_ns;
  std::int64_t end_ns = segment.front()->end_ns;
  for (const GraphLaunchOccurrenceRow* launch : segment) {
    hash_input += std::to_string(launch->raw_launch_connection_id);
    hash_input += "\n";
    start_ns = std::min(start_ns, launch->start_ns);
    end_ns = std::max(end_ns, launch->end_ns);
  }
  const ReplayCompositionCandidateId candidate_id =
      ir.replay_composition_candidates.append(
          segment.front()->source_ref_id, segment.front()->device_id,
          segment.front()->id, segment.front()->id,
          static_cast<std::uint32_t>(segment.size()), 0, 0, 0, 0,
          stable_hash64(hash_input),
          ReplayCompositionIdentityPolicy::kUnavailable, order_policy,
          ReplayCompositionShapePolicy::kUnclassified,
          ReplayCompositionBoundaryPolicy::kIncompleteLaunchEvidence);
  const ReplayCompositionRegionId region_id =
      ir.replay_composition_regions.append(
          candidate_id, 0, segment.front()->id, segment.back()->id, start_ns,
          end_ns, static_cast<std::uint32_t>(segment.size()),
          static_cast<std::uint32_t>(segment.size()),
          ReplayCompositionRegionStatus::
              kUnrecognizedMissingCompletionEvidence);
  for (std::size_t index = 0; index < segment.size(); ++index) {
    ir.replay_composition_region_members.append(
        region_id, static_cast<std::uint32_t>(index), segment[index]->id, -1);
  }
}

bool graph_launches_in_host_submission_order(
    const NativeIr& ir,
    std::vector<const GraphLaunchOccurrenceRow*>& ordered) {
  if (ir.graph_launch_occurrences.empty() ||
      ir.graph_launch_activity_members.empty()) {
    return false;
  }
  std::vector<std::uint32_t> launch_membership_counts(
      ir.graph_launch_occurrences.size(), 0);
  std::vector<std::uint32_t> activity_member_counts(
      ir.graph_launch_activities.size(), 0);
  ordered.clear();
  ordered.reserve(ir.graph_launch_occurrences.size());
  for (const GraphLaunchActivityMemberRow& member :
       ir.graph_launch_activity_members.rows()) {
    if (!member.graph_launch_activity_id.valid() ||
        !member.graph_launch_occurrence_id.valid()) {
      return false;
    }
    ++activity_member_counts[member.graph_launch_activity_id.value()];
    ++launch_membership_counts[member.graph_launch_occurrence_id.value()];
    ordered.push_back(
        &ir.graph_launch_occurrences.row(member.graph_launch_occurrence_id));
  }
  for (const GraphLaunchActivityRow& activity :
       ir.graph_launch_activities.rows()) {
    const std::uint32_t member_count = activity_member_counts[activity.id.value()];
    if (member_count == 0) {
      continue;
    }
    if (activity.boundary_policy !=
            GraphLaunchActivityBoundaryPolicy::kHostBlockingSync ||
        activity.host_execute_count != activity.matched_launch_count ||
        activity.matched_launch_count != member_count) {
      ordered.clear();
      return false;
    }
  }
  if (ordered.size() != ir.graph_launch_occurrences.size() ||
      std::any_of(launch_membership_counts.begin(),
                  launch_membership_counts.end(),
                  [](std::uint32_t count) { return count != 1; })) {
    ordered.clear();
    return false;
  }
  return true;
}

void materialize_replay_composition_candidates_for_order(
    NativeIr& ir,
    const std::vector<const GraphLaunchOccurrenceRow*>& ordered_launches,
    ReplayCompositionOrderPolicy order_policy,
    const std::set<GraphLaunchOccurrenceId>&
        missing_body_capability_launches) {
  std::map<std::uint32_t, std::vector<const GraphLaunchOccurrenceRow*>>
      launches_by_device;
  for (const GraphLaunchOccurrenceRow* launch : ordered_launches) {
    launches_by_device[launch->device_id].push_back(launch);
  }
  for (const auto& item : launches_by_device) {
    std::vector<const GraphLaunchOccurrenceRow*> segment;
    std::vector<const GraphLaunchOccurrenceRow*> incomplete_segment;
    for (const GraphLaunchOccurrenceRow* launch : item.second) {
      if (launch->raw_graph_connection_id < 0) {
        materialize_replay_composition_segment(
            ir, segment, order_policy, missing_body_capability_launches);
        segment.clear();
        incomplete_segment.push_back(launch);
        continue;
      }
      materialize_incomplete_launch_segment(
          ir, incomplete_segment, order_policy);
      incomplete_segment.clear();
      segment.push_back(launch);
    }
    materialize_replay_composition_segment(
        ir, segment, order_policy, missing_body_capability_launches);
    materialize_incomplete_launch_segment(
        ir, incomplete_segment, order_policy);
  }
}

void materialize_replay_composition_candidates(
    NativeIr& ir,
    const std::set<GraphLaunchOccurrenceId>&
        missing_body_capability_launches) {
  std::vector<const GraphLaunchOccurrenceRow*> device_order;
  device_order.reserve(ir.graph_launch_occurrences.size());
  for (const GraphLaunchOccurrenceRow& launch :
       ir.graph_launch_occurrences.rows()) {
    device_order.push_back(&launch);
  }

  std::vector<const GraphLaunchOccurrenceRow*> host_order;
  if (!graph_launches_in_host_submission_order(ir, host_order)) {
    materialize_replay_composition_candidates_for_order(
        ir, device_order, ReplayCompositionOrderPolicy::kDeviceExecutionOrder,
        missing_body_capability_launches);
    return;
  }
  const bool identical_order =
      std::equal(device_order.begin(), device_order.end(), host_order.begin(),
                 host_order.end(),
                 [](const GraphLaunchOccurrenceRow* lhs,
                    const GraphLaunchOccurrenceRow* rhs) {
                   return lhs->id == rhs->id;
                 });
  if (!identical_order) {
    materialize_replay_composition_candidates_for_order(
        ir, device_order, ReplayCompositionOrderPolicy::kDeviceExecutionOrder,
        missing_body_capability_launches);
  }
  materialize_replay_composition_candidates_for_order(
      ir, host_order, ReplayCompositionOrderPolicy::kHostSubmissionOrder,
      missing_body_capability_launches);
}

std::set<std::uint32_t> materialize_exact_aclgraph_replay_units(
    NativeIr& ir,
    const std::string& source_kind,
    const std::string& source_path) {
  std::map<std::uint32_t,
           std::vector<const ReplayCompositionCandidateRow*>>
      host_candidates_by_device;
  std::map<std::uint32_t,
           std::vector<const ReplayCompositionCandidateRow*>>
      device_candidates_by_device;
  for (const ReplayCompositionCandidateRow& candidate :
       ir.replay_composition_candidates.rows()) {
    if (candidate.shape_policy !=
        ReplayCompositionShapePolicy::kHeadRepeatedLayerTail) {
      continue;
    }
    auto& destination =
        candidate.order_policy ==
                ReplayCompositionOrderPolicy::kHostSubmissionOrder
            ? host_candidates_by_device
            : device_candidates_by_device;
    destination[candidate.device_id].push_back(&candidate);
  }

  std::map<std::uint32_t,
           std::vector<const ReplayCompositionCandidateRow*>>
      candidates_by_device;
  for (const auto& item : host_candidates_by_device) {
    candidates_by_device.emplace(item.first, item.second);
  }
  for (const auto& item : device_candidates_by_device) {
    if (candidates_by_device.find(item.first) == candidates_by_device.end()) {
      candidates_by_device.emplace(item.first, item.second);
    }
  }
  if (candidates_by_device.empty()) {
    return {};
  }

  // Once strong exact H/L/T evidence exists for a device, do not fall back
  // to the older capture-cardinality heuristic on that device.  Ambiguous,
  // missing, or contradictory exact evidence must remain explicit unknowns
  // rather than being silently reclassified by a weaker projector.
  std::set<std::uint32_t> exact_claimed_devices;
  for (const auto& device_candidates : candidates_by_device) {
    exact_claimed_devices.insert(device_candidates.first);
  }

  const SourceRefId source_ref = ir.source_refs.append(
      source_kind, source_path, "ACLGRAPH_REPLAY_UNIT", 0);
  std::map<std::string, GraphTemplateId> templates_by_signature;
  for (const auto& device_candidates : candidates_by_device) {
    std::int64_t previous_end_ns = std::numeric_limits<std::int64_t>::min();
    for (const ReplayCompositionCandidateRow* candidate_ptr :
         device_candidates.second) {
      const ReplayCompositionCandidateRow& candidate = *candidate_ptr;
      std::vector<const ReplayCompositionSlotRow*> slots(
          candidate.pattern_length, nullptr);
      for (const ReplayCompositionSlotRow& slot :
           ir.replay_composition_slots.rows()) {
        if (slot.replay_composition_candidate_id != candidate.id) {
          continue;
        }
        if (slot.slot_order >= slots.size() ||
            slots[slot.slot_order] != nullptr) {
          throw std::logic_error(
              "exact replay composition has invalid slot membership");
        }
        slots[slot.slot_order] = &slot;
      }
      if (std::any_of(slots.begin(), slots.end(),
                      [](const ReplayCompositionSlotRow* slot) {
                        return slot == nullptr ||
                               !slot->replay_body_template_id.valid();
                      })) {
        continue;
      }

      std::string template_signature = "exact_replay_composition_v1\n";
      for (const ReplayCompositionSlotRow* slot : slots) {
        const ReplayBodyTemplateRow& body = ir.replay_body_templates.row(
            slot->replay_body_template_id);
        template_signature += std::to_string(slot->slot_order);
        template_signature += ":";
        template_signature += std::to_string(body.exact_sequence_hash);
        template_signature += ":";
        template_signature += std::to_string(static_cast<unsigned>(slot->role));
        template_signature += "\n";
      }
      GraphTemplateId graph_template = GraphTemplateId::invalid();
      const auto existing_template =
          templates_by_signature.find(template_signature);
      if (existing_template == templates_by_signature.end()) {
        graph_template = ir.graph_templates.append(
            source_ref, stable_hash64(template_signature),
            candidate.pattern_length);
        templates_by_signature.emplace(template_signature, graph_template);
      } else {
        graph_template = existing_template->second;
      }

      std::vector<std::vector<const ReplayCompositionRegionMemberRow*>>
          members_by_region(ir.replay_composition_regions.size());
      for (const ReplayCompositionRegionMemberRow& member :
           ir.replay_composition_region_members.rows()) {
        if (member.replay_composition_region_id.valid() &&
            member.replay_composition_region_id.value() <
                members_by_region.size()) {
          members_by_region[member.replay_composition_region_id.value()]
              .push_back(&member);
        }
      }

      for (const ReplayCompositionRegionRow& region :
           ir.replay_composition_regions.rows()) {
        if (region.replay_composition_candidate_id != candidate.id ||
            region.status !=
                ReplayCompositionRegionStatus::kRecognizedCompletePattern) {
          continue;
        }
        std::vector<const ReplayCompositionRegionMemberRow*>& members =
            members_by_region[region.id.value()];
        std::sort(members.begin(), members.end(),
                  [](const ReplayCompositionRegionMemberRow* lhs,
                     const ReplayCompositionRegionMemberRow* rhs) {
                    return lhs->member_order < rhs->member_order;
                  });
        if (members.size() != candidate.pattern_length ||
            region.observed_launch_count != candidate.pattern_length ||
            region.expected_launch_count != candidate.pattern_length ||
            region.start_ns >= region.end_ns ||
            region.start_ns < previous_end_ns) {
          throw std::logic_error(
              "recognized exact replay region violates projection invariants");
        }

        std::uint32_t raw_stream_id =
            std::numeric_limits<std::uint32_t>::max();
        for (std::size_t index = 0; index < members.size(); ++index) {
          const ReplayCompositionRegionMemberRow& member = *members[index];
          if (member.member_order != index ||
              member.expected_slot_order != static_cast<std::int64_t>(index)) {
            throw std::logic_error(
                "recognized exact replay region lost ordered slot membership");
          }
          const GraphLaunchOccurrenceRow& launch =
              ir.graph_launch_occurrences.row(
                  member.graph_launch_occurrence_id);
          if (launch.device_id != candidate.device_id ||
              replay_body_template_for_launch(ir, launch.id) !=
                  slots[index]->replay_body_template_id) {
            throw std::logic_error(
                "recognized exact replay region body no longer matches slot");
          }
          if (index == 0) {
            const StreamId stream_id = launch.model_stream_id.valid()
                                           ? launch.model_stream_id
                                           : launch.execute_stream_id;
            if (stream_id.valid()) {
              raw_stream_id = static_cast<std::uint32_t>(
                  ir.streams.row(stream_id).raw_stream_id);
            }
          }
        }

        const std::string symbol =
            "GraphReplayUnit ExactT" +
            std::to_string(graph_template.value() +
                           static_cast<std::uint32_t>(1));
        const TraceEventId event_id = ir.trace_events.append(
            source_ref, region.id.value() + 1, candidate.device_id,
            raw_stream_id, region.start_ns, region.end_ns,
            ir.symbols.intern(symbol));
        const ReplayUnitId replay_unit = ir.replay_units.append(
            graph_template, source_ref, AnchorId::invalid(),
            AnchorId::invalid(), event_id, region.id);
        for (std::size_t index = 0; index < members.size(); ++index) {
          ir.replay_unit_launch_members.append(
              replay_unit, static_cast<std::uint32_t>(index),
              members[index]->graph_launch_occurrence_id, slots[index]->id);
        }
        previous_end_ns = region.end_ns;
      }
    }
  }
  return exact_claimed_devices;
}

void materialize_aclgraph_replay_units(
    NativeIr& ir,
    const std::unordered_map<std::uint32_t,
                             std::unordered_set<std::uint64_t>>&
        model_streams_by_device,
    const std::vector<GraphLaunchView>& execute_launches,
    SourceRefId source_ref,
    std::uint32_t capture_group_size,
    const std::string& capture_replay_unit_signature) {
  if (model_streams_by_device.empty()) {
    return;
  }

  const std::unordered_set<std::uint64_t> model_stream_keys =
      flatten_model_stream_keys(model_streams_by_device);
  const GraphTaskSymbolSets graph_symbols =
      build_graph_task_symbol_sets(ir.symbols);
  ir.trace_events.reserve(ir.trace_events.size() + ir.tasks.size());
  std::vector<GraphTaskView> model_rows;
  std::vector<GraphTaskView> control_rows;
  model_rows.reserve(ir.tasks.size() / 2u);
  control_rows.reserve(ir.tasks.size() / 8u);
  for (const TaskRow& task : ir.tasks.rows()) {
    if (!task.trace_event_id.valid()) {
      continue;
    }
    const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
    const bool is_model_stream =
        model_stream_keys.find(stream_key(event.device_id, event.stream_id)) !=
        model_stream_keys.end();
    if (is_model_stream && event.end_ns > event.start_ns) {
      model_rows.push_back(GraphTaskView{&task, &event});
    }
    if (symbol_in_set(graph_symbols.graph_control, task.task_type_symbol_id) &&
        event.end_ns > event.start_ns) {
      control_rows.push_back(GraphTaskView{&task, &event});
    }
  }
  if (model_rows.empty()) {
    return;
  }
  std::sort(model_rows.begin(), model_rows.end(),
            [](const GraphTaskView& lhs, const GraphTaskView& rhs) {
              if (lhs.event->start_ns != rhs.event->start_ns) {
                return lhs.event->start_ns < rhs.event->start_ns;
              }
              if (lhs.event->end_ns != rhs.event->end_ns) {
                return lhs.event->end_ns < rhs.event->end_ns;
              }
              if (lhs.event->stream_id != rhs.event->stream_id) {
                return lhs.event->stream_id < rhs.event->stream_id;
              }
              return lhs.task->raw_task_id < rhs.task->raw_task_id;
            });
  std::sort(control_rows.begin(), control_rows.end(),
            [](const GraphTaskView& lhs, const GraphTaskView& rhs) {
              if (lhs.event->start_ns != rhs.event->start_ns) {
                return lhs.event->start_ns < rhs.event->start_ns;
              }
              if (lhs.event->end_ns != rhs.event->end_ns) {
                return lhs.event->end_ns < rhs.event->end_ns;
              }
              return lhs.task->raw_task_id < rhs.task->raw_task_id;
            });
  const std::vector<GraphTaskView> notify_wait_rows =
      controls_with_symbol_set(control_rows, graph_symbols.notify_wait);
  const std::vector<GraphTaskView> model_execute_rows =
      controls_with_symbol_set(control_rows, graph_symbols.model_execute);

  std::vector<GraphReplayUnitView> units;
  if (!execute_launches.empty()) {
    const std::vector<GraphLaunchView> device_launches =
        device_backed_execute_launches(execute_launches, model_execute_rows,
                                       capture_group_size);
    units = split_rows_by_execute_waves(model_rows, device_launches,
                                        capture_group_size);
  }

  static constexpr std::int64_t kGapNs = 5'000'000;
  if (units.empty()) {
    std::vector<std::vector<GraphTaskView>> activities;
    std::vector<GraphTaskView> current;
    std::int64_t last_end = model_rows.front().event->end_ns;
    for (const GraphTaskView& row : model_rows) {
      if (!current.empty() && row.event->start_ns - last_end > kGapNs) {
        activities.push_back(std::move(current));
        current = {};
      }
      current.push_back(row);
      last_end = std::max(last_end, row.event->end_ns);
    }
    if (!current.empty()) {
      activities.push_back(std::move(current));
    }

    std::size_t notify_wait_cursor = 0;
    std::size_t model_execute_cursor = 0;
    for (const std::vector<GraphTaskView>& activity : activities) {
      const std::int64_t start_ns = activity.front().event->start_ns;
      std::int64_t end_ns = activity.front().event->end_ns;
      for (const GraphTaskView& row : activity) {
        end_ns = std::max(end_ns, row.event->end_ns);
      }
      std::vector<GraphTaskView> notify_waits =
          controls_in_interval_from_sorted(notify_wait_rows, start_ns, end_ns,
                                           notify_wait_cursor);
      std::vector<GraphTaskView> model_execs =
          controls_in_interval_from_sorted(model_execute_rows, start_ns, end_ns,
                                           model_execute_cursor);
      std::vector<GraphReplayUnitView> split =
          split_activity(ir, activity, notify_waits, model_execs,
                         capture_group_size);
      units.insert(units.end(), split.begin(), split.end());
    }
  }
  if (units.empty()) {
    return;
  }
  if (!capture_replay_unit_signature.empty()) {
    for (GraphReplayUnitView& unit : units) {
      unit.template_signature = capture_replay_unit_signature;
    }
  }

  std::vector<ReplayUnitWindow> windows;
  windows.reserve(units.size());
  for (const GraphReplayUnitView& unit : units) {
    if (unit.rows.empty()) {
      windows.push_back(ReplayUnitWindow{});
      continue;
    }
    if (unit.has_window) {
      windows.push_back(ReplayUnitWindow{unit.start_ns, unit.end_ns});
      continue;
    }
    std::int64_t start_ns = unit.rows.front().event->start_ns;
    std::int64_t end_ns = unit.rows.front().event->end_ns;
    for (const GraphTaskView& row : unit.rows) {
      start_ns = std::min(start_ns, row.event->start_ns);
      end_ns = std::max(end_ns, row.event->end_ns);
    }
    windows.push_back(ReplayUnitWindow{start_ns, end_ns});
  }

  std::map<std::string, GraphTemplateId> templates_by_signature;
  for (std::size_t unit_index = 0; unit_index < units.size(); ++unit_index) {
    const GraphReplayUnitView& unit = units[unit_index];
    if (unit.rows.empty()) {
      continue;
    }
    std::int64_t start_ns = windows[unit_index].start_ns;
    std::int64_t end_ns = windows[unit_index].end_ns;
    std::uint32_t device_id = unit.rows.front().event->device_id;
    std::map<std::uint64_t, std::uint64_t> duration_by_stream;
    for (const GraphTaskView& row : unit.rows) {
      device_id = row.event->device_id;
      duration_by_stream[row.event->stream_id] += static_cast<std::uint64_t>(
          std::max<std::int64_t>(0, row.event->end_ns - row.event->start_ns));
    }
    std::uint64_t primary_stream = unit.rows.front().event->stream_id;
    std::uint64_t primary_duration = 0;
    for (const auto& item : duration_by_stream) {
      if (item.second > primary_duration) {
        primary_stream = item.first;
        primary_duration = item.second;
      }
    }

    const std::string signature = unit.template_signature.empty()
                                      ? body_signature(ir, unit.rows)
                                      : unit.template_signature;
    const std::uint64_t hash = stable_hash64(signature);
    auto template_found = templates_by_signature.find(signature);
    GraphTemplateId graph_template;
    if (template_found == templates_by_signature.end()) {
      graph_template =
          ir.graph_templates.append(source_ref, hash, capture_group_size);
      templates_by_signature.emplace(signature, graph_template);
    } else {
      graph_template = template_found->second;
    }

    const std::string symbol =
        "GraphReplayUnit T" +
        std::to_string(graph_template.value() + static_cast<std::uint32_t>(1));
    const SymbolId symbol_id = ir.symbols.intern(symbol);
    const TraceEventId event_id = ir.trace_events.append(
        source_ref, ir.replay_units.size() + 1, device_id, primary_stream,
        start_ns, end_ns, symbol_id);
    ir.replay_units.append(graph_template, source_ref, AnchorId::invalid(),
                           AnchorId::invalid(), event_id);
  }
}

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
                     compute.synthetic_global_task_id, connection_id, -1,
                     model_id},
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
                    raw.communication.task_type_symbol_id);
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

}  // namespace

bool ascend_sqlite_has_usable_task_table(const std::string& db_path) {
  if (!file_exists(db_path) || !sqlite_table_has_rows(db_path, "TASK")) {
    return false;
  }
  try {
    SqliteDb db(db_path);
    return table_has_columns(db, "TASK",
                             {"startNs", "endNs", "deviceId", "streamId",
                              "taskId", "globalTaskId", "connectionId",
                              "taskType"});
  } catch (const std::exception&) {
    return false;
  }
}

bool looks_like_ascend_split_sqlite_profile(const std::string& profile_dir) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path root(profile_dir);
  if (!fs::is_directory(root, ec)) {
    return false;
  }
  fs::directory_iterator iterator(
      root, fs::directory_options::skip_permission_denied, ec);
  const fs::directory_iterator end;
  while (!ec && iterator != end) {
    const fs::path device_dir = iterator->path();
    const std::string name = device_dir.filename().string();
    if (iterator->is_directory(ec) && name.rfind("device_", 0) == 0) {
      const fs::path task_db = device_dir / "sqlite" / "ascend_task.db";
      if (sqlite_table_has_rows(task_db.string(), "AscendTask")) {
        return true;
      }
    }
    iterator.increment(ec);
  }
  return false;
}

std::vector<AscendSplitSQLiteTableInfo>
inventory_ascend_split_sqlite_profile(const std::string& profile_dir) {
  return inventory_split_profile_impl(profile_dir);
}

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
  std::error_code path_error;
  if (std::filesystem::is_directory(options_.db_path, path_error)) {
    return load_split_profile(options_);
  }
  if (!file_exists(options_.db_path)) {
    throw std::invalid_argument("Ascend SQLite DB does not exist: " +
                                options_.db_path);
  }

  AscendLoadTiming timing;
  const Stopwatch sqlite_open_watch;
  SqliteDb db(options_.db_path);
  timing.sqlite_open_ms = sqlite_open_watch.elapsed_ms();
  NativeIr ir;

  static constexpr const char* kInventorySql =
      "SELECT name FROM sqlite_master "
      "WHERE type IN ('table', 'view') "
      "ORDER BY name";

  bool saw_schema_object = false;
  std::unordered_map<std::string, SourceRefId> table_refs;
  timing.inventory_ms = time_stage([&]() {
    SqliteStmt stmt(db.get(), kInventorySql);
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
  });

  if (!saw_schema_object) {
    ir.source_refs.append(options_.source_kind, options_.db_path,
                          "sqlite_schema", 0);
  }

  const auto has_table = [&](const char* table_name) {
    return table_refs.find(table_name) != table_refs.end();
  };
  if (has_table("TASK") &&
      !table_has_columns(db, "TASK",
                         {"startNs", "endNs", "deviceId", "streamId",
                          "taskId", "globalTaskId", "connectionId",
                          "taskType"})) {
    throw std::invalid_argument(
        "Ascend SQLite profile has an incompatible TASK schema: " +
        options_.db_path);
  }
  const bool string_ids_usable =
      has_table("STRING_IDS") &&
      table_has_columns(db, "STRING_IDS", {"id", "value"});
  const bool compute_info_usable =
      has_table("COMPUTE_TASK_INFO") &&
      table_has_columns(db, "COMPUTE_TASK_INFO",
                        {"globalTaskId", "name", "opType", "taskType"});
  const bool communication_task_info_usable =
      has_table("COMMUNICATION_TASK_INFO") &&
      table_has_columns(db, "COMMUNICATION_TASK_INFO",
                        {"globalTaskId", "name"});
  const bool cann_api_usable =
      has_table("CANN_API") &&
      table_has_columns(db, "CANN_API",
                        {"startNs", "endNs", "connectionId", "name"});
  const bool communication_op_usable =
      has_table("COMMUNICATION_OP") &&
      table_has_columns(db, "COMMUNICATION_OP",
                        {"startNs", "endNs", "deviceId", "connectionId",
                         "opName", "opId"});

  std::unordered_map<std::int64_t, std::string> string_ids;
  timing.string_ids_ms = time_stage([&]() {
    if (string_ids_usable) {
      string_ids = load_string_ids(db, ir);
    }
  });
  std::unordered_map<std::int64_t, ComputeInfo> compute_info;
  timing.compute_info_ms = time_stage([&]() {
    if (compute_info_usable) {
      compute_info = load_compute_info(db, ir, string_ids);
    }
  });
  AclGraphCannApiMetadata cann_api_metadata;
  timing.cann_api_metadata_ms = time_stage([&]() {
    if (cann_api_usable) {
      cann_api_metadata = load_aclgraph_cann_api_metadata(db, string_ids);
    }
  });
  std::unordered_map<std::int64_t, CommunicationTaskInfo>
      communication_task_info;
  timing.communication_task_info_ms = time_stage([&]() {
    if (communication_task_info_usable) {
      communication_task_info = load_communication_task_info(
          db, ir, string_ids,
          table_has_column(db, "COMMUNICATION_TASK_INFO", "taskType"));
    }
  });
  StreamIndex streams;
  TaskLinkIndex task_links;
  timing.task_rows_ms = time_stage([&]() {
    if (has_table("TASK")) {
      load_task_rows(db, options_.db_path, options_.thread_count, ir, streams,
                     task_links, string_ids, compute_info,
                     communication_task_info, table_refs.at("TASK"));
    }
  });
  timing.communication_op_rows_ms = time_stage([&]() {
    if (communication_op_usable) {
      load_communication_op_rows(db, ir, streams, task_links, string_ids,
                                 table_refs.at("COMMUNICATION_OP"),
                                 table_has_column(db, "COMMUNICATION_OP",
                                                  "opType"));
    }
  });
  const std::string stream_info_path =
      stream_info_db_path_for_msprof(options_.db_path);
  const bool capture_stream_usable =
      aclgraph_capture_stream_schema_usable(stream_info_path);
  AclGraphCaptureInfo capture_info;
  timing.stream_info_capture_ms = time_stage([&]() {
    if (capture_stream_usable) {
      capture_info = load_aclgraph_capture_info(stream_info_path);
    }
  });
  timing.cann_api_capture_slots_ms = time_stage([&]() {
    if (cann_api_usable) {
      capture_info.capture_slots = std::move(cann_api_metadata.capture_slots);
      capture_info.replay_unit_signature = build_capture_replay_unit_signature(
          capture_info.capture_slots, capture_info.capture_group_size);
    }
  });
  CapturedGraphInstanceIndexes captured_graph_instances;
  timing.aclgraph_capture_instances_ms = time_stage([&]() {
    if (!capture_info.model_groups.empty()) {
      const SourceRefId capture_stream_source_ref = ir.source_refs.append(
          options_.source_kind, stream_info_path, "CaptureStreamInfo", 0);
      captured_graph_instances = materialize_aclgraph_capture_instances(
          ir, capture_info, capture_stream_source_ref,
          !cann_api_usable
              ? SourceRefId::invalid()
              : table_refs.at("CANN_API"));
    }
  });
  timing.aclgraph_launch_occurrences_ms = time_stage([&]() {
    if (has_table("TASK")) {
      materialize_aclgraph_launch_occurrences(
          ir, streams, captured_graph_instances,
          cann_api_metadata.execute_launches,
          !cann_api_usable
              ? SourceRefId::invalid()
              : table_refs.at("CANN_API"));
    }
  });
  std::set<GraphLaunchOccurrenceId> missing_body_capability_launches;
  timing.aclgraph_launch_bodies_ms = time_stage([&]() {
    missing_body_capability_launches = materialize_graph_launch_bodies(
        ir, compute_info_usable, communication_task_info_usable);
  });
  timing.aclgraph_launch_activities_ms = time_stage([&]() {
    const auto host_api_source = table_refs.find("CANN_API");
    if (cann_api_usable && host_api_source != table_refs.end()) {
      materialize_graph_launch_activities(
          ir, cann_api_metadata.launch_activities, host_api_source->second);
    }
  });
  timing.replay_composition_candidates_ms =
      time_stage([&]() {
        materialize_replay_composition_candidates(
            ir, missing_body_capability_launches);
      });
  timing.aclgraph_replay_units_ms = time_stage([&]() {
    const std::set<std::uint32_t> exact_claimed_devices =
        materialize_exact_aclgraph_replay_units(
            ir, options_.source_kind, options_.db_path);
    auto legacy_model_streams = capture_info.model_streams_by_device;
    for (std::uint32_t device_id : exact_claimed_devices) {
      legacy_model_streams.erase(device_id);
    }
    for (GraphLaunchOccurrenceId launch_id :
         missing_body_capability_launches) {
      const GraphLaunchOccurrenceRow& launch =
          ir.graph_launch_occurrences.row(launch_id);
      legacy_model_streams.erase(launch.device_id);
    }
    if (!legacy_model_streams.empty()) {
      const SourceRefId replay_source_ref = ir.source_refs.append(
          options_.source_kind, stream_info_path, "ACLGRAPH_REPLAY_UNIT", 0);
      materialize_aclgraph_replay_units(
          ir, legacy_model_streams,
          cann_api_metadata.execute_launches, replay_source_ref,
          capture_info.capture_group_size,
          capture_info.replay_unit_signature);
    }
  });

  if (options_.timing_diagnostics) {
    print_load_timing(timing);
  }

  return ir;
}

}  // namespace traceloom
