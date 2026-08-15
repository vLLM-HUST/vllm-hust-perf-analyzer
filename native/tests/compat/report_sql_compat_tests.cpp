#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kSnapshotVersion =
    "traceloom_report_query_snapshot_v1";

struct QueryCase {
  std::string query_filename;
  std::string corpus_filename;
};

struct QueryResult {
  std::vector<std::string> columns;
  std::vector<std::vector<std::string>> rows;
};

[[noreturn]] void fail(const std::string& message) {
  throw std::runtime_error(message);
}

fs::path repo_root() { return fs::path(TRACELOOM_REPO_ROOT); }

fs::path report_sql_dir() { return repo_root() / "docs" / "report-sql"; }

fs::path corpus_dir() {
  return repo_root() / "native" / "tests" / "fixtures" / "report_sql";
}

fs::path expected_dir() { return corpus_dir() / "expected"; }

std::string read_text(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.good()) {
    fail("cannot read " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void write_text(const fs::path& path, const std::string& contents) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output.good()) {
    fail("cannot write " + path.string());
  }
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!output.good()) {
    fail("failed to write " + path.string());
  }
}

bool has_suffix(const std::string& value, const std::string& suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
             0;
}

void require_plain_filename(const std::string& value,
                            const std::string& context) {
  if (value.empty() || fs::path(value).filename() != fs::path(value)) {
    fail(context + " must be a plain filename: " + value);
  }
}

std::vector<QueryCase> read_cases() {
  const fs::path path = corpus_dir() / "cases.tsv";
  std::istringstream input(read_text(path));
  std::string line;
  if (!std::getline(input, line) || line != "query\tcorpus") {
    fail(path.string() + ": expected query\\tcorpus header");
  }

  std::vector<QueryCase> cases;
  std::set<std::string> queries;
  std::size_t line_number = 1;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const std::size_t separator = line.find('\t');
    if (separator == std::string::npos ||
        line.find('\t', separator + 1) != std::string::npos) {
      fail(path.string() + ":" + std::to_string(line_number) +
           ": expected exactly two tab-separated fields");
    }
    QueryCase query_case{line.substr(0, separator),
                         line.substr(separator + 1)};
    require_plain_filename(query_case.query_filename, "query filename");
    require_plain_filename(query_case.corpus_filename, "corpus filename");
    if (!has_suffix(query_case.query_filename, ".sql") ||
        !has_suffix(query_case.corpus_filename, ".sql")) {
      fail(path.string() + ":" + std::to_string(line_number) +
           ": query and corpus must be .sql files");
    }
    if (!queries.insert(query_case.query_filename).second) {
      fail(path.string() + ": duplicate query " + query_case.query_filename);
    }
    cases.push_back(std::move(query_case));
  }
  if (cases.empty()) {
    fail(path.string() + ": no query cases");
  }
  return cases;
}

std::set<std::string> sql_filenames(const fs::path& directory) {
  std::set<std::string> filenames;
  for (const fs::directory_entry& entry :
       fs::directory_iterator(directory)) {
    if (entry.is_regular_file() && entry.path().extension() == ".sql") {
      filenames.insert(entry.path().filename().string());
    }
  }
  return filenames;
}

std::set<std::string> expected_snapshot_queries() {
  std::set<std::string> queries;
  if (!fs::exists(expected_dir())) {
    return queries;
  }
  for (const fs::directory_entry& entry :
       fs::directory_iterator(expected_dir())) {
    if (!entry.is_regular_file() || entry.path().extension() != ".snap") {
      continue;
    }
    queries.insert(entry.path().stem().string());
  }
  return queries;
}

std::string quote_identifier(const std::string& value) {
  std::string quoted = "\"";
  for (const char character : value) {
    quoted += character;
    if (character == '"') {
      quoted += '"';
    }
  }
  quoted += '"';
  return quoted;
}

std::string sqlite_error(sqlite3* db, const std::string& context) {
  return context + ": " + (db == nullptr ? "unknown SQLite error"
                                         : sqlite3_errmsg(db));
}

