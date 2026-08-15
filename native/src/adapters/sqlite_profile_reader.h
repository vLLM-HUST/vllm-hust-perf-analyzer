#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <string>

namespace traceloom::sqlite_profile_detail {

// Provider-neutral SQLite ownership only. Schema interpretation and NativeIr
// construction stay in the individual provider adapters.
class ReadOnlyDatabase {
 public:
  explicit ReadOnlyDatabase(const std::string& path);
  ~ReadOnlyDatabase();

  ReadOnlyDatabase(const ReadOnlyDatabase&) = delete;
  ReadOnlyDatabase& operator=(const ReadOnlyDatabase&) = delete;

  sqlite3* get() const noexcept { return db_; }

 private:
  sqlite3* db_ = nullptr;
};

class Statement {
 public:
  Statement(sqlite3* db, const std::string& sql);
  ~Statement();

  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;

  sqlite3_stmt* get() const noexcept { return stmt_; }
  sqlite3* db() const noexcept { return db_; }

 private:
  sqlite3* db_ = nullptr;
  sqlite3_stmt* stmt_ = nullptr;
};

std::int64_t read_i64(sqlite3_stmt* stmt,
                      int column,
                      std::int64_t fallback);
std::uint32_t read_u32(sqlite3_stmt* stmt, int column);
std::uint64_t read_u64(sqlite3_stmt* stmt, int column);
std::string read_text(sqlite3_stmt* stmt, int column);
std::string quote_identifier(const std::string& value);

}  // namespace traceloom::sqlite_profile_detail
