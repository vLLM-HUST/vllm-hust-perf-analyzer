#include "traceloom/adapters/ascend_sqlite_adapter.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

std::string temp_db_path(const char* suffix) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("traceloom_ascend_sqlite_adapter_" + std::to_string(now) + suffix +
       ".db");
  return path.string();
}

void create_minimal_db(const std::string& path) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db,
                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(rc == SQLITE_OK, "failed to create temporary sqlite DB");

  char* error = nullptr;
  rc = sqlite3_exec(db,
                    "CREATE TABLE STRING_IDS(id INTEGER PRIMARY KEY, "
                    "value TEXT);"
                    "INSERT INTO STRING_IDS(id, value) VALUES "
                    "(10, 'AI_CORE'), "
                    "(11, 'EVENT_WAIT'), "
                    "(20, 'model.layers.0.mlp.gate_up_proj'), "
                    "(21, 'MatMul'), "
                    "(22, 'MIX_AIC'), "
                    "(30, 'hcclAllReduce'), "
                    "(40, 'comm_task_hccl_allreduce');"
                    "CREATE TABLE TASK("
                    "startNs INTEGER, "
                    "endNs INTEGER, "
                    "deviceId INTEGER, "
                    "connectionId INTEGER, "
                    "globalTaskId INTEGER, "
                    "globalPid INTEGER, "
                    "taskType INTEGER, "
                    "contextId INTEGER, "
                    "streamId INTEGER, "
                    "taskId INTEGER, "
                    "modelId INTEGER);"
                    "INSERT INTO TASK(startNs, endNs, deviceId, connectionId, "
                    "globalTaskId, globalPid, taskType, contextId, streamId, "
                    "taskId, modelId) VALUES "
                    "(100, 160, 0, 700, 9001, 1, 10, 0, 3, 99, 2), "
                    "(170, 210, 0, 701, 9002, 1, 11, 0, 3, 100, 2);"
                    "CREATE TABLE COMPUTE_TASK_INFO("
                    "globalTaskId INTEGER, "
                    "name INTEGER, "
                    "opType INTEGER, "
                    "taskType INTEGER);"
                    "INSERT INTO COMPUTE_TASK_INFO(globalTaskId, name, opType, "
                    "taskType) VALUES "
                    "(9001, 20, 21, 22);"
                    "CREATE TABLE COMMUNICATION_TASK_INFO("
                    "globalTaskId INTEGER, "
                    "name INTEGER);"
                    "INSERT INTO COMMUNICATION_TASK_INFO(globalTaskId, name) "
                    "VALUES "
                    "(9002, 40);"
                    "CREATE TABLE COMMUNICATION_OP("
                    "opName INTEGER, "
                    "startNs INTEGER, "
                    "endNs INTEGER, "
                    "connectionId INTEGER, "
                    "groupName INTEGER, "
                    "opId INTEGER, "
                    "deviceId INTEGER);"
                    "INSERT INTO COMMUNICATION_OP(opName, startNs, endNs, "
                    "connectionId, groupName, opId, deviceId) VALUES "
                    "(30, 90, 165, 700, NULL, 55, 0);",
                    nullptr, nullptr, &error);
  if (rc != SQLITE_OK) {
    std::cerr << "failed to create test table: "
              << (error == nullptr ? "unknown" : error) << '\n';
    sqlite3_free(error);
    sqlite3_close(db);
    std::exit(1);
  }

  sqlite3_close(db);
}

}  // namespace