class CorpusDatabase {
 public:
  CorpusDatabase(const std::string& name, const fs::path& dump_path)
      : path_(temp_path(name)) {
    fs::remove(path_);
    const int rc = sqlite3_open_v2(path_.string().c_str(), &db_,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                                   nullptr);
    if (rc != SQLITE_OK) {
      const std::string message = sqlite_error(db_, "cannot create corpus DB");
      close();
      fs::remove(path_);
      fail(message);
    }
    try {
      execute(read_text(dump_path), "load " + dump_path.string());
      validate_integrity();
      validate_views();
    } catch (...) {
      close();
      fs::remove(path_);
      throw;
    }
  }

  CorpusDatabase(const CorpusDatabase&) = delete;
  CorpusDatabase& operator=(const CorpusDatabase&) = delete;

  ~CorpusDatabase() {
    close();
    std::error_code ignored;
    fs::remove(path_, ignored);
  }

  QueryResult query(const fs::path& query_path) const {
    const std::string sql = read_text(query_path);
    sqlite3_stmt* statement = nullptr;
    const char* tail = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &statement, &tail);
    if (rc != SQLITE_OK || statement == nullptr) {
      if (statement != nullptr) {
        sqlite3_finalize(statement);
      }
      fail(sqlite_error(db_, "prepare " + query_path.string()));
    }

    QueryResult result;
    const int column_count = sqlite3_column_count(statement);
    for (int column = 0; column < column_count; ++column) {
      result.columns.emplace_back(sqlite3_column_name(statement, column));
    }

