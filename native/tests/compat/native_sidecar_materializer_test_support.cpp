#include "native_sidecar_materializer_test_support.h"

#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace traceloom::testing::sidecar_materializer {

using namespace traceloom;

std::string temp_db_path() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("traceloom_native_compat_materializer_" + std::to_string(now) + ".db");
  return path.string();
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

std::string run_scalar_text(const std::string& path, const std::string& sql) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);

  sqlite3_stmt* raw_stmt = nullptr;
  rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw_stmt, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);
  rc = sqlite3_step(raw_stmt);
  traceloom::testing::require(rc == SQLITE_ROW);
  const unsigned char* raw_text = sqlite3_column_text(raw_stmt, 0);
  const std::string value =
      raw_text == nullptr ? "" : reinterpret_cast<const char*>(raw_text);
  rc = sqlite3_step(raw_stmt);
  traceloom::testing::require(rc == SQLITE_DONE);
  sqlite3_finalize(raw_stmt);
  sqlite3_close(db);
  return value;
}

void run_sql(const std::string& path, const std::string& sql) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db,
                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                           nullptr);
  traceloom::testing::require(rc == SQLITE_OK);
  char* error = nullptr;
  rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
  if (error != nullptr) {
    sqlite3_free(error);
  }
  sqlite3_close(db);
  traceloom::testing::require(rc == SQLITE_OK);
}

NativeIr build_collective_repeat_ir() {
  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "collective", "TASK", 0);
  const SymbolId ai_core = ir.symbols.intern("AI_CORE");
  const SymbolId matmul = ir.symbols.intern("MatMul");
  const SymbolId all_reduce = ir.symbols.intern("HcclAllReduce");

  std::vector<AnchorId> anchors;
  for (std::uint32_t idx = 0; idx < 8; ++idx) {
    const bool collective = idx % 2 == 1;
    const SymbolId symbol = collective ? all_reduce : matmul;
    const std::int64_t start_ns = 1000 + static_cast<std::int64_t>(idx) * 1000;
    const TraceEventId event =
        ir.trace_events.append(source, idx, 0, 5, start_ns, start_ns + 500,
                               symbol);
    ir.tasks.append(source, event, idx + 1, 9000 + idx,
                    collective ? 700 : -1, ai_core, symbol, symbol, ai_core,
                    collective ? all_reduce : SymbolId::invalid());
    anchors.push_back(ir.anchors.append(
        source, event, ReplayUnitId::invalid(),
        collective ? AnchorKind::kCommunication : AnchorKind::kDeviceEvent,
        symbol, 0, 5, start_ns, start_ns + 500));
  }
  for (std::uint32_t idx = 0; idx < anchors.size(); ++idx) {
    const AnchorRow& anchor = ir.anchors.row(anchors[idx]);
    ir.tokens.append(anchors[idx], anchor.symbol_id, 0, idx, anchor.start_ns,
                     anchor.end_ns);
  }
  return ir;
}

