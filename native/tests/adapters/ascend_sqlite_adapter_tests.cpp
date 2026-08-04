#include "traceloom/adapters/ascend_sqlite_adapter.h"
#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/analysis/idle_evidence_semantic_rules.h"
#include "traceloom/analysis/native_pipeline.h"
#include "traceloom/analysis/productive_timeline.h"
#include "traceloom/analysis/semantic_task_classifier.h"
#include "traceloom/analysis/stream_state_timeline.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

void create_aclgraph_launch_identity_profile(const std::string& path) {
  sqlite3* db = nullptr;
  const int rc = sqlite3_open_v2(
      path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(rc == SQLITE_OK, "failed to create graph launch identity DB");
  exec_sql(db,
           "CREATE TABLE STRING_IDS(id INTEGER PRIMARY KEY, value TEXT);"
           "INSERT INTO STRING_IDS VALUES "
           "(10, 'MODEL_EXECUTE'), (11, 'NOTIFY_WAIT'), "
           "(12, 'NOTIFY_RECORD'), (20, 'aclmdlRIExecuteAsync'), "
           "(21, 'aclmdlRICaptureBegin'), (22, 'aclmdlRICaptureEnd'), "
           "(23, 'aclnnMuls'), (24, 'aclnnAdds'), (25, 'aclnnSubs'), "
           "(26, 'aclrtSynchronizeStreamWithTimeout'), "
           "(30, 'KERNEL_AIVEC'), (31, 'Add'), (32, 'Sub'), "
           "(33, 'hcom_allReduce__fixture'), (34, 'Reduce_Inline');"
           "CREATE TABLE TASK(startNs INTEGER, endNs INTEGER, "
           "deviceId INTEGER, connectionId INTEGER, globalTaskId INTEGER, "
           "globalPid INTEGER, taskType INTEGER, contextId INTEGER, "
           "streamId INTEGER, taskId INTEGER, modelId INTEGER);"
           "INSERT INTO TASK VALUES "
           "(100, 110, 0, 100, 1, 1, 10, 0, 3, 1, 7), "
           "(115, 120, 0, 100, 2, 1, 11, 0, 3, 2, 7), "
           "(118, 120, 0, 9001, 3, 1, 12, 0, 36, 3, 7), "
           "(200, 210, 0, 101, 4, 1, 10, 0, 3, 4, 8), "
           "(215, 220, 0, 101, 5, 1, 11, 0, 3, 5, 8), "
           "(218, 220, 0, 9002, 6, 1, 12, 0, 37, 6, 8), "
           "(300, 310, 0, 102, 7, 1, 10, 0, 3, 7, 7), "
           "(315, 320, 0, 102, 8, 1, 11, 0, 3, 8, 7), "
           "(699990, 700000, 0, 9001, 9, 1, 12, 0, 36, 9, 7), "
           "(800000, 800010, 0, 103, 10, 1, 10, 0, 3, 10, 7), "
           "(116, 117, 0, 300, 100, 1, 30, 0, 36, 11, 7), "
           "(216, 217, 0, 301, 101, 1, 30, 0, 37, 12, 8), "
           "(316, 317, 0, 302, 100, 1, 30, 0, 36, 13, 7), "
           "(116, 117, 0, 303, 102, 1, 30, 0, 38, 14, 7), "
           "(316, 317, 0, 304, 102, 1, 30, 0, 38, 15, 7);"
           "CREATE TABLE COMPUTE_TASK_INFO(globalTaskId INTEGER, "
           "name INTEGER, opType INTEGER, taskType INTEGER);"
           "INSERT INTO COMPUTE_TASK_INFO VALUES "
           "(100, 31, 31, 30), (101, 32, 32, 30);"
           "CREATE TABLE COMMUNICATION_TASK_INFO(globalTaskId INTEGER, "
           "name INTEGER, taskType INTEGER);"
           "INSERT INTO COMMUNICATION_TASK_INFO VALUES (102, 33, 34);"
           "CREATE TABLE CANN_API(startNs INTEGER, endNs INTEGER, "
           "type INTEGER, globalTid INTEGER, connectionId INTEGER, "
           "name INTEGER);"
           "INSERT INTO CANN_API VALUES "
           "(10, 11, 0, 1, 10, 21), "
           "(12, 13, 0, 1, 11, 23), "
           "(14, 15, 0, 1, 12, 24), "
           "(16, 17, 0, 1, 13, 22), "
           "(20, 21, 0, 1, 20, 21), "
           "(22, 23, 0, 1, 21, 23), "
           "(24, 25, 0, 1, 22, 25), "
           "(26, 27, 0, 1, 23, 22), "
           "(90, 95, 0, 1, 100, 20), "
           "(190, 195, 0, 1, 101, 20), "
           "(290, 295, 0, 1, 102, 20), "
           "(799990, 799995, 0, 1, 103, 20), "
           "(250, 260, 0, 1, 200, 26), "
           "(800020, 800030, 0, 1, 201, 26);");
  sqlite3_close(db);

  const std::filesystem::path stream_info_path =
      std::filesystem::path(path).parent_path() / "host" / "sqlite" /
      "stream_info.db";
  std::filesystem::create_directories(stream_info_path.parent_path());
  db = nullptr;
  const int stream_rc = sqlite3_open_v2(
      stream_info_path.string().c_str(), &db,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(stream_rc == SQLITE_OK,
          "failed to create graph launch stream_info DB");
  exec_sql(db,
           "CREATE TABLE CaptureStreamInfo(device_id INTEGER, "
           "model_id INTEGER, original_stream_id INTEGER, stream_id INTEGER, "
           "timestamp NUMERIC);"
           "INSERT INTO CaptureStreamInfo VALUES "
           "(0, 7, 3, 36, 100), (0, 7, 4, 38, 100), "
           "(0, 8, 3, 37, 200);");
  sqlite3_close(db);
}

void create_split_aclgraph_profile_from_monolithic(
    const std::filesystem::path& root,
    const std::string& monolithic_path,
    const std::string& stream_info_path) {
  std::filesystem::create_directories(root / "host" / "sqlite");
  std::filesystem::create_directories(root / "device_0" / "sqlite");
  const std::string attach_monolithic =
      "ATTACH DATABASE '" + monolithic_path + "' AS source;";

  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(
      (root / "device_0" / "sqlite" / "ascend_task.db").string().c_str(),
      &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(rc == SQLITE_OK, "failed to create split graph AscendTask DB");
  exec_sql(db, attach_monolithic.c_str());
  exec_sql(
      db,
      "CREATE TABLE AscendTask AS "
      "SELECT modelId AS model_id, -1 AS index_id, streamId AS stream_id, "
      "taskId AS task_id, contextId AS context_id, 0 AS batch_id, "
      "startNs AS start_time, endNs - startNs AS duration, "
      "(SELECT value FROM source.STRING_IDS WHERE id = t.taskType) AS "
      "host_task_type, "
      "(SELECT value FROM source.STRING_IDS WHERE id = t.taskType) AS "
      "device_task_type, connectionId AS connection_id "
      "FROM source.TASK t");
  sqlite3_close(db);

  db = nullptr;
  rc = sqlite3_open_v2(
      (root / "host" / "sqlite" / "ge_info.db").string().c_str(), &db,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(rc == SQLITE_OK, "failed to create split graph TaskInfo DB");
  exec_sql(db, attach_monolithic.c_str());
  exec_sql(
      db,
      "CREATE TABLE TaskInfo AS "
      "SELECT t.modelId AS model_id, names.value AS op_name, "
      "t.streamId AS stream_id, t.taskId AS task_id, types.value AS "
      "task_type, ops.value AS op_type, -1 AS index_id, "
      "t.deviceId AS device_id, t.contextId AS context_id "
      "FROM source.TASK t "
      "JOIN source.COMPUTE_TASK_INFO c ON c.globalTaskId = t.globalTaskId "
      "LEFT JOIN source.STRING_IDS names ON names.id = c.name "
      "LEFT JOIN source.STRING_IDS types ON types.id = c.taskType "
      "LEFT JOIN source.STRING_IDS ops ON ops.id = c.opType");
  sqlite3_close(db);

  db = nullptr;
  rc = sqlite3_open_v2(
      (root / "host" / "sqlite" / "api_event.db").string().c_str(), &db,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(rc == SQLITE_OK, "failed to create split graph ApiData DB");
  exec_sql(db, attach_monolithic.c_str());
  exec_sql(
      db,
      "CREATE TABLE ApiData AS "
      "SELECT 'api' AS struct_type, names.value AS id, 'node' AS level, "
      "globalTid AS thread_id, CAST(api.rowid AS TEXT) AS item_id, "
      "startNs AS start, endNs AS end, connectionId AS connection_id "
      "FROM source.CANN_API api "
      "LEFT JOIN source.STRING_IDS names ON names.id = api.name");
  sqlite3_close(db);

  db = nullptr;
  rc = sqlite3_open_v2(
      (root / "host" / "sqlite" / "stream_info.db").string().c_str(), &db,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(rc == SQLITE_OK,
          "failed to create split graph CaptureStreamInfo DB");
  const std::string attach_stream_info =
      "ATTACH DATABASE '" + stream_info_path + "' AS source;";
  exec_sql(db, attach_stream_info.c_str());
  exec_sql(db,
           "CREATE TABLE CaptureStreamInfo AS "
           "SELECT device_id, model_id, original_stream_id, "
           "stream_id, 0 AS batch_id, 0 AS capture_status, timestamp "
           "FROM source.CaptureStreamInfo");
  sqlite3_close(db);

  db = nullptr;
  rc = sqlite3_open_v2(
      (root / "device_0" / "sqlite" / "hccl_single_device.db")
          .string()
          .c_str(),
      &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(rc == SQLITE_OK,
          "failed to create split graph HCCLTaskSingleDevice DB");
  exec_sql(db, attach_monolithic.c_str());
  exec_sql(
      db,
      "CREATE TABLE HCCLTaskSingleDevice AS "
      "SELECT t.streamId AS stream_id, t.taskId AS task_id, "
      "t.contextId AS context_id, names.value AS op_name, "
      "types.value AS hccl_name "
      "FROM source.TASK t "
      "JOIN source.COMMUNICATION_TASK_INFO c "
      "ON c.globalTaskId = t.globalTaskId "
      "LEFT JOIN source.STRING_IDS names ON names.id = c.name "
      "LEFT JOIN source.STRING_IDS types ON types.id = c.taskType");
  sqlite3_close(db);
}

void create_aclgraph_body_mismatch_profile(const std::string& path) {
  sqlite3* db = nullptr;
  const int rc = sqlite3_open_v2(
      path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(rc == SQLITE_OK, "failed to create graph body mismatch DB");
  exec_sql(db,
           "CREATE TABLE STRING_IDS(id INTEGER PRIMARY KEY, value TEXT);"
           "INSERT INTO STRING_IDS VALUES "
           "(10, 'MODEL_EXECUTE'), (11, 'NOTIFY_WAIT'), "
           "(12, 'NOTIFY_RECORD'), (20, 'aclmdlRIExecuteAsync'), "
           "(30, 'KERNEL_AIVEC'), (31, 'Add'), (32, 'Sub');"
           "CREATE TABLE TASK(startNs INTEGER, endNs INTEGER, "
           "deviceId INTEGER, connectionId INTEGER, globalTaskId INTEGER, "
           "globalPid INTEGER, taskType INTEGER, contextId INTEGER, "
           "streamId INTEGER, taskId INTEGER, modelId INTEGER);"
           "CREATE TABLE COMPUTE_TASK_INFO(globalTaskId INTEGER, "
           "name INTEGER, opType INTEGER, taskType INTEGER);"
           "INSERT INTO COMPUTE_TASK_INFO VALUES "
           "(100, 31, 31, 30), (101, 32, 32, 30);"
           "CREATE TABLE CANN_API(startNs INTEGER, endNs INTEGER, "
           "type INTEGER, globalTid INTEGER, connectionId INTEGER, "
           "name INTEGER);");
  for (std::int64_t index = 0; index < 7; ++index) {
    const std::int64_t base = 100 + index * 100;
    const std::int64_t connection = 1000 + index;
    const std::int64_t first_task_id = index * 4 + 1;
    const std::int64_t compute_global_task_id = index == 3 ? 101 : 100;
    const std::string task_sql =
        "INSERT INTO TASK VALUES "
        "(" + std::to_string(base) + ", " + std::to_string(base + 10) +
        ", 0, " + std::to_string(connection) + ", " +
        std::to_string(200 + index) + ", 1, 10, 0, 3, " +
        std::to_string(first_task_id) + ", 7), "
        "(" + std::to_string(base + 15) + ", " +
        std::to_string(base + 20) + ", 0, " +
        std::to_string(connection) + ", " + std::to_string(300 + index) +
        ", 1, 11, 0, 3, " + std::to_string(first_task_id + 1) +
        ", 7), "
        "(" + std::to_string(base + 16) + ", " +
        std::to_string(base + 17) + ", 0, " +
        std::to_string(400 + index) + ", " +
        std::to_string(compute_global_task_id) + ", 1, 30, 0, 36, " +
        std::to_string(first_task_id + 2) + ", 7), "
        "(" + std::to_string(base + 18) + ", " +
        std::to_string(base + 20) + ", 0, 9001, " +
        std::to_string(500 + index) + ", 1, 12, 0, 36, " +
        std::to_string(first_task_id + 3) + ", 7);";
    exec_sql(db, task_sql.c_str());
    const std::string api_sql =
        "INSERT INTO CANN_API VALUES (" + std::to_string(base - 10) +
        ", " + std::to_string(base - 5) + ", 0, 1, " +
        std::to_string(connection) + ", 20);";
    exec_sql(db, api_sql.c_str());
  }
  sqlite3_close(db);

  const std::filesystem::path stream_info_path =
      std::filesystem::path(path).parent_path() / "host" / "sqlite" /
      "stream_info.db";
  std::filesystem::create_directories(stream_info_path.parent_path());
  db = nullptr;
  const int stream_rc = sqlite3_open_v2(
      stream_info_path.string().c_str(), &db,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(stream_rc == SQLITE_OK,
          "failed to create graph body mismatch stream_info DB");
  exec_sql(db,
           "CREATE TABLE CaptureStreamInfo(device_id INTEGER, "
           "model_id INTEGER, original_stream_id INTEGER, stream_id INTEGER, "
           "timestamp NUMERIC);"
           "INSERT INTO CaptureStreamInfo VALUES (0, 7, 3, 36, 1);");
  sqlite3_close(db);
}

void create_aclgraph_exact_hlt_profile(
    const std::string& path,
    std::int64_t missing_body_launch_index = -1) {
  sqlite3* db = nullptr;
  const int rc = sqlite3_open_v2(
      path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(rc == SQLITE_OK, "failed to create exact HLT graph DB");
  exec_sql(db,
           "CREATE TABLE STRING_IDS(id INTEGER PRIMARY KEY, value TEXT);"
           "INSERT INTO STRING_IDS VALUES "
           "(10, 'MODEL_EXECUTE'), (11, 'NOTIFY_WAIT'), "
           "(12, 'NOTIFY_RECORD'), (20, 'aclmdlRIExecuteAsync'), "
           "(21, 'aclrtSynchronizeStreamWithTimeout'), "
           "(30, 'KERNEL_AIVEC'), (31, 'HeadOp'), (32, 'LayerOp'), "
           "(33, 'TailOp'), (34, 'PrefillHeadOp'), "
           "(35, 'PrefillLayerOp'), (36, 'PrefillTailOp'), "
           "(37, 'AuxGraphOp');"
           "CREATE TABLE TASK(startNs INTEGER, endNs INTEGER, "
           "deviceId INTEGER, connectionId INTEGER, globalTaskId INTEGER, "
           "globalPid INTEGER, taskType INTEGER, contextId INTEGER, "
           "streamId INTEGER, taskId INTEGER, modelId INTEGER);"
           "CREATE TABLE COMPUTE_TASK_INFO(globalTaskId INTEGER, "
           "name INTEGER, opType INTEGER, taskType INTEGER);"
           "INSERT INTO COMPUTE_TASK_INFO VALUES "
           "(100, 31, 31, 30), (101, 32, 32, 30), "
           "(102, 33, 33, 30), (103, 34, 34, 30), "
           "(104, 35, 35, 30), (105, 36, 36, 30), "
           "(106, 37, 37, 30);"
           "CREATE TABLE COMMUNICATION_TASK_INFO(globalTaskId INTEGER, "
           "name INTEGER, taskType INTEGER);"
           "CREATE TABLE CANN_API(startNs INTEGER, endNs INTEGER, "
           "type INTEGER, globalTid INTEGER, connectionId INTEGER, "
           "name INTEGER);");

  // One exact one-shot prefill H/L/T, three periodic decode H/L/T
  // compositions, then a valid decode H/L prefix.  The final prefix must
  // remain explicit unrecognized evidence rather than becoming a partial
  // replay unit.
  for (std::int64_t index = 0; index < 14; ++index) {
    const std::int64_t base = 100 + index * 100;
    const std::int64_t launch_connection = 1000 + index;
    const bool is_prefill = index < 3;
    const std::int64_t slot = is_prefill ? index : (index - 3) % 3;
    const std::int64_t graph_connection =
        (is_prefill ? 9101 : 9001) + slot;
    const bool missing_body = index == missing_body_launch_index;
    const std::int64_t body_global_task_id =
        missing_body ? 107 : (is_prefill ? 103 : 100) + slot;
    const std::int64_t aux_global_task_id = missing_body ? 108 : 106;
    const std::int64_t model_id = (is_prefill ? 10 : 7) + slot;
    const std::int64_t main_stream = 36 + (model_id - 7) * 2;
    const std::int64_t aux_stream = main_stream + 1;
    const std::int64_t first_task_id = index * 5 + 1;
    const std::string task_sql =
        "INSERT INTO TASK VALUES "
        "(" + std::to_string(base) + ", " + std::to_string(base + 10) +
        ", 0, " + std::to_string(launch_connection) + ", " +
        std::to_string(200 + index) + ", 1, 10, 0, 3, " +
        std::to_string(first_task_id) + ", " +
        std::to_string(model_id) + "), "
        "(" + std::to_string(base + 15) + ", " +
        std::to_string(base + 20) + ", 0, " +
        std::to_string(launch_connection) + ", " +
        std::to_string(300 + index) + ", 1, 11, 0, 3, " +
        std::to_string(first_task_id + 1) + ", " +
        std::to_string(model_id) + "), "
        "(" + std::to_string(base + 16) + ", " +
        std::to_string(base + 17) + ", 0, " +
        std::to_string(400 + index) + ", " +
        std::to_string(body_global_task_id) + ", 1, 30, 0, " +
        std::to_string(main_stream) + ", " +
        std::to_string(first_task_id + 2) + ", " +
        std::to_string(model_id) + "), "
        "(" + std::to_string(base + 16) + ", " +
        std::to_string(base + 17) + ", 0, " +
        std::to_string(450 + index) + ", " +
        std::to_string(aux_global_task_id) + ", 1, 30, 0, " +
        std::to_string(aux_stream) + ", " +
        std::to_string(first_task_id + 3) + ", " +
        std::to_string(model_id) + "), "
        "(" + std::to_string(base + 18) + ", " +
        std::to_string(base + 20) + ", 0, " +
        std::to_string(graph_connection) + ", " +
        std::to_string(500 + index) + ", 1, 12, 0, " +
        std::to_string(main_stream) + ", " +
        std::to_string(first_task_id + 4) + ", " +
        std::to_string(model_id) + ");";
    exec_sql(db, task_sql.c_str());
    const std::string api_sql =
        "INSERT INTO CANN_API VALUES (" + std::to_string(base - 10) +
        ", " + std::to_string(base - 5) + ", 0, 1, " +
        std::to_string(launch_connection) + ", 20);";
    exec_sql(db, api_sql.c_str());
  }
  exec_sql(db,
           "INSERT INTO CANN_API VALUES "
           "(1490, 1500, 0, 1, 9999, 21);");
  sqlite3_close(db);

  const std::filesystem::path stream_info_path =
      std::filesystem::path(path).parent_path() / "host" / "sqlite" /
      "stream_info.db";
  std::filesystem::create_directories(stream_info_path.parent_path());
  db = nullptr;
  const int stream_rc = sqlite3_open_v2(
      stream_info_path.string().c_str(), &db,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(stream_rc == SQLITE_OK,
          "failed to create exact HLT stream_info DB");
  exec_sql(db,
           "CREATE TABLE CaptureStreamInfo(device_id INTEGER, "
           "model_id INTEGER, original_stream_id INTEGER, stream_id INTEGER, "
           "timestamp NUMERIC);"
           "INSERT INTO CaptureStreamInfo VALUES "
           "(0, 7, 3, 36, 1), (0, 7, 4, 37, 1), "
           "(0, 8, 3, 38, 2), (0, 8, 4, 39, 2), "
           "(0, 9, 3, 40, 3), (0, 9, 4, 41, 3), "
           "(0, 10, 3, 42, 4), (0, 10, 4, 43, 4), "
           "(0, 11, 3, 44, 5), (0, 11, 4, 45, 5), "
           "(0, 12, 3, 46, 6), (0, 12, 4, 47, 6);");
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
  require(ir.tasks.row(TaskId(0)).raw_model_id == 2,
          "first TASK model id mismatch");
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

  const std::filesystem::path launch_identity_dir =
      temp_prof_dir("_launch_identity");
  std::filesystem::create_directories(launch_identity_dir);
  const std::string launch_identity_path =
      (launch_identity_dir / "msprof.db").string();
  create_aclgraph_launch_identity_profile(launch_identity_path);
  const NativeIr launch_identity_ir =
      AscendSQLiteAdapter(launch_identity_path, "graph_launch_identity").load();
  require(launch_identity_ir.graph_launch_occurrences.size() == 4,
          "graph launch occurrence count mismatch");
  const GraphLaunchOccurrenceRow& launch0 =
      launch_identity_ir.graph_launch_occurrences.row(
          GraphLaunchOccurrenceId(0));
  const GraphLaunchOccurrenceRow& launch1 =
      launch_identity_ir.graph_launch_occurrences.row(
          GraphLaunchOccurrenceId(1));
  const GraphLaunchOccurrenceRow& launch2 =
      launch_identity_ir.graph_launch_occurrences.row(
          GraphLaunchOccurrenceId(2));
  const GraphLaunchOccurrenceRow& launch3 =
      launch_identity_ir.graph_launch_occurrences.row(
          GraphLaunchOccurrenceId(3));
  require(launch0.match_policy ==
                  GraphLaunchMatchPolicy::kNotifyCompletionAdjacent &&
              launch1.match_policy ==
                  GraphLaunchMatchPolicy::kNotifyCompletionAdjacent,
          "completion-adjacent launch match policy mismatch");
  require(launch2.match_policy ==
              GraphLaunchMatchPolicy::kNotifyOrderedFallback,
          "ordered fallback launch match policy mismatch");
  require(launch3.match_policy == GraphLaunchMatchPolicy::kUnmatched,
          "incomplete launch should remain unmatched");
  require(launch0.raw_launch_connection_id == 100 &&
              launch0.raw_graph_connection_id == 9001 &&
              launch0.raw_model_id == 7,
          "first graph launch identity mismatch");
  require(launch1.raw_graph_connection_id == 9002 &&
              launch1.raw_model_id == 8,
          "second graph launch identity mismatch");
  require(launch2.raw_graph_connection_id == 9001 &&
              launch2.raw_model_id == 7,
          "fallback graph launch identity mismatch");
  require(launch3.raw_graph_connection_id == -1 &&
              launch3.raw_model_id == -1 &&
              !launch3.notify_wait_task_id.valid() &&
              !launch3.notify_record_task_id.valid(),
          "unmatched graph launch should not invent identity evidence");
  require(launch0.raw_host_api_row_id == 9 &&
              launch1.raw_host_api_row_id == 10 &&
              launch2.raw_host_api_row_id == 11 &&
              launch3.raw_host_api_row_id == 12,
          "host graph execute provenance mismatch");
  require(launch_identity_ir.source_refs
                  .row(launch0.host_api_source_ref_id)
                  .table_name == "CANN_API",
          "host graph execute source table mismatch");
  require(launch0.wait_record_end_delta_ns == 0 &&
              launch2.wait_record_end_delta_ns == -699680,
          "wait-record completion delta mismatch");
  require(launch0.execute_stream_id.valid() &&
              launch0.model_stream_id.valid() &&
              launch0.execute_stream_id != launch0.model_stream_id,
          "graph launch stream identities were not normalized");
  require(launch_identity_ir.captured_graph_instances.size() == 2 &&
              launch_identity_ir.captured_graph_streams.size() == 3,
          "capture model groups were not preserved");
  require(launch_identity_ir.graph_slot_templates.size() == 2,
          "distinct capture signatures should form two slot templates");
  require(launch_identity_ir.replay_body_templates.size() == 2,
          "distinct replay compute bodies should form two body templates");
  require(launch_identity_ir.graph_launch_bodies.size() == 3,
          "matched launches with compute work should materialize bodies");
  const GraphLaunchBodyRow& body0 =
      launch_identity_ir.graph_launch_bodies.row(GraphLaunchBodyId(0));
  const GraphLaunchBodyRow& body1 =
      launch_identity_ir.graph_launch_bodies.row(GraphLaunchBodyId(1));
  const GraphLaunchBodyRow& body2 =
      launch_identity_ir.graph_launch_bodies.row(GraphLaunchBodyId(2));
  require(body0.graph_launch_occurrence_id == GraphLaunchOccurrenceId(0) &&
              body1.graph_launch_occurrence_id == GraphLaunchOccurrenceId(1) &&
              body2.graph_launch_occurrence_id == GraphLaunchOccurrenceId(2),
          "graph launch bodies lost launch occurrence order");
  require(body0.replay_body_template_id == body2.replay_body_template_id &&
              body0.replay_body_template_id != body1.replay_body_template_id,
          "repeated replay compute bodies lost exact template identity");
  require(body0.compute_task_count == 1 &&
              body0.communication_task_count == 1 &&
              body1.compute_task_count == 1 &&
              body1.communication_task_count == 0 &&
              body2.compute_task_count == 1 &&
              body2.communication_task_count == 1 &&
              body0.stream_count == 2 && body1.stream_count == 1 &&
              body2.stream_count == 2,
          "graph launch body compute task counts mismatch");
  require(launch_identity_ir.replay_body_templates
                  .row(body0.replay_body_template_id)
                  .topology_policy ==
              ReplayBodyTopologyPolicy::kCapturedStreamSetUnordered &&
              launch_identity_ir.replay_body_templates
                      .row(body0.replay_body_template_id)
                      .stream_count == 2,
          "multi-stream graph body lost its captured stream topology");
  require(launch_identity_ir.symbols
                  .value(launch_identity_ir.replay_body_templates
                             .row(body0.replay_body_template_id)
                             .op_sequence_symbol_id)
                  .find("comm:hcom_allReduce/Reduce_Inline") !=
              std::string::npos,
          "multi-stream graph body lost normalized communication topology");
  require(launch0.captured_graph_instance_id.valid() &&
              launch1.captured_graph_instance_id.valid() &&
              launch2.captured_graph_instance_id ==
                  launch0.captured_graph_instance_id &&
              !launch3.captured_graph_instance_id.valid(),
          "launches did not link to captured graph instances by model id");
  require(launch_identity_ir.captured_graph_instances
                  .row(launch0.captured_graph_instance_id)
                  .slot_template_id !=
              launch_identity_ir.captured_graph_instances
                  .row(launch1.captured_graph_instance_id)
                  .slot_template_id,
          "same-profile graph instances lost their slot-template identity");
  require(launch_identity_ir.graph_launch_activities.size() == 2 &&
              launch_identity_ir.graph_launch_activity_members.size() == 4,
          "host blocking sync boundaries did not preserve launch activities");
  const GraphLaunchActivityRow& activity0 =
      launch_identity_ir.graph_launch_activities.row(GraphLaunchActivityId(0));
  const GraphLaunchActivityRow& activity1 =
      launch_identity_ir.graph_launch_activities.row(GraphLaunchActivityId(1));
  require(activity0.host_execute_count == 2 &&
              activity0.matched_launch_count == 2 &&
              activity0.boundary_host_api_row_id == 13 &&
              activity1.host_execute_count == 2 &&
              activity1.matched_launch_count == 2 &&
              activity1.boundary_host_api_row_id == 14,
          "graph launch activity cardinality/provenance mismatch");
  require(activity0.boundary_policy ==
                  GraphLaunchActivityBoundaryPolicy::kHostBlockingSync &&
              activity1.boundary_policy ==
                  GraphLaunchActivityBoundaryPolicy::kHostBlockingSync,
          "graph launch activity boundary policy mismatch");

  const std::filesystem::path split_graph_dir =
      launch_identity_dir / "split_profile";
  create_split_aclgraph_profile_from_monolithic(
      split_graph_dir, launch_identity_path,
      (launch_identity_dir / "host" / "sqlite" / "stream_info.db").string());
  const NativeIr split_graph_ir =
      AscendSQLiteAdapter(split_graph_dir.string(),
                          "graph_launch_identity_split")
          .load();
  require(split_graph_ir.graph_launch_occurrences.size() ==
                  launch_identity_ir.graph_launch_occurrences.size() &&
              split_graph_ir.captured_graph_instances.size() ==
                  launch_identity_ir.captured_graph_instances.size() &&
              split_graph_ir.captured_graph_streams.size() ==
                  launch_identity_ir.captured_graph_streams.size() &&
              split_graph_ir.graph_launch_bodies.size() ==
                  launch_identity_ir.graph_launch_bodies.size() &&
              split_graph_ir.graph_launch_activities.size() ==
                  launch_identity_ir.graph_launch_activities.size() &&
              split_graph_ir.graph_launch_activity_members.size() ==
                  launch_identity_ir.graph_launch_activity_members.size(),
          "split ACLGraph evidence cardinality differs from monolithic");
  for (std::uint32_t index = 0;
       index < split_graph_ir.graph_launch_occurrences.size(); ++index) {
    const GraphLaunchOccurrenceRow& monolithic =
        launch_identity_ir.graph_launch_occurrences.row(
            GraphLaunchOccurrenceId(index));
    const GraphLaunchOccurrenceRow& split =
        split_graph_ir.graph_launch_occurrences.row(
            GraphLaunchOccurrenceId(index));
    require(split.raw_host_api_row_id == monolithic.raw_host_api_row_id &&
                split.raw_launch_connection_id ==
                    monolithic.raw_launch_connection_id &&
                split.raw_graph_connection_id ==
                    monolithic.raw_graph_connection_id &&
                split.raw_model_id == monolithic.raw_model_id &&
                split.match_policy == monolithic.match_policy,
            "split ACLGraph launch identity differs from monolithic");
    require(split_graph_ir.source_refs.row(split.source_ref_id).table_name ==
                "AscendTask",
            "split graph launch provenance did not retain AscendTask");
  }
  std::vector<std::uint64_t> monolithic_body_hashes;
  std::vector<std::uint64_t> split_body_hashes;
  for (const ReplayBodyTemplateRow& body :
       launch_identity_ir.replay_body_templates.rows()) {
    monolithic_body_hashes.push_back(body.exact_sequence_hash);
  }
  for (const ReplayBodyTemplateRow& body :
       split_graph_ir.replay_body_templates.rows()) {
    split_body_hashes.push_back(body.exact_sequence_hash);
  }
  std::sort(monolithic_body_hashes.begin(), monolithic_body_hashes.end());
  std::sort(split_body_hashes.begin(), split_body_hashes.end());
  require(split_body_hashes == monolithic_body_hashes,
          "split ACLGraph body templates differ from monolithic");
  require(split_graph_ir.graph_launch_bodies.row(GraphLaunchBodyId(0))
                  .communication_task_count == 1 &&
              split_graph_ir.symbols
                      .value(split_graph_ir.replay_body_templates
                                 .row(split_graph_ir.graph_launch_bodies
                                          .row(GraphLaunchBodyId(0))
                                          .replay_body_template_id)
                                 .op_sequence_symbol_id)
                      .find("comm:hcom_allReduce/Reduce_Inline") !=
                  std::string::npos,
          "split ACLGraph body lost HCCL task identity");

  const std::filesystem::path body_mismatch_dir =
      temp_prof_dir("_body_mismatch");
  std::filesystem::create_directories(body_mismatch_dir);
  const std::string body_mismatch_path =
      (body_mismatch_dir / "msprof.db").string();
  create_aclgraph_body_mismatch_profile(body_mismatch_path);
  const NativeIr body_mismatch_ir =
      AscendSQLiteAdapter(body_mismatch_path, "graph_body_mismatch").load();
  require(body_mismatch_ir.replay_composition_candidates.size() == 1 &&
              body_mismatch_ir.replay_composition_regions.size() == 7 &&
              body_mismatch_ir.replay_composition_region_members.size() == 7,
          "body mismatch profile lost exact composition membership");
  std::size_t recognized_regions = 0;
  std::size_t mismatched_regions = 0;
  for (const ReplayCompositionRegionRow& region :
       body_mismatch_ir.replay_composition_regions.rows()) {
    if (region.status ==
        ReplayCompositionRegionStatus::kRecognizedCompletePattern) {
      ++recognized_regions;
    } else if (region.status ==
               ReplayCompositionRegionStatus::kUnrecognizedBodyMismatch) {
      ++mismatched_regions;
    }
  }
  require(recognized_regions == 6 && mismatched_regions == 1 &&
              body_mismatch_ir.replay_composition_regions.row(
                  ReplayCompositionRegionId(3))
                      .status == ReplayCompositionRegionStatus::
                                     kUnrecognizedBodyMismatch,
          "repeated graph identity silently accepted a changed compute body");

  const std::filesystem::path exact_hlt_dir =
      temp_prof_dir("_exact_hlt");
  std::filesystem::create_directories(exact_hlt_dir);
  const std::string exact_hlt_path =
      (exact_hlt_dir / "msprof.db").string();
  create_aclgraph_exact_hlt_profile(exact_hlt_path);
  NativeIr exact_hlt_ir =
      AscendSQLiteAdapter(exact_hlt_path, "graph_exact_hlt").load();
  require(exact_hlt_ir.replay_composition_candidates.size() == 2 &&
              exact_hlt_ir.replay_composition_slots.size() == 6,
          "exact HLT profile did not preserve prefill/decode compositions");
  require(exact_hlt_ir.replay_composition_candidates
                      .row(ReplayCompositionCandidateId(0))
                      .boundary_policy ==
                  ReplayCompositionBoundaryPolicy::
                      kExactOneShotLeadingComposition &&
              exact_hlt_ir.replay_composition_candidates
                      .row(ReplayCompositionCandidateId(1))
                      .boundary_policy ==
                  ReplayCompositionBoundaryPolicy::kExactPeriodicSuffix,
          "exact HLT prefill/decode boundary policies mismatch");
  require(exact_hlt_ir.replay_composition_regions.size() == 5 &&
              exact_hlt_ir.replay_composition_region_members.size() == 14,
          "exact HLT profile lost prefill/decode/tail membership");
  require(exact_hlt_ir.replay_units.size() == 4 &&
              exact_hlt_ir.graph_templates.size() == 2 &&
              exact_hlt_ir.replay_unit_launch_members.size() == 12,
          "exact HLT prefill/decode regions did not cut over to units");
  require(exact_hlt_ir.replay_composition_regions
                  .row(ReplayCompositionRegionId(4))
                  .status == ReplayCompositionRegionStatus::
                                 kUnrecognizedIncompleteTail,
          "exact HLT prefix tail should remain explicitly unrecognized");
  for (const ReplayUnitRow& unit : exact_hlt_ir.replay_units.rows()) {
    require(unit.replay_composition_region_id.valid(),
            "exact HLT replay unit lost its composition region link");
  }
  require(exact_hlt_ir.captured_graph_instances.size() == 6 &&
              exact_hlt_ir.captured_graph_streams.size() == 12 &&
              exact_hlt_ir.graph_launch_bodies.size() == 14,
          "exact HLT fixture lost its multi-stream body evidence");
  for (const GraphLaunchBodyRow& body :
       exact_hlt_ir.graph_launch_bodies.rows()) {
    const ReplayBodyTemplateRow& body_template =
        exact_hlt_ir.replay_body_templates.row(body.replay_body_template_id);
    require(body.compute_task_count == 2 &&
                body.communication_task_count == 0 &&
                body.stream_count == 2 &&
                body_template.stream_count == 2 &&
                body_template.topology_policy ==
                    ReplayBodyTopologyPolicy::kCapturedStreamSetUnordered,
            "exact HLT projection did not retain both graph body lanes");
  }
  for (std::size_t index = 0;
       index < exact_hlt_ir.replay_unit_launch_members.size(); ++index) {
    const ReplayUnitLaunchMemberRow& member =
        exact_hlt_ir.replay_unit_launch_members.row(
            ReplayUnitLaunchMemberId(index));
    require(member.member_order == index % 3,
            "exact HLT replay membership order is not unit-local");
  }

  const std::filesystem::path split_exact_hlt_dir =
      exact_hlt_dir / "split_profile";
  create_split_aclgraph_profile_from_monolithic(
      split_exact_hlt_dir, exact_hlt_path,
      (exact_hlt_dir / "host" / "sqlite" / "stream_info.db").string());
  const NativeIr split_exact_hlt_ir =
      AscendSQLiteAdapter(split_exact_hlt_dir.string(),
                          "graph_exact_hlt_split")
          .load();
  require(split_exact_hlt_ir.replay_composition_candidates.size() == 2 &&
              split_exact_hlt_ir.replay_composition_slots.size() == 6 &&
              split_exact_hlt_ir.replay_composition_regions.size() == 5 &&
              split_exact_hlt_ir.replay_composition_region_members.size() ==
                  14 &&
              split_exact_hlt_ir.replay_units.size() == 4 &&
              split_exact_hlt_ir.replay_unit_launch_members.size() == 12,
          "split exact HLT reconstruction differs from monolithic");
  require(split_exact_hlt_ir.replay_composition_regions
                  .row(ReplayCompositionRegionId(4))
                  .status == ReplayCompositionRegionStatus::
                                 kUnrecognizedIncompleteTail,
          "split exact HLT tail did not remain unrecognized");
  std::vector<std::uint64_t> exact_hlt_body_hashes;
  std::vector<std::uint64_t> split_exact_hlt_body_hashes;
  for (const ReplayBodyTemplateRow& body :
       exact_hlt_ir.replay_body_templates.rows()) {
    exact_hlt_body_hashes.push_back(body.exact_sequence_hash);
  }
  for (const ReplayBodyTemplateRow& body :
       split_exact_hlt_ir.replay_body_templates.rows()) {
    split_exact_hlt_body_hashes.push_back(body.exact_sequence_hash);
  }
  std::sort(exact_hlt_body_hashes.begin(), exact_hlt_body_hashes.end());
  std::sort(split_exact_hlt_body_hashes.begin(),
            split_exact_hlt_body_hashes.end());
  require(split_exact_hlt_body_hashes == exact_hlt_body_hashes,
          "split exact HLT body identities differ from monolithic");

  FlatAnchorBuildConfig exact_anchor_config;
  exact_anchor_config.filter_auxiliary_task_anchors = true;
  exact_anchor_config.skip_events_covered_by_replay_units = true;
  const FlatAnchorBuildStats exact_anchor_stats =
      build_flat_anchors(exact_hlt_ir, exact_anchor_config);
  require(exact_anchor_stats.device_event_anchors == 12 &&
              exact_anchor_stats.communication_anchors == 0 &&
              exact_hlt_ir.protected_intervals.size() == 4,
          "exact HLT projection did not preserve its complete units");
  std::size_t head_anchors = 0;
  std::size_t layer_anchors = 0;
  std::size_t tail_anchors = 0;
  std::size_t raw_anchors = 0;
  for (const AnchorRow& anchor : exact_hlt_ir.anchors.rows()) {
    switch (anchor.kind) {
      case AnchorKind::kGraphH:
        ++head_anchors;
        break;
      case AnchorKind::kGraphL:
        ++layer_anchors;
        break;
      case AnchorKind::kGraphT:
        ++tail_anchors;
        break;
      case AnchorKind::kDeviceEvent:
        ++raw_anchors;
        break;
      default:
        break;
    }
  }
  require(head_anchors == 4 && layer_anchors == 4 && tail_anchors == 4 &&
              raw_anchors == 0,
          "exact HLT anchor roles mismatch");
  for (std::size_t index = 0;
       index < exact_hlt_ir.protected_intervals.size(); ++index) {
    const ProtectedIntervalRow& interval =
        exact_hlt_ir.protected_intervals.row(ProtectedIntervalId(index));
    require(interval.first_token_id.value() == index * 3 &&
                interval.last_token_id.value() == index * 3 + 2 &&
                interval.boundary_policy == BoundaryPolicy::kNoCross,
            "exact HLT protected interval is not one full H/L/T unit");
  }

  const std::filesystem::path missing_body_dir =
      temp_prof_dir("_missing_body");
  std::filesystem::create_directories(missing_body_dir);
  const std::string missing_body_path =
      (missing_body_dir / "msprof.db").string();
  create_aclgraph_exact_hlt_profile(missing_body_path, 6);
  const NativeIr missing_body_ir =
      AscendSQLiteAdapter(missing_body_path, "graph_missing_body").load();
  std::size_t missing_body_regions = 0;
  for (const ReplayCompositionRegionRow& region :
       missing_body_ir.replay_composition_regions.rows()) {
    if (region.status == ReplayCompositionRegionStatus::
                             kUnrecognizedMissingBodyEvidence) {
      ++missing_body_regions;
    }
  }
  require(missing_body_ir.replay_composition_candidates.size() == 2 &&
              missing_body_ir.replay_composition_regions.size() == 5 &&
              missing_body_regions == 1 &&
              missing_body_ir.graph_launch_bodies.size() == 13 &&
              missing_body_ir.replay_units.size() == 3 &&
              missing_body_ir.replay_unit_launch_members.size() == 9,
          "missing graph body evidence did not stay typed and unpromoted");

  const std::filesystem::path truncated_launch_dir =
      temp_prof_dir("_truncated_launch");
  std::filesystem::create_directories(truncated_launch_dir);
  const std::string truncated_launch_path =
      (truncated_launch_dir / "msprof.db").string();
  create_aclgraph_exact_hlt_profile(truncated_launch_path);
  sqlite3* truncated_db = nullptr;
  const int truncated_rc = sqlite3_open_v2(
      truncated_launch_path.c_str(), &truncated_db, SQLITE_OPEN_READWRITE,
      nullptr);
  require(truncated_rc == SQLITE_OK,
          "failed to reopen truncated launch fixture");
  exec_sql(truncated_db,
           "DELETE FROM TASK WHERE taskType = 12 AND taskId = 70;");
  sqlite3_close(truncated_db);
  const NativeIr truncated_launch_ir =
      AscendSQLiteAdapter(truncated_launch_path, "graph_truncated_launch")
          .load();
  std::size_t missing_completion_regions = 0;
  for (const ReplayCompositionRegionRow& region :
       truncated_launch_ir.replay_composition_regions.rows()) {
    if (region.status == ReplayCompositionRegionStatus::
                             kUnrecognizedMissingCompletionEvidence) {
      ++missing_completion_regions;
      const ReplayCompositionCandidateRow& candidate =
          truncated_launch_ir.replay_composition_candidates.row(
              region.replay_composition_candidate_id);
      require(candidate.identity_policy ==
                  ReplayCompositionIdentityPolicy::kUnavailable &&
                  candidate.boundary_policy ==
                      ReplayCompositionBoundaryPolicy::
                          kIncompleteLaunchEvidence &&
                  region.observed_launch_count == 1 &&
                  region.expected_launch_count == 1,
              "truncated launch region lost its explicit evidence policy");
    }
  }
  require(truncated_launch_ir.graph_launch_occurrences.size() == 14 &&
              truncated_launch_ir.graph_launch_bodies.size() == 13 &&
              truncated_launch_ir.replay_composition_candidates.size() == 3 &&
              truncated_launch_ir.replay_composition_regions.size() == 6 &&
              truncated_launch_ir.replay_composition_region_members.size() ==
                  14 &&
              missing_completion_regions == 1 &&
              truncated_launch_ir.replay_units.size() == 4 &&
              truncated_launch_ir.replay_unit_launch_members.size() == 12,
          "truncated completion evidence disappeared or changed exact units");
  const GraphLaunchOccurrenceRow& truncated_launch =
      truncated_launch_ir.graph_launch_occurrences.row(
          GraphLaunchOccurrenceId(13));
  require(truncated_launch.match_policy == GraphLaunchMatchPolicy::kUnmatched &&
              !truncated_launch.notify_record_task_id.valid() &&
              truncated_launch.raw_graph_connection_id < 0,
          "truncated launch unexpectedly acquired completion identity");

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
    require(split_task.raw_model_id == golden_task.raw_model_id,
            "split task model id does not match monolithic golden");
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

  // E2: productive timeline must be identical for monolithic and split
  // layouts, and the coverage invariant must hold.
  const SemanticTaskRuleset idle_rules =
      load_default_idle_evidence_semantic_ruleset();
  const SemanticTaskClassificationResult golden_class =
      classify_semantic_tasks(golden_ir, idle_rules);
  const SemanticTaskClassificationResult split_class =
      classify_semantic_tasks(split_ir, idle_rules);
  const ProductiveTimelineRunResult golden_run =
      build_productive_timelines(golden_ir, golden_class);
  const ProductiveTimelineRunResult split_run =
      build_productive_timelines(split_ir, split_class);
  require(golden_run.status == AnalysisStatus::kOk &&
              split_run.status == AnalysisStatus::kOk,
          "E2: run status ok for both layouts");
  require(golden_run.devices.size() == split_run.devices.size() &&
              golden_run.devices.size() == 1,
          "E2: golden fixture must produce one device timeline");
  const DeviceTimelineResult& golden_timeline = golden_run.devices[0];
  const DeviceTimelineResult& split_timeline = split_run.devices[0];
  require(golden_timeline.status == AnalysisStatus::kOk &&
              split_timeline.status == AnalysisStatus::kOk,
          "E2: timeline status ok for both layouts");
  require(golden_timeline.intervals.size() == split_timeline.intervals.size() &&
              !golden_timeline.intervals.empty(),
          "E2: split and monolithic interval counts differ");
  for (std::size_t index = 0; index < golden_timeline.intervals.size();
       ++index) {
    require(golden_timeline.intervals[index].start_ns ==
                    split_timeline.intervals[index].start_ns &&
                golden_timeline.intervals[index].end_ns ==
                    split_timeline.intervals[index].end_ns &&
                golden_timeline.intervals[index].kind ==
                    split_timeline.intervals[index].kind,
            "E2: split timeline does not match monolithic");
  }
  require(golden_timeline.intervals[0].kind ==
              DeviceIntervalKind::kProductiveActive,
          "E2: first interval is productive (MatMul)");
  std::int64_t covered_ns = 0;
  for (const DeviceIntervalRow& row : golden_timeline.intervals) {
    covered_ns += row.end_ns - row.start_ns;
  }
  require(golden_timeline.span_start_ns && golden_timeline.span_end_ns &&
              covered_ns ==
                  *golden_timeline.span_end_ns - *golden_timeline.span_start_ns,
          "E2: productive + gap covers the span exactly");

  // E3: per-stream observable state timelines must be identical for the
  // monolithic and split layouts through the real adapter. Lineage is
  // compared by stable source identity (source table + source row id +
  // kind + matched_rule_id + event geometry), never by internal
  // TraceEventId/TaskId/SourceRefId, which may shift with import order.
  const StreamStateRunResult golden_streams =
      build_stream_state_timelines(golden_ir, golden_class, golden_run);
  const StreamStateRunResult split_streams =
      build_stream_state_timelines(split_ir, split_class, split_run);
  require(golden_streams.status == AnalysisStatus::kOk &&
              split_streams.status == AnalysisStatus::kOk,
          "E3: run status ok for both layouts");
  require(golden_streams.stream_universe_size ==
                  split_streams.stream_universe_size &&
              golden_streams.observed_universe_scan_complete ==
                  split_streams.observed_universe_scan_complete &&
              golden_streams.devices.size() == split_streams.devices.size(),
          "E3: run-level universe metadata equal");
  for (std::size_t device_index = 0;
       device_index < golden_streams.devices.size(); ++device_index) {
    const StreamStateDeviceResult& golden_device =
        golden_streams.devices[device_index];
    const StreamStateDeviceResult& split_device =
        split_streams.devices[device_index];
    require(golden_device.device_id == split_device.device_id &&
                golden_device.span_start_ns == split_device.span_start_ns &&
                golden_device.span_end_ns == split_device.span_end_ns &&
                golden_device.stream_universe_size ==
                    split_device.stream_universe_size &&
                golden_device.observed_universe_scan_complete ==
                    split_device.observed_universe_scan_complete,
            "E3: per-device universe metadata equal");
    require(golden_device.timelines.size() == split_device.timelines.size(),
            "E3: timeline counts equal");
    for (std::size_t timeline_index = 0;
         timeline_index < golden_device.timelines.size(); ++timeline_index) {
      const StreamStateTimeline& golden_timeline =
          golden_device.timelines[timeline_index];
      const StreamStateTimeline& split_timeline =
          split_device.timelines[timeline_index];
      require(golden_timeline.stream_id == split_timeline.stream_id &&
                  golden_timeline.span_start_ns ==
                      split_timeline.span_start_ns &&
                  golden_timeline.span_end_ns == split_timeline.span_end_ns,
              "E3: timeline identity equal");
      require(golden_timeline.intervals.size() ==
                  split_timeline.intervals.size(),
              "E3: interval counts equal");
      for (std::size_t interval_index = 0;
           interval_index < golden_timeline.intervals.size();
           ++interval_index) {
        const StreamStateInterval& golden_interval =
            golden_timeline.intervals[interval_index];
        const StreamStateInterval& split_interval =
            split_timeline.intervals[interval_index];
        require(golden_interval.start_ns == split_interval.start_ns &&
                    golden_interval.end_ns == split_interval.end_ns &&
                    golden_interval.state == split_interval.state &&
                    golden_interval.source_links.size() ==
                        split_interval.source_links.size(),
                "E3: interval structure equal");
        for (std::size_t link_index = 0;
             link_index < golden_interval.source_links.size(); ++link_index) {
          const StreamStateSourceLink& golden_link =
              golden_interval.source_links[link_index];
          const StreamStateSourceLink& split_link =
              split_interval.source_links[link_index];
          const TraceEventRow& golden_event =
              golden_ir.trace_events.row(golden_link.trace_event_id);
          const TraceEventRow& split_event =
              split_ir.trace_events.row(split_link.trace_event_id);
          const SourceRefRow& golden_source =
              golden_ir.source_refs.row(golden_event.source_ref_id);
          const SourceRefRow& split_source =
              split_ir.source_refs.row(split_event.source_ref_id);
          require(golden_link.kind == split_link.kind &&
                      golden_link.matched_rule_id ==
                          split_link.matched_rule_id &&
                      golden_event.device_id == split_event.device_id &&
                      golden_event.stream_id == split_event.stream_id &&
                      golden_event.start_ns == split_event.start_ns &&
                      golden_event.end_ns == split_event.end_ns &&
                      golden_event.source_row_id == split_event.source_row_id,
                  "E3: stable lineage equal (row id + kind + rule, not "
                  "internal ids)");
          // The physical source table legitimately differs between layouts
          // (monolithic TASK vs split AscendTask); each layout must carry
          // its own table, and the row identity must match across layouts.
          require(golden_source.table_name == "TASK" &&
                      split_source.table_name == "AscendTask",
                  "E3: each layout carries its own source table");
        }
      }
      require(golden_timeline.diagnostics == split_timeline.diagnostics,
              "E3: timeline diagnostics equal");
    }
    require(golden_device.diagnostics == split_device.diagnostics,
            "E3: device diagnostics equal");
  }
  require(golden_streams.diagnostics == split_streams.diagnostics,
          "E3: run diagnostics equal");

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
  std::filesystem::remove_all(launch_identity_dir);
  std::filesystem::remove_all(body_mismatch_dir);
  std::filesystem::remove_all(exact_hlt_dir);
  std::filesystem::remove_all(missing_body_dir);
  std::filesystem::remove_all(graph_dir);
  std::filesystem::remove_all(split_dir);
  std::filesystem::remove_all(incomplete_dir);
  return 0;
}