    while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
      std::vector<std::string> row;
      row.reserve(static_cast<std::size_t>(column_count));
      for (int column = 0; column < column_count; ++column) {
        row.push_back(render_cell(statement, column));
      }
      result.rows.push_back(std::move(row));
    }
    if (rc != SQLITE_DONE) {
      const std::string message =
          sqlite_error(db_, "execute " + query_path.string());
      sqlite3_finalize(statement);
      fail(message);
    }
    sqlite3_finalize(statement);

    while (tail != nullptr && *tail != '\0') {
      sqlite3_stmt* extra = nullptr;
      const char* next = nullptr;
      rc = sqlite3_prepare_v2(db_, tail, -1, &extra, &next);
      if (rc != SQLITE_OK) {
        fail(sqlite_error(db_, "parse query tail " + query_path.string()));
      }
      if (extra != nullptr) {
        sqlite3_finalize(extra);
        fail(query_path.string() + ": multiple SQL statements are not allowed");
      }
      if (next == tail) {
        break;
      }
      tail = next;
    }
    return result;
  }

 private:
  static fs::path temp_path(const std::string& name) {
    static std::uint64_t sequence = 0;
    const auto now =
        std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() /
           ("traceloom_report_corpus_" + std::to_string(now) + "_" +
            std::to_string(sequence++) + "_" + fs::path(name).stem().string() +
            ".db");
  }

  void close() noexcept {
    if (db_ != nullptr) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
  }

  void execute(const std::string& sql, const std::string& context) const {
    char* raw_error = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &raw_error);
    if (rc == SQLITE_OK) {
      return;
    }
    const std::string detail =
        raw_error == nullptr ? sqlite3_errmsg(db_) : raw_error;
    sqlite3_free(raw_error);
    fail(context + ": " + detail);
  }

  std::string scalar_text(const std::string& sql,
                          const std::string& context) const {
    sqlite3_stmt* statement = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &statement, nullptr);
    if (rc != SQLITE_OK || statement == nullptr) {
      if (statement != nullptr) {
        sqlite3_finalize(statement);
      }
      fail(sqlite_error(db_, context));
    }
    rc = sqlite3_step(statement);
    if (rc != SQLITE_ROW) {
      const std::string message = sqlite_error(db_, context);
      sqlite3_finalize(statement);
      fail(message);
    }
    const unsigned char* value = sqlite3_column_text(statement, 0);
    const std::string result =
        value == nullptr ? std::string() : reinterpret_cast<const char*>(value);
    sqlite3_finalize(statement);
    return result;
  }

  void validate_integrity() const {
    if (scalar_text("PRAGMA integrity_check", "integrity_check") != "ok") {
      fail(path_.string() + ": SQLite integrity_check failed");
    }
  }

  void validate_views() const {
    sqlite3_stmt* statement = nullptr;
    int rc = sqlite3_prepare_v2(
        db_,
        "SELECT name FROM sqlite_master WHERE type = 'view' ORDER BY name",
        -1, &statement, nullptr);
    if (rc != SQLITE_OK || statement == nullptr) {
      fail(sqlite_error(db_, "list corpus views"));
    }
    std::vector<std::string> views;
    while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
      const unsigned char* value = sqlite3_column_text(statement, 0);
      views.emplace_back(reinterpret_cast<const char*>(value));
    }
    if (rc != SQLITE_DONE) {
      const std::string message = sqlite_error(db_, "list corpus views");
      sqlite3_finalize(statement);
      fail(message);
    }
    sqlite3_finalize(statement);

    for (const std::string& view : views) {
      sqlite3_stmt* view_statement = nullptr;
      const std::string sql =
          "SELECT * FROM " + quote_identifier(view) + " LIMIT 0";
      rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &view_statement, nullptr);
      if (rc != SQLITE_OK || view_statement == nullptr) {
        if (view_statement != nullptr) {
          sqlite3_finalize(view_statement);
        }
        fail(sqlite_error(db_, path_.string() + ": invalid view " + view));
      }
      sqlite3_finalize(view_statement);
    }
  }

  static std::string escape(std::string_view value) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const unsigned char character : value) {
      switch (character) {
        case '\\':
          output << "\\\\";
          break;
        case '\t':
          output << "\\t";
          break;
        case '\n':
          output << "\\n";
          break;
        case '\r':
          output << "\\r";
          break;
        default:
          if (character < 0x20 || character == 0x7f) {
            output << "\\x" << std::setw(2)
                   << static_cast<unsigned int>(character);
          } else {
            output << static_cast<char>(character);
          }
      }
    }
    return output.str();
  }

  static std::string render_float(double value) {
    if (value == 0.0) {
      value = 0.0;
    }
    if (std::isnan(value)) {
      return "nan";
    }
    if (std::isinf(value)) {
      return value < 0.0 ? "-inf" : "inf";
    }
    std::ostringstream output;
    constexpr int kReviewableDecimalDigits = 15;
    output << std::setprecision(kReviewableDecimalDigits) << value;
    return output.str();
  }

  static std::string render_cell(sqlite3_stmt* statement, int column) {
    switch (sqlite3_column_type(statement, column)) {
      case SQLITE_NULL:
        return "n:";
      case SQLITE_INTEGER:
        return "i:" +
               std::to_string(sqlite3_column_int64(statement, column));
      case SQLITE_FLOAT:
        return "f:" + render_float(sqlite3_column_double(statement, column));
      case SQLITE_TEXT: {
        const auto* value = reinterpret_cast<const char*>(
            sqlite3_column_text(statement, column));
        const int bytes = sqlite3_column_bytes(statement, column);
        return "t:" + escape(std::string_view(value, bytes));
      }
      case SQLITE_BLOB: {
        const auto* value = static_cast<const unsigned char*>(
            sqlite3_column_blob(statement, column));
        const int bytes = sqlite3_column_bytes(statement, column);
        std::ostringstream output;
        output << "b:" << std::hex << std::setfill('0');
        for (int index = 0; index < bytes; ++index) {
          output << std::setw(2) << static_cast<unsigned int>(value[index]);
        }
        return output.str();
      }
      default:
        fail("unexpected SQLite column type");
    }
  }

  fs::path path_;
  sqlite3* db_ = nullptr;
};

std::string escape_header(std::string_view value) {
  std::string escaped;
  for (const char character : value) {
    switch (character) {
      case '\\':
        escaped += "\\\\";
        break;
      case '\t':
        escaped += "\\t";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      default:
        escaped += character;
    }
  }
  return escaped;
}

std::string render_snapshot(const QueryResult& result) {
  std::ostringstream output;
  output << kSnapshotVersion << "\ncolumns";
  for (const std::string& column : result.columns) {
    output << '\t' << escape_header(column);
  }
  output << '\n';
  for (const std::vector<std::string>& row : result.rows) {
    output << "row";
    for (const std::string& value : row) {
      output << '\t' << value;
    }
    output << '\n';
  }
  return output.str();
}

