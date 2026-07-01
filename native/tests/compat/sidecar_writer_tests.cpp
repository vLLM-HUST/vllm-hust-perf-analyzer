#include "traceloom/compat/schema.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct ColumnInfo {
  std::string name;
  std::string type;
  bool not_null = false;
};

struct StoredAnchorCostRow {
  int anchor_idx = 0;
  std::string symbol;
  std::string anchor_kind;
  double total_us = 0.0;
  double self_us = 0.0;
  double aux_us = 0.0;
  double graph_child_us = 0.0;
  double residual_us = 0.0;
  int raw_child_task_count = 0;
  std::string top_ops;
  std::string diagnostic_flags;
};

std::string temp_db_path() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("traceloom_compat_sidecar_" + std::to_string(now) + ".db");
  return path.string();
}

std::vector<ColumnInfo> load_columns(const std::string& path,
                                     const std::string& table_name) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);

  sqlite3_stmt* raw_stmt = nullptr;
  const std::string sql = "PRAGMA table_info(" + table_name + ")";
  rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw_stmt, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);

  std::vector<ColumnInfo> columns;
  while ((rc = sqlite3_step(raw_stmt)) == SQLITE_ROW) {
    const unsigned char* name = sqlite3_column_text(raw_stmt, 1);
    const unsigned char* type = sqlite3_column_text(raw_stmt, 2);
    columns.push_back(ColumnInfo{
        name == nullptr ? "" : reinterpret_cast<const char*>(name),
        type == nullptr ? "" : reinterpret_cast<const char*>(type),
        sqlite3_column_int(raw_stmt, 3) != 0,
    });
  }
  traceloom::testing::require(rc == SQLITE_DONE);
  sqlite3_finalize(raw_stmt);
  sqlite3_close(db);
  return columns;
}

std::string sqlite_text(sqlite3_stmt* stmt, int column) {
  const unsigned char* value = sqlite3_column_text(stmt, column);
  return value == nullptr ? "" : reinterpret_cast<const char*>(value);
}

std::vector<StoredAnchorCostRow> load_anchor_cost_rows(
    const std::string& path) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);

  sqlite3_stmt* raw_stmt = nullptr;
  rc = sqlite3_prepare_v2(
      db,
      "SELECT anchor_idx, symbol, anchor_kind, total_us, self_us, aux_us, "
      "graph_child_us, residual_us, raw_child_task_count, top_ops, "
      "diagnostic_flags "
      "FROM traceloom_anchor_cost_breakdown ORDER BY anchor_idx",
      -1, &raw_stmt, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);

  std::vector<StoredAnchorCostRow> rows;
  while ((rc = sqlite3_step(raw_stmt)) == SQLITE_ROW) {
    rows.push_back(StoredAnchorCostRow{
        sqlite3_column_int(raw_stmt, 0),
        sqlite_text(raw_stmt, 1),
        sqlite_text(raw_stmt, 2),
        sqlite3_column_double(raw_stmt, 3),
        sqlite3_column_double(raw_stmt, 4),
        sqlite3_column_double(raw_stmt, 5),
        sqlite3_column_double(raw_stmt, 6),
        sqlite3_column_double(raw_stmt, 7),
        sqlite3_column_int(raw_stmt, 8),
        sqlite_text(raw_stmt, 9),
        sqlite_text(raw_stmt, 10),
    });
  }
  traceloom::testing::require(rc == SQLITE_DONE);
  sqlite3_finalize(raw_stmt);
  sqlite3_close(db);
  return rows;
}

}  // namespace

int main() {
  using traceloom::compat::CompatColumnType;
  using traceloom::testing::require;

  const std::string db_path = temp_db_path();
  traceloom::compat::materialize_compatibility_schema(db_path);
  traceloom::compat::materialize_compatibility_schema(db_path);

  const traceloom::compat::CompatTableSchema& schema =
      traceloom::compat::anchor_cost_breakdown_table_schema();
  const std::vector<ColumnInfo> columns = load_columns(db_path, schema.name);
  require(columns.size() == schema.columns.size());
  for (std::size_t index = 0; index < schema.columns.size(); ++index) {
    require(columns[index].name == schema.columns[index].name);
    require(columns[index].not_null == !schema.columns[index].nullable);
    if (schema.columns[index].type == CompatColumnType::kInteger) {
      require(columns[index].type == "INTEGER");
    } else if (schema.columns[index].type == CompatColumnType::kReal) {
      require(columns[index].type == "REAL");
    } else {
      require(columns[index].type == "TEXT");
    }
  }

  bool rejected_bad_schema = false;
  try {
    traceloom::compat::materialize_compatibility_schema(
        db_path,
        {traceloom::compat::CompatTableSchema{"bad-table", schema.columns}});
  } catch (const std::invalid_argument&) {
    rejected_bad_schema = true;
  }
  require(rejected_bad_schema);

  std::vector<traceloom::compat::AnchorCostBreakdownSqlRow> rows(2);
  rows[0].anchor_idx = 2;
  rows[0].symbol = "ACLL";
  rows[0].anchor_kind = "graph_l";
  rows[0].total_us = 123.456;
  rows[0].self_us = 1.0;
  rows[0].aux_us = 2.0;
  rows[0].graph_child_us = 120.0;
  rows[0].residual_us = 0.456;
  rows[0].raw_child_task_count = 20;
  rows[0].top_ops = "MatMul:16";
  rows[0].diagnostic_flags = "partial_overlap";
  rows[1].anchor_idx = 3;
  rows[1].symbol = "Kernel";
  rows[1].anchor_kind = "exec";
  rows[1].total_us = 7.0;
  rows[1].self_us = 7.0;

  traceloom::compat::replace_anchor_cost_breakdown_rows(db_path, rows);
  const std::vector<StoredAnchorCostRow> stored = load_anchor_cost_rows(db_path);
  require(stored.size() == 2);
  require(stored[0].anchor_idx == 2);
  require(stored[0].symbol == "ACLL");
  require(stored[0].anchor_kind == "graph_l");
  require(stored[0].total_us == 123.456);
  require(stored[0].self_us == 1.0);
  require(stored[0].aux_us == 2.0);
  require(stored[0].graph_child_us == 120.0);
  require(stored[0].residual_us == 0.456);
  require(stored[0].raw_child_task_count == 20);
  require(stored[0].top_ops == "MatMul:16");
  require(stored[0].diagnostic_flags == "partial_overlap");
  require(stored[1].anchor_idx == 3);
  require(stored[1].symbol == "Kernel");
  require(stored[1].anchor_kind == "exec");
  require(stored[1].total_us == 7.0);

  traceloom::compat::replace_anchor_cost_breakdown_rows(
      db_path, {rows.front()});
  require(load_anchor_cost_rows(db_path).size() == 1);

  std::remove(db_path.c_str());
  return 0;
}
