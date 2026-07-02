#include "traceloom/adapters/ascend_sqlite_adapter.h"
#include "traceloom/analysis/flat_anchor_builder.h"

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
                    "(31, 'hcom_allReduce_'), "
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
                    "name INTEGER, "
                    "taskType INTEGER);"
                    "INSERT INTO COMMUNICATION_TASK_INFO(globalTaskId, name, "
                    "taskType) "
                    "VALUES "
                    "(9002, 40, 31);"
                    "CREATE TABLE COMMUNICATION_OP("
                    "opName INTEGER, "
                    "opType INTEGER, "
                    "startNs INTEGER, "
                    "endNs INTEGER, "
                    "connectionId INTEGER, "
                    "groupName INTEGER, "
                    "opId INTEGER, "
                    "deviceId INTEGER);"
                    "INSERT INTO COMMUNICATION_OP(opName, opType, startNs, "
                    "endNs, connectionId, groupName, opId, deviceId) VALUES "
                    "(30, 31, 165, 215, 701, NULL, 55, 0);",
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

std::filesystem::path temp_prof_dir(const char* suffix) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("traceloom_ascend_sqlite_adapter_" + std::to_string(now) + suffix);
}

void exec_sql(sqlite3* db, const char* sql) {
  char* error = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
  if (rc != SQLITE_OK) {
    std::cerr << "failed to execute SQL: "
              << (error == nullptr ? "unknown" : error) << '\n';
    sqlite3_free(error);
    sqlite3_close(db);
    std::exit(1);
  }
}