std::string first_difference(const std::string& expected,
                             const std::string& actual) {
  std::istringstream expected_lines(expected);
  std::istringstream actual_lines(actual);
  std::string expected_line;
  std::string actual_line;
  std::size_t line_number = 1;
  while (true) {
    const bool has_expected =
        static_cast<bool>(std::getline(expected_lines, expected_line));
    const bool has_actual =
        static_cast<bool>(std::getline(actual_lines, actual_line));
    if (!has_expected && !has_actual) {
      return "unknown difference";
    }
    if (has_expected != has_actual || expected_line != actual_line) {
      return "line " + std::to_string(line_number) + "\nexpected: " +
             (has_expected ? expected_line : "<end-of-file>") +
             "\nactual:   " +
             (has_actual ? actual_line : "<end-of-file>");
    }
    ++line_number;
  }
}

fs::path snapshot_path(const QueryCase& query_case) {
  return expected_dir() / (query_case.query_filename + ".snap");
}

void require_inventory(const std::vector<QueryCase>& cases,
                       bool update_goldens) {
  std::set<std::string> mapped_queries;
  std::set<std::string> mapped_corpora{"empty_full.sql"};
  for (const QueryCase& query_case : cases) {
    mapped_queries.insert(query_case.query_filename);
    mapped_corpora.insert(query_case.corpus_filename);
  }
  if (mapped_queries != sql_filenames(report_sql_dir())) {
    fail("cases.tsv must map every docs/report-sql/*.sql file exactly once");
  }
  if (mapped_corpora != sql_filenames(corpus_dir())) {
    fail("report SQL corpus contains an unmapped or missing .sql fixture");
  }
  if (!update_goldens && mapped_queries != expected_snapshot_queries()) {
    fail("expected snapshots must cover every report query exactly once");
  }
}

int run(bool update_goldens) {
  const std::vector<QueryCase> cases = read_cases();
  require_inventory(cases, update_goldens);
  if (update_goldens) {
    fs::create_directories(expected_dir());
  }

  std::map<std::string, std::unique_ptr<CorpusDatabase>> databases;
  const auto database = [&](const std::string& corpus_filename)
      -> CorpusDatabase& {
    auto found = databases.find(corpus_filename);
    if (found == databases.end()) {
      auto inserted = databases.emplace(
          corpus_filename,
          std::make_unique<CorpusDatabase>(
              corpus_filename, corpus_dir() / corpus_filename));
      found = inserted.first;
    }
    return *found->second;
  };

  CorpusDatabase& empty = database("empty_full.sql");
  for (const QueryCase& query_case : cases) {
    const fs::path query_path = report_sql_dir() / query_case.query_filename;
    const QueryResult expected_shape = empty.query(query_path);
    if (!expected_shape.rows.empty()) {
      fail(query_case.query_filename +
           ": empty_full.sql unexpectedly returned rows");
    }

    const QueryResult result =
        database(query_case.corpus_filename).query(query_path);
    if (result.columns != expected_shape.columns) {
      fail(query_case.query_filename +
           ": populated and empty corpus columns differ");
    }
    if (result.rows.empty()) {
      fail(query_case.query_filename +
           ": mapped corpus did not exercise the query");
    }

    const std::string actual = render_snapshot(result);
    const fs::path expected_path = snapshot_path(query_case);
    if (update_goldens) {
      write_text(expected_path, actual);
      continue;
    }
    const std::string expected = read_text(expected_path);
    if (expected != actual) {
      fail(query_case.query_filename + ": golden mismatch at " +
           expected_path.string() + "\n" +
           first_difference(expected, actual) +
           "\nReview the semantic change; use --update-goldens only when "
           "intentional.");
    }
  }

  if (update_goldens) {
    require_inventory(cases, false);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    bool update_goldens = false;
    if (argc == 2 && std::string(argv[1]) == "--update-goldens") {
      update_goldens = true;
    } else if (argc != 1) {
      std::cerr << "usage: " << (argc > 0 ? argv[0] : "report-sql-tests")
                << " [--update-goldens]\n";
      return 2;
    }
    return run(update_goldens);
  } catch (const std::exception& error) {
    std::cerr << "report SQL compatibility test failed: " << error.what()
              << "\n";
    return 1;
  }
}
