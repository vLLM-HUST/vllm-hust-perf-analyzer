#include "perfetto_export_internal.h"

#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace traceloom::compat::perfetto_internal {

struct StatementCloser {
  void operator()(sqlite3_stmt* stmt) const { sqlite3_finalize(stmt); }
};
using Statement = std::unique_ptr<sqlite3_stmt, StatementCloser>;

Statement prepare(sqlite3* db, const std::string& sql) {
  sqlite3_stmt* raw = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Perfetto raw-provider export query failed: " +
                             std::string(sqlite3_errmsg(db)) + "\n" + sql);
  }
  return Statement(raw);
}

std::string text(sqlite3_stmt* stmt, int column) {
  const auto* value = sqlite3_column_text(stmt, column);
  return value ? reinterpret_cast<const char*>(value) : "";
}

std::string quote_identifier(const std::string& value) {
  std::string out = "\"";
  for (char ch : value) out += ch == '"' ? "\"\"" : std::string(1, ch);
  return out + '"';
}

bool has_object(sqlite3* db, const std::string& name) {
  auto stmt = prepare(db, "SELECT 1 FROM sqlite_master WHERE name=? LIMIT 1");
  sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_TRANSIENT);
  return sqlite3_step(stmt.get()) == SQLITE_ROW;
}

std::string json_quote(const std::string& value) {
  std::ostringstream out;
  out << '"';
  for (unsigned char ch : value) {
    switch (ch) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\b':
        out << "\\b";
        break;
      case '\f':
        out << "\\f";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (ch < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch)
              << std::dec;
        } else {
          out << static_cast<char>(ch);
        }
    }
  }
  return out.str() + '"';
}

std::string number(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(3) << value;
  return out.str();
}

std::int64_t raw_timeline_origin(sqlite3* db, std::int64_t current) {
  if (!has_object(db, "traceloom_raw_table")) return current;
  auto tables =
      prepare(db,
              "SELECT source_table,embedded_table_name FROM traceloom_raw_table WHERE source_table "
              "IN ('PYTORCH_API','CANN_API','TASK','COMMUNICATION_OP','AICORE_FREQ') "
              "OR source_table GLOB 'HIP_[0-9]*' OR source_table GLOB 'HIPOPS_*' "
              "OR source_table GLOB 'HIPCOPY_*'");
  while (sqlite3_step(tables.get()) == SQLITE_ROW) {
    const auto source = text(tables.get(), 0), table = text(tables.get(), 1);
    const auto column = source.rfind("HIP", 0) == 0 ? "BeginNs"
                        : source == "AICORE_FREQ" ? "timestampNs" : "startNs";
    auto stmt = prepare(db, "SELECT MIN(CAST(" + quote_identifier(column) + " AS INTEGER)) FROM " +
                                quote_identifier(table));
    if (sqlite3_step(stmt.get()) == SQLITE_ROW && sqlite3_column_type(stmt.get(), 0) != SQLITE_NULL)
      current = std::min(current, static_cast<std::int64_t>(sqlite3_column_int64(stmt.get(), 0)));
  }
  return current;
}

using StringMap = std::unordered_map<std::int64_t, std::string>;
StringMap load_strings(sqlite3* db, const std::string& table) {
  StringMap out;
  if (table.empty()) return out;
  auto stmt = prepare(db, "SELECT id,value FROM " + quote_identifier(table));
  while (sqlite3_step(stmt.get()) == SQLITE_ROW)
    out[sqlite3_column_int64(stmt.get(), 0)] = text(stmt.get(), 1);
  return out;
}
std::string resolved(const StringMap& strings, std::int64_t id, const std::string& fallback) {
  auto it = strings.find(id);
  return it == strings.end() ? fallback + " " + std::to_string(id) : it->second;
}

