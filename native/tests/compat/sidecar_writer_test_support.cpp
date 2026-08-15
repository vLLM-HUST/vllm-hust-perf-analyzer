#include "sidecar_writer_test_support.h"
#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace traceloom::testing::sidecar_writer_test {

std::string temp_db_path() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("traceloom_compat_sidecar_" + std::to_string(now) + ".db");
  return path.string();
}

void execute_sql(const std::string& path, const std::string& sql) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db,
                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                           nullptr);
  traceloom::testing::require(rc == SQLITE_OK);
  char* error = nullptr;
  rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
  if (error != nullptr) {
    sqlite3_free(error);
  }
  traceloom::testing::require(rc == SQLITE_OK);
  sqlite3_close(db);
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

std::vector<std::string> load_sqlite_master_names(const std::string& path,
                                                  const std::string& type) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);

  sqlite3_stmt* raw_stmt = nullptr;
  rc = sqlite3_prepare_v2(
      db,
      "SELECT name FROM sqlite_master WHERE type = ? ORDER BY name",
      -1, &raw_stmt, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);
  rc = sqlite3_bind_text(raw_stmt, 1, type.c_str(), -1, SQLITE_TRANSIENT);
  traceloom::testing::require(rc == SQLITE_OK);

  std::vector<std::string> names;
  while ((rc = sqlite3_step(raw_stmt)) == SQLITE_ROW) {
    const unsigned char* name = sqlite3_column_text(raw_stmt, 0);
    names.push_back(name == nullptr ? "" : reinterpret_cast<const char*>(name));
  }
  traceloom::testing::require(rc == SQLITE_DONE);
  sqlite3_finalize(raw_stmt);
  sqlite3_close(db);
  return names;
}

bool has_column(const std::vector<ColumnInfo>& columns,
                const std::string& name) {
  return std::any_of(columns.begin(), columns.end(),
                     [&](const ColumnInfo& column) {
                       return column.name == name;
                     });
}

std::string sqlite_text(sqlite3_stmt* stmt, int column) {
  const unsigned char* value = sqlite3_column_text(stmt, column);
  return value == nullptr ? "" : reinterpret_cast<const char*>(value);
}

int run_scalar_int(const std::string& path, const std::string& sql) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);

  sqlite3_stmt* raw_stmt = nullptr;
  rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw_stmt, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);
  rc = sqlite3_step(raw_stmt);
  traceloom::testing::require(rc == SQLITE_ROW);
  const int value = sqlite3_column_int(raw_stmt, 0);
  rc = sqlite3_step(raw_stmt);
  traceloom::testing::require(rc == SQLITE_DONE);
  sqlite3_finalize(raw_stmt);
  sqlite3_close(db);
  return value;
}

std::string run_scalar_text(const std::string& path, const std::string& sql) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);

  sqlite3_stmt* raw_stmt = nullptr;
  rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw_stmt, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);
  rc = sqlite3_step(raw_stmt);
  traceloom::testing::require(rc == SQLITE_ROW);
  const std::string value = sqlite_text(raw_stmt, 0);
  rc = sqlite3_step(raw_stmt);
  traceloom::testing::require(rc == SQLITE_DONE);
  sqlite3_finalize(raw_stmt);
  sqlite3_close(db);
  return value;
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

std::vector<StoredMetadataRow> load_metadata_rows(const std::string& path) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);

  sqlite3_stmt* raw_stmt = nullptr;
  rc = sqlite3_prepare_v2(
      db,
      "SELECT key, value FROM traceloom_metadata ORDER BY key",
      -1, &raw_stmt, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);

  std::vector<StoredMetadataRow> rows;
  while ((rc = sqlite3_step(raw_stmt)) == SQLITE_ROW) {
    rows.push_back(StoredMetadataRow{
        sqlite_text(raw_stmt, 0),
        sqlite_text(raw_stmt, 1),
    });
  }
  traceloom::testing::require(rc == SQLITE_DONE);
  sqlite3_finalize(raw_stmt);
  sqlite3_close(db);
  return rows;
}

void require_columns_match_schema(
    const std::string& db_path,
    const traceloom::compat::CompatTableSchema& schema) {
  using traceloom::compat::CompatColumnType;
  using traceloom::testing::require;

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
}

}  // namespace traceloom::testing::sidecar_writer_test
