#include "sqlite_profile_reader.h"

#include <stdexcept>

namespace traceloom::sqlite_profile_detail {

ReadOnlyDatabase::ReadOnlyDatabase(const std::string& path) {
  sqlite3* raw = nullptr;
  const int rc =
      sqlite3_open_v2(path.c_str(), &raw, SQLITE_OPEN_READONLY, nullptr);
  db_ = raw;
  if (rc == SQLITE_OK) {
    return;
  }

  const std::string message =
      db_ == nullptr ? "unknown SQLite open error" : sqlite3_errmsg(db_);
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
  throw std::runtime_error("failed to open read-only SQLite profile '" + path +
                           "': " + message);
}

ReadOnlyDatabase::~ReadOnlyDatabase() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
  }
}

Statement::Statement(sqlite3* db, const std::string& sql) : db_(db) {
  sqlite3_stmt* raw = nullptr;
  const int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &raw, nullptr);
  stmt_ = raw;
  if (rc == SQLITE_OK) {
    return;
  }

  const std::string message = sqlite3_errmsg(db_);
  if (stmt_ != nullptr) {
    sqlite3_finalize(stmt_);
    stmt_ = nullptr;
  }
  throw std::runtime_error("failed to prepare SQLite profile query: " +
                           message + "; sql=" + sql);
}

Statement::~Statement() {
  if (stmt_ != nullptr) {
    sqlite3_finalize(stmt_);
  }
}

std::int64_t read_i64(sqlite3_stmt* stmt,
                      int column,
                      std::int64_t fallback) {
  if (sqlite3_column_type(stmt, column) == SQLITE_NULL) {
    return fallback;
  }
  return sqlite3_column_int64(stmt, column);
}

std::uint32_t read_u32(sqlite3_stmt* stmt, int column) {
  const std::int64_t value = read_i64(stmt, column, 0);
  return value < 0 ? 0u : static_cast<std::uint32_t>(value);
}

std::uint64_t read_u64(sqlite3_stmt* stmt, int column) {
  const std::int64_t value = read_i64(stmt, column, 0);
  return value < 0 ? 0u : static_cast<std::uint64_t>(value);
}

std::string read_text(sqlite3_stmt* stmt, int column) {
  const unsigned char* raw = sqlite3_column_text(stmt, column);
  return raw == nullptr ? std::string()
                        : std::string(reinterpret_cast<const char*>(raw));
}

std::string quote_identifier(const std::string& value) {
  std::string out = "\"";
  for (const char ch : value) {
    out += ch;
    if (ch == '"') {
      out += '"';
    }
  }
  out += '"';
  return out;
}

}  // namespace traceloom::sqlite_profile_detail
