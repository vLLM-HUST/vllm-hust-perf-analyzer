#include "traceloom/adapters/ascend_sqlite_adapter.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::int64_t connection_id = 0;
};

struct CaptureSlotSignature {
  std::uint32_t slot_index = 0;
  std::string host_api_signature;
};

struct AclGraphCaptureInfo {
  std::unordered_map<std::uint32_t, std::unordered_set<std::uint64_t>>
      model_streams_by_device;
  std::uint32_t capture_group_size = 0;
  std::vector<CaptureSlotSignature> capture_slots;
  std::string replay_unit_signature;
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
  if (rows.empty() || capture_group_size <= 1 ||
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
    const std::size_t launch_index = wave * group_size;
    unit.has_window = true;
    unit.start_ns = ordered_launches[launch_index].start_ns;
    if (wave + 1 < wave_count) {
      unit.end_ns = ordered_launches[(wave + 1) * group_size].start_ns;
    } else {
      unit.end_ns = ordered_launches.back().end_ns;
    }
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

std::vector<GraphLaunchView> load_aclgraph_execute_launches(
    SqliteDb& db,
    const std::unordered_map<std::int64_t, std::string>& string_ids) {
  std::vector<std::int64_t> execute_name_ids;
  for (const auto& item : string_ids) {
    if (item.second == "aclmdlRIExecuteAsync") {
      execute_name_ids.push_back(item.first);
    }
  }
  if (execute_name_ids.empty() || !table_has_column(db, "CANN_API", "name")) {
    return {};
  }
  std::sort(execute_name_ids.begin(), execute_name_ids.end());
  std::string placeholders;
  for (std::size_t index = 0; index < execute_name_ids.size(); ++index) {
    if (index != 0) {
      placeholders += ",";
    }
    placeholders += "?";
  }
  const std::string sql =
      "SELECT startNs, endNs, connectionId "
      "FROM CANN_API "
      "WHERE name IN (" +
      placeholders +
      ") AND startNs IS NOT NULL AND endNs IS NOT NULL AND endNs > startNs "
      "ORDER BY startNs, endNs, connectionId";
  SqliteStmt stmt(db.get(), sql.c_str());
  for (std::size_t index = 0; index < execute_name_ids.size(); ++index) {
    sqlite3_bind_int64(stmt.get(), static_cast<int>(index + 1),
                       execute_name_ids[index]);
  }
  std::vector<GraphLaunchView> out;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      out.push_back(GraphLaunchView{sqlite_i64(stmt.get(), 0, 0),
                                    sqlite_i64(stmt.get(), 1, 0),
                                    sqlite_i64(stmt.get(), 2, 0)});
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    throw std::runtime_error("failed to load ACLGraph execute launches: " +
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

std::vector<CaptureSlotSignature> load_aclgraph_capture_slots(
    SqliteDb& db,
    const std::unordered_map<std::int64_t, std::string>& string_ids) {
  if (!table_has_column(db, "CANN_API", "name")) {
    return {};
  }
  const std::vector<std::int64_t> begin_ids =
      string_ids_for_value(string_ids, "aclmdlRICaptureBegin");
  const std::vector<std::int64_t> end_ids =
      string_ids_for_value(string_ids, "aclmdlRICaptureEnd");
  if (begin_ids.empty() || end_ids.empty()) {
    return {};
  }

  struct CaptureInterval {
    std::int64_t start_ns = 0;
    std::int64_t end_ns = 0;
  };

  std::string placeholders;
  for (std::size_t index = 0; index < begin_ids.size() + end_ids.size();
       ++index) {
    if (index != 0) {
      placeholders += ",";
    }
    placeholders += "?";
  }
  const std::string interval_sql =
      "SELECT startNs, endNs, name "
      "FROM CANN_API "
      "WHERE name IN (" +
      placeholders +
      ") AND startNs IS NOT NULL AND endNs IS NOT NULL AND endNs > startNs "
      "ORDER BY startNs, endNs, connectionId";
  SqliteStmt interval_stmt(db.get(), interval_sql.c_str());
  int bind_index = 1;
  for (std::int64_t id : begin_ids) {
    sqlite3_bind_int64(interval_stmt.get(), bind_index++, id);
  }
  for (std::int64_t id : end_ids) {
    sqlite3_bind_int64(interval_stmt.get(), bind_index++, id);
  }

  std::vector<CaptureInterval> intervals;
  std::vector<CaptureInterval> pending;
  while (true) {
    const int rc = sqlite3_step(interval_stmt.get());
    if (rc == SQLITE_ROW) {
      const std::int64_t start_ns = sqlite_i64(interval_stmt.get(), 0, 0);
      const std::int64_t end_ns = sqlite_i64(interval_stmt.get(), 1, 0);
      const std::int64_t name_id = sqlite_i64(interval_stmt.get(), 2, -1);
      if (contains_i64(begin_ids, name_id)) {
        pending.push_back(CaptureInterval{start_ns, end_ns});
      } else if (contains_i64(end_ids, name_id) && !pending.empty()) {
        CaptureInterval interval = pending.front();
        pending.erase(pending.begin());
        interval.end_ns = end_ns;
        if (interval.end_ns > interval.start_ns) {
          intervals.push_back(interval);
        }
      }
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    throw std::runtime_error("failed to load ACLGraph capture intervals: " +
                             std::string(sqlite3_errmsg(interval_stmt.db())));
  }
  if (intervals.empty()) {
    return {};
  }

  const std::int64_t min_start = intervals.front().start_ns;
  std::int64_t max_end = intervals.front().end_ns;
  for (const CaptureInterval& interval : intervals) {
    max_end = std::max(max_end, interval.end_ns);
  }

  std::vector<std::vector<std::string>> tokens(intervals.size());
  static constexpr const char* kApiSql =
      "SELECT startNs, endNs, name "
      "FROM CANN_API "
      "WHERE startNs >= ? AND endNs <= ? "
      "ORDER BY startNs, endNs, connectionId";
  SqliteStmt api_stmt(db.get(), kApiSql);
  sqlite3_bind_int64(api_stmt.get(), 1, min_start);
  sqlite3_bind_int64(api_stmt.get(), 2, max_end);
  std::size_t cursor = 0;
  while (true) {
    const int rc = sqlite3_step(api_stmt.get());
    if (rc == SQLITE_ROW) {
      const std::int64_t start_ns = sqlite_i64(api_stmt.get(), 0, 0);
      const std::int64_t end_ns = sqlite_i64(api_stmt.get(), 1, 0);
      while (cursor < intervals.size() &&
             start_ns > intervals[cursor].end_ns) {
        ++cursor;
      }
      if (cursor >= intervals.size() ||
          start_ns < intervals[cursor].start_ns ||
          end_ns > intervals[cursor].end_ns) {
        continue;
      }
      const auto name_found = string_ids.find(sqlite_i64(api_stmt.get(), 2, -1));
      if (name_found == string_ids.end()) {
        continue;
      }
      const std::string token = capture_host_api_token(name_found->second);
      if (!token.empty()) {
        tokens[cursor].push_back(token);
      }
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    throw std::runtime_error("failed to load ACLGraph capture APIs: " +
                             std::string(sqlite3_errmsg(api_stmt.db())));
  }

  std::vector<CaptureSlotSignature> out;
  out.reserve(intervals.size());
  for (std::size_t index = 0; index < intervals.size(); ++index) {
    out.push_back(CaptureSlotSignature{static_cast<std::uint32_t>(index),
                                       join_capture_tokens(tokens[index])});
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

std::string stream_info_db_path_for_msprof(const std::string& db_path) {
  const std::filesystem::path path(db_path);
  return (path.parent_path() / "host" / "sqlite" / "stream_info.db").string();
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
  if (!table_has_column(db, "CaptureStreamInfo", "model_stream_id")) {
    return out;
  }
  const bool has_model_id =
      table_has_column(db, "CaptureStreamInfo", "model_id");
  const bool has_original_stream_id =
      table_has_column(db, "CaptureStreamInfo", "original_stream_id");
  const char* kSql =
      "SELECT device_id, model_id, original_stream_id, model_stream_id "
      "FROM CaptureStreamInfo "
      "ORDER BY device_id, model_id, model_stream_id";
  const char* kLegacySql =
      "SELECT device_id, model_stream_id "
      "FROM CaptureStreamInfo "
      "ORDER BY device_id, model_stream_id";
  SqliteStmt stmt(db.get(),
                  has_model_id && has_original_stream_id ? kSql : kLegacySql);
  std::set<std::uint64_t> model_ids;
  std::set<std::uint64_t> original_stream_ids;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      const std::uint32_t device_id = sqlite_u32(stmt.get(), 0);
      if (has_model_id && has_original_stream_id) {
        model_ids.insert(sqlite_u64(stmt.get(), 1));
        original_stream_ids.insert(sqlite_u64(stmt.get(), 2));
        out.model_streams_by_device[device_id].insert(
            sqlite_u64(stmt.get(), 3));
      } else {
        out.model_streams_by_device[device_id].insert(
            sqlite_u64(stmt.get(), 1));
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
  return out;
}

bool event_overlaps(const TraceEventRow& event,
                    std::int64_t start_ns,
                    std::int64_t end_ns) {
  return event.start_ns <= end_ns && event.end_ns >= start_ns;
}

std::vector<GraphTaskView> controls_with_key(
    const std::vector<GraphTaskView>& controls,
    const std::string& key,
    const NativeIr& ir) {
  std::vector<GraphTaskView> out;
  for (const GraphTaskView& row : controls) {
    const std::string task_key =
        normalize_key(symbol_value_or_empty(ir, row.task->task_type_symbol_id));
    if (task_key == key) {
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

  std::vector<GraphTaskView> model_rows;
  std::vector<GraphTaskView> control_rows;
  for (const TaskRow& task : ir.tasks.rows()) {
    if (!task.trace_event_id.valid()) {
      continue;
    }
    const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
    const auto streams_found = model_streams_by_device.find(event.device_id);
    const bool is_model_stream =
        streams_found != model_streams_by_device.end() &&
        streams_found->second.find(event.stream_id) != streams_found->second.end();
    const std::string task_key =
        normalize_key(symbol_value_or_empty(ir, task.task_type_symbol_id));
    if (is_model_stream && event.end_ns > event.start_ns) {
      model_rows.push_back(GraphTaskView{&task, &event});
    }
    if (graph_task_key(task_key) && event.end_ns > event.start_ns) {
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
      controls_with_key(control_rows, "NOTIFY_WAIT", ir);
  const std::vector<GraphTaskView> model_execute_rows =
      controls_with_key(control_rows, "MODEL_EXECUTE", ir);

  std::vector<GraphReplayUnitView> units;
  bool used_execute_waves = false;
  if (!execute_launches.empty()) {
    units = split_rows_by_execute_waves(model_rows, execute_launches,
                                        capture_group_size);
    used_execute_waves = !units.empty();
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
  if (!used_execute_waves) {
    for (std::size_t index = 0; index + 1 < windows.size(); ++index) {
      if (windows[index].end_ns <= 0 || windows[index + 1].start_ns <= 0) {
        continue;
      }
      const std::int64_t gap =
          windows[index + 1].start_ns - windows[index].end_ns;
      if (gap > 0 && gap <= kGapNs) {
        windows[index].end_ns = windows[index + 1].start_ns;
      }
    }
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
      graph_template = ir.graph_templates.append(source_ref, hash, 0);
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
  const std::vector<GraphLaunchView> aclgraph_execute_launches =
      table_refs.find("CANN_API") == table_refs.end()
          ? std::vector<GraphLaunchView>()
          : load_aclgraph_execute_launches(db, string_ids);
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
  const std::string stream_info_path =
      stream_info_db_path_for_msprof(options_.db_path);
  AclGraphCaptureInfo capture_info =
      load_aclgraph_capture_info(stream_info_path);
  if (table_refs.find("CANN_API") != table_refs.end()) {
    capture_info.capture_slots = load_aclgraph_capture_slots(db, string_ids);
    capture_info.replay_unit_signature = build_capture_replay_unit_signature(
        capture_info.capture_slots, capture_info.capture_group_size);
  }
  if (!capture_info.model_streams_by_device.empty()) {
    const SourceRefId replay_source_ref = ir.source_refs.append(
        options_.source_kind, stream_info_path, "ACLGRAPH_REPLAY_UNIT", 0);
    materialize_aclgraph_replay_units(ir, capture_info.model_streams_by_device,
                                      aclgraph_execute_launches,
                                      replay_source_ref,
                                      capture_info.capture_group_size,
                                      capture_info.replay_unit_signature);
  }

  return ir;
}

}  // namespace traceloom
