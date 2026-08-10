#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
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

std::string read_file(const fs::path& path) {
  std::ifstream input(path);
  traceloom::testing::require(input.good(),
                              "CLI multi-device report file was not written");
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

struct CliResult {
  int exit_code = 0;
  std::string output;
};

CliResult run_cli(const std::string& binary,
                  const std::vector<std::string>& args,
                  const fs::path& capture_path) {
  std::string command = shell_quote(binary);
  for (const std::string& arg : args) {
    command += " " + shell_quote(arg);
  }
  command += " > " + shell_quote(capture_path.string()) + " 2>&1";
  CliResult result;
  result.exit_code = std::system(command.c_str());
  result.output = read_file(capture_path);
  return result;
}

std::string anchor_count_field(const std::string& markdown) {
  const std::string needle = "anchor_count: `";
  const std::string::size_type begin = markdown.find(needle);
  traceloom::testing::require(
      begin != std::string::npos, "markdown header lacks anchor_count");
  const std::string::size_type end =
      markdown.find('`', begin + needle.size());
  traceloom::testing::require(end != std::string::npos,
                              "markdown anchor_count is unterminated");
  return markdown.substr(begin + needle.size(), end - begin - needle.size());
}

std::string trace_event_count_field(const std::string& markdown) {
  const std::string needle = "trace_event_count: `";
  const std::string::size_type begin = markdown.find(needle);
  traceloom::testing::require(
      begin != std::string::npos, "markdown header lacks trace_event_count");
  const std::string::size_type end =
      markdown.find('`', begin + needle.size());
  traceloom::testing::require(end != std::string::npos,
                              "markdown trace_event_count is unterminated");
  return markdown.substr(begin + needle.size(), end - begin - needle.size());
}

std::string device_id_field(const std::string& markdown) {
  const std::string needle = "device_id: `";
  const std::string::size_type begin = markdown.find(needle);
  traceloom::testing::require(begin != std::string::npos,
                              "markdown header lacks device_id");
  const std::string::size_type end =
      markdown.find('`', begin + needle.size());
  traceloom::testing::require(end != std::string::npos,
                              "markdown device_id is unterminated");
  return markdown.substr(begin + needle.size(), end - begin - needle.size());
}

int run_scalar_int(const std::string& path, const std::string& sql) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);

  sqlite3_stmt* raw_stmt = nullptr;
  rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw_stmt, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);
  rc = sqlite3_step(raw_stmt);
  traceloom::testing::require(rc == SQLITE_ROW);
  const int value = sqlite3_column_int(raw_stmt, 0);
  rc = sqlite3_step(raw_stmt);
  traceloom::testing::require(rc == SQLITE_DONE);
  sqlite3_finalize(raw_stmt);
  sqlite3_close(db);
  return value;
}

}  // namespace