struct RawTable {
  std::string source_id, name, table;
};
std::vector<RawTable> raw_tables(sqlite3* db) {
  std::vector<RawTable> out;
  if (!has_object(db, "traceloom_raw_table")) return out;
  auto stmt = prepare(db,
                      "SELECT source_id,source_path,source_table,embedded_table_name FROM "
                      "traceloom_raw_table ORDER BY source_id,source_table");
  while (sqlite3_step(stmt.get()) == SQLITE_ROW)
    out.push_back({text(stmt.get(), 0), text(stmt.get(), 2), text(stmt.get(), 3)});
  return out;
}

std::string raw_args(const RawTable& table, sqlite3_stmt* stmt, int rowid_col,
                     const std::vector<std::pair<std::string, int>>& fields) {
  std::ostringstream out;
  out << "{\"source_id\":" << json_quote(table.source_id)
      << ",\"source_table\":" << json_quote(table.name)
      << ",\"embedded_table_name\":" << json_quote(table.table)
      << ",\"source_rowid\":" << sqlite3_column_int64(stmt, rowid_col);
  for (const auto& [name, col] : fields) {
    out << ',' << json_quote(name) << ':';
    const int type = sqlite3_column_type(stmt, col);
    if (type == SQLITE_NULL)
      out << "null";
    else if (type == SQLITE_INTEGER)
      out << sqlite3_column_int64(stmt, col);
    else if (type == SQLITE_FLOAT)
      out << number(sqlite3_column_double(stmt, col));
    else
      out << json_quote(text(stmt, col));
  }
  return out.str() + '}';
}

// Export literal HIP records even when the semantic adapter cannot interpret
// a host API id. No version-dependent HIP enum names or causal edges are guessed.
void export_raw_hip(sqlite3* db, RawTraceWriter& writer,
                    PerfettoExportReceipt& receipt,
                    const std::vector<RawTable>& tables) {
  int next_pid = 10000;
  for (const auto& t : tables) {
    const bool kernel = t.name.rfind("HIPOPS_", 0) == 0;
    const bool copy = t.name.rfind("HIPCOPY_", 0) == 0;
    const bool host = t.name.rfind("HIP_", 0) == 0;
    if (!kernel && !copy && !host) continue;
    std::map<std::pair<std::string, std::string>, std::string> names;
    if (kernel) {
      for (const auto& st : tables) {
        if (st.source_id != t.source_id || st.name != "STR_TABLE") continue;
        auto q = prepare(db, "SELECT PID,STR_ID,STR_NAME FROM " + quote_identifier(st.table));
        while (sqlite3_step(q.get()) == SQLITE_ROW)
          names[{text(q.get(), 0), text(q.get(), 1)}] = text(q.get(), 2);
      }
    }
    const std::string kind = kernel ? "HIPOPS" : copy ? "HIPCOPY" : "HIP";
    const std::string group1 = host ? "pid" : "dev_id";
    const std::string group2 = host ? "tid" : "queue_id";
    const std::string label_col = copy ? "Kind" : "Name";
    const std::string extra = copy ? "Bytes,MemoryType" : host ? "args" : "PARS";
    auto q = prepare(db, "SELECT rowid,BeginNs,EndNs,pid," + group1 + "," +
                            group2 + "," + label_col + ",_Index," + extra +
                            " FROM " + quote_identifier(t.table) +
                            " WHERE EndNs >= BeginNs ORDER BY BeginNs,EndNs,rowid");
    const int pid = next_pid++;
    writer.process(pid, "Raw provider · " + kind + " · " + t.source_id, pid);
    // Separate crossing intervals rather than implying nesting on one lane.
    using Lane = std::pair<std::int64_t, int>;
    std::map<std::string, std::vector<Lane>> lanes;
    int next_tid = 1;
    while (sqlite3_step(q.get()) == SQLITE_ROW) {
      const auto start = sqlite3_column_int64(q.get(), 1);
      const auto end = sqlite3_column_int64(q.get(), 2);
      const auto group = "pid " + text(q.get(), 3) + " · " + group1 + " " +
                         text(q.get(), 4) + " · " + group2 + " " + text(q.get(), 5);
      auto& available = lanes[group];
      auto lane = std::find_if(available.begin(), available.end(),
                               [&](const Lane& value) { return value.first <= start; });
      if (lane == available.end()) {
        available.emplace_back(end, next_tid++);
        lane = available.end() - 1;
        writer.thread(pid, lane->second, group + " · lane " +
                          std::to_string(available.size() - 1), lane->second);
      }
      lane->first = end;
      const auto raw_name = text(q.get(), 6);
      const auto found = names.find({text(q.get(), 3), raw_name});
      const auto label = kernel ? (found == names.end() ? raw_name : found->second)
                               : kind + " " + label_col + "=" + raw_name;
      std::vector<std::pair<std::string, int>> fields = {
          {"source_pid", 3}, {group1, 4}, {group2, 5}, {label_col, 6}, {"_Index", 7},
          {copy ? "Bytes" : host ? "args" : "PARS", 8}};
      if (copy) fields.emplace_back("MemoryType", 9);
      writer.slice(pid, lane->second, label, start, end, "raw." + kind,
                   raw_args(t, q.get(), 0, fields));
      ++receipt.raw_slices;
    }
  }
}

