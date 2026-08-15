#include "native_sidecar_packaging.h"

#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "sidecar_sqlite_utils.h"
#include "traceloom/core/sha256.h"

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
#include <sqlite3.h>

namespace traceloom::compat::detail {
namespace {

namespace fs = std::filesystem;

std::string readonly_file_uri(const std::string& path) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string uri = "file:";
  for (const unsigned char ch : path) {
    const bool unreserved =
        (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == '/' || ch == '-' || ch == '_' ||
        ch == '.' || ch == '~';
    if (unreserved) {
      uri += static_cast<char>(ch);
    } else {
      uri += '%';
      uri += kHex[(ch >> 4) & 0xf];
      uri += kHex[ch & 0xf];
    }
  }
  uri += "?mode=ro";
  return uri;
}

std::vector<std::string> sqlite_table_names(sqlite3* db,
                                            const std::string& schema) {
  const std::string sql =
      "SELECT name FROM " + quote_identifier(schema) +
      ".sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' "
      "ORDER BY name";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to inventory profiler tables: " +
                             std::string(sqlite3_errmsg(db)));
  }
  std::vector<std::string> names;
  while (true) {
    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      const std::string message = sqlite3_errmsg(db);
      sqlite3_finalize(stmt);
      throw std::runtime_error("failed to inventory profiler tables: " +
                               message);
    }
    const unsigned char* text = sqlite3_column_text(stmt, 0);
    names.emplace_back(text == nullptr
                           ? ""
                           : reinterpret_cast<const char*>(text));
  }
  sqlite3_finalize(stmt);
  return names;
}

bool sqlite_table_has_rowid(sqlite3* db, const std::string& schema,
                            const std::string& table) {
  const std::string sql = "SELECT rowid FROM " + quote_identifier(schema) +
                          "." + quote_identifier(table) + " LIMIT 0";
  sqlite3_stmt* stmt = nullptr;
  const int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
  if (stmt != nullptr) {
    sqlite3_finalize(stmt);
  }
  return rc == SQLITE_OK;
}

std::set<std::string> sqlite_table_columns(sqlite3* db,
                                           const std::string& schema,
                                           const std::string& table) {
  const std::string sql = "PRAGMA " + quote_identifier(schema) +
                          ".table_info(" + quote_identifier(table) + ")";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to inventory profiler columns: " +
                             std::string(sqlite3_errmsg(db)));
  }
  std::set<std::string> columns;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char* text = sqlite3_column_text(stmt, 1);
    if (text != nullptr) {
      columns.emplace(reinterpret_cast<const char*>(text));
    }
  }
  sqlite3_finalize(stmt);
  return columns;
}

std::string unique_source_rowid_column(const std::set<std::string>& columns) {
  std::string candidate = "__traceloom_source_rowid__";
  while (columns.count(candidate) != 0) {
    candidate += '_';
  }
  return candidate;
}

