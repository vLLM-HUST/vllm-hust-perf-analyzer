#pragma once

#include <stdexcept>
#include <string>

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
#include <sqlite3.h>
#endif

namespace traceloom::compat {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)

class SqliteDb {
 public:
  explicit SqliteDb(const std::string& path) {
    sqlite3* raw = nullptr;
    const int rc =
        sqlite3_open_v2(path.c_str(), &raw,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    db_ = raw;
    if (rc != SQLITE_OK) {
      const std::string message =
          db_ == nullptr ? "unknown sqlite open error" : sqlite3_errmsg(db_);
      if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
      }
      throw std::runtime_error("failed to open compatibility sidecar: " +
                               message);
    }
  }

  ~SqliteDb() {
    if (db_ != nullptr) {
      sqlite3_close(db_);
    }
  }

  SqliteDb(const SqliteDb&) = delete;
  SqliteDb& operator=(const SqliteDb&) = delete;

  void exec(const std::string& sql) {
    char* error = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
      const std::string message =
          error == nullptr ? sqlite3_errmsg(db_) : error;
      sqlite3_free(error);
      throw std::runtime_error("failed to materialize compatibility sidecar: " +
                               message);
    }
  }

  sqlite3* get() const noexcept { return db_; }

 private:
  sqlite3* db_ = nullptr;
};

class SqliteStmt {
 public:
  SqliteStmt(sqlite3* db, const std::string& sql) : db_(db) {
    sqlite3_stmt* raw = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &raw, nullptr);
    stmt_ = raw;
    if (rc != SQLITE_OK) {
      throw std::runtime_error("failed to prepare compatibility sidecar SQL: " +
                               std::string(sqlite3_errmsg(db_)));
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

inline bool table_has_column(sqlite3* db,
                             const std::string& table,
                             const std::string& column) {
  SqliteStmt stmt(db, "PRAGMA table_info(" + table + ")");
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
    const unsigned char* raw_name = sqlite3_column_text(stmt.get(), 1);
    if (raw_name != nullptr &&
        column == reinterpret_cast<const char*>(raw_name)) {
      return true;
    }
  }
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to inspect compatibility table: " +
                             std::string(sqlite3_errmsg(db)));
  }
  return false;
}

inline void ensure_viz_node_anchor_cost_columns(SqliteDb& db) {
  static const char* const columns[] = {
      "compute_us", "comm_us",   "idle_us",   "total_us",
      "self_us",    "aux_events", "aux_us",
  };
  for (const char* column : columns) {
    if (table_has_column(db.get(), "traceloom_viz_node_anchor", column)) {
      continue;
    }
    db.exec(
        std::string("ALTER TABLE traceloom_viz_node_anchor ADD COLUMN ") +
        column + " REAL NOT NULL DEFAULT 0.0");
  }
}

inline void bind_text(SqliteStmt& stmt, int column, const std::string& value) {
  const int rc = sqlite3_bind_text(stmt.get(), column, value.c_str(), -1,
                                   SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    throw std::runtime_error("failed to bind compatibility sidecar text: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
}

// `value` must remain alive until the caller steps and clears `stmt`.
inline void bind_borrowed_text(SqliteStmt& stmt, int column,
                               const std::string& value) {
  const int rc = sqlite3_bind_text(stmt.get(), column, value.c_str(), -1,
                                   SQLITE_STATIC);
  if (rc != SQLITE_OK) {
    throw std::runtime_error(
        "failed to bind borrowed compatibility sidecar text: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
}

inline void bind_int64(SqliteStmt& stmt, int column, sqlite3_int64 value) {
  const int rc = sqlite3_bind_int64(stmt.get(), column, value);
  if (rc != SQLITE_OK) {
    throw std::runtime_error("failed to bind compatibility sidecar integer: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
}

inline void bind_null(SqliteStmt& stmt, int column) {
  const int rc = sqlite3_bind_null(stmt.get(), column);
  if (rc != SQLITE_OK) {
    throw std::runtime_error("failed to bind compatibility sidecar null: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
}

inline void bind_nullable_text(SqliteStmt& stmt, int column,
                               const std::string& value) {
  if (value.empty()) {
    bind_null(stmt, column);
  } else {
    bind_text(stmt, column, value);
  }
}

inline void bind_nullable_borrowed_text(SqliteStmt& stmt, int column,
                                        const std::string& value) {
  if (value.empty()) {
    bind_null(stmt, column);
  } else {
    bind_borrowed_text(stmt, column, value);
  }
}

inline void bind_nullable_int64_text(SqliteStmt& stmt, int column,
                                      const std::string& value) {
  if (value.empty()) {
    bind_null(stmt, column);
  } else {
    bind_int64(stmt, column, std::stoll(value));
  }
}

inline void bind_double(SqliteStmt& stmt, int column, double value) {
  const int rc = sqlite3_bind_double(stmt.get(), column, value);
  if (rc != SQLITE_OK) {
    throw std::runtime_error("failed to bind compatibility sidecar real: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
}

#endif

}  // namespace traceloom::compat
