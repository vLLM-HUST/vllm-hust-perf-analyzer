#include "support/sqlite_fixture.h"

#include <sqlite3.h>

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace traceloom::test {
namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.good()) {
    throw std::runtime_error("cannot read SQLite fixture script: " +
                             path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void execute_script(const std::filesystem::path& database_path,
                    const std::filesystem::path& script_path, int flags) {
  const std::string script = read_text(script_path);
  sqlite3* database = nullptr;
  const int open_rc = sqlite3_open_v2(database_path.string().c_str(), &database,
                                      flags, nullptr);
  if (open_rc != SQLITE_OK) {
    const std::string detail =
        database == nullptr ? "unknown SQLite error" : sqlite3_errmsg(database);
    if (database != nullptr) {
      sqlite3_close(database);
    }
    throw std::runtime_error("cannot open SQLite fixture database " +
                             database_path.string() + ": " + detail);
  }

  char* error = nullptr;
  const int exec_rc =
      sqlite3_exec(database, script.c_str(), nullptr, nullptr, &error);
  if (exec_rc != SQLITE_OK) {
    const std::string detail = error == nullptr ? sqlite3_errmsg(database) : error;
    sqlite3_free(error);
    sqlite3_close(database);
    throw std::runtime_error("cannot apply SQLite fixture script " +
                             script_path.string() + " to " +
                             database_path.string() + ": " + detail);
  }

  sqlite3_close(database);
}

}  // namespace

void materialize_sqlite_fixture(const std::filesystem::path& database_path,
                                const std::filesystem::path& script_path) {
  if (std::filesystem::exists(database_path)) {
    throw std::runtime_error("refusing to overwrite SQLite fixture database: " +
                             database_path.string());
  }
  std::filesystem::create_directories(database_path.parent_path());
  try {
    execute_script(database_path, script_path,
                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(database_path, ignored);
    throw;
  }
}

void apply_sqlite_fixture_mutation(
    const std::filesystem::path& database_path,
    const std::filesystem::path& script_path) {
  if (!std::filesystem::is_regular_file(database_path)) {
    throw std::runtime_error("SQLite mutation target does not exist: " +
                             database_path.string());
  }
  execute_script(database_path, script_path, SQLITE_OPEN_READWRITE);
}

}  // namespace traceloom::test
