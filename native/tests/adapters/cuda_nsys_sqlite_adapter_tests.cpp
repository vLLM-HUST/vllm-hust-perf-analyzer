#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "traceloom/adapters/cuda_nsys_sqlite_adapter.h"
#include "traceloom/analysis/flat_anchor_builder.h"

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
      ("traceloom_cuda_nsys_sqlite_adapter_" + std::to_string(now) + suffix +
       ".sqlite");
  return path.string();
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

void create_db(const std::string& path, const char* sql) {
  sqlite3* db = nullptr;
  const int rc = sqlite3_open_v2(
      path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(rc == SQLITE_OK, "failed to create temporary CUDA sqlite DB");
  exec_sql(db, sql);
  sqlite3_close(db);
}

std::string symbol(const traceloom::NativeIr& ir, traceloom::SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : "<invalid>";
}

std::string snapshot(const traceloom::NativeIr& ir) {
  std::ostringstream out;
  out << "sources=" << ir.source_refs.size() << '\n';
  for (const traceloom::SourceRefRow& row : ir.source_refs.rows()) {
    out << "source[" << row.id.value() << "]=" << row.source_kind << '|'
        << row.table_name << '\n';
  }
  out << "streams=" << ir.streams.size() << '\n';
  for (const traceloom::StreamRow& row : ir.streams.rows()) {
    out << "stream[" << row.id.value() << "]=" << row.device_id << ':'
        << row.raw_stream_id << '\n';
  }
  out << "events=" << ir.trace_events.size() << '\n';
  for (const traceloom::TraceEventRow& row : ir.trace_events.rows()) {
    out << "event[" << row.id.value() << "]=" << row.source_row_id << '|'
        << row.device_id << ':' << row.stream_id << '|' << row.start_ns << '-'
        << row.end_ns << '|' << symbol(ir, row.raw_name_symbol_id) << '\n';
  }
  out << "tasks=" << ir.tasks.size() << '\n';
  for (const traceloom::TaskRow& row : ir.tasks.rows()) {
    out << "task[" << row.id.value() << "]=" << row.raw_task_id << '|'
        << row.raw_connection_id << '|' << symbol(ir, row.task_type_symbol_id)
        << '|' << symbol(ir, row.op_name_symbol_id) << '|'
        << symbol(ir, row.op_type_symbol_id) << '\n';
  }
  return out.str();
}

template <typename Fn>
void require_throws_with(const std::string& needle, const Fn& fn) {
  try {
    fn();
  } catch (const std::exception& ex) {
    require(std::string(ex.what()).find(needle) != std::string::npos,
            "exception did not explain the unsupported CUDA schema");
    return;
  }
  require(false, "expected CUDA adapter to reject an unsupported schema");
}

}  // namespace

