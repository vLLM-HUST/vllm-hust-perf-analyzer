#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace traceloom::inference::detail {

inline void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
struct DbCloser {
  void operator()(sqlite3* p) const { sqlite3_close(p); }
};
using Db = std::unique_ptr<sqlite3, DbCloser>;
struct StmtCloser {
  void operator()(sqlite3_stmt* p) const { sqlite3_finalize(p); }
};
using Stmt = std::unique_ptr<sqlite3_stmt, StmtCloser>;
Db open(const std::string& path, bool readonly = false);
Stmt prepare(sqlite3* db, const std::string& sql);
inline bool next_row(sqlite3_stmt* stmt) {
  const int rc = sqlite3_step(stmt);
  require(rc == SQLITE_ROW || rc == SQLITE_DONE, "inference SQL query failed");
  return rc == SQLITE_ROW;
}
void exec(sqlite3* db, const std::string& sql);
void bind_text(sqlite3_stmt* stmt, int index, const std::string& value);
std::string text(sqlite3_stmt* stmt, int index);
std::string quote(const std::string& value);
void initialize(sqlite3* db, bool summaries);
void check_version(sqlite3* db);
std::string normalize(sqlite3* db, const std::string& line, bool summaries,
                      std::size_t& discarded);

struct Span {
  std::string trace, id, parent, name, kind, status, producer, clock;
  std::string attributes, summary, evidence, terminal_status;
  std::vector<std::string> dependencies;
  std::int64_t start = 0, end = 0, wall = 0;
  bool has_start = false, has_end = false;
  bool missing_parent = false;
  bool path_observed = false;
  std::string observations;
  std::int64_t path_ns = 0;
  std::size_t depth = 0;
};
struct Observation {
  std::string trace, span, producer, clock, name, payload;
  std::int64_t monotonic = 0;
};
struct Snapshot {
  std::vector<Observation> observations;
  std::vector<Span> spans;
  std::map<std::string, std::string> trace_states;
  std::map<std::string, std::int64_t> dropped;
  std::size_t unresolved_links = 0;
  std::size_t cross_clock_links = 0;
};
Snapshot load(sqlite3* db);
void validate_and_measure(Snapshot& snapshot);
void write_html(const Snapshot& snapshot, const std::string& output, bool live);
void write_perfetto(const Snapshot& snapshot, const std::string& output);
void atomic_write(const std::string& path, const std::string& contents);

}  // namespace traceloom::inference::detail
