#include "traceloom/adapters/ascend_sqlite_adapter.h"

#include "traceloom/runtime/thread_pool.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::int64_t connection_id = 0;
};

struct CaptureSlotSignature {
  std::uint32_t slot_index = 0;
  std::string host_api_signature;
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
  std::vector<CaptureSlotSignature> capture_slots;
};

struct AclGraphCaptureInfo {
  std::unordered_map<std::uint32_t, std::unordered_set<std::uint64_t>>
      model_streams_by_device;
  std::uint32_t capture_group_size = 0;
  std::vector<CaptureSlotSignature> capture_slots;
  std::string replay_unit_signature;
};

struct GraphTaskSymbolSets {
  std::unordered_set<std::uint32_t> graph_control;
  std::unordered_set<std::uint32_t> notify_wait;
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
    out.push_back(CaptureSlotSignature{static_cast<std::uint32_t>(index),
                                       join_capture_tokens(tokens[index])});
  }
  return out;
}

AclGraphCannApiMetadata load_aclgraph_cann_api_metadata(
    SqliteDb& db,
    const std::unordered_map<std::int64_t, std::string>& string_ids) {
  AclGraphCannApiMetadata metadata;
  if (!table_has_column(db, "CANN_API", "name")) {
    return metadata;
  }

  const std::vector<std::int64_t> execute_ids =
      string_ids_for_value(string_ids, "aclmdlRIExecuteAsync");
  const std::vector<std::int64_t> begin_ids =
      string_ids_for_value(string_ids, "aclmdlRICaptureBegin");
  const std::vector<std::int64_t> end_ids =
      string_ids_for_value(string_ids, "aclmdlRICaptureEnd");
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
      "SELECT startNs, endNs, connectionId, name "
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
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      const std::int64_t start_ns = sqlite_i64(stmt.get(), 0, 0);
      const std::int64_t end_ns = sqlite_i64(stmt.get(), 1, 0);
      const std::int64_t connection_id = sqlite_i64(stmt.get(), 2, 0);
      const std::int64_t name_id = sqlite_i64(stmt.get(), 3, -1);
      if (contains_i64(execute_ids, name_id)) {
        metadata.execute_launches.push_back(
            GraphLaunchView{start_ns, end_ns, connection_id});
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
              return lhs.connection_id < rhs.connection_id;
            });
  const std::vector<CaptureInterval> intervals =
      build_capture_intervals(std::move(begin_markers), std::move(end_markers));
  metadata.capture_slots =
      build_aclgraph_capture_slots(intervals, std::move(token_candidates));
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
                                           bool ordered) {
  std::string sql =
      "SELECT rowid, startNs, endNs, deviceId, streamId, taskId, "
      "globalTaskId, connectionId, taskType "
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
          sqlite_i64(stmt.get(), 8)});
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
  if (reader_count <= 1) {
    return read_task_raw_rows(db, nullptr, true);
  }
  const std::vector<RowidRange> ranges = task_rowid_ranges(db, reader_count);
  if (ranges.size() <= 1) {
    return read_task_raw_rows(db, nullptr, true);
  }

  std::vector<std::vector<RawTaskRow>> chunks(ranges.size());
  ThreadPool pool(reader_count);
  pool.parallel_for(ranges.size(), [&](std::size_t index) {
    SqliteDb worker_db(db_path);
    chunks[index] = read_task_raw_rows(worker_db, &ranges[index], false);
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
                    comm_task.comm_name_symbol_id);
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
  const std::string quoted_model_stream_column =
      quote_identifier(model_stream_column);
  const std::string sql =
      has_model_id && has_original_stream_id
          ? "SELECT device_id, model_id, original_stream_id, " +
                quoted_model_stream_column + " FROM CaptureStreamInfo " +
                "ORDER BY device_id, model_id, " +
                quoted_model_stream_column
          : "SELECT device_id, " + quoted_model_stream_column +
                " FROM CaptureStreamInfo ORDER BY device_id, " +
                quoted_model_stream_column;
  SqliteStmt stmt(db.get(), sql.c_str());
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
  SqliteStmt stmt(
      db.get(),
      "SELECT start, end, connection_id, id FROM ApiData "
      "WHERE start IS NOT NULL AND end IS NOT NULL AND end > start "
      "ORDER BY start, end, rowid");
  std::vector<CaptureInterval> begin_markers;
  std::vector<CaptureInterval> end_markers;
  std::vector<CaptureTokenCandidate> token_candidates;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      throw std::runtime_error("failed to load split ApiData: " +
                               std::string(sqlite3_errmsg(stmt.db())));
    }
    const std::int64_t start_ns = sqlite_i64(stmt.get(), 0, 0) * 10;
    const std::int64_t end_ns = sqlite_i64(stmt.get(), 1, 0) * 10;
    const std::int64_t connection_id = sqlite_i64(stmt.get(), 2, 0);
    const std::string name = sqlite_text(stmt.get(), 3);
    if (name == "aclmdlRIExecuteAsync") {
      metadata.execute_launches.push_back(
          GraphLaunchView{start_ns, end_ns, connection_id});
    } else if (name == "aclmdlRICaptureBegin") {
      begin_markers.push_back(CaptureInterval{start_ns, end_ns});
    } else if (name == "aclmdlRICaptureEnd") {
      end_markers.push_back(CaptureInterval{start_ns, end_ns});
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
  return metadata;
}

