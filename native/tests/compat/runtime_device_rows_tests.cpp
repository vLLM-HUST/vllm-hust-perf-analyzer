#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/aux_attribution_rows.h"
#include "traceloom/compat/runtime_device_rows.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

std::string temp_db_path() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return (std::filesystem::temp_directory_path() /
          ("traceloom_runtime_device_" + std::to_string(now) + ".db"))
      .string();
}

int scalar_int(const std::string& path, const std::string& sql) {
  sqlite3* db = nullptr;
  traceloom::testing::require(
      sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) ==
      SQLITE_OK);
  sqlite3_stmt* stmt = nullptr;
  traceloom::testing::require(
      sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK);
  traceloom::testing::require(sqlite3_step(stmt) == SQLITE_ROW);
  const int value = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return value;
}

std::string scalar_text(const std::string& path, const std::string& sql) {
  sqlite3* db = nullptr;
  traceloom::testing::require(
      sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) ==
      SQLITE_OK);
  sqlite3_stmt* stmt = nullptr;
  traceloom::testing::require(
      sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK);
  traceloom::testing::require(sqlite3_step(stmt) == SQLITE_ROW);
  const unsigned char* raw = sqlite3_column_text(stmt, 0);
  const std::string value =
      raw == nullptr ? std::string() : reinterpret_cast<const char*>(raw);
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return value;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  NativeIr ir;
  const SourceRefId runtime_source = ir.source_refs.append(
      "cuda_nsys_sqlite", "memory", "CUPTI_ACTIVITY_KIND_RUNTIME", 0);
  const SourceRefId kernel_source = ir.source_refs.append(
      "cuda_nsys_sqlite", "memory", "CUPTI_ACTIVITY_KIND_KERNEL", 0);
  const SymbolId runtime_type = ir.symbols.intern("cuda_runtime");
  const SymbolId left_api = ir.symbols.intern("cudaLaunchKernel_left");
  const SymbolId middle_api = ir.symbols.intern("cudaEventQuery");
  const SymbolId right_api = ir.symbols.intern("cudaLaunchKernel_right");
  const SymbolId kernel = ir.symbols.intern("kernel");

  ir.runtime_calls.append(
      runtime_source, 1, RuntimeCallProvider::kCuda,
      RuntimeCallClockDomain::kProfilerHost,
      RuntimeCallMatchPolicy::kCudaCorrelationId, 100, 110, 41, runtime_type,
      left_api, 7, 9);
  ir.runtime_calls.append(
      runtime_source, 2, RuntimeCallProvider::kCuda,
      RuntimeCallClockDomain::kProfilerHost,
      RuntimeCallMatchPolicy::kCudaCorrelationId, 150, 160, 99, runtime_type,
      middle_api, 7, 9);
  ir.runtime_calls.append(
      runtime_source, 3, RuntimeCallProvider::kCuda,
      RuntimeCallClockDomain::kProfilerHost,
      RuntimeCallMatchPolicy::kCudaCorrelationId, 200, 210, 42, runtime_type,
      right_api, 7, 9);
  ir.runtime_calls.append(
      runtime_source, 4, RuntimeCallProvider::kCuda,
      RuntimeCallClockDomain::kProfilerHost,
      RuntimeCallMatchPolicy::kCudaCorrelationId, 300, 310, -1, runtime_type,
      middle_api, 7, 9);
  ir.runtime_calls.append(
      runtime_source, 5, RuntimeCallProvider::kCuda,
      RuntimeCallClockDomain::kProfilerHost,
      RuntimeCallMatchPolicy::kCudaCorrelationId, 400, 410, 55, runtime_type,
      middle_api, 7, 9, -1, 8);

  const TraceEventId first =
      ir.trace_events.append(kernel_source, 10, 0, 5, 1000, 1100, kernel);
  const TraceEventId second =
      ir.trace_events.append(kernel_source, 11, 0, 5, 2000, 2100, kernel);
  const TraceEventId auxiliary =
      ir.trace_events.append(kernel_source, 12, 0, 7, 1500, 1550, kernel);
  const TraceEventId context_mismatch =
      ir.trace_events.append(kernel_source, 13, 0, 7, 2500, 2550, kernel);
  ir.tasks.append(kernel_source, first, 1, 1, 41, kernel, kernel, kernel,
                  kernel, SymbolId::invalid(), -1, SymbolId::invalid(), 7, 3);
  ir.tasks.append(kernel_source, second, 2, 2, 42, kernel, kernel, kernel,
                  kernel, SymbolId::invalid(), -1, SymbolId::invalid(), 7, 3);
  ir.tasks.append(kernel_source, auxiliary, 3, 3, 41, kernel, kernel, kernel,
                  kernel, SymbolId::invalid(), -1, SymbolId::invalid(), 7, 4);
  ir.tasks.append(kernel_source, context_mismatch, 4, 4, 55, kernel, kernel,
                  kernel, kernel, SymbolId::invalid(), -1,
                  SymbolId::invalid(), 7, 9);
  const AnchorId first_anchor = ir.anchors.append(
      kernel_source, first, ReplayUnitId::invalid(), AnchorKind::kDeviceEvent,
      kernel, 0, 5, 1000, 1100);
  const AnchorId second_anchor = ir.anchors.append(
      kernel_source, second, ReplayUnitId::invalid(), AnchorKind::kDeviceEvent,
      kernel, 0, 5, 2000, 2100);
  ir.tokens.append(first_anchor, kernel, 0, 0, 1000, 1100);
  ir.tokens.append(second_anchor, kernel, 0, 1, 2000, 2100);

  const compat::RuntimeDeviceSqlRows rows =
      compat::build_runtime_device_sql_rows(ir, 3);
  require(rows.runtime_calls.size() == 5);
  require(rows.device_works.size() == 4);
  require(rows.relations.size() == 6);
  require(rows.relations[0].support_state == "supported_exact");
  require(rows.relations[0].cardinality == "one_to_many");
  require(rows.relations[1].support_state == "supported_exact");
  require(rows.relations[1].cardinality == "one_to_many");
  require(rows.relations[2].support_state == "supported_exact");
  require(rows.relations[3].support_state == "context_identity_mismatch");
  require(rows.relations[4].support_state == "unmatched_runtime_call");
  require(rows.relations[5].support_state == "missing_runtime_identifier");
  require(rows.anchor_relations.size() == 2);
  require(rows.host_intervals.size() == 1);
  require(rows.host_intervals[0].support_state == "supported_ordered");
  require(rows.host_intervals[0].scope_policy == "same_thread");
  require(rows.host_activities.size() == 1);

  // A graph DeviceWork locator must point to its device-side execute row. The
  // host launch remains reachable through the relation's runtime endpoint.
  NativeIr graph_ir;
  const SourceRefId graph_host_source = graph_ir.source_refs.append(
      "ascend_sqlite", "memory", "CANN_API", 0);
  const SourceRefId graph_task_source = graph_ir.source_refs.append(
      "ascend_sqlite", "memory", "TASK", 0);
  const SymbolId graph_api = graph_ir.symbols.intern("aclmdlRICaptureTask");
  const SymbolId graph_type = graph_ir.symbols.intern("cann_api");
  const SymbolId graph_execute = graph_ir.symbols.intern("MODEL_EXECUTE");
  graph_ir.runtime_calls.append(
      graph_host_source, 77, RuntimeCallProvider::kAscend,
      RuntimeCallClockDomain::kProfilerHost,
      RuntimeCallMatchPolicy::kAscendConnectionId, 100, 110, 500,
      graph_type, graph_api, 7, 9);
  const TraceEventId graph_event = graph_ir.trace_events.append(
      graph_task_source, 88, 0, 5, 1000, 2000, graph_execute);
  const TaskId graph_task = graph_ir.tasks.append(
      graph_task_source, graph_event, 8, 8, 500, graph_execute,
      graph_execute, graph_execute, graph_execute, SymbolId::invalid(), -1,
      SymbolId::invalid(), 7, 3);
  graph_ir.graph_launch_occurrences.append(
      graph_task_source, graph_host_source, 0, 77, 500, -1, -1,
      StreamId::invalid(), StreamId::invalid(),
      CapturedGraphInstanceId::invalid(), graph_task, TaskId::invalid(),
      TaskId::invalid(), 1000, 2000, -1,
      GraphLaunchMatchPolicy::kNotifyCompletionAdjacent);
  const compat::RuntimeDeviceSqlRows graph_rows =
      compat::build_runtime_device_sql_rows(graph_ir);
  require(graph_rows.device_works.size() == 2);
  require(graph_rows.device_works[1].work_kind == "graph_launch");
  require(graph_rows.device_works[1].source_table == "TASK");
  require(graph_rows.device_works[1].source_key == "88");
  require(graph_rows.relations.back().support_state ==
          "supported_deterministic");

  NativeIr ambiguous_ir;
  const SourceRefId ambiguous_runtime_source =
      ambiguous_ir.source_refs.append(
          "cuda_nsys_sqlite", "memory", "CUPTI_ACTIVITY_KIND_RUNTIME", 0);
  const SourceRefId ambiguous_kernel_source = ambiguous_ir.source_refs.append(
      "cuda_nsys_sqlite", "memory", "CUPTI_ACTIVITY_KIND_KERNEL", 0);
  const SymbolId ambiguous_symbol = ambiguous_ir.symbols.intern("kernel");
  const SymbolId ambiguous_type =
      ambiguous_ir.symbols.intern("cuda_runtime");
  ambiguous_ir.runtime_calls.append(
      ambiguous_runtime_source, 1, RuntimeCallProvider::kCuda,
      RuntimeCallClockDomain::kProfilerHost,
      RuntimeCallMatchPolicy::kCudaCorrelationId, 100, 110, 1,
      ambiguous_type, ambiguous_symbol);
  ambiguous_ir.runtime_calls.append(
      ambiguous_runtime_source, 2, RuntimeCallProvider::kCuda,
      RuntimeCallClockDomain::kProfilerHost,
      RuntimeCallMatchPolicy::kCudaCorrelationId, 120, 130, 1,
      ambiguous_type, ambiguous_symbol);
  ambiguous_ir.runtime_calls.append(
      ambiguous_runtime_source, 3, RuntimeCallProvider::kCuda,
      RuntimeCallClockDomain::kProfilerHost,
      RuntimeCallMatchPolicy::kUnsupported, 140, 150, 2, ambiguous_type,
      ambiguous_symbol);
  const TraceEventId ambiguous_event = ambiguous_ir.trace_events.append(
      ambiguous_kernel_source, 1, 0, 1, 1000, 1100, ambiguous_symbol);
  ambiguous_ir.tasks.append(
      ambiguous_kernel_source, ambiguous_event, 1, 1, 1, ambiguous_symbol,
      ambiguous_symbol, ambiguous_symbol, ambiguous_symbol,
      SymbolId::invalid());
  const compat::RuntimeDeviceSqlRows ambiguous_rows =
      compat::build_runtime_device_sql_rows(ambiguous_ir);
  require(ambiguous_rows.relations.size() == 3);
  require(ambiguous_rows.relations[0].support_state ==
          "ambiguous_runtime_candidates");
  require(ambiguous_rows.relations[1].support_state ==
          "ambiguous_runtime_candidates");
  require(ambiguous_rows.relations[2].support_state ==
          "unsupported_provider_schema");

  NativeIr cross_provider_ir;
  const SourceRefId cuda_runtime = cross_provider_ir.source_refs.append(
      "cuda_nsys_sqlite", "memory", "CUPTI_ACTIVITY_KIND_RUNTIME", 0);
  const SourceRefId cuda_kernel = cross_provider_ir.source_refs.append(
      "cuda_nsys_sqlite", "memory", "CUPTI_ACTIVITY_KIND_KERNEL", 0);
  const SourceRefId ascend_runtime = cross_provider_ir.source_refs.append(
      "ascend_sqlite", "memory", "CANN_API", 0);
  const SourceRefId ascend_task = cross_provider_ir.source_refs.append(
      "ascend_sqlite", "memory", "TASK", 0);
  const SymbolId cross_symbol = cross_provider_ir.symbols.intern("work");
  cross_provider_ir.runtime_calls.append(
      cuda_runtime, 1, RuntimeCallProvider::kCuda,
      RuntimeCallClockDomain::kProfilerHost,
      RuntimeCallMatchPolicy::kCudaCorrelationId, 100, 110, 10,
      cross_symbol, cross_symbol);
  cross_provider_ir.runtime_calls.append(
      ascend_runtime, 2, RuntimeCallProvider::kAscend,
      RuntimeCallClockDomain::kProfilerHost,
      RuntimeCallMatchPolicy::kAscendConnectionId, 200, 210, 20,
      cross_symbol, cross_symbol);
  const TraceEventId cuda_event = cross_provider_ir.trace_events.append(
      cuda_kernel, 1, 0, 1, 1000, 1100, cross_symbol);
  const TraceEventId ascend_event = cross_provider_ir.trace_events.append(
      ascend_task, 2, 0, 1, 2000, 2100, cross_symbol);
  cross_provider_ir.tasks.append(cuda_kernel, cuda_event, 1, 1, 10,
                                 cross_symbol, cross_symbol, cross_symbol,
                                 cross_symbol, SymbolId::invalid());
  cross_provider_ir.tasks.append(ascend_task, ascend_event, 2, 2, 20,
                                 cross_symbol, cross_symbol, cross_symbol,
                                 cross_symbol, SymbolId::invalid());
  const AnchorId cuda_anchor = cross_provider_ir.anchors.append(
      cuda_kernel, cuda_event, ReplayUnitId::invalid(),
      AnchorKind::kDeviceEvent, cross_symbol, 0, 1, 1000, 1100);
  const AnchorId ascend_anchor = cross_provider_ir.anchors.append(
      ascend_task, ascend_event, ReplayUnitId::invalid(),
      AnchorKind::kDeviceEvent, cross_symbol, 0, 1, 2000, 2100);
  cross_provider_ir.tokens.append(cuda_anchor, cross_symbol, 0, 0, 1000,
                                  1100);
  cross_provider_ir.tokens.append(ascend_anchor, cross_symbol, 0, 1, 2000,
                                  2100);
  const compat::RuntimeDeviceSqlRows cross_rows =
      compat::build_runtime_device_sql_rows(cross_provider_ir);
  require(cross_rows.host_intervals.size() == 1);
  require(cross_rows.host_intervals[0].support_state ==
          "incompatible_host_domain");

  NativeIr invalid_ir;
  invalid_ir.runtime_calls.append(
      SourceRefId(9), 1, RuntimeCallProvider::kCuda,
      RuntimeCallClockDomain::kProfilerHost,
      RuntimeCallMatchPolicy::kCudaCorrelationId, 100, 110, 1,
      SymbolId::invalid(), SymbolId::invalid());
  bool rejected_invalid_source = false;
  try {
    (void)compat::build_runtime_device_sql_rows(invalid_ir);
  } catch (const std::invalid_argument&) {
    rejected_invalid_source = true;
  }
  require(rejected_invalid_source);

  const std::string path = temp_db_path();
  compat::materialize_compatibility_schema(path);
  compat::replace_runtime_device_rows(path, rows);
  compat::replace_anchor_rows(path,
                              compat::build_anchor_sequence_sql_rows(ir, 3));
  compat::replace_aux_attribution_rows(
      path, compat::build_aux_attribution_sql_rows(ir, 3));
  compat::NodeCoverageSqlRows node_rows;
  compat::VizNodeSqlRow node;
  node.node_id = "node-1";
  node.local_node_id = "local-node-1";
  node.db_idx = 3;
  node.device_id = 0;
  node.view_name = "anchor_tree";
  node.node_type = "leaf";
  node.kind = "leaf";
  node.symbol = "kernel";
  node.label = "kernel";
  node.category = "compute";
  node.path = "root/kernel";
  node_rows.nodes.push_back(node);
  compat::VizNodeAnchorSqlRow node_anchor;
  node_anchor.node_id = node.node_id;
  node_anchor.anchor_id = "anchor-0";
  node_anchor.db_idx = 3;
  node_anchor.device_id = 0;
  node_anchor.view_name = "anchor_tree";
  node_anchor.coverage_kind = "self";
  node_rows.node_anchors.push_back(node_anchor);
  compat::replace_node_coverage_rows(path, node_rows);
  compat::materialize_report_compatibility_views(path);

  require(scalar_int(path,
                     "SELECT COUNT(*) FROM traceloom_v_anchor_runtime_call "
                     "WHERE support_state='supported_exact'") == 2);
  require(scalar_text(path,
                      "SELECT support_state FROM "
                      "traceloom_v_anchor_host_interval") ==
          "supported_ordered");
  require(scalar_int(path,
                     "SELECT COUNT(*) FROM traceloom_v_anchor_host_activity") ==
          1);
  require(scalar_text(path,
                      "SELECT api_name FROM "
                      "traceloom_v_anchor_host_activity") ==
          "cudaEventQuery");
  require(scalar_text(path,
                      "SELECT interval_relation FROM "
                      "traceloom_v_anchor_host_activity") == "contained");
  require(scalar_int(path,
                     "SELECT COUNT(*) FROM traceloom_v_aux_runtime_call "
                     "WHERE support_state='supported_exact'") == 1);
  require(scalar_text(path,
                      "SELECT scope_policy FROM "
                      "traceloom_v_anchor_host_interval") ==
          "same_thread");
  require(scalar_int(path,
                     "SELECT COUNT(*) FROM traceloom_v_node_runtime_call "
                     "WHERE node_id='node-1' AND occurrence_idx=0 AND "
                     "runtime_call_id='runtime-call-0' AND "
                     "support_state='supported_exact'") == 1);
  require(scalar_text(path,
                      "SELECT local_node_id FROM "
                      "traceloom_v_node_runtime_call WHERE "
                      "runtime_call_id='runtime-call-0'") ==
          "local-node-1");
  require(scalar_int(path,
                     "SELECT COUNT(*) FROM traceloom_v_node_host_activity "
                     "WHERE node_id='node-1' AND occurrence_idx=0 AND "
                     "observed_runtime_call_id='runtime-call-1'") == 1);
  require(scalar_text(path,
                      "SELECT placement_semantics FROM "
                      "traceloom_v_node_host_activity") ==
          "after_anchor_interval");
  require(scalar_text(path,
                      "SELECT right_anchor_symbol FROM "
                      "traceloom_v_node_host_activity") == "kernel");

  std::remove(path.c_str());
  return 0;
}