int main(int argc, char** argv) {
  using namespace traceloom;

  require(argc <= 2,
          "usage: cuda_nsys_sqlite_adapter_tests [fixture-output.sqlite]");
  const bool keep_fixture = argc == 2;
  const std::string db_path =
      keep_fixture ? std::string(argv[1]) : temp_db_path("_golden");
  if (keep_fixture) {
    std::remove(db_path.c_str());
  }
  create_db(
      db_path,
      "CREATE TABLE StringIds(id INTEGER PRIMARY KEY, value TEXT);"
      "INSERT INTO StringIds(id, value) VALUES "
      "(1, 'flash_fwd_kernel_bf16'), "
      "(2, 'kernel'), "
      "(3, 'void cutlass_gemm_kernel()'), "
      "(4, 'vectorized_elementwise_kernel');"
      "CREATE TABLE CUPTI_ACTIVITY_KIND_KERNEL("
      "start INTEGER, end INTEGER, deviceId INTEGER, streamId INTEGER, "
      "correlationId INTEGER, demangledName INTEGER, shortName INTEGER);"
      "INSERT INTO CUPTI_ACTIVITY_KIND_KERNEL VALUES "
      "(300, 360, 1, 4, 33, 3, 2), "
      "(100, 140, 0, 7, 11, 1, 1), "
      "(200, 215, 0, 9, 22, 4, 4);"
      "CREATE TABLE CUPTI_ACTIVITY_KIND_RUNTIME("
      "start INTEGER, end INTEGER, correlationId INTEGER, nameId INTEGER);"
      "INSERT INTO CUPTI_ACTIVITY_KIND_RUNTIME VALUES (80, 85, 9, 2);"
      "CREATE TABLE CUPTI_ACTIVITY_KIND_MEMCPY("
      "start INTEGER, end INTEGER, deviceId INTEGER, streamId INTEGER, "
      "correlationId INTEGER);"
      "INSERT INTO CUPTI_ACTIVITY_KIND_MEMCPY VALUES (86, 89, 0, 7, 10);"
      "CREATE TABLE CUPTI_ACTIVITY_KIND_CUDA_EVENT("
      "deviceId INTEGER, contextId INTEGER, streamId INTEGER, "
      "correlationId INTEGER, eventId INTEGER);"
      "INSERT INTO CUPTI_ACTIVITY_KIND_CUDA_EVENT VALUES (0, 1, 7, 10, 3);"
      "CREATE TABLE CUPTI_ACTIVITY_KIND_GRAPH_TRACE("
      "start INTEGER, end INTEGER, deviceId INTEGER, streamId INTEGER, "
      "graphId INTEGER);"
      "INSERT INTO CUPTI_ACTIVITY_KIND_GRAPH_TRACE VALUES (90, 150, 0, 7, 42);"
      "INSERT INTO CUPTI_ACTIVITY_KIND_GRAPH_TRACE VALUES (290, 370, 1, 4, 42);");

  const CudaNsightSQLiteInventory inventory =
      inspect_cuda_nsys_sqlite_profile(db_path);
  require(inventory.has_kernel_table, "kernel table was not inventoried");
  require(inventory.has_string_ids_table, "StringIds was not inventoried");
  require(inventory.kernel_row_count == 3, "kernel row count mismatch");
  require(inventory.missing_required_kernel_columns.empty(),
          "valid kernel schema was rejected");
  require(inventory.present_activity_tables.size() == 4 &&
              inventory.present_activity_tables.front() ==
                  "CUPTI_ACTIVITY_KIND_RUNTIME" &&
              inventory.present_activity_tables.back() ==
                  "CUPTI_ACTIVITY_KIND_GRAPH_TRACE",
          "optional CUPTI tables were not surfaced by inventory");
  require(looks_like_cuda_nsys_sqlite_profile(db_path),
          "CUDA profile sniffing did not recognize a kernel export");

  CudaNsightSQLiteAdapterOptions options;
  options.db_path = db_path;
  options.source_kind = "cuda_nsys_sqlite_test";
  const CudaNsightSQLiteAdapter adapter(options);
  NativeIr ir = adapter.load();

  const std::string expected =
      "sources=4\n"
      "source[0]=cuda_nsys_sqlite_test|CUPTI_ACTIVITY_KIND_KERNEL\n"
      "source[1]=cuda_nsys_sqlite_test|CUPTI_ACTIVITY_KIND_RUNTIME\n"
      "source[2]=cuda_nsys_sqlite_test|CUPTI_ACTIVITY_KIND_MEMCPY\n"
      "source[3]=cuda_nsys_sqlite_test|CUPTI_ACTIVITY_KIND_GRAPH_TRACE\n"
      "streams=7\n"
      "stream[0]=0:7\n"
      "stream[1]=0:9\n"
      "stream[2]=1:4\n"
      "stream[3]=0:0\n"
      "stream[4]=0:7\n"
      "stream[5]=0:7\n"
      "stream[6]=1:4\n"
      "events=7\n"
      "event[0]=2|0:7|100-140|flash_fwd_kernel_bf16\n"
      "event[1]=3|0:9|200-215|vectorized_elementwise_kernel\n"
      "event[2]=1|1:4|300-360|void cutlass_gemm_kernel()\n"
      "event[3]=1|0:0|80-85|kernel\n"
      "event[4]=1|0:7|86-89|CudaMemcpy_1\n"
      "event[5]=1|0:7|90-150|CudaGraphReplay T1\n"
      "event[6]=2|1:4|290-370|CudaGraphReplay T1\n"
      "tasks=5\n"
      "task[0]=11|11|CUDA_KERNEL|flash_fwd_kernel_bf16|CudaFlashAttention\n"
      "task[1]=22|22|CUDA_KERNEL_AUX|vectorized_elementwise_kernel|CudaAux:Pointwise\n"
      "task[2]=33|33|CUDA_KERNEL|void cutlass_gemm_kernel()|CudaMatMul\n"
      "task[3]=9|9|CUDA_RUNTIME_AUX|kernel|CUDA_RUNTIME_AUX\n"
      "task[4]=10|10|CUDA_MEMCPY_AUX|CudaMemcpy_1|CUDA_MEMCPY_AUX\n";
  require(snapshot(ir) == expected,
          "CUDA adapter output changed from its deterministic golden snapshot");
  require(snapshot(adapter.load()) == expected,
          "CUDA adapter ordering changed between identical loads");

  FlatAnchorBuildConfig anchor_config;
  anchor_config.filter_auxiliary_task_anchors = true;
  anchor_config.skip_events_covered_by_replay_units = true;
  const FlatAnchorBuildStats anchor_stats =
      build_flat_anchors(ir, anchor_config);
  require(anchor_stats.device_event_anchors == 2,
          "CUDA graph traces did not enter the native report model");
  require(ir.anchors.size() == 2,
          "CUDA auxiliary kernel unexpectedly became an anchor");
  require(ir.graph_templates.size() == 1 && ir.replay_units.size() == 2,
          "CUDA graph trace rows did not form stable replay units");

  const std::string fallback_path = temp_db_path("_fallback");
  create_db(fallback_path,
            "CREATE TABLE CUPTI_ACTIVITY_KIND_KERNEL("
            "start INTEGER, end INTEGER, deviceId INTEGER, streamId INTEGER);"
            "INSERT INTO CUPTI_ACTIVITY_KIND_KERNEL VALUES (10, 20, 2, 5);");
  NativeIr fallback_ir = CudaNsightSQLiteAdapter(fallback_path).load();
  require(fallback_ir.trace_events.size() == 1,
          "minimal CUDA schema did not produce a trace event");
  require(fallback_ir.symbols.value(
              fallback_ir.trace_events.row(TraceEventId(0)).raw_name_symbol_id) ==
              "cuda_kernel_1",
          "missing optional CUDA names did not use deterministic fallback");
  require(fallback_ir.tasks.row(TaskId(0)).raw_connection_id == -1 &&
              fallback_ir.tasks.row(TaskId(0)).raw_task_id == 1,
          "missing optional correlationId did not use documented fallback");
  build_flat_anchors(fallback_ir, anchor_config);
  require(fallback_ir.anchors.size() == 1,
          "unattributed CUDA kernel disappeared from the report model");

  const std::string collective_path = temp_db_path("_collective");
  create_db(
      collective_path,
      "CREATE TABLE StringIds(id INTEGER PRIMARY KEY, value TEXT);"
      "INSERT INTO StringIds VALUES "
      "(1, 'ncclDevKernel_AllReduce_Sum_bf16_RING_LL');"
      "CREATE TABLE CUPTI_ACTIVITY_KIND_KERNEL("
      "start INTEGER, end INTEGER, deviceId INTEGER, streamId INTEGER, "
      "correlationId INTEGER, shortName INTEGER);"
      "INSERT INTO CUPTI_ACTIVITY_KIND_KERNEL VALUES "
      "(10, 20, 1, 7, 42, 1);");
  NativeIr collective_ir = CudaNsightSQLiteAdapter(collective_path).load();
  require(collective_ir.tasks.size() == 1 &&
              collective_ir.communication_ops.size() == 1,
          "NCCL kernel did not materialize task and communication evidence");
  const TaskRow& collective_task = collective_ir.tasks.row(TaskId(0));
  require(!collective_task.compute_task_type_symbol_id.valid() &&
              collective_task.comm_name_symbol_id.valid() &&
              collective_task.communication_task_type_symbol_id.valid(),
          "NCCL task was not marked as communication evidence");
  const CommunicationOpRow& collective_op =
      collective_ir.communication_ops.row(CommunicationOpId(0));
  require(collective_op.trace_event_id == TraceEventId(0) &&
              collective_op.raw_connection_id == 42 &&
              collective_op.raw_op_id == 1 &&
              collective_op.linked_task_count == 1 &&
              collective_op.linked_stream_count == 1,
          "NCCL communication provenance was not preserved");
  FlatAnchorBuildConfig collective_anchor_config;
  collective_anchor_config.filter_auxiliary_task_anchors = true;
  collective_anchor_config.skip_tasks_covered_by_communication_ops = true;
  const FlatAnchorBuildStats collective_anchor_stats =
      build_flat_anchors(collective_ir, collective_anchor_config);
  require(collective_anchor_stats.communication_anchors == 1 &&
              collective_anchor_stats.device_event_anchors == 0 &&
              collective_ir.symbols.value(
                  collective_ir.anchors.row(AnchorId(0)).symbol_id) ==
                  "AllReduce",
          "NCCL kernel did not become a normalized collective anchor");

  const std::string malformed_path = temp_db_path("_malformed");
  create_db(malformed_path,
            "CREATE TABLE CUPTI_ACTIVITY_KIND_KERNEL("
            "start INTEGER, end INTEGER, deviceId INTEGER);"
            "INSERT INTO CUPTI_ACTIVITY_KIND_KERNEL VALUES (10, 20, 0);");
  const CudaNsightSQLiteInventory malformed_inventory =
      inspect_cuda_nsys_sqlite_profile(malformed_path);
  require(malformed_inventory.missing_required_kernel_columns.size() == 1 &&
              malformed_inventory.missing_required_kernel_columns.front() ==
                  "streamId",
          "inventory did not identify the missing required CUDA field");
  require_throws_with("missing required column(s): streamId", [&]() {
    (void)CudaNsightSQLiteAdapter(malformed_path).load();
  });

  const std::string malformed_aux_path = temp_db_path("_malformed_aux");
  create_db(malformed_aux_path,
            "CREATE TABLE CUPTI_ACTIVITY_KIND_KERNEL("
            "start INTEGER, end INTEGER, deviceId INTEGER, streamId INTEGER);"
            "INSERT INTO CUPTI_ACTIVITY_KIND_KERNEL VALUES (10, 20, 0, 1);"
            "CREATE TABLE CUPTI_ACTIVITY_KIND_MEMCPY("
            "end INTEGER, deviceId INTEGER, streamId INTEGER);"
            "INSERT INTO CUPTI_ACTIVITY_KIND_MEMCPY VALUES (30, 0, 1);");
  require_throws_with("CUPTI_ACTIVITY_KIND_MEMCPY schema: missing start", [&]() {
    (void)CudaNsightSQLiteAdapter(malformed_aux_path).load();
  });

  if (!keep_fixture) {
    std::remove(db_path.c_str());
  }
  std::remove(fallback_path.c_str());
  std::remove(collective_path.c_str());
  std::remove(malformed_path.c_str());
  std::remove(malformed_aux_path.c_str());
  return 0;
}
