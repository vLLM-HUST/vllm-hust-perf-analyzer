#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/aux_attribution_rows.h"
#include "traceloom/compat/runtime_device_rows.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

void exec_sql(const std::string& path, const std::string& sql) {
  sqlite3* db = nullptr;
  traceloom::testing::require(
      sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) ==
      SQLITE_OK);
  char* error = nullptr;
  const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
  if (error != nullptr) {
    sqlite3_free(error);
  }
  sqlite3_close(db);
  traceloom::testing::require(rc == SQLITE_OK);
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
  const TraceEventId context_annotated =
      ir.trace_events.append(kernel_source, 13, 0, 7, 2500, 2550, kernel);
  ir.tasks.append(kernel_source, first, 1, 1, 41, kernel, kernel, kernel,
                  kernel, SymbolId::invalid());
  ir.tasks.append(kernel_source, second, 2, 2, 42, kernel, kernel, kernel,
                  kernel, SymbolId::invalid());
  ir.tasks.append(kernel_source, auxiliary, 3, 3, 41, kernel, kernel, kernel,
                  kernel, SymbolId::invalid());
  ir.tasks.append(kernel_source, context_annotated, 4, 4, 55, kernel, kernel,
                  kernel, kernel, SymbolId::invalid());
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
  // Runtime context metadata is observation only. A direct provider key is
  // scoped by this source DB; it is not joined to device-task PID/context.
  require(rows.relations[3].support_state == "supported_exact");
  require(rows.relations[4].support_state == "unmatched_runtime_call");
  require(rows.relations[5].support_state == "missing_runtime_identifier");
  require(rows.anchor_relations.size() == 2);
  require(rows.host_intervals.size() == 1);
  require(rows.host_intervals[0].support_state == "supported_ordered");
  require(rows.host_intervals[0].scope_policy == "same_thread");
  require(rows.host_activities.size() == 1);
  require(rows.host_api_summaries.size() == 1);
  require(rows.host_api_summaries[0].api_family == "query");
  require(rows.host_api_summaries[0].call_count == 1);
  require(rows.host_api_summaries[0].distinct_api_name_count == 1);
  for (const compat::RuntimeCallSqlRow& row : rows.runtime_calls) {
    require(row.db_idx == 3);
  }
  for (const compat::DeviceWorkSqlRow& row : rows.device_works) {
    require(row.db_idx == 3);
  }
  for (const compat::RuntimeDeviceRelationSqlRow& row : rows.relations) {
    require(row.db_idx == 3);
  }

  // Runtime/device locators use an absolute path when the profiler source is
  // an existing file, matching the raw-source packaging catalog even if the
  // CLI input was relative. Synthetic source names remain unchanged.
  const std::string locator_source_path = temp_db_path();
  {
    std::ofstream source(locator_source_path);
    source << "locator";
  }
  const std::filesystem::path relative_locator_source =
      std::filesystem::relative(locator_source_path,
                                std::filesystem::current_path());
  NativeIr locator_ir;
  const SourceRefId locator_source = locator_ir.source_refs.append(
      "cuda_nsys_sqlite", relative_locator_source.string(),
      "CUPTI_ACTIVITY_KIND_RUNTIME", 0);
  const SymbolId locator_symbol = locator_ir.symbols.intern("runtime");
  locator_ir.runtime_calls.append(
      locator_source, 1, RuntimeCallProvider::kCuda,
      RuntimeCallClockDomain::kProfilerHost,
      RuntimeCallMatchPolicy::kCudaCorrelationId, 1, 2, 1, locator_symbol,
      locator_symbol);
  const compat::RuntimeDeviceSqlRows locator_rows =
      compat::build_runtime_device_sql_rows(locator_ir);
  require(locator_rows.runtime_calls.size() == 1);
  const std::string canonical_locator_source =
      std::filesystem::absolute(locator_source_path).lexically_normal().string();
  require(locator_rows.runtime_calls[0].raw_json.find(canonical_locator_source) !=
          std::string::npos);
  std::remove(locator_source_path.c_str());

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
      SymbolId::invalid());
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

  // Nsight may reuse a CUDA correlationId in one exported DB. For a typed
  // CUPTI synchronization observation only, interval containment may
  // disambiguate candidates already selected by that documented provider
  // identifier. It must retain the rejected candidate and must not turn a
  // non-unique containment into an exact edge.
  NativeIr reused_sync_ir;
  const SourceRefId reused_runtime_source = reused_sync_ir.source_refs.append(
      "cuda_nsys_sqlite", "memory", "CUPTI_ACTIVITY_KIND_RUNTIME", 0);
  const SourceRefId reused_sync_source = reused_sync_ir.source_refs.append(
      "cuda_nsys_sqlite", "memory",
      "CUPTI_ACTIVITY_KIND_SYNCHRONIZATION", 0);
  const SymbolId reused_symbol = reused_sync_ir.symbols.intern("sync");
  const SymbolId reused_type = reused_sync_ir.symbols.intern("cuda_runtime");
  reused_sync_ir.runtime_calls.append(
      reused_runtime_source, 1, RuntimeCallProvider::kCuda,
      RuntimeCallClockDomain::kProfilerHost,
      RuntimeCallMatchPolicy::kCudaCorrelationId, 100, 200, 77, reused_type,
      reused_symbol);
  reused_sync_ir.runtime_calls.append(
      reused_runtime_source, 2, RuntimeCallProvider::kCuda,
      RuntimeCallClockDomain::kProfilerHost,
      RuntimeCallMatchPolicy::kCudaCorrelationId, 300, 400, 77, reused_type,
      reused_symbol);
  const TraceEventId reused_sync_event = reused_sync_ir.trace_events.append(
      reused_sync_source, 1, 0, 1, 320, 330, reused_symbol);
  reused_sync_ir.tasks.append(
      reused_sync_source, reused_sync_event, 77, 77, 77, reused_symbol,
      reused_symbol, reused_symbol, reused_symbol, SymbolId::invalid());
  const compat::RuntimeDeviceSqlRows reused_sync_rows =
      compat::build_runtime_device_sql_rows(reused_sync_ir);
  require(reused_sync_rows.relations.size() == 2);
  require(reused_sync_rows.relations[0].support_state ==
          "rejected_reused_correlation_id");
  require(reused_sync_rows.relations[1].support_state ==
          "supported_deterministic");
  require(reused_sync_rows.relations[1].match_policy ==
          "cuda_correlation_id_time_containment");
  require(reused_sync_rows.relations[1].evidence_level ==
          "direct_identifier_time_disambiguated");
  const std::string reused_sync_path = temp_db_path();
  compat::materialize_compatibility_schema(reused_sync_path);
  compat::replace_runtime_device_rows(reused_sync_path, reused_sync_rows);
  compat::materialize_report_compatibility_views(reused_sync_path);
  require(scalar_int(reused_sync_path,
                     "SELECT COUNT(*) FROM traceloom_v_sync_runtime_call") ==
          2);
  require(scalar_text(reused_sync_path,
                      "SELECT sync_kind FROM traceloom_v_sync_runtime_call "
                      "WHERE support_state='supported_deterministic'") ==
          "sync");
  std::remove(reused_sync_path.c_str());

  NativeIr overlapping_sync_ir;
  const SourceRefId overlap_runtime_source =
      overlapping_sync_ir.source_refs.append(
          "cuda_nsys_sqlite", "memory", "CUPTI_ACTIVITY_KIND_RUNTIME", 0);
  const SourceRefId overlap_sync_source = overlapping_sync_ir.source_refs.append(
      "cuda_nsys_sqlite", "memory",
      "CUPTI_ACTIVITY_KIND_SYNCHRONIZATION", 0);
  const SymbolId overlap_symbol = overlapping_sync_ir.symbols.intern("sync");
  overlapping_sync_ir.runtime_calls.append(
      overlap_runtime_source, 1, RuntimeCallProvider::kCuda,
      RuntimeCallClockDomain::kProfilerHost,
      RuntimeCallMatchPolicy::kCudaCorrelationId, 100, 400, 88,
      overlap_symbol, overlap_symbol);
  overlapping_sync_ir.runtime_calls.append(
      overlap_runtime_source, 2, RuntimeCallProvider::kCuda,
      RuntimeCallClockDomain::kProfilerHost,
      RuntimeCallMatchPolicy::kCudaCorrelationId, 200, 300, 88,
      overlap_symbol, overlap_symbol);
  const TraceEventId overlap_sync_event =
      overlapping_sync_ir.trace_events.append(
          overlap_sync_source, 1, 0, 1, 220, 230, overlap_symbol);
  overlapping_sync_ir.tasks.append(
      overlap_sync_source, overlap_sync_event, 88, 88, 88, overlap_symbol,
      overlap_symbol, overlap_symbol, overlap_symbol, SymbolId::invalid());
  const compat::RuntimeDeviceSqlRows overlapping_sync_rows =
      compat::build_runtime_device_sql_rows(overlapping_sync_ir);
  require(overlapping_sync_rows.relations.size() == 2);
  require(overlapping_sync_rows.relations[0].support_state ==
              "ambiguous_runtime_candidates" &&
          overlapping_sync_rows.relations[1].support_state ==
              "ambiguous_runtime_candidates");

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
  node.occurrence_count = 2;
  node_rows.nodes.push_back(node);
  compat::VizNodeAnchorSqlRow node_anchor;
  node_anchor.node_id = node.node_id;
  node_anchor.anchor_id = "anchor-0";
  node_anchor.db_idx = 3;
  node_anchor.device_id = 0;
  node_anchor.view_name = "anchor_tree";
  node_anchor.occurrence_idx = 1;
  node_anchor.anchor_order = 1;
  node_anchor.coverage_kind = "self";
  node_anchor.compute_us = 0.1;
  node_anchor.total_us = 0.1;
  node_rows.node_anchors.push_back(node_anchor);
  node_anchor.anchor_id = "anchor-1";
  node_anchor.occurrence_idx = 2;
  node_anchor.idle_us = 0.9;
  node_anchor.total_us = 1.0;
  node_rows.node_anchors.push_back(node_anchor);
  compat::AnchorPrimaryNodeSqlRow primary;
  primary.node_id = node.node_id;
  primary.db_idx = 3;
  primary.device_id = 0;
  primary.view_name = "anchor_tree";
  primary.anchor_id = "anchor-0";
  primary.reason = "atom_leaf";
  node_rows.anchor_primary_nodes.push_back(primary);
  primary.anchor_id = "anchor-1";
  node_rows.anchor_primary_nodes.push_back(primary);
  compat::replace_node_coverage_rows(path, node_rows);
  compat::materialize_report_compatibility_views(path);

  require(scalar_int(path,
                     "SELECT COUNT(*) FROM traceloom_v_anchor_runtime_call "
                     "WHERE support_state='supported_exact'") == 2);
  require(scalar_int(path,
                     "SELECT COUNT(*) FROM traceloom_v_sync_runtime_call") ==
          0);
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
                     "WHERE node_id='node-1' AND occurrence_idx=1 AND "
                     "runtime_call_id='runtime-call-0' AND "
                     "support_state='supported_exact'") == 1);
  require(scalar_text(path,
                      "SELECT local_node_id FROM "
                      "traceloom_v_node_runtime_call WHERE "
                      "runtime_call_id='runtime-call-0'") ==
          "local-node-1");
  require(scalar_int(path,
                     "SELECT COUNT(*) FROM traceloom_v_node_host_activity "
                     "WHERE node_id='node-1' AND occurrence_idx=1 AND "
                     "observed_runtime_call_id='runtime-call-1'") == 1);
  require(scalar_text(path,
                      "SELECT placement_semantics FROM "
                      "traceloom_v_node_host_activity") ==
          "after_anchor_interval");
  require(scalar_text(path,
                      "SELECT right_anchor_symbol FROM "
                      "traceloom_v_node_host_activity") == "kernel");
  require(scalar_int(path,
                     "SELECT COUNT(*) FROM traceloom_v_node_host_interval "
                     "WHERE node_id='node-1' AND "
                     "support_state='supported_ordered'") == 1);
  require(scalar_int(path,
                     "SELECT COUNT(*) FROM "
                     "traceloom_v_structure_bubble_occurrence") == 1);
  require(scalar_int(path,
                     "SELECT COUNT(*) FROM "
                     "traceloom_v_structure_bubble_occurrence "
                     "WHERE bubble_us <= 0.0") == 0);
  require(scalar_text(path,
                      "SELECT structural_position_id FROM "
                      "traceloom_v_structure_bubble_occurrence") ==
          "node-1");
  require(scalar_text(path,
                      "SELECT host_observation_status FROM "
                      "traceloom_v_structure_bubble_occurrence") ==
          "supported_ordered");
  require(scalar_int(path,
                     "SELECT COUNT(*) FROM "
                     "traceloom_v_structure_bubble_runtime_call") == 1);
  require(scalar_text(path,
                      "SELECT api_family FROM "
                      "traceloom_v_structure_bubble_runtime_call") ==
          "query");
  require(scalar_int(path,
                     "SELECT call_count FROM "
                     "traceloom_v_structure_bubble_api_occurrence") == 1);
  require(scalar_int(path,
                     "SELECT total_call_count FROM "
                     "traceloom_v_structure_bubble_api_stats") == 1);
  require(scalar_int(path,
                     "SELECT bubble_occurrence_count FROM "
                     "traceloom_v_structure_bubble_position") == 1);
  require(scalar_int(path,
                     "SELECT supported_host_occurrence_count FROM "
                     "traceloom_v_structure_bubble_host_context") == 1);

  std::remove(path.c_str());

  // Changing from a structural position to host context must not erase a
  // position whose adjacent anchors have no supported host endpoints.  The
  // typed interval and bubble remain queryable, while literal activity stays
  // empty and the host-context row carries a NULL API family.
  const std::string unsupported_path = temp_db_path();
  compat::materialize_compatibility_schema(unsupported_path);
  exec_sql(
      unsupported_path,
      "INSERT INTO traceloom_anchor(anchor_id,db_idx,device_id,anchor_idx,"
      "event_id,step_idx,symbol,role,label,family,start_ns,end_ns,dur_us) "
      "VALUES('anchor-left',0,0,0,'event-left',0,'left','compute','left',"
      "'left',1000,1100,0.1),('anchor-right',0,0,1,'event-right',1,'right',"
      "'compute','right','right',2000,2100,0.1);"
      "INSERT INTO traceloom_viz_node(node_id,db_idx,device_id,view_name,"
      "local_node_id,path,node_type,kind,symbol,label,category,depth,level,"
      "repeat_count,occurrence_count,anchor_count,compute_us,comm_us,idle_us,"
      "total_us,avg_compute_us,avg_comm_us,avg_idle_us,avg_total_us,self_us,"
      "aux_events,aux_us,raw_json) VALUES('node-missing',0,0,'anchor_tree',"
      "'local-missing','root/missing','leaf','leaf','right','right','compute',"
      "1,1,1,1,2,0.2,0.0,0.9,1.1,0.2,0.0,0.9,1.1,1.1,0,0.0,'{}');"
      "INSERT INTO traceloom_viz_node_anchor(node_id,anchor_id,db_idx,"
      "device_id,view_name,occurrence_idx,anchor_order,coverage_kind,"
      "repeat_context,compute_us,comm_us,idle_us,total_us,self_us,aux_events,"
      "aux_us) VALUES('node-missing','anchor-left',0,0,'anchor_tree',1,0,"
      "'self','',0.1,0.0,0.0,0.1,0.1,0,0.0),('node-missing',"
      "'anchor-right',0,0,'anchor_tree',1,1,'self','',0.1,0.0,0.9,1.0,1.0,"
      "0,0.0);"
      "INSERT INTO traceloom_anchor_host_interval(interval_id,db_idx,"
      "device_id,left_anchor_id,right_anchor_id,left_endpoint_count,"
      "right_endpoint_count,provider,clock_domain,scope_policy,support_state) "
      "VALUES('interval-missing',0,0,'anchor-left','anchor-right',0,0,'cuda',"
      "'profiler_host','unavailable','missing_endpoint');");
  compat::materialize_report_compatibility_views(unsupported_path);
  require(scalar_int(
              unsupported_path,
              "SELECT COUNT(*) FROM traceloom_v_node_host_interval WHERE "
              "interval_id='interval-missing' AND "
              "support_state='missing_endpoint'") == 1);
  require(scalar_int(
              unsupported_path,
              "SELECT COUNT(*) FROM traceloom_v_node_host_activity WHERE "
              "interval_id='interval-missing'") == 0);
  require(scalar_int(
              unsupported_path,
              "SELECT COUNT(*) FROM traceloom_v_structure_bubble_position "
              "WHERE structural_position_id='node-missing' AND "
              "missing_endpoint_occurrence_count=1") == 1);
  require(scalar_int(
              unsupported_path,
              "SELECT COUNT(*) FROM "
              "traceloom_v_structure_bubble_host_context WHERE "
              "structural_position_id='node-missing' AND api_family IS NULL") ==
          1);
  std::remove(unsupported_path.c_str());
  return 0;
}