int main(int argc, char** argv) {
  traceloom::testing::require(
      argc == 3,
      "usage: cli_multidevice_report_tests TRACELOOM_EXECUTABLE "
      "MULTI_DEVICE_FIXTURE_DB");
  const std::string binary = argv[1];
  const fs::path fixture_db = argv[2];
  traceloom::testing::require(fs::is_regular_file(fixture_db),
                              "multi-device fixture DB is missing");

  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path temp_dir =
      fs::temp_directory_path() /
      ("traceloom_cli_multidevice_report_" + std::to_string(nonce));
  fs::create_directories(temp_dir);
  const fs::path capture = temp_dir / "cli.capture";

  // ---- Default emission: one per-device report file per device.
  const fs::path single_db = temp_dir / "capture.sqlite";
  fs::copy_file(fixture_db, single_db,
                fs::copy_options::overwrite_existing);
  CliResult default_result =
      run_cli(binary, {single_db.string()}, capture);
  traceloom::testing::require(default_result.exit_code == 0,
                              "default per-device emission failed");
  const fs::path output_root = temp_dir / "traceloom";
  const fs::path device0_file = output_root / "device0_loop_tree_v2.md";
  const fs::path device1_file = output_root / "device1_loop_tree_v2.md";
  traceloom::testing::require(fs::is_regular_file(device0_file),
                              "device0 report was not emitted");
  traceloom::testing::require(fs::is_regular_file(device1_file),
                              "device1 report was not emitted");
  const std::string device0_markdown = read_file(device0_file);
  const std::string device1_markdown = read_file(device1_file);
  traceloom::testing::require(device_id_field(device0_markdown) == "0",
                              "device0 report header device id");
  traceloom::testing::require(device_id_field(device1_markdown) == "1",
                              "device1 report header device id");

  // Header counts must be device-local. Cross-check against the anchor
  // table materialized from the same DB by the same binary: each device
  // report must claim exactly its own anchors and no other device's.
  const fs::path sidecar_db = temp_dir / "sidecar.db";
  const CliResult sidecar_result = run_cli(
      binary,
      {"--source-db", single_db.string(), "--sidecar-only",
       "--compat-db-out", sidecar_db.string()},
      capture);
  traceloom::testing::require(sidecar_result.exit_code == 0,
                              "sidecar materialization failed");
  const int anchors_device0 =
      run_scalar_int(sidecar_db.string(),
                     "SELECT COUNT(*) FROM traceloom_anchor "
                     "WHERE device_id = 0");
  const int anchors_device1 =
      run_scalar_int(sidecar_db.string(),
                     "SELECT COUNT(*) FROM traceloom_anchor "
                     "WHERE device_id = 1");
  traceloom::testing::require(anchors_device0 > 0 && anchors_device1 > 0,
                              "fixture must own anchors on both devices");
  traceloom::testing::require(
      anchor_count_field(device0_markdown) ==
          std::to_string(anchors_device0),
      "device0 report claims another device's anchors");
  traceloom::testing::require(
      anchor_count_field(device1_markdown) ==
          std::to_string(anchors_device1),
      "device1 report claims another device's anchors");
  // The fixture's total anchor count differs from each device-local count,
  // so the exact-match checks above would fail if a device report fell back
  // to DB-global header counts.
  const int anchors_total = anchors_device0 + anchors_device1;
  traceloom::testing::require(
      anchors_total != anchors_device0 && anchors_total != anchors_device1,
      "fixture must distinguish global from device-local anchor counts");
  // Trace-event provenance must be device-local too: each report header
  // claims exactly its own device's events from the same materialized DB.
  const int events_device0 =
      run_scalar_int(sidecar_db.string(),
                     "SELECT COUNT(*) FROM traceloom_event "
                     "WHERE device_id = 0");
  const int events_device1 =
      run_scalar_int(sidecar_db.string(),
                     "SELECT COUNT(*) FROM traceloom_event "
                     "WHERE device_id = 1");
  traceloom::testing::require(events_device0 > 0 && events_device1 > 0,
                              "fixture must own events on both devices");
  traceloom::testing::require(
      trace_event_count_field(device0_markdown) ==
          std::to_string(events_device0),
      "device0 report claims another device's trace events");
  traceloom::testing::require(
      trace_event_count_field(device1_markdown) ==
          std::to_string(events_device1),
      "device1 report claims another device's trace events");
  const int events_total = events_device0 + events_device1;
  traceloom::testing::require(
      events_total != events_device0 && events_total != events_device1,
      "fixture must distinguish global from device-local event counts");

  // ---- Collision avoidance: multiple multi-device DBs in one directory get
  // DB identity plus device id in every default report filename.
  const fs::path multi_dir = temp_dir / "multi";
  fs::create_directories(multi_dir);
  const fs::path db_a = multi_dir / "aaa.sqlite";
  const fs::path db_b = multi_dir / "bbb.sqlite";
  fs::copy_file(fixture_db, db_a, fs::copy_options::overwrite_existing);
  fs::copy_file(fixture_db, db_b, fs::copy_options::overwrite_existing);
  const CliResult multi_result = run_cli(binary, {multi_dir.string()},
                                         capture);
  traceloom::testing::require(multi_result.exit_code == 0,
                              "multi-DB default emission failed");
  const fs::path multi_output = multi_dir / "traceloom";
  const std::vector<std::string> expected_files = {
      "db01_device0_loop_tree_v2.md", "db01_device1_loop_tree_v2.md",
      "db02_device0_loop_tree_v2.md", "db02_device1_loop_tree_v2.md"};
  for (const std::string& name : expected_files) {
    traceloom::testing::require(
        fs::is_regular_file(multi_output / name),
        ("multi-DB per-device report name collision: " + name).c_str());
  }
  const std::string db01_device0 = read_file(
      multi_output / "db01_device0_loop_tree_v2.md");
  const std::string db01_device1 = read_file(
      multi_output / "db01_device1_loop_tree_v2.md");
  traceloom::testing::require(device_id_field(db01_device0) == "0");
  traceloom::testing::require(device_id_field(db01_device1) == "1");

  // ---- Unknown device diagnostic.
  const CliResult unknown_result = run_cli(
      binary,
      {"--source-db", single_db.string(),
       "--loop-tree-out", (temp_dir / "unknown.md").string(),
       "--loop-tree-device-id", "9"},
      capture);
  traceloom::testing::require(unknown_result.exit_code != 0,
                              "unknown device id must fail");
  traceloom::testing::require(
      unknown_result.output.find(
          "no native_report_tree rows for device 9; available devices: 0, 1") !=
          std::string::npos,
      "unknown device diagnostic missing");

  // ---- Explicit --loop-tree-out on a multi-device DB requires a device id.
  const CliResult explicit_result = run_cli(
      binary,
      {"--source-db", single_db.string(),
       "--loop-tree-out", (temp_dir / "explicit.md").string()},
      capture);
  traceloom::testing::require(explicit_result.exit_code != 0,
                              "explicit path without device id must fail");
  traceloom::testing::require(
      explicit_result.output.find(
          "profile DB contains multiple devices; select one with "
          "--loop-tree-device-id when --loop-tree-out is given") !=
          std::string::npos,
      "explicit path rejection diagnostic missing");

  fs::remove_all(temp_dir);
  return 0;
}
