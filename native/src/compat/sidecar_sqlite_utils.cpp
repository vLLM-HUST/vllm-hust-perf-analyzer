#include "sidecar_sqlite_utils.h"

#include <stdexcept>

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
namespace traceloom::compat::detail {

std::string quote_identifier(const std::string& value) {
  std::string out = "\"";
  for (const char ch : value) {
    out += ch == '"' ? "\"\"" : std::string(1, ch);
  }
  out += '"';
  return out;
}

std::string quote_literal(const std::string& value) {
  std::string out = "'";
  for (const char ch : value) {
    out += ch == '\'' ? "''" : std::string(1, ch);
  }
  out += '\'';
  return out;
}

void sqlite_exec(sqlite3* db, const std::string& sql,
                 const std::string& context) {
  char* error = nullptr;
  const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
  if (rc != SQLITE_OK) {
    const std::string message = error != nullptr ? error : sqlite3_errmsg(db);
    sqlite3_free(error);
    throw std::runtime_error(context + ": " + message);
  }
}

std::uint64_t sqlite_scalar_u64(sqlite3* db, const std::string& sql,
                                const std::string& context) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(context + ": " + sqlite3_errmsg(db));
  }
  const int rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    const std::string message = sqlite3_errmsg(db);
    sqlite3_finalize(stmt);
    throw std::runtime_error(context + ": " + message);
  }
  const sqlite3_int64 value = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return value < 0 ? 0 : static_cast<std::uint64_t>(value);
}

sqlite3* open_sqlite_readwrite(const std::string& path) {
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(path.c_str(), &db,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_URI,
                      nullptr) != SQLITE_OK) {
    const std::string message = db ? sqlite3_errmsg(db) : "open failed";
    if (db != nullptr) {
      sqlite3_close(db);
    }
    throw std::runtime_error("failed to open augmented DB: " + message);
  }
  return db;
}

}  // namespace traceloom::compat::detail
#endif
