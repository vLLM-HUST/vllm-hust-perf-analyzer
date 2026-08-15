#include "traceloom/adapters/ascend_sqlite_adapter.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

std::string temp_db_path() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return (std::filesystem::temp_directory_path() /
          ("traceloom_ascend_tied_stream_" + std::to_string(nonce) + ".db"))
      .string();
}

void create_tied_stream_fixture(const std::string& path) {
  sqlite3* db = nullptr;
  require(sqlite3_open_v2(path.c_str(), &db,
                          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                          nullptr) == SQLITE_OK,
          "failed to create tied-stream fixture DB");
  char* error = nullptr;
  const int rc = sqlite3_exec(
      db,
      "CREATE TABLE STRING_IDS(id INTEGER PRIMARY KEY, value TEXT);"
      "INSERT INTO STRING_IDS(id, value) VALUES "
      "(10, 'AI_CORE'), (30, 'hcclAllReduce'), (31, 'hcom_allReduce_');"
      "CREATE TABLE TASK("
      "startNs INTEGER, endNs INTEGER, deviceId INTEGER, "
      "connectionId INTEGER, globalTaskId INTEGER, globalPid INTEGER, "
      "taskType INTEGER, contextId INTEGER, streamId INTEGER, "
      "taskId INTEGER, modelId INTEGER);"
      "INSERT INTO TASK VALUES "
      "(100, 200, 0, 701, 9001, 1, 10, 0, 9, 99, 2), "
      "(100, 200, 0, 701, 9002, 1, 10, 0, 2, 100, 2);"
      "CREATE TABLE COMMUNICATION_OP("
      "opName INTEGER, opType INTEGER, startNs INTEGER, endNs INTEGER, "
      "connectionId INTEGER, groupName INTEGER, opId INTEGER, "
      "deviceId INTEGER);"
      "INSERT INTO COMMUNICATION_OP VALUES "
      "(30, 31, 90, 210, 701, NULL, 55, 0);",
      nullptr, nullptr, &error);
  if (rc != SQLITE_OK) {
    std::cerr << "failed to populate tied-stream fixture: "
              << (error == nullptr ? "unknown" : error) << '\n';
    sqlite3_free(error);
    sqlite3_close(db);
    std::exit(1);
  }
  sqlite3_close(db);
}

void require_stable_primary_stream(const traceloom::NativeIr& ir) {
  using namespace traceloom;
  require(ir.communication_ops.size() == 1,
          "tied-stream fixture did not produce one communication op");
  const CommunicationOpRow& op =
      ir.communication_ops.row(CommunicationOpId(0));
  require(op.linked_task_count == 2 && op.linked_stream_count == 2,
          "tied-stream fixture did not link both equal-duration streams");
  require(ir.trace_events.row(op.trace_event_id).stream_id == 2,
          "equal-duration streams did not choose the smallest stable id");
}

}  // namespace

int main() {
  using namespace traceloom;
  const std::string path = temp_db_path();
  create_tied_stream_fixture(path);

  AscendSQLiteAdapterOptions serial_options{path, "ascend_tied_stream"};
  serial_options.thread_count = 1;
  require_stable_primary_stream(AscendSQLiteAdapter(serial_options).load());

  AscendSQLiteAdapterOptions parallel_options = serial_options;
  parallel_options.thread_count = 4;
  require_stable_primary_stream(AscendSQLiteAdapter(parallel_options).load());

  require(std::remove(path.c_str()) == 0,
          "failed to remove tied-stream fixture DB");
  return 0;
}