void export_raw_provider_timeline(sqlite3* db, RawTraceWriter& writer,
                                  PerfettoExportReceipt& receipt) {
  const auto tables = raw_tables(db);
  export_raw_hip(db, writer, receipt, tables);
  std::map<std::pair<std::string, std::string>, RawTable> by_source;
  for (const auto& t : tables) by_source[{t.source_id, t.name}] = t;
  std::map<std::string, int> source_order;
  for (const auto& t : tables)
    if (!source_order.count(t.source_id))
      source_order[t.source_id] = static_cast<int>(source_order.size());
  for (const auto& [source_id, si] : source_order) {
    std::string strings_table;
    auto sit = by_source.find({source_id, "STRING_IDS"});
    if (sit != by_source.end()) strings_table = sit->second.table;
    const auto strings = load_strings(db, strings_table);
    const std::vector<std::string> simple = {"PYTORCH_API", "CANN_API", "COMMUNICATION_OP"};
    int kind_index = 0;
    for (const auto& name : simple) {
      auto it = by_source.find({source_id, name});
      if (it == by_source.end()) {
        ++kind_index;
        continue;
      }
      const RawTable& t = it->second;
      const int pid = 300 + si * 10 + kind_index++;
      writer.process(pid, "Raw provider · " + name, 10 + pid);
      const std::string name_col = name == "COMMUNICATION_OP" ? "opName" : "name";
      const std::string group_cols = name == "COMMUNICATION_OP" ? "deviceId" : "globalTid";
      auto stmt =
          prepare(db, "SELECT rowid,startNs,endNs," + quote_identifier(group_cols) + "," +
                          quote_identifier(name_col) + " FROM " + quote_identifier(t.table) +
                          " ORDER BY " + quote_identifier(group_cols) +
                          ",CAST(startNs AS INTEGER),CAST(endNs AS INTEGER),rowid");
      std::map<std::int64_t, int> tids;
      while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        const auto group = sqlite3_column_int64(stmt.get(), 3);
        if (!tids.count(group)) {
          const int tid = static_cast<int>(tids.size() + 1);
          tids[group] = tid;
          writer.thread(pid, tid, group_cols + " " + std::to_string(group), tid);
        }
        const auto label = resolved(strings, sqlite3_column_int64(stmt.get(), 4), name);
        writer.slice(pid, tids[group], label, sqlite3_column_int64(stmt.get(), 1),
                     sqlite3_column_int64(stmt.get(), 2), "raw." + name,
                     raw_args(t, stmt.get(), 0, {{group_cols, 3}, {name_col, 4}}));
        ++receipt.raw_slices;
      }
    }
    auto task_it = by_source.find({source_id, "TASK"});
    if (task_it != by_source.end()) {
      const RawTable& t = task_it->second;
      const int pid = 300 + si * 10 + 3;
      writer.process(pid, "Raw provider · TASK", 10 + pid);
      StringMap task_names;
      auto ci = by_source.find({source_id, "COMPUTE_TASK_INFO"});
      if (ci != by_source.end()) {
        auto q = prepare(db, "SELECT globalTaskId,name FROM " + quote_identifier(ci->second.table));
        while (sqlite3_step(q.get()) == SQLITE_ROW)
          task_names[sqlite3_column_int64(q.get(), 0)] =
              resolved(strings, sqlite3_column_int64(q.get(), 1), "Task");
      }
      auto stmt = prepare(
          db, "SELECT rowid,startNs,endNs,deviceId,streamId,globalTaskId,taskType FROM " +
                  quote_identifier(t.table) + " ORDER BY deviceId,streamId,startNs,endNs,rowid");
      using Heap = std::priority_queue<std::pair<std::int64_t, int>,
                                       std::vector<std::pair<std::int64_t, int>>,
                                       std::greater<std::pair<std::int64_t, int>>>;
      std::pair<std::int64_t, std::int64_t> group = {-1, -1};
      std::priority_queue<int, std::vector<int>, std::greater<int>> free_lanes;
      Heap active;
      std::vector<int> lane_tids;
      int next_tid = 1;
      while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        const std::pair<std::int64_t, std::int64_t> next = {
            static_cast<std::int64_t>(sqlite3_column_int64(stmt.get(), 3)),
            static_cast<std::int64_t>(sqlite3_column_int64(stmt.get(), 4))};
        if (next != group) {
          group = next;
          active = Heap();
          free_lanes = {};
          lane_tids.clear();
        }
        const auto start = sqlite3_column_int64(stmt.get(), 1);
        while (!active.empty() && active.top().first <= start) {
          free_lanes.push(active.top().second);
          active.pop();
        }
        int lane;
        if (free_lanes.empty()) {
          lane = static_cast<int>(lane_tids.size());
          lane_tids.push_back(next_tid++);
          writer.thread(pid, lane_tids[lane],
                        "device " + std::to_string(group.first) + " · stream " +
                            std::to_string(group.second) + " · lane " + std::to_string(lane),
                        lane_tids[lane]);
        } else {
          lane = free_lanes.top();
          free_lanes.pop();
        }
        const auto gid = sqlite3_column_int64(stmt.get(), 5);
        auto ni = task_names.find(gid);
        const auto label = ni == task_names.end()
                               ? "Task " + std::to_string(sqlite3_column_int64(stmt.get(), 6))
                               : ni->second;
        writer.slice(
            pid, lane_tids[lane], label, start, sqlite3_column_int64(stmt.get(), 2), "raw.TASK",
            raw_args(t, stmt.get(), 0,
                     {{"deviceId", 3}, {"streamId", 4}, {"globalTaskId", 5}, {"taskType", 6}}));
        active.push({sqlite3_column_int64(stmt.get(), 2), lane});
        ++receipt.raw_slices;
      }
    }
    auto fi = by_source.find({source_id, "AICORE_FREQ"});
    if (fi != by_source.end()) {
      const int pid = 300 + si * 10 + 4;
      writer.process(pid, "Raw provider · AICORE frequency", 10 + pid);
      auto stmt = prepare(db, "SELECT rowid,deviceId,timestampNs,freq FROM " +
                                  quote_identifier(fi->second.table) +
                                  " ORDER BY deviceId,timestampNs,rowid");
      std::set<int> devices;
      while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        const int device = sqlite3_column_int(stmt.get(), 1), tid = device + 1;
        if (devices.insert(device).second)
          writer.thread(pid, tid, "device " + std::to_string(device) + " · AICORE MHz", tid);
        writer.counter(pid, tid, "AICORE frequency", sqlite3_column_int64(stmt.get(), 2), "freq",
                       sqlite3_column_double(stmt.get(), 3));
        ++receipt.counter_samples;
      }
    }
  }
}

}  // namespace traceloom::compat::perfetto_internal
