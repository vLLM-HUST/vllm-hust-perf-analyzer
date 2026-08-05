#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

namespace fs = std::filesystem;

std::string shell_quote(const std::string& value) {
  std::string quoted = "'";
  for (const char ch : value) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted += ch;
    }
  }
  quoted += "'";
  return quoted;
}

void create_diagnostic_fixture(const fs::path& path) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.string().c_str(), &db,
                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                           nullptr);
  traceloom::testing::require(rc == SQLITE_OK,
                              "failed to create audit CLI fixture");

  char* error = nullptr;
  rc = sqlite3_exec(
      db,
      "CREATE TABLE STRING_IDS(id INTEGER PRIMARY KEY, value TEXT);"
      "INSERT INTO STRING_IDS VALUES "
      "(10, 'AI_CORE'), (20, 'audit_fixture_matmul'), "
      "(21, 'MatMul'), (22, 'MIX_AIC');"
      "CREATE TABLE TASK("
      "startNs INTEGER, endNs INTEGER, deviceId INTEGER, "
      "connectionId INTEGER, globalTaskId INTEGER, globalPid INTEGER, "
      "taskType INTEGER, contextId INTEGER, streamId INTEGER, "
      "taskId INTEGER, modelId INTEGER);"
      // Source row 42 deliberately has a negative duration. Keep its raw task
      // identifier equal to the SQLite rowid so the report assertion is
      // unambiguous across the E2 and E3 diagnostic contracts.
      "INSERT INTO TASK(rowid, startNs, endNs, deviceId, connectionId, "
      "globalTaskId, globalPid, taskType, contextId, streamId, taskId, "
      "modelId) VALUES "
      "(42, 200, 100, 7, 700, 42, 1, 10, 0, 5, 101, 2), "
      "(43, 300, 400, 7, 701, 43, 1, 10, 0, 5, 102, 2);"
      "CREATE TABLE COMPUTE_TASK_INFO(globalTaskId INTEGER, name INTEGER, "
      "opType INTEGER, taskType INTEGER);"
      "INSERT INTO COMPUTE_TASK_INFO VALUES "
      "(42, 20, 21, 22), (43, 20, 21, 22);",
      nullptr, nullptr, &error);
  if (rc != SQLITE_OK) {
    const std::string message =
        error == nullptr ? "unknown SQLite error" : error;
    sqlite3_free(error);
    sqlite3_close(db);
    const std::string full_message =
        "failed to populate audit CLI fixture: " + message;
    traceloom::testing::require(false, full_message.c_str());
  }
  sqlite3_close(db);
}

std::string read_file(const fs::path& path) {
  std::ifstream input(path);
  traceloom::testing::require(input.good(),
                              "idle evidence audit report was not written");
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

}  // namespace

int main(int argc, char** argv) {
  traceloom::testing::require(
      argc == 2,
      "usage: idle_evidence_audit_cli_tests IDLE_EVIDENCE_AUDIT_EXECUTABLE");

  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path temp_dir =
      fs::temp_directory_path() /
      ("traceloom_idle_evidence_audit_cli_" + std::to_string(nonce));
  fs::create_directories(temp_dir);
  const fs::path source_db = temp_dir / "diagnostics.sqlite";
  const fs::path report = temp_dir / "audit.md";

  create_diagnostic_fixture(source_db);
  const std::string command = shell_quote(argv[1]) + " --source-db " +
                              shell_quote(source_db.string()) + " --out " +
                              shell_quote(report.string());
  const int exit_code = std::system(command.c_str());
  traceloom::testing::require(exit_code == 0,
                              "idle evidence audit CLI failed");

  const std::string markdown = read_file(report);
  traceloom::testing::require(
      markdown.find("### E2 diagnostics") != std::string::npos,
      "audit report omitted E2 diagnostics");
  traceloom::testing::require(
      markdown.find("| 7 | invalid_event_duration | 42 |") !=
          std::string::npos,
      "E2 diagnostic lost its device or source_row_id");
  traceloom::testing::require(
      markdown.find("### E3 diagnostic detail") != std::string::npos,
      "audit report omitted E3 diagnostic detail");
  traceloom::testing::require(
      markdown.find("| 7 | - | invalid_event_duration | 42 |") !=
          std::string::npos,
      "E3 device diagnostic lost its device, stream level, or source_row_id");

  fs::remove_all(temp_dir);
  return 0;
}