struct SplitRawTask {
  RawTaskRow row;
  SourceRefId source_ref;
  SymbolId task_type_symbol;
  SplitComputeInfo compute;
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

  const SplitComputeIndex compute_info =
      load_split_task_info(find_split_table(inventory, "TaskInfo"), ir);
  const auto host_task_types =
      load_split_host_task_types(find_split_table(inventory, "HostTask"));
  std::vector<SplitRawTask> raw_tasks;
  for (const AscendSplitSQLiteTableInfo* table : task_tables) {
    const std::uint32_t table_device_id =
        split_device_id_from_path(table->db_path);
    const SourceRefId source_ref =
        table_refs.at(table->db_path + "\n" + table->table_name);
    SqliteDb db(table->db_path);
    SqliteStmt stmt(
        db.get(),
        "SELECT rowid, start_time, duration, device_task_type, stream_id, "
        "task_id, context_id, connection_id, host_task_type "
        "FROM AscendTask WHERE start_time >= 0 AND duration >= 0 "
        "ORDER BY start_time, stream_id, task_id, context_id, rowid");
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
      raw_tasks.push_back(SplitRawTask{
          RawTaskRow{sqlite_u64(stmt.get(), 0), start_ns,
                     start_ns + duration_ns, table_device_id, stream_id, task_id,
                     compute.synthetic_global_task_id, connection_id, -1},
          source_ref, ir.symbols.intern(host_task_type), compute});
    }
  }
  std::sort(raw_tasks.begin(), raw_tasks.end(),
            [](const SplitRawTask& lhs, const SplitRawTask& rhs) {
              return raw_task_row_less(lhs.row, rhs.row);
            });

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
                    SymbolId::invalid());
  }

  // Parse the split host API metadata now so schema/unit drift fails clearly.
  // Graph replay and communication attribution need additional split-only
  // clocks and are intentionally left to the incremental attribution model.
  (void)load_split_aclgraph_api_metadata(
      find_split_table(inventory, "ApiData"));
  return ir;
}

}  // namespace

bool ascend_sqlite_has_usable_task_table(const std::string& db_path) {
  return file_exists(db_path) && sqlite_table_has_rows(db_path, "TASK");
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

  std::unordered_map<std::int64_t, std::string> string_ids;
  timing.string_ids_ms = time_stage([&]() {
    if (table_refs.find("STRING_IDS") != table_refs.end()) {
      string_ids = load_string_ids(db, ir);
    }
  });
  std::unordered_map<std::int64_t, ComputeInfo> compute_info;
  timing.compute_info_ms = time_stage([&]() {
    if (table_refs.find("COMPUTE_TASK_INFO") != table_refs.end()) {
      compute_info = load_compute_info(db, ir, string_ids);
    }
  });
  AclGraphCannApiMetadata cann_api_metadata;
  timing.cann_api_metadata_ms = time_stage([&]() {
    if (table_refs.find("CANN_API") != table_refs.end()) {
      cann_api_metadata = load_aclgraph_cann_api_metadata(db, string_ids);
    }
  });
  std::unordered_map<std::int64_t, CommunicationTaskInfo>
      communication_task_info;
  timing.communication_task_info_ms = time_stage([&]() {
    if (table_refs.find("COMMUNICATION_TASK_INFO") != table_refs.end()) {
      communication_task_info = load_communication_task_info(
          db, ir, string_ids,
          table_has_column(db, "COMMUNICATION_TASK_INFO", "taskType"));
    }
  });
  StreamIndex streams;
  TaskLinkIndex task_links;
  timing.task_rows_ms = time_stage([&]() {
    if (table_refs.find("TASK") != table_refs.end()) {
      load_task_rows(db, options_.db_path, options_.thread_count, ir, streams,
                     task_links, string_ids, compute_info,
                     communication_task_info, table_refs.at("TASK"));
    }
  });
  timing.communication_op_rows_ms = time_stage([&]() {
    if (table_refs.find("COMMUNICATION_OP") != table_refs.end()) {
      load_communication_op_rows(db, ir, streams, task_links, string_ids,
                                 table_refs.at("COMMUNICATION_OP"),
                                 table_has_column(db, "COMMUNICATION_OP",
                                                  "opType"));
    }
  });
  const std::string stream_info_path =
      stream_info_db_path_for_msprof(options_.db_path);
  AclGraphCaptureInfo capture_info;
  timing.stream_info_capture_ms = time_stage(
      [&]() { capture_info = load_aclgraph_capture_info(stream_info_path); });
  timing.cann_api_capture_slots_ms = time_stage([&]() {
    if (table_refs.find("CANN_API") != table_refs.end()) {
      capture_info.capture_slots = std::move(cann_api_metadata.capture_slots);
      capture_info.replay_unit_signature = build_capture_replay_unit_signature(
          capture_info.capture_slots, capture_info.capture_group_size);
    }
  });
  timing.aclgraph_replay_units_ms = time_stage([&]() {
    if (!capture_info.model_streams_by_device.empty()) {
      const SourceRefId replay_source_ref = ir.source_refs.append(
          options_.source_kind, stream_info_path, "ACLGRAPH_REPLAY_UNIT", 0);
      materialize_aclgraph_replay_units(
          ir, capture_info.model_streams_by_device,
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
