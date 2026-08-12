#include <sqlite3.h>

#include <algorithm>
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

enum class GraphFixtureVariant {
  kExactSchedule,
  kSingleton,
  kDuplicateLaunchCorrelation,
  kMissingBody,
  kUnsupportedGraphNodeActivity,
  kIncompleteMemcpyCapability,
  kBodyMismatch,
  kAmbiguousOriginalGraphNode,
  kMissingOriginalGraphNodeColumn,
};

void create_graph_node_db(const std::string& path,
                          GraphFixtureVariant variant) {
  std::ostringstream sql;
  sql << "CREATE TABLE StringIds(id INTEGER PRIMARY KEY, value TEXT);"
         "INSERT INTO StringIds VALUES "
         "(1, 'cudaGraphLaunch_v10000'),"
         "(2, 'graph_a_gemm'), (3, 'graph_a_pointwise'),"
         "(4, 'graph_b_gemm'), (5, 'graph_b_pointwise');"
         "CREATE TABLE CUPTI_ACTIVITY_KIND_RUNTIME("
         "start INTEGER, end INTEGER, correlationId INTEGER, nameId INTEGER);"
         "CREATE TABLE CUPTI_ACTIVITY_KIND_KERNEL("
         "start INTEGER, end INTEGER, deviceId INTEGER, contextId INTEGER,"
         "streamId INTEGER, correlationId INTEGER, graphNodeId INTEGER,"
         "shortName INTEGER);";
  sql << "CREATE TABLE CUPTI_ACTIVITY_KIND_MEMCPY("
         "start INTEGER, end INTEGER, deviceId INTEGER, contextId INTEGER,"
         "streamId INTEGER, correlationId INTEGER, graphNodeId INTEGER,"
         "bytes INTEGER";
  if (variant != GraphFixtureVariant::kIncompleteMemcpyCapability) {
    sql << ", copyKind INTEGER";
  }
  sql << ");";
  if (variant == GraphFixtureVariant::kMissingOriginalGraphNodeColumn) {
    sql << "CREATE TABLE CUDA_GRAPH_NODE_EVENTS("
           "start INTEGER, end INTEGER, graphNodeId INTEGER NOT NULL);";
  } else {
    sql << "CREATE TABLE CUDA_GRAPH_NODE_EVENTS("
           "start INTEGER, end INTEGER, graphNodeId INTEGER NOT NULL,"
           "originalGraphNodeId INTEGER);";
  }

  const std::int64_t graph_a = 8589934592LL;
  const std::int64_t graph_b = 21474836480LL;
  const int schedule[] = {0, 1, 0, 0, 1};
  std::size_t launch_count =
      variant == GraphFixtureVariant::kBodyMismatch ? 2 : 5;
  if (variant == GraphFixtureVariant::kSingleton ||
      variant == GraphFixtureVariant::kDuplicateLaunchCorrelation ||
      variant == GraphFixtureVariant::kMissingBody ||
      variant == GraphFixtureVariant::kUnsupportedGraphNodeActivity ||
      variant == GraphFixtureVariant::kIncompleteMemcpyCapability) {
    launch_count = 1;
  }
  for (std::size_t index = 0; index < launch_count; ++index) {
    const std::int64_t correlation = 101 + static_cast<std::int64_t>(index);
    const std::int64_t base = 1000 + static_cast<std::int64_t>(index) * 100;
    const bool graph_b_case =
        variant != GraphFixtureVariant::kBodyMismatch && schedule[index] == 1;
    const std::int64_t graph_node_base = graph_b_case ? graph_b : graph_a;
    const int gemm_name = graph_b_case ? 4 : 2;
    const int pointwise_name =
        variant == GraphFixtureVariant::kBodyMismatch && index == 1
            ? 5
            : (graph_b_case ? 5 : 3);
    sql << "INSERT INTO CUPTI_ACTIVITY_KIND_RUNTIME VALUES(" << base << ','
        << base + 5 << ',' << correlation << ",1);";
    if (variant == GraphFixtureVariant::kDuplicateLaunchCorrelation) {
      sql << "INSERT INTO CUPTI_ACTIVITY_KIND_RUNTIME VALUES(" << base + 1
          << ',' << base + 6 << ',' << correlation << ",1);";
    }
    if (variant != GraphFixtureVariant::kMissingBody) {
      sql << "INSERT INTO CUPTI_ACTIVITY_KIND_KERNEL VALUES("
          << base + 10 << ',' << base + 20 << ",0,7,11," << correlation
          << ',' << graph_node_base << ',' << gemm_name << ");"
          << "INSERT INTO CUPTI_ACTIVITY_KIND_KERNEL VALUES("
          << base + 21 << ',' << base + 25 << ",0,7,11," << correlation
          << ',' << graph_node_base + 1 << ',' << pointwise_name << ");"
          << "INSERT INTO CUPTI_ACTIVITY_KIND_KERNEL VALUES("
          << base + 26 << ',' << base + 36 << ",0,7,11," << correlation
          << ',' << graph_node_base + 2 << ',' << gemm_name << ");"
          << "INSERT INTO CUPTI_ACTIVITY_KIND_MEMCPY VALUES("
          << base + 37 << ',' << base + 42 << ",0,7,11," << correlation
          << ',' << graph_node_base + 3 << ','
          << (graph_b_case ? 3276800 : 2097152);
      if (variant != GraphFixtureVariant::kIncompleteMemcpyCapability) {
        sql << ",8";
      }
      sql << ");";
    }
  }
  if (variant == GraphFixtureVariant::kMissingOriginalGraphNodeColumn) {
    // The table stays non-empty so graph-node reconstruction runs; only the
    // optional originalGraphNodeId column is absent.
    for (int node = 0; node < 4; ++node) {
      sql << "INSERT INTO CUDA_GRAPH_NODE_EVENTS VALUES(" << 100 + node
          << ',' << 100 + node << ',' << graph_a + node << ");"
          << "INSERT INTO CUDA_GRAPH_NODE_EVENTS VALUES(" << 200 + node
          << ',' << 200 + node << ',' << graph_b + node << ");";
    }
  } else {
    for (int node = 0; node < 4; ++node) {
      sql << "INSERT INTO CUDA_GRAPH_NODE_EVENTS VALUES(" << 100 + node
          << ',' << 100 + node << ',' << graph_a + node << ','
          << 5000 + node << ");"
          << "INSERT INTO CUDA_GRAPH_NODE_EVENTS VALUES(" << 200 + node
          << ',' << 200 + node << ',' << graph_b + node << ','
          << 6000 + node << ");";
    }
  }
  if (variant == GraphFixtureVariant::kAmbiguousOriginalGraphNode) {
    // graph_a+1 now maps to two distinct originals: ambiguous, so the
    // original identity for that raw node must stay unmapped (fail closed).
    sql << "INSERT INTO CUDA_GRAPH_NODE_EVENTS VALUES(150, 150, "
        << graph_a + 1 << ", 777);";
  }
  if (variant == GraphFixtureVariant::kUnsupportedGraphNodeActivity) {
    sql << "CREATE TABLE CUPTI_ACTIVITY_KIND_MEMSET("
           "start INTEGER, end INTEGER, graphNodeId INTEGER);"
           "INSERT INTO CUPTI_ACTIVITY_KIND_MEMSET VALUES(10, 11, "
        << graph_a << ");";
  }
  create_db(path, sql.str().c_str());
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
      "CREATE TABLE ENUM_CUPTI_SYNC_TYPE("
      "id INTEGER PRIMARY KEY, name TEXT, label TEXT);"
      "INSERT INTO ENUM_CUPTI_SYNC_TYPE VALUES "
      "(2, 'CUPTI_ACTIVITY_SYNCHRONIZATION_TYPE_STREAM_WAIT_EVENT', "
      "'Stream wait sync');"
      "CREATE TABLE CUPTI_ACTIVITY_KIND_SYNCHRONIZATION("
      "start INTEGER, end INTEGER, deviceId INTEGER, contextId INTEGER, "
      "streamId INTEGER, correlationId INTEGER, syncType INTEGER, "
      "eventId INTEGER);"
      "INSERT INTO CUPTI_ACTIVITY_KIND_SYNCHRONIZATION VALUES "
      "(87, 88, 0, 1, 7, 10, 2, 3);"
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
  require(inventory.present_activity_tables.size() == 5 &&
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
  require(ir.runtime_calls.size() == 1,
          "CUDA runtime activity was not retained as host-side evidence");
  require(ir.runtime_calls.row(RuntimeCallId(0)).raw_correlation_id == 9 &&
              ir.runtime_calls.row(RuntimeCallId(0)).start_ns == 80 &&
              ir.runtime_calls.row(RuntimeCallId(0)).end_ns == 85,
          "CUDA runtime-call correlation/timing mismatch");

  const std::string expected =
      "sources=5\n"
      "source[0]=cuda_nsys_sqlite_test|CUPTI_ACTIVITY_KIND_KERNEL\n"
      "source[1]=cuda_nsys_sqlite_test|CUPTI_ACTIVITY_KIND_RUNTIME\n"
      "source[2]=cuda_nsys_sqlite_test|CUPTI_ACTIVITY_KIND_MEMCPY\n"
      "source[3]=cuda_nsys_sqlite_test|CUPTI_ACTIVITY_KIND_SYNCHRONIZATION\n"
      "source[4]=cuda_nsys_sqlite_test|CUPTI_ACTIVITY_KIND_GRAPH_TRACE\n"
      "streams=8\n"
      "stream[0]=0:7\n"
      "stream[1]=0:9\n"
      "stream[2]=1:4\n"
      "stream[3]=0:0\n"
      "stream[4]=0:7\n"
      "stream[5]=0:7\n"
      "stream[6]=0:7\n"
      "stream[7]=1:4\n"
      "events=8\n"
      "event[0]=2|0:7|100-140|flash_fwd_kernel_bf16\n"
      "event[1]=3|0:9|200-215|vectorized_elementwise_kernel\n"
      "event[2]=1|1:4|300-360|void cutlass_gemm_kernel()\n"
      "event[3]=1|0:0|80-85|kernel\n"
      "event[4]=1|0:7|86-89|CudaMemcpy_1\n"
      "event[5]=1|0:7|87-88|STREAM_WAIT_EVENT\n"
      "event[6]=1|0:7|90-150|CudaGraphReplay T1\n"
      "event[7]=2|1:4|290-370|CudaGraphReplay T1\n"
      "tasks=6\n"
      "task[0]=11|11|CUDA_KERNEL|flash_fwd_kernel_bf16|CudaFlashAttention\n"
      "task[1]=22|22|CUDA_KERNEL_AUX|vectorized_elementwise_kernel|CudaAux:Pointwise\n"
      "task[2]=33|33|CUDA_KERNEL|void cutlass_gemm_kernel()|CudaMatMul\n"
      "task[3]=9|9|CUDA_RUNTIME_AUX|kernel|CUDA_RUNTIME_AUX\n"
      "task[4]=10|10|CUDA_MEMCPY_AUX|CudaMemcpy_1|CUDA_MEMCPY_AUX\n"
      "task[5]=10|10|CUDA_SYNC_AUX|STREAM_WAIT_EVENT|CUDA_SYNC_AUX\n";
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

  const std::string ambiguous_communication_path =
      temp_db_path("_ambiguous_communication");
  create_db(
      ambiguous_communication_path,
      "CREATE TABLE StringIds(id INTEGER PRIMARY KEY, value TEXT);"
      "INSERT INTO StringIds VALUES "
      "(1, 'broadcast_kernel'), (2, 'ncclDevKernel_SendRecv');"
      "CREATE TABLE CUPTI_ACTIVITY_KIND_KERNEL("
      "start INTEGER, end INTEGER, deviceId INTEGER, streamId INTEGER, "
      "correlationId INTEGER, shortName INTEGER);"
      "INSERT INTO CUPTI_ACTIVITY_KIND_KERNEL VALUES "
      "(10, 20, 0, 7, 41, 1), (30, 40, 0, 7, 42, 2);");
  NativeIr ambiguous_communication_ir =
      CudaNsightSQLiteAdapter(ambiguous_communication_path).load();
  require(ambiguous_communication_ir.tasks.size() == 2 &&
              ambiguous_communication_ir.communication_ops.empty(),
          "ambiguous CUDA kernel names became communication evidence");
  for (const TaskRow& task : ambiguous_communication_ir.tasks.rows()) {
    require(task.compute_task_type_symbol_id.valid() &&
                !task.comm_name_symbol_id.valid() &&
                !task.communication_task_type_symbol_id.valid(),
            "ambiguous CUDA kernel task was promoted to communication");
  }
  const FlatAnchorBuildStats ambiguous_communication_anchor_stats =
      build_flat_anchors(ambiguous_communication_ir,
                         collective_anchor_config);
  require(ambiguous_communication_anchor_stats.communication_anchors == 0 &&
              ambiguous_communication_anchor_stats.device_event_anchors == 2,
          "ambiguous CUDA kernel names became collective anchors");

  const std::string graph_exact_path = temp_db_path("_graph_exact");
  create_graph_node_db(graph_exact_path, GraphFixtureVariant::kExactSchedule);
  const NativeIr graph_exact_ir =
      CudaNsightSQLiteAdapter(graph_exact_path).load();
  require(graph_exact_ir.graph_launch_occurrences.size() == 5 &&
              graph_exact_ir.graph_launch_bodies.size() == 5 &&
              graph_exact_ir.replay_body_templates.size() == 2,
          "direct CUDA graph node evidence did not materialize five bodies");
  require(graph_exact_ir.replay_composition_candidates.size() == 5 &&
              graph_exact_ir.replay_composition_slots.size() == 5 &&
              graph_exact_ir.replay_composition_regions.size() == 5 &&
              graph_exact_ir.replay_composition_region_members.size() == 5,
          "direct CUDA graph occurrences did not retain composition evidence");
  require(graph_exact_ir.graph_templates.size() == 2 &&
              graph_exact_ir.replay_units.size() == 5 &&
              graph_exact_ir.replay_unit_launch_members.size() == 5,
          "repeated CUDA graph bodies were not promoted to exact replay units");
  for (std::size_t occurrence_index = 0;
       occurrence_index < graph_exact_ir.graph_launch_occurrences.size();
       ++occurrence_index) {
    const GraphLaunchOccurrenceRow& occurrence =
        graph_exact_ir.graph_launch_occurrences.rows()[occurrence_index];
    require(occurrence.match_policy ==
                    GraphLaunchMatchPolicy::kCudaRuntimeCorrelation &&
                occurrence.instance_association_policy ==
                    GraphLaunchInstanceAssociationPolicy::kCudaGraphNodeSet,
            "CUDA graph occurrence lost its direct correlation provenance");
    require(occurrence.raw_launch_connection_id ==
                101 + static_cast<std::int64_t>(occurrence_index),
            "CUDA graph occurrence lost its runtime correlation identity");
  }
  for (const ReplayBodyTemplateRow& body :
       graph_exact_ir.replay_body_templates.rows()) {
    require(body.compute_task_count == 3 &&
                body.communication_task_count == 0 &&
                body.stream_count == 1 &&
                body.topology_policy ==
                    ReplayBodyTopologyPolicy::kObservedStreamSetUnordered,
            "CUDA visible body summary or topology policy is incorrect");
  }
  for (const ReplayCompositionCandidateRow& candidate :
       graph_exact_ir.replay_composition_candidates.rows()) {
    require(candidate.identity_policy ==
                    ReplayCompositionIdentityPolicy::kCudaGraphNodeSet &&
                candidate.order_policy ==
                    ReplayCompositionOrderPolicy::kHostSubmissionOrder &&
                candidate.shape_policy ==
                    ReplayCompositionShapePolicy::kSingleGraph &&
                candidate.boundary_policy ==
                    ReplayCompositionBoundaryPolicy::
                        kDirectObservedGraphLaunch,
            "CUDA exact composition policy is not direct and fail-closed");
  }
  for (const ReplayCompositionSlotRow& slot :
       graph_exact_ir.replay_composition_slots.rows()) {
    require(slot.role == ReplayCompositionSlotRole::kCudaGraph &&
                slot.replay_body_template_id.valid(),
            "CUDA exact composition slot lost its body identity");
  }
  for (const ReplayCompositionRegionRow& region :
       graph_exact_ir.replay_composition_regions.rows()) {
    require(region.status ==
                ReplayCompositionRegionStatus::kRecognizedCompletePattern,
            "repeated direct CUDA graph body was not recognized");
  }
  const auto& exact_bodies = graph_exact_ir.graph_launch_bodies.rows();
  std::size_t expected_body_members = 0;
  for (const GraphLaunchBodyRow& body : exact_bodies) {
    expected_body_members +=
        body.compute_task_count + body.communication_task_count +
        body.data_move_task_count;
  }
  require(graph_exact_ir.graph_launch_body_members.size() ==
              expected_body_members,
          "CUDA graph launch body members lost exact child provenance");
  require(std::any_of(
              graph_exact_ir.graph_launch_body_members.rows().begin(),
              graph_exact_ir.graph_launch_body_members.rows().end(),
              [](const GraphLaunchBodyMemberRow& member) {
                return member.kind ==
                       GraphLaunchBodyMemberRow::Kind::kDataMove;
              }),
          "CUDA graph launch body members lost memcpy evidence");
  const std::int64_t graph_a = 8589934592LL;
  const std::int64_t graph_b = 21474836480LL;
  for (const GraphLaunchBodyMemberRow& member :
       graph_exact_ir.graph_launch_body_members.rows()) {
    require(member.raw_graph_node_id >= graph_a &&
                member.raw_graph_node_id < graph_b + 4,
            "CUDA graph body member lost its raw graphNodeId");
    const std::int64_t expected_original =
        member.raw_graph_node_id < graph_b
            ? 5000 + (member.raw_graph_node_id - graph_a)
            : 6000 + (member.raw_graph_node_id - graph_b);
    require(member.original_graph_node_id == expected_original,
            "CUDA graph body member lost its unique originalGraphNodeId");
  }
  require(exact_bodies[0].replay_body_template_id ==
                  exact_bodies[2].replay_body_template_id &&
              exact_bodies[0].replay_body_template_id ==
                  exact_bodies[3].replay_body_template_id &&
              exact_bodies[1].replay_body_template_id ==
                  exact_bodies[4].replay_body_template_id &&
              exact_bodies[0].replay_body_template_id !=
                  exact_bodies[1].replay_body_template_id,
          "CUDA graph body schedule did not preserve A/B/A/A/B identity");
  const NativeIr graph_exact_reload =
      CudaNsightSQLiteAdapter(graph_exact_path).load();
  for (std::size_t index = 0; index < exact_bodies.size(); ++index) {
    require(graph_exact_reload.graph_launch_bodies.rows()[index]
                    .replay_body_template_id ==
                exact_bodies[index].replay_body_template_id &&
                graph_exact_reload.replay_composition_candidates.rows()[index]
                        .pattern_sequence_hash ==
                    graph_exact_ir.replay_composition_candidates.rows()[index]
                        .pattern_sequence_hash,
            "CUDA graph identity changed between identical loads");
  }

  const std::string graph_singleton_path =
      temp_db_path("_graph_singleton");
  create_graph_node_db(graph_singleton_path,
                       GraphFixtureVariant::kSingleton);
  const NativeIr graph_singleton_ir =
      CudaNsightSQLiteAdapter(graph_singleton_path).load();
  require(graph_singleton_ir.graph_launch_bodies.size() == 1 &&
              graph_singleton_ir.replay_units.empty() &&
              graph_singleton_ir.replay_composition_regions.size() == 1 &&
              graph_singleton_ir.replay_composition_regions.rows()[0].status ==
                  ReplayCompositionRegionStatus::
                      kUnrecognizedInsufficientRepeatEvidence,
          "singleton CUDA graph body was not kept as typed unknown evidence");

  const std::string graph_duplicate_path =
      temp_db_path("_graph_duplicate");
  create_graph_node_db(graph_duplicate_path,
                       GraphFixtureVariant::kDuplicateLaunchCorrelation);
  const NativeIr graph_duplicate_ir =
      CudaNsightSQLiteAdapter(graph_duplicate_path).load();
  require(graph_duplicate_ir.graph_launch_occurrences.size() == 2 &&
              graph_duplicate_ir.graph_launch_bodies.empty() &&
              graph_duplicate_ir.replay_units.empty(),
          "ambiguous CUDA launch correlation fabricated exact body evidence");
  for (const ReplayCompositionRegionRow& region :
       graph_duplicate_ir.replay_composition_regions.rows()) {
    require(region.status == ReplayCompositionRegionStatus::
                                 kUnrecognizedAmbiguousLaunchEvidence,
            "ambiguous CUDA launch correlation lost its typed status");
  }

  const std::string graph_missing_path = temp_db_path("_graph_missing");
  create_graph_node_db(graph_missing_path, GraphFixtureVariant::kMissingBody);
  const NativeIr graph_missing_ir =
      CudaNsightSQLiteAdapter(graph_missing_path).load();
  require(graph_missing_ir.graph_launch_occurrences.size() == 1 &&
              graph_missing_ir.graph_launch_bodies.empty() &&
              graph_missing_ir.replay_units.empty() &&
              graph_missing_ir.replay_composition_regions.rows()[0].status ==
                  ReplayCompositionRegionStatus::
                      kUnrecognizedMissingBodyEvidence,
          "CUDA launch without children did not remain typed unknown");

  const std::string graph_unsupported_path =
      temp_db_path("_graph_unsupported");
  create_graph_node_db(graph_unsupported_path,
                       GraphFixtureVariant::kUnsupportedGraphNodeActivity);
  const NativeIr graph_unsupported_ir =
      CudaNsightSQLiteAdapter(graph_unsupported_path).load();
  require(graph_unsupported_ir.graph_launch_bodies.empty() &&
              graph_unsupported_ir.replay_units.empty() &&
              graph_unsupported_ir.replay_composition_regions.rows()[0]
                      .status ==
                  ReplayCompositionRegionStatus::
                      kUnrecognizedMissingBodyCapability,
          "unsupported graph-node activity table did not fail closed");

  const std::string graph_incomplete_memcpy_path =
      temp_db_path("_graph_incomplete_memcpy");
  create_graph_node_db(graph_incomplete_memcpy_path,
                       GraphFixtureVariant::kIncompleteMemcpyCapability);
  const NativeIr graph_incomplete_memcpy_ir =
      CudaNsightSQLiteAdapter(graph_incomplete_memcpy_path).load();
  require(graph_incomplete_memcpy_ir.graph_launch_bodies.empty() &&
              graph_incomplete_memcpy_ir.replay_units.empty() &&
              graph_incomplete_memcpy_ir.replay_composition_regions.rows()[0]
                      .status ==
                  ReplayCompositionRegionStatus::
                      kUnrecognizedMissingBodyCapability,
          "incomplete supported graph-node schema did not fail closed");

  const std::string graph_body_mismatch_path =
      temp_db_path("_graph_body_mismatch");
  create_graph_node_db(graph_body_mismatch_path,
                       GraphFixtureVariant::kBodyMismatch);
  const NativeIr graph_body_mismatch_ir =
      CudaNsightSQLiteAdapter(graph_body_mismatch_path).load();
  require(graph_body_mismatch_ir.graph_launch_bodies.size() == 2 &&
              graph_body_mismatch_ir.replay_units.empty(),
          "contradictory CUDA bodies for one node set became exact units");
  for (const ReplayCompositionRegionRow& region :
       graph_body_mismatch_ir.replay_composition_regions.rows()) {
    require(region.status ==
                ReplayCompositionRegionStatus::kUnrecognizedBodyMismatch,
            "contradictory CUDA graph body lost its typed mismatch status");
  }

  const std::string graph_ambiguous_original_path =
      temp_db_path("_graph_ambiguous_original");
  create_graph_node_db(graph_ambiguous_original_path,
                       GraphFixtureVariant::kAmbiguousOriginalGraphNode);
  const NativeIr graph_ambiguous_original_ir =
      CudaNsightSQLiteAdapter(graph_ambiguous_original_path).load();
  require(graph_ambiguous_original_ir.graph_launch_body_members.size() ==
              graph_exact_ir.graph_launch_body_members.size(),
          "ambiguous original mapping changed exact body membership");
  const std::int64_t graph_a_node_id = 8589934592LL;
  const std::int64_t graph_b_node_id = 21474836480LL;
  std::size_t ambiguous_member_count = 0;
  for (const GraphLaunchBodyMemberRow& member :
       graph_ambiguous_original_ir.graph_launch_body_members.rows()) {
    if (member.raw_graph_node_id == graph_a_node_id + 1) {
      require(member.original_graph_node_id == -1,
              "ambiguous originalGraphNodeId was guessed instead of "
              "left unmapped");
      ++ambiguous_member_count;
    } else if (member.raw_graph_node_id == graph_a_node_id) {
      require(member.original_graph_node_id == 5000,
              "unique originalGraphNodeId mapping was lost");
    }
  }
  require(ambiguous_member_count > 0,
          "ambiguous original mapping fixture did not exercise the node");

  const std::string graph_missing_original_column_path =
      temp_db_path("_graph_missing_original_column");
  create_graph_node_db(graph_missing_original_column_path,
                       GraphFixtureVariant::kMissingOriginalGraphNodeColumn);
  const NativeIr graph_missing_original_column_ir =
      CudaNsightSQLiteAdapter(graph_missing_original_column_path).load();
  require(graph_missing_original_column_ir.replay_units.size() ==
                  graph_exact_ir.replay_units.size() &&
              graph_missing_original_column_ir.graph_launch_bodies.size() ==
                  graph_exact_ir.graph_launch_bodies.size(),
          "missing originalGraphNodeId column regressed exact replay "
          "reconstruction");
  require(graph_missing_original_column_ir.graph_launch_body_members.size() ==
              graph_exact_ir.graph_launch_body_members.size(),
          "missing originalGraphNodeId column changed exact body membership");
  std::size_t missing_original_member_count = 0;
  for (const GraphLaunchBodyMemberRow& member :
       graph_missing_original_column_ir.graph_launch_body_members.rows()) {
    if (member.raw_graph_node_id >= graph_a_node_id &&
        member.raw_graph_node_id < graph_b_node_id + 4) {
      ++missing_original_member_count;
    }
    require(member.original_graph_node_id == -1,
            "missing originalGraphNodeId column was guessed instead of "
            "left unmapped");
  }
  require(missing_original_member_count ==
              graph_exact_ir.graph_launch_body_members.size(),
          "missing originalGraphNodeId fixture lost raw graphNodeId "
          "evidence");

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

  const std::string partial_cuda_event_path =
      temp_db_path("_partial_cuda_event");
  create_db(partial_cuda_event_path,
            "CREATE TABLE CUPTI_ACTIVITY_KIND_KERNEL("
            "start INTEGER, end INTEGER, deviceId INTEGER, streamId INTEGER);"
            "INSERT INTO CUPTI_ACTIVITY_KIND_KERNEL VALUES (10, 20, 0, 1);"
            "CREATE TABLE CUPTI_ACTIVITY_KIND_CUDA_EVENT("
            "end INTEGER, deviceId INTEGER, contextId INTEGER, "
            "streamId INTEGER, eventId INTEGER);"
            "INSERT INTO CUPTI_ACTIVITY_KIND_CUDA_EVENT VALUES "
            "(30, 0, 1, 7, 3);");
  require_throws_with("end without start or timestamp", [&]() {
    (void)CudaNsightSQLiteAdapter(partial_cuda_event_path).load();
  });

  const std::string malformed_cuda_event_path =
      temp_db_path("_malformed_cuda_event");
  create_db(malformed_cuda_event_path,
            "CREATE TABLE CUPTI_ACTIVITY_KIND_KERNEL("
            "start INTEGER, end INTEGER, deviceId INTEGER, streamId INTEGER);"
            "INSERT INTO CUPTI_ACTIVITY_KIND_KERNEL VALUES (10, 20, 0, 1);"
            "CREATE TABLE CUPTI_ACTIVITY_KIND_CUDA_EVENT("
            "deviceId INTEGER, eventId INTEGER);"
            "INSERT INTO CUPTI_ACTIVITY_KIND_CUDA_EVENT VALUES (0, 3);");
  require_throws_with(
      "missing identity column(s): contextId, streamId", [&]() {
        (void)CudaNsightSQLiteAdapter(malformed_cuda_event_path).load();
      });

  if (!keep_fixture) {
    std::remove(db_path.c_str());
  }
  std::remove(fallback_path.c_str());
  std::remove(collective_path.c_str());
  std::remove(ambiguous_communication_path.c_str());
  std::remove(graph_exact_path.c_str());
  std::remove(graph_singleton_path.c_str());
  std::remove(graph_duplicate_path.c_str());
  std::remove(graph_missing_path.c_str());
  std::remove(graph_unsupported_path.c_str());
  std::remove(graph_incomplete_memcpy_path.c_str());
  std::remove(graph_body_mismatch_path.c_str());
  std::remove(malformed_path.c_str());
  std::remove(malformed_aux_path.c_str());
  std::remove(partial_cuda_event_path.c_str());
  std::remove(malformed_cuda_event_path.c_str());
  return 0;
}