void create_aclgraph_profile(const std::filesystem::path& dir) {
  std::filesystem::create_directories(dir / "host" / "sqlite");

  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2((dir / "msprof.db").string().c_str(), &db,
                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(rc == SQLITE_OK, "failed to create aclgraph msprof DB");
  exec_sql(db,
           "CREATE TABLE STRING_IDS(id INTEGER PRIMARY KEY, value TEXT);"
           "INSERT INTO STRING_IDS(id, value) VALUES "
           "(10, 'AI_CORE'), "
           "(11, 'MODEL_EXECUTE'), "
           "(12, 'NOTIFY_WAIT'), "
           "(13, 'MIX_AIC'), "
           "(14, 'aclmdlRIExecuteAsync'), "
           "(15, 'aclmdlRICaptureBegin'), "
           "(16, 'aclmdlRICaptureEnd'), "
           "(17, 'aclnnGather'), "
           "(18, 'aclnnMm'), "
           "(20, 'GatherV2'), "
           "(21, 'MatMulV2'), "
           "(30, 'hcclAllGather'), "
           "(31, 'hcom_allGather_');"
           "CREATE TABLE TASK("
           "startNs INTEGER, endNs INTEGER, deviceId INTEGER, "
           "connectionId INTEGER, globalTaskId INTEGER, globalPid INTEGER, "
           "taskType INTEGER, contextId INTEGER, streamId INTEGER, "
           "taskId INTEGER, modelId INTEGER);"
           "INSERT INTO TASK(startNs, endNs, deviceId, connectionId, "
           "globalTaskId, globalPid, taskType, contextId, streamId, taskId, "
           "modelId) VALUES "
           "(100, 110, 0, 1000, 1, 1, 13, 0, 36, 1, 7), "
           "(120, 130, 0, 1001, 2, 1, 13, 0, 36, 2, 7), "
           "(200, 210, 0, 1002, 3, 1, 13, 0, 36, 3, 7), "
           "(220, 230, 0, 1003, 4, 1, 13, 0, 36, 4, 7), "
           "(100, 101, 0, 2000, 5, 1, 11, 0, 3, 5, 7), "
           "(200, 201, 0, 2001, 6, 1, 11, 0, 3, 6, 7);"
           "CREATE TABLE COMPUTE_TASK_INFO("
           "globalTaskId INTEGER, name INTEGER, opType INTEGER, "
           "taskType INTEGER);"
           "INSERT INTO COMPUTE_TASK_INFO(globalTaskId, name, opType, "
           "taskType) VALUES "
           "(1, 20, 20, 13), "
           "(2, 21, 21, 13), "
           "(3, 21, 21, 13), "
           "(4, 21, 21, 13);"
           "CREATE TABLE COMMUNICATION_OP("
           "opName INTEGER, "
           "opType INTEGER, "
           "startNs INTEGER, "
           "endNs INTEGER, "
           "connectionId INTEGER, "
           "groupName INTEGER, "
           "opId INTEGER, "
           "deviceId INTEGER);"
           "INSERT INTO COMMUNICATION_OP(opName, opType, startNs, endNs, "
           "connectionId, groupName, opId, deviceId) VALUES "
           "(30, 31, 105, 109, 3000, NULL, 1, 0), "
           "(30, 31, 300, 320, 3001, NULL, 2, 0);"
           "CREATE TABLE CANN_API("
           "startNs INTEGER, endNs INTEGER, type INTEGER, globalTid INTEGER, "
           "connectionId INTEGER, name INTEGER);"
           "INSERT INTO CANN_API(startNs, endNs, type, globalTid, "
           "connectionId, name) VALUES "
           "(10, 11, 0, 1, 9000, 15), "
           "(12, 13, 0, 1, 9001, 17), "
           "(14, 15, 0, 1, 9002, 16), "
           "(20, 21, 0, 1, 9003, 15), "
           "(22, 23, 0, 1, 9004, 18), "
           "(24, 25, 0, 1, 9005, 16), "
           "(90, 95, 0, 1, 2000, 14), "
           "(115, 118, 0, 1, 2001, 14), "
           "(190, 195, 0, 1, 2002, 14), "
           "(215, 218, 0, 1, 2003, 14);");
  sqlite3_close(db);

  sqlite3* stream_db = nullptr;
  rc = sqlite3_open_v2((dir / "host" / "sqlite" / "stream_info.db")
                           .string()
                           .c_str(),
                       &stream_db,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(rc == SQLITE_OK, "failed to create stream_info DB");
  exec_sql(stream_db,
           "CREATE TABLE CaptureStreamInfo("
           "device_id INTEGER, model_id INTEGER, original_stream_id INTEGER, "
           "model_stream_id INTEGER);"
           "INSERT INTO CaptureStreamInfo(device_id, model_id, "
           "original_stream_id, model_stream_id) VALUES "
           "(0, 7, 3, 36), "
           "(0, 8, 3, 37);");
  sqlite3_close(stream_db);
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
  require(ir.strings.size() == 8, "adapter did not load STRING_IDS values");
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
              701,
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
  require(ir.symbols.value(
              ir.communication_ops.row(CommunicationOpId(0)).op_type_symbol_id) ==
              "hcom_allReduce_",
          "COMMUNICATION_OP op type decode mismatch");
  require(ir.symbols.value(ir.communication_ops.row(CommunicationOpId(0))
                               .linked_task_name_symbol_id) ==
              "comm_task_hccl_allreduce",
          "COMMUNICATION_OP linked task name mismatch");
  require(ir.symbols.value(ir.communication_ops.row(CommunicationOpId(0))
                               .linked_task_type_symbol_id) ==
              "hcom_allReduce_",
          "COMMUNICATION_OP linked task type mismatch");

  const std::filesystem::path graph_dir = temp_prof_dir("_graph");
  create_aclgraph_profile(graph_dir);
  const AscendSQLiteAdapter graph_adapter(
      AscendSQLiteAdapterOptions{(graph_dir / "msprof.db").string(),
                                 "ascend_graph_smoke"});
  NativeIr graph_ir = graph_adapter.load();
  require(graph_ir.replay_units.size() == 2,
          "ACLGraph replay units were not reconstructed");
  require(graph_ir.graph_templates.size() == 1,
          "ACLGraph equivalent units should share one template");

  FlatAnchorBuildConfig anchor_config;
  anchor_config.filter_auxiliary_task_anchors = true;
  anchor_config.skip_events_covered_by_replay_units = true;
  const FlatAnchorBuildStats graph_stats =
      build_flat_anchors(graph_ir, anchor_config);
  require(graph_stats.device_event_anchors == 2,
          "ACLGraph replay units should become device anchors");
  require(graph_stats.communication_anchors == 1,
          "only communication outside graph replay units should remain");
  require(graph_ir.tokens.size() == 3,
          "covered graph events should be replaced by replay-unit tokens");
  require(graph_ir.anchors.row(AnchorId(0)).kind == AnchorKind::kGraphReplayUnit,
          "first graph anchor kind mismatch");
  require(graph_ir.anchors.row(AnchorId(1)).kind == AnchorKind::kGraphReplayUnit,
          "second graph anchor kind mismatch");
  require(graph_ir.anchors.row(AnchorId(2)).kind == AnchorKind::kCommunication,
          "outside communication anchor should remain");
  require(graph_ir.protected_intervals.empty(),
          "single-token GraphReplayUnit anchors should remain grammar-compressible");

  bool caught_missing = false;
  try {
    const AscendSQLiteAdapter missing(temp_db_path("_missing"));
    (void)missing.load();
  } catch (const std::invalid_argument&) {
    caught_missing = true;
  }
  require(caught_missing, "missing DB path did not raise invalid_argument");

  std::remove(db_path.c_str());
  std::filesystem::remove_all(graph_dir);
  return 0;
}