void sqlite_snapshot(const std::string& source_path,
                     const std::string& destination_path) {
  sqlite3* source = nullptr;
  sqlite3* destination = nullptr;
  if (sqlite3_open_v2(source_path.c_str(), &source, SQLITE_OPEN_READONLY,
                      nullptr) != SQLITE_OK) {
    const std::string message = source ? sqlite3_errmsg(source) : "open failed";
    if (source != nullptr) {
      sqlite3_close(source);
    }
    throw std::runtime_error("failed to open profiler DB for snapshot: " +
                             message);
  }
  if (sqlite3_open_v2(destination_path.c_str(), &destination,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
      SQLITE_OK) {
    const std::string message =
        destination ? sqlite3_errmsg(destination) : "open failed";
    if (destination != nullptr) {
      sqlite3_close(destination);
    }
    sqlite3_close(source);
    throw std::runtime_error("failed to create augmented DB snapshot: " +
                             message);
  }
  sqlite3_backup* backup =
      sqlite3_backup_init(destination, "main", source, "main");
  if (backup == nullptr) {
    const std::string message = sqlite3_errmsg(destination);
    sqlite3_close(destination);
    sqlite3_close(source);
    throw std::runtime_error("failed to initialize augmented DB snapshot: " +
                             message);
  }
  const int step_rc = sqlite3_backup_step(backup, -1);
  const int finish_rc = sqlite3_backup_finish(backup);
  const std::string message = sqlite3_errmsg(destination);
  sqlite3_close(destination);
  sqlite3_close(source);
  if (step_rc != SQLITE_DONE || finish_rc != SQLITE_OK) {
    throw std::runtime_error("failed to copy profiler DB into augmented DB: " +
                             message);
  }
}

RawPackagingResult inventory_snapshot_source(const std::string& source_path,
                                              sqlite3* snapshot_db) {
  RawPackagingResult result;
  RawSourceDatabase source;
  source.source_id = "raw-source-000";
  source.source_path = source_path;
  source.embedded_mode = "sqlite_snapshot";
  source.size_bytes = fs::file_size(source_path);
  source.sha256 = sha256_file_hex(source_path);
  result.sources.push_back(source);
  for (const std::string& table : sqlite_table_names(snapshot_db, "main")) {
    if (table.rfind("traceloom_", 0) == 0) {
      throw std::invalid_argument(
          "input already contains TraceLoom-owned tables: " + table);
    }
    RawTableCopy copy;
    copy.source_id = source.source_id;
    copy.source_path = source_path;
    copy.source_table = table;
    copy.embedded_table_name = table;
    copy.source_rowid_column =
        sqlite_table_has_rowid(snapshot_db, "main", table) ? "rowid" : "";
    copy.row_count = sqlite_scalar_u64(
        snapshot_db, "SELECT COUNT(*) FROM " + quote_identifier(table),
        "failed to count profiler table");
    result.tables.push_back(std::move(copy));
  }
  return result;
}

RawPackagingResult copy_multiple_sqlite_sources(
    const std::vector<std::string>& source_paths,
    const std::string& destination_path) {
  sqlite3* db = open_sqlite_readwrite(destination_path);
  RawPackagingResult result;
  try {
    for (std::size_t index = 0; index < source_paths.size(); ++index) {
      const fs::path source =
          fs::absolute(source_paths[index]).lexically_normal();
      if (!fs::is_regular_file(source)) {
        throw std::invalid_argument("raw source is not a regular SQLite file: " +
                                    source.string());
      }
      std::ostringstream id;
      id << "raw-source-" << std::setw(3) << std::setfill('0') << index;
      std::ostringstream alias;
      alias << "raw_source_" << std::setw(3) << std::setfill('0') << index;
      RawSourceDatabase source_row;
      source_row.source_id = id.str();
      source_row.source_ordinal = static_cast<std::uint32_t>(index);
      source_row.source_path = source.string();
      source_row.embedded_mode = "namespaced_table_copy";
      source_row.size_bytes = fs::file_size(source);
      source_row.sha256 = sha256_file_hex(source.string());
      result.sources.push_back(source_row);

      sqlite_exec(db, "ATTACH DATABASE " +
                          quote_literal(readonly_file_uri(source.string())) +
                          " AS " + quote_identifier(alias.str()),
                  "failed to attach split profiler DB");
      try {
        for (const std::string& table : sqlite_table_names(db, alias.str())) {
          const std::string embedded =
              "traceloom_raw_" + id.str().substr(id.str().size() - 3) +
              "__" + table;
          const bool has_rowid =
              sqlite_table_has_rowid(db, alias.str(), table);
          const std::set<std::string> columns =
              sqlite_table_columns(db, alias.str(), table);
          const std::string rowid_column =
              has_rowid ? unique_source_rowid_column(columns) : std::string();
          std::string select;
          if (has_rowid) {
            select = "SELECT rowid AS " + quote_identifier(rowid_column) +
                     ", * FROM " + quote_identifier(alias.str()) + "." +
                     quote_identifier(table) + " ORDER BY rowid";
          } else {
            select = "SELECT * FROM " + quote_identifier(alias.str()) + "." +
                     quote_identifier(table);
          }
          sqlite_exec(db,
                      "CREATE TABLE " + quote_identifier(embedded) +
                          " AS " + select,
                      "failed to embed split profiler table");
          RawTableCopy table_row;
          table_row.source_id = source_row.source_id;
          table_row.source_path = source.string();
          table_row.source_table = table;
          table_row.embedded_table_name = embedded;
          table_row.source_rowid_column = rowid_column;
          table_row.row_count = sqlite_scalar_u64(
              db, "SELECT COUNT(*) FROM " + quote_identifier(embedded),
              "failed to count embedded profiler table");
          result.tables.push_back(std::move(table_row));
        }
        sqlite_exec(db, "DETACH DATABASE " + quote_identifier(alias.str()),
                    "failed to detach split profiler DB");
      } catch (...) {
        try {
          sqlite_exec(db,
                      "DETACH DATABASE " + quote_identifier(alias.str()),
                      "failed to detach split profiler DB after error");
        } catch (...) {
        }
        throw;
      }
    }
    sqlite3_close(db);
    return result;
  } catch (...) {
    sqlite3_close(db);
    throw;
  }
}

}  // namespace

RawPackagingResult package_sqlite_sources(
    const std::vector<std::string>& source_paths,
    const std::string& destination_path) {
  if (source_paths.size() == 1) {
    sqlite_snapshot(source_paths.front(), destination_path);
    sqlite3* snapshot_db = open_sqlite_readwrite(destination_path);
    try {
      RawPackagingResult result =
          inventory_snapshot_source(source_paths.front(), snapshot_db);
      sqlite3_close(snapshot_db);
      return result;
    } catch (...) {
      sqlite3_close(snapshot_db);
      throw;
    }
  }
  return copy_multiple_sqlite_sources(source_paths, destination_path);
}

}  // namespace traceloom::compat::detail
#endif