NativeIr build_exact_cuda_graph_replay_ir() {
  NativeIr ir;
  const SourceRefId source = ir.source_refs.append(
      "cuda_nsys_sqlite", "cuda-exact-graph", "CUDA_GRAPH_REPLAY_UNIT", 0);
  const TraceEventId launch = ir.trace_events.append(
      source, 1, 0, 11, 1000, 2000,
      ir.symbols.intern("CUDAGraph ExactT1"));
  const GraphLaunchOccurrenceId occurrence =
      ir.graph_launch_occurrences.append(
          source, source, 0, 1, 101, 9001, -1, StreamId::invalid(),
          StreamId::invalid(), CapturedGraphInstanceId::invalid(),
          TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), 1000,
          2000, -1, GraphLaunchMatchPolicy::kCudaRuntimeCorrelation,
          GraphLaunchInstanceAssociationPolicy::kCudaGraphNodeSet);
  const ReplayBodyTemplateId body = ir.replay_body_templates.append(
      source, 11, ir.symbols.intern("lane 0:\nkernel"), 2, 0, 1,
      ReplayBodyTopologyPolicy::kObservedStreamSetUnordered, 1);
  const SourceRefId kernel_source = ir.source_refs.append(
      "cuda_nsys_sqlite", "cuda-exact-graph", "CUPTI_ACTIVITY_KIND_KERNEL",
      0);
  const SourceRefId memcpy_source = ir.source_refs.append(
      "cuda_nsys_sqlite", "cuda-exact-graph", "CUPTI_ACTIVITY_KIND_MEMCPY",
      0);
  const SymbolId ai_core = ir.symbols.intern("AI_CORE");
  const SymbolId gemm = ir.symbols.intern("graph_a_gemm");
  const SymbolId memcpy = ir.symbols.intern("CudaMemcpy kind=8 bytes=1048576");
  const TraceEventId gemm_event = ir.trace_events.append(
      kernel_source, 101, 0, 11, 1010, 1020, gemm);
  const TaskId gemm_task = ir.tasks.append(
      kernel_source, gemm_event, 101, 101, -1, ai_core, gemm, gemm, ai_core,
      SymbolId::invalid());
  const TraceEventId gemm2_event = ir.trace_events.append(
      kernel_source, 102, 0, 11, 1030, 1040, gemm);
  const TaskId gemm2_task = ir.tasks.append(
      kernel_source, gemm2_event, 102, 102, -1, ai_core, gemm, gemm, ai_core,
      SymbolId::invalid());
  const TraceEventId memcpy_event = ir.trace_events.append(
      memcpy_source, 201, 0, 11, 1050, 1060, memcpy);
  const TaskId memcpy_task = ir.tasks.append(
      memcpy_source, memcpy_event, 201, 201, -1, ai_core, memcpy, memcpy,
      ai_core, SymbolId::invalid());
  const ReplayCompositionCandidateId composition =
      ir.replay_composition_candidates.append(
          source, 0, occurrence, occurrence, 1, 0, 1, 1, 0, 22,
          ReplayCompositionIdentityPolicy::kCudaGraphNodeSet,
          ReplayCompositionOrderPolicy::kHostSubmissionOrder,
          ReplayCompositionShapePolicy::kSingleGraph,
          ReplayCompositionBoundaryPolicy::kDirectObservedGraphLaunch);
  const ReplayCompositionSlotId slot =
      ir.replay_composition_slots.append(
          composition, 0, CapturedGraphInstanceId::invalid(),
          GraphSlotTemplateId::invalid(), body,
          ReplayCompositionSlotRole::kCudaGraph, 9001);
  const ReplayCompositionRegionId region =
      ir.replay_composition_regions.append(
          composition, 0, occurrence, occurrence, 1000, 2000, 1, 1,
          ReplayCompositionRegionStatus::kRecognizedCompletePattern);
  ir.replay_composition_region_members.append(region, 0, occurrence, 0);
  const GraphLaunchBodyId body_id = ir.graph_launch_bodies.append(
      occurrence, body, gemm_task, memcpy_task, 2, 0, 1, 1);
  const std::int64_t graph_node_base = 8589934592LL;
  ir.graph_launch_body_members.append(
      body_id, gemm_task, 0, 0, GraphLaunchBodyMemberRow::Kind::kCompute,
      graph_node_base, 5000);
  ir.graph_launch_body_members.append(
      body_id, gemm2_task, 0, 1, GraphLaunchBodyMemberRow::Kind::kCompute,
      graph_node_base + 1, 5001);
  ir.graph_launch_body_members.append(
      body_id, memcpy_task, 0, 2, GraphLaunchBodyMemberRow::Kind::kDataMove,
      graph_node_base + 2, 5002);
  const GraphTemplateId graph_template =
      ir.graph_templates.append(source, 33, 1);
  const ReplayUnitId unit = ir.replay_units.append(
      graph_template, source, AnchorId::invalid(), AnchorId::invalid(),
      launch, region);
  const ReplayUnitLaunchMemberId launch_member =
      ir.replay_unit_launch_members.append(unit, 0, occurrence, slot);
  const AnchorId graph_anchor = ir.anchors.append(
      source, TraceEventId::invalid(), unit, AnchorKind::kGraphReplayUnit,
      ir.symbols.intern("CUDAGraph"), 0, 11, 1000, 2000, launch_member);
  // Token drives the report-tree node coverage so the exact anchor gets a
  // real traceloom_viz_node_anchor occurrence for the node-view join.
  ir.tokens.append(graph_anchor, ir.symbols.intern("CUDAGraph"), 0, 0, 1000,
                   2000);
  ir.replay_units.set_anchor_bounds(unit, graph_anchor, graph_anchor);
  ir.protected_intervals.append(
      ProtectedIntervalKind::kGraphReplayUnit, BoundaryPolicy::kNoCross,
      TokenId(0), TokenId(0), graph_anchor, graph_anchor, source);
  return ir;
}

}  // namespace traceloom::testing::sidecar_materializer
