#include "traceloom/adapters/ascend_sqlite_adapter.h"
#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/analysis/native_pipeline.h"

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

void create_split_golden_profiles(const std::filesystem::path& root,
                                  const std::string& monolithic_path) {
  std::filesystem::create_directories(root / "host" / "sqlite");
  std::filesystem::create_directories(root / "device_0" / "sqlite");

  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(monolithic_path.c_str(), &db,
                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(rc == SQLITE_OK, "failed to create golden monolithic DB");
  exec_sql(db,
           "CREATE TABLE STRING_IDS(id INTEGER PRIMARY KEY, value TEXT);"
           "INSERT INTO STRING_IDS VALUES "
           "(10, 'AI_CORE'), (11, 'EVENT_WAIT'), (20, 'linear'), "
           "(21, 'MatMul'), (22, 'MIX_AIC');"
           "CREATE TABLE TASK(startNs INTEGER, endNs INTEGER, "
           "deviceId INTEGER, connectionId INTEGER, globalTaskId INTEGER, "
           "globalPid INTEGER, taskType INTEGER, contextId INTEGER, "
           "streamId INTEGER, taskId INTEGER, modelId INTEGER);"
           "INSERT INTO TASK VALUES "
           "(100, 160, 0, 700, 0, 1, 10, 0, 3, 99, 2), "
           "(170, 210, 0, 701, 1, 1, 11, 0, 3, 100, 2);"
           "CREATE TABLE COMPUTE_TASK_INFO(globalTaskId INTEGER, "
           "name INTEGER, opType INTEGER, taskType INTEGER);"
           "INSERT INTO COMPUTE_TASK_INFO VALUES (0, 20, 21, 22);");
  sqlite3_close(db);

  rc = sqlite3_open_v2(
      (root / "device_0" / "sqlite" / "ascend_task.db").string().c_str(),
      &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(rc == SQLITE_OK, "failed to create split AscendTask DB");
  exec_sql(db,
           "CREATE TABLE AscendTask(model_id INTEGER, index_id INTEGER, "
           "stream_id INTEGER, task_id INTEGER, context_id INTEGER, "
           "batch_id INTEGER, start_time NUMERIC, duration NUMERIC, "
           "host_task_type TEXT, device_task_type TEXT, "
           "connection_id INTEGER);"
           "INSERT INTO AscendTask VALUES "
           "(2, -1, 3, 99, 0, 0, 100, 60, 'AI_CORE', 'AI_CORE', 700), "
           "(2, -1, 3, 100, 0, 0, 170, 40, 'EVENT_WAIT', 'UNKNOWN', 701);"
           "INSERT INTO AscendTask VALUES "
           "(2, -1, 3, 101, 0, 0, -1, -1, 'AI_CORE', 'UNKNOWN', 702);");
  sqlite3_close(db);

  rc = sqlite3_open_v2(
      (root / "host" / "sqlite" / "ge_info.db").string().c_str(), &db,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(rc == SQLITE_OK, "failed to create split TaskInfo DB");
  exec_sql(db,
           "CREATE TABLE TaskInfo(model_id INTEGER, op_name TEXT, "
           "stream_id INTEGER, task_id INTEGER, task_type TEXT, "
           "op_type TEXT, index_id INTEGER, device_id INTEGER, "
           "context_id INTEGER);"
           "INSERT INTO TaskInfo VALUES "
           "(2, 'linear', 3, 99, 'MIX_AIC', 'MatMul', -1, 0, 0);");
  sqlite3_close(db);

  rc = sqlite3_open_v2(
      (root / "host" / "sqlite" / "runtime.db").string().c_str(), &db,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(rc == SQLITE_OK, "failed to create split HostTask DB");
  exec_sql(db,
           "CREATE TABLE HostTask(connection_id INTEGER, task_type TEXT);"
           "INSERT INTO HostTask VALUES (700, 'AI_CORE'), "
           "(701, 'EVENT_WAIT');");
  sqlite3_close(db);

  rc = sqlite3_open_v2(
      (root / "host" / "sqlite" / "api_event.db").string().c_str(), &db,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(rc == SQLITE_OK, "failed to create split ApiData DB");
  exec_sql(db,
           "CREATE TABLE ApiData(start INTEGER, end INTEGER, "
           "connection_id INTEGER, id TEXT);"
           "INSERT INTO ApiData VALUES (1, 2, 10, 'aclrtSynchronizeStream');");
  sqlite3_close(db);
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
           "(120, 121, 0, 2001, 6, 1, 11, 0, 3, 6, 7), "
           "(200, 201, 0, 2002, 7, 1, 11, 0, 3, 7, 7), "
           "(220, 221, 0, 2003, 8, 1, 11, 0, 3, 8, 7);"
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
  require(graph_ir.graph_templates.row(GraphTemplateId(0)).slot_count == 2,
          "ACLGraph template should retain its capture group size");
  require(graph_ir.trace_events
                  .row(graph_ir.replay_units.row(ReplayUnitId(0))
                           .launch_trace_event_id)
                  .start_ns == 100 &&
              graph_ir.trace_events
                      .row(graph_ir.replay_units.row(ReplayUnitId(0))
                               .launch_trace_event_id)
                      .end_ns == 130,
          "ACLGraph replay window should not absorb the inter-wave gap");

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

  sqlite3* current_stream_db = nullptr;
  const std::string current_stream_db_path =
      (graph_dir / "host" / "sqlite" / "stream_info.db").string();
  const int current_stream_rc = sqlite3_open_v2(
      current_stream_db_path.c_str(), &current_stream_db,
      SQLITE_OPEN_READWRITE, nullptr);
  require(current_stream_rc == SQLITE_OK,
          "failed to reopen ACLGraph stream_info DB");
  exec_sql(current_stream_db,
           "ALTER TABLE CaptureStreamInfo RENAME COLUMN model_stream_id "
           "TO stream_id;"
           "ALTER TABLE CaptureStreamInfo ADD COLUMN batch_id INTEGER;"
           "ALTER TABLE CaptureStreamInfo ADD COLUMN capture_status INTEGER;"
           "ALTER TABLE CaptureStreamInfo ADD COLUMN timestamp NUMERIC;");
  sqlite3_close(current_stream_db);

  const AscendSQLiteAdapter current_graph_adapter(
      AscendSQLiteAdapterOptions{(graph_dir / "msprof.db").string(),
                                 "ascend_graph_current_schema_smoke"});
  const NativeIr current_graph_ir = current_graph_adapter.load();
  require(current_graph_ir.replay_units.size() == 2,
          "ACLGraph replay reconstruction missed current stream_id schema");
  require(current_graph_ir.graph_templates.size() == 1,
          "current ACLGraph stream_id schema changed graph identity");

  current_stream_db = nullptr;
  const int single_slot_stream_rc = sqlite3_open_v2(
      current_stream_db_path.c_str(), &current_stream_db,
      SQLITE_OPEN_READWRITE, nullptr);
  require(single_slot_stream_rc == SQLITE_OK,
          "failed to reopen current ACLGraph stream_info DB");
  exec_sql(current_stream_db,
           "DELETE FROM CaptureStreamInfo WHERE model_id = 8;");
  sqlite3_close(current_stream_db);
  const AscendSQLiteAdapter single_slot_graph_adapter(
      AscendSQLiteAdapterOptions{(graph_dir / "msprof.db").string(),
                                 "ascend_graph_single_slot_smoke"});
  const NativeIr single_slot_graph_ir = single_slot_graph_adapter.load();
  require(single_slot_graph_ir.replay_units.size() == 4,
          "single-slot ACLGraph launches should reconstruct one unit each");
  require(single_slot_graph_ir.graph_templates.row(GraphTemplateId(0))
              .slot_count == 1,
          "single-slot ACLGraph template should retain slot count one");
  const TraceEventRow& single_slot_second = single_slot_graph_ir.trace_events.row(
      single_slot_graph_ir.replay_units.row(ReplayUnitId(1))
          .launch_trace_event_id);
  require(single_slot_second.start_ns == 120 &&
              single_slot_second.end_ns == 130,
          "single-slot replay window should use launch/body evidence only");

  const std::filesystem::path split_dir = temp_prof_dir("_split");
  const std::string golden_path = temp_db_path("_split_golden");
  create_split_golden_profiles(split_dir, golden_path);
  require(ascend_sqlite_has_usable_task_table(golden_path),
          "golden monolithic TASK table should be usable");
  require(looks_like_ascend_split_sqlite_profile(split_dir.string()),
          "split profile discovery missed AscendTask");
  const auto split_inventory =
      inventory_ascend_split_sqlite_profile(split_dir.string());
  bool saw_split_tasks = false;
  for (const auto& table : split_inventory) {
    if (table.table_name == "AscendTask") {
      saw_split_tasks = table.row_count == 3 && !table.create_sql.empty();
    }
  }
  require(saw_split_tasks,
          "split inventory did not report AscendTask schema/row count");
  const std::string unusable_monolithic =
      (split_dir / "msprof_without_task.db").string();
  sqlite3* unusable_db = nullptr;
  int unusable_rc = sqlite3_open_v2(
      unusable_monolithic.c_str(), &unusable_db,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(unusable_rc == SQLITE_OK,
          "failed to create unusable monolithic fixture");
  exec_sql(unusable_db, "CREATE TABLE metadata(value TEXT);");
  sqlite3_close(unusable_db);
  require(!ascend_sqlite_has_usable_task_table(unusable_monolithic),
          "monolithic DB without TASK should not suppress split fallback");

  AscendSQLiteAdapter golden_adapter(golden_path, "golden_monolithic");
  AscendSQLiteAdapter split_adapter(split_dir.string(), "golden_split");
  NativeIr golden_ir = golden_adapter.load();
  NativeIr split_ir = split_adapter.load();
  require(split_ir.tasks.size() == golden_ir.tasks.size(),
          "split TASK normalization changed task count");
  require(split_ir.trace_events.size() == golden_ir.trace_events.size(),
          "split TASK normalization changed timeline row count");
  for (std::size_t index = 0; index < golden_ir.tasks.size(); ++index) {
    const TaskRow& golden_task = golden_ir.tasks.row(TaskId(index));
    const TaskRow& split_task = split_ir.tasks.row(TaskId(index));
    const TraceEventRow& golden_event =
        golden_ir.trace_events.row(golden_task.trace_event_id);
    const TraceEventRow& split_event =
        split_ir.trace_events.row(split_task.trace_event_id);
    require(split_event.device_id == golden_event.device_id &&
                split_event.stream_id == golden_event.stream_id &&
                split_event.start_ns == golden_event.start_ns &&
                split_event.end_ns == golden_event.end_ns,
            "split timeline does not match monolithic golden");
    require(split_ir.symbols.value(split_task.task_type_symbol_id) ==
                golden_ir.symbols.value(golden_task.task_type_symbol_id),
            "split task type does not match monolithic golden");
  }
  require(split_ir.symbols.value(split_ir.tasks.row(TaskId(0)).op_name_symbol_id) ==
              "linear",
          "split TaskInfo op name was not normalized");
  require(split_ir.symbols.value(split_ir.tasks.row(TaskId(0)).op_type_symbol_id) ==
              "MatMul",
          "split TaskInfo op type was not normalized");

  NativePipelineOptions golden_pipeline_options;
  golden_pipeline_options.thread_count = 1;
  const NativePipelineResult golden_pipeline =
      run_native_pipeline(golden_ir, golden_pipeline_options);
  const NativePipelineResult split_pipeline =
      run_native_pipeline(split_ir, golden_pipeline_options);
  require(split_pipeline.anchor_stats.device_event_anchors ==
              golden_pipeline.anchor_stats.device_event_anchors,
          "split and monolithic anchor summaries differ");
  require(split_pipeline.cost_summary_lite.total_duration_ns ==
              golden_pipeline.cost_summary_lite.total_duration_ns,
          "split and monolithic cost summaries differ");

  const std::filesystem::path incomplete_dir = temp_prof_dir("_incomplete");
  std::filesystem::create_directories(incomplete_dir / "host" / "sqlite");
  bool caught_split_missing = false;
  try {
    const AscendSQLiteAdapter incomplete(incomplete_dir.string());
    (void)incomplete.load();
  } catch (const std::invalid_argument& ex) {
    caught_split_missing =
        std::string(ex.what()).find("AscendTask") != std::string::npos;
  }
  require(caught_split_missing,
          "missing split AscendTask did not produce a useful diagnostic");

  bool caught_missing = false;
  try {
    const AscendSQLiteAdapter missing(temp_db_path("_missing"));
    (void)missing.load();
  } catch (const std::invalid_argument&) {
    caught_missing = true;
  }
  require(caught_missing, "missing DB path did not raise invalid_argument");

  std::remove(db_path.c_str());
  std::remove(golden_path.c_str());
  std::filesystem::remove_all(graph_dir);
  std::filesystem::remove_all(split_dir);
  std::filesystem::remove_all(incomplete_dir);
  return 0;
}
