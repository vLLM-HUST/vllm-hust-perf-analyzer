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
bool file_exists(const std::string& path) {
  std::ifstream input(path);
  return input.good();
}

std::string quote_identifier(const std::string& value) {
  return sqlite_profile_detail::quote_identifier(value);
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
GraphReplayUnitView replay_unit_for_rows(std::vector<GraphTaskView> rows) {
  GraphReplayUnitView unit;
  unit.rows = std::move(rows);
  return unit;
}
std::int64_t sqlite_i64(sqlite3_stmt* stmt,
                        int column,
                        std::int64_t fallback) {
  return sqlite_profile_detail::read_i64(stmt, column, fallback);
}

std::uint32_t sqlite_u32(sqlite3_stmt* stmt, int column) {
  return sqlite_profile_detail::read_u32(stmt, column);
}

std::uint64_t sqlite_u64(sqlite3_stmt* stmt, int column) {
  return sqlite_profile_detail::read_u64(stmt, column);
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
  return sqlite_profile_detail::read_text(stmt, column);
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

bool graph_body_task_is_communication(const TaskRow& task) {
  // Some CANN schemas populate compute-looking op columns on HCCL task rows.
  // Communication provenance is the stronger signal and must win so body
  // topology and cost accounting do not silently classify collectives as
  // compute.
  return task.comm_name_symbol_id.valid() ||
         task.communication_task_type_symbol_id.valid();
}

std::string graph_body_family(const std::string& label) {
  const std::string low = lower_ascii(label);
  if (low.find("dispatchffncombine") != std::string::npos ||
      (low.find("dispatch") != std::string::npos &&
       low.find("ffn") != std::string::npos &&
       low.find("combine") != std::string::npos)) {
    return "moe_fused";
  }
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
  if (family == "moe_fused") {
    return "MoeDispatchFFNCombine";
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

}  // namespace traceloom::ascend_sqlite_detail
