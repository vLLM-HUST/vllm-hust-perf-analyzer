#include "sqlite_profile_reader.h"

#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

std::string temp_db_path() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return (std::filesystem::temp_directory_path() /
          ("traceloom_sqlite_profile_reader_" + std::to_string(now) + ".db"))
      .string();
}

void create_fixture(const std::string& path) {
  sqlite3* db = nullptr;
  traceloom::testing::require(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
  char* error = nullptr;
  const int rc = sqlite3_exec(
      db,
      "CREATE TABLE \"odd\"\"name\"(value INTEGER, label TEXT);"
      "INSERT INTO \"odd\"\"name\" VALUES(NULL, NULL), (7, 'seven');",
      nullptr, nullptr, &error);
  if (rc != SQLITE_OK) {
    const std::string message = error == nullptr ? "unknown" : error;
    sqlite3_free(error);
    sqlite3_close(db);
    throw std::runtime_error(message);
  }
  sqlite3_close(db);
}

}  // namespace

int main() {
  using namespace traceloom::sqlite_profile_detail;
  using traceloom::testing::require;

  const std::string path = temp_db_path();
  create_fixture(path);

  ReadOnlyDatabase db(path);
  Statement rows(db.get(),
                 "SELECT value, label FROM \"odd\"\"name\" ORDER BY rowid");
  require(sqlite3_step(rows.get()) == SQLITE_ROW);
  require(read_i64(rows.get(), 0, -9) == -9);
  require(read_u32(rows.get(), 0) == 0);
  require(read_u64(rows.get(), 0) == 0);
  require(read_text(rows.get(), 1).empty());
  require(sqlite3_step(rows.get()) == SQLITE_ROW);
  require(read_i64(rows.get(), 0, -9) == 7);
  require(read_u32(rows.get(), 0) == 7);
  require(read_u64(rows.get(), 0) == 7);
  require(read_text(rows.get(), 1) == "seven");
  require(sqlite3_step(rows.get()) == SQLITE_DONE);
  require(quote_identifier("odd\"name") == "\"odd\"\"name\"");

  bool rejected_bad_sql = false;
  try {
    Statement invalid(db.get(), "SELECT missing FROM");
  } catch (const std::runtime_error& error) {
    rejected_bad_sql =
        std::string(error.what()).find("SELECT missing FROM") !=
        std::string::npos;
  }
  require(rejected_bad_sql);

  const std::string missing_path = path + ".missing";
  bool rejected_missing = false;
  try {
    ReadOnlyDatabase missing(missing_path);
  } catch (const std::runtime_error&) {
    rejected_missing = true;
  }
  require(rejected_missing);
  require(!std::filesystem::exists(missing_path));

  std::remove(path.c_str());
  return 0;
}
