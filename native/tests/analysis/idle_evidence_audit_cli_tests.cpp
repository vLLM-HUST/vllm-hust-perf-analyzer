#include "traceloom/analysis/stream_state_timeline.h"
#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

// Diagnostics use "<code>: <detail>" free text (E2 style); the code is the
// part before the first colon. Mirrors the audit report writer's
// diagnostic_code so the constructed rows use the same contract.
std::string diagnostic_code(const std::string& message) {
  const std::size_t colon = message.find(':');
  return message.substr(0, colon);
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
      "(43, 300, 400, 7, 701, 43, 1, 10, 0, 5, 102, 2), "
      // Source row 44 repeats row 43's exact interval on stream 5 with a
      // distinct lineage, so the E3 report's stream-level output path emits a
      // real coincident_distinct_events diagnostic at (7, 5).
      "(44, 300, 400, 7, 702, 44, 1, 10, 0, 5, 103, 2);"
      "CREATE TABLE COMPUTE_TASK_INFO(globalTaskId INTEGER, name INTEGER, "
      "opType INTEGER, taskType INTEGER);"
      "INSERT INTO COMPUTE_TASK_INFO VALUES "
      "(42, 20, 21, 22), (43, 20, 21, 22), (44, 20, 21, 22);",
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

// Renders the E3 diagnostic detail section with the exact row format of the
// audit report writer (idle_evidence_audit_main.cpp): run-level rows use
// ("-", "-"), device-level rows use ("<device>", "-"), and stream-level rows
// use ("<device>", "<stream>"). Keep in sync with the writer.
std::string render_e3_diagnostic_detail(
    const traceloom::StreamStateRunResult& streams) {
  std::string output = "### E3 diagnostic detail\n\n";
  output += "| device | stream | code | source_row_id | message |\n";
  output += "| --- | --- | --- | --- | --- |\n";
  const auto append_diagnostics =
      [&output](std::string device, std::string stream,
                const std::vector<traceloom::TimelineDiagnostic>& notes) {
        for (const traceloom::TimelineDiagnostic& diagnostic : notes) {
          output += "| " + device + " | " + stream + " | " +
                    diagnostic_code(diagnostic.message) + " | " +
                    std::to_string(diagnostic.source_row_id) + " | " +
                    diagnostic.message + " |\n";
        }
      };
  append_diagnostics("-", "-", streams.diagnostics);
  for (const traceloom::StreamStateDeviceResult& device : streams.devices) {
    append_diagnostics(std::to_string(device.device_id), "-",
                       device.diagnostics);
    for (const traceloom::StreamStateTimeline& stream_timeline :
         device.timelines) {
      append_diagnostics(std::to_string(device.device_id),
                         std::to_string(stream_timeline.stream_id),
                         stream_timeline.diagnostics);
    }
  }
  return output;
}

// The ascend adapter always attaches a valid trace event to every task and
// E2 visits every device that has any event, so the E3 report's run-level
// (-, -) path is unreachable through a fixture: it only fires for malformed
// IRs (out-of-range trace_event_id) or devices absent from E2. Directly
// construct one diagnostic per E3 layer and render them with the report
// writer's row format, so a dropped or misattributed layer fails the test.
// Each layer uses a distinct raw row id to catch column or layer swaps.
void assert_e3_layers_render_exactly() {
  traceloom::StreamStateRunResult streams;
  streams.status = traceloom::AnalysisStatus::kInvalidInput;
  // Run level: damage with no owning device surfaces at (-, -).
  streams.diagnostics.push_back(traceloom::TimelineDiagnostic{
      "invalid_trace_event_reference: task has an out-of-range "
      "trace_event_id",
      41});

  traceloom::StreamStateDeviceResult device;
  device.device_id = 7;
  device.status = traceloom::AnalysisStatus::kInvalidInput;
  device.diagnostics.push_back(traceloom::TimelineDiagnostic{
      "invalid_event_duration: task interval (end <= start)", 42});

  traceloom::StreamStateTimeline timeline;
  timeline.device_id = 7;
  timeline.stream_id = 5;
  timeline.diagnostics.push_back(traceloom::TimelineDiagnostic{
      "coincident_distinct_events: 2 distinct events share interval "
      "[300,400)",
      43});
  device.timelines.push_back(std::move(timeline));
  streams.devices.push_back(std::move(device));

  const std::string rendered = render_e3_diagnostic_detail(streams);
  traceloom::testing::require(
      rendered.find("| - | - | invalid_trace_event_reference | 41 |") !=
          std::string::npos,
      "E3 run-level (-, -) diagnostic lost its code or source_row_id");
  traceloom::testing::require(
      rendered.find("| 7 | - | invalid_event_duration | 42 |") !=
          std::string::npos,
      "E3 device-level (7, -) diagnostic lost its code or source_row_id");
  traceloom::testing::require(
      rendered.find("| 7 | 5 | coincident_distinct_events | 43 |") !=
          std::string::npos,
      "E3 stream-level (7, 5) diagnostic lost its code or source_row_id");
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
  traceloom::testing::require(
      markdown.find("| 7 | 5 | coincident_distinct_events | -1 |") !=
          std::string::npos,
      "E3 stream diagnostic lost its device, stream level, or source_row_id");

  assert_e3_layers_render_exactly();

  fs::remove_all(temp_dir);
  return 0;
}