int main() {
  using namespace traceloom;

  const std::string db_path = temp_db_path("_ok");
  create_minimal_db(db_path);

  const AscendSQLiteAdapter adapter(
      AscendSQLiteAdapterOptions{db_path, "ascend_smoke"});
  const NativeIr ir = adapter.load();

  require(!ir.source_refs.empty(), "adapter did not emit SourceRef rows");
  require(ir.source_refs.row(SourceRefId(0)).source_kind == "ascend_smoke",
          "SourceRef source_kind mismatch");
  require(ir.source_refs.row(SourceRefId(0)).source_path == db_path,
          "SourceRef source_path mismatch");

  bool found_test_table = false;
  for (const SourceRefRow& row : ir.source_refs.rows()) {
    if (row.table_name == "COMPUTE_TASK_INFO") {
      found_test_table = true;
    }
  }
  require(found_test_table, "adapter inventory did not include test table");
  require(ir.strings.size() == 7, "adapter did not load STRING_IDS values");
  require(ir.streams.size() == 1, "adapter did not normalize streams");
  require(ir.trace_events.size() == 3, "adapter did not load TASK/COMM events");
  require(ir.tasks.size() == 2, "adapter did not load TASK facts");
  require(ir.communication_ops.size() == 1,
          "adapter did not load COMMUNICATION_OP facts");
  require(ir.trace_events.row(TraceEventId(0)).device_id == 0,
          "first TASK event device mismatch");
  require(ir.source_refs.row(ir.trace_events.row(TraceEventId(0)).source_ref_id)
              .table_name == "TASK",
          "first TASK event source table ref mismatch");
  require(ir.trace_events.row(TraceEventId(0)).source_row_id > 0,
          "first TASK event source row id missing");
  require(ir.trace_events.row(TraceEventId(0)).stream_id == 3,
          "first TASK event stream mismatch");
  require(ir.trace_events.row(TraceEventId(0)).start_ns == 100,
          "first TASK event start mismatch");
  require(ir.trace_events.row(TraceEventId(0)).end_ns == 160,
          "first TASK event end mismatch");
  require(ir.symbols.value(ir.trace_events.row(TraceEventId(0)).raw_name_symbol_id) ==
              "AI_CORE",
          "first TASK event task type decode mismatch");
  require(ir.tasks.row(TaskId(0)).raw_task_id == 99,
          "first TASK raw task id mismatch");
  require(ir.tasks.row(TaskId(0)).raw_global_task_id == 9001,
          "first TASK global task id mismatch");
  require(ir.tasks.row(TaskId(0)).raw_connection_id == 700,
          "first TASK connection id mismatch");
  require(ir.symbols.value(ir.tasks.row(TaskId(0)).op_name_symbol_id) ==
              "model.layers.0.mlp.gate_up_proj",
          "first TASK op name decode mismatch");
  require(ir.symbols.value(ir.tasks.row(TaskId(0)).op_type_symbol_id) ==
              "MatMul",
          "first TASK op type decode mismatch");
  require(ir.symbols.value(ir.tasks.row(TaskId(0)).compute_task_type_symbol_id) ==
              "MIX_AIC",
          "first TASK compute task type decode mismatch");
  require(!ir.tasks.row(TaskId(1)).op_name_symbol_id.valid(),
          "missing COMPUTE_TASK_INFO should leave op name invalid");
  require(ir.symbols.value(ir.tasks.row(TaskId(1)).comm_name_symbol_id) ==
              "comm_task_hccl_allreduce",
          "COMMUNICATION_TASK_INFO name decode mismatch");
  require(ir.source_refs
              .row(ir.communication_ops.row(CommunicationOpId(0)).source_ref_id)
              .table_name == "COMMUNICATION_OP",
          "COMMUNICATION_OP source table ref mismatch");
  require(ir.communication_ops.row(CommunicationOpId(0)).trace_event_id ==
              TraceEventId(2),
          "COMMUNICATION_OP trace event id mismatch");
  require(ir.communication_ops.row(CommunicationOpId(0)).raw_connection_id ==
              700,
          "COMMUNICATION_OP connection id mismatch");
  require(ir.communication_ops.row(CommunicationOpId(0)).raw_op_id == 55,
          "COMMUNICATION_OP op id mismatch");
  require(ir.communication_ops.row(CommunicationOpId(0)).linked_task_count ==
              1,
          "COMMUNICATION_OP linked task count mismatch");
  require(ir.communication_ops.row(CommunicationOpId(0)).linked_stream_count ==
              1,
          "COMMUNICATION_OP linked stream count mismatch");
  require(ir.symbols.value(
              ir.communication_ops.row(CommunicationOpId(0)).op_name_symbol_id) ==
              "hcclAllReduce",
          "COMMUNICATION_OP op name decode mismatch");

  bool caught_missing = false;
  try {
    const AscendSQLiteAdapter missing(temp_db_path("_missing"));
    (void)missing.load();
  } catch (const std::invalid_argument&) {
    caught_missing = true;
  }
  require(caught_missing, "missing DB path did not raise invalid_argument");

  std::remove(db_path.c_str());
  return 0;
}
