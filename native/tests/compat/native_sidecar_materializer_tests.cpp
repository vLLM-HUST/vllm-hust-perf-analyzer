#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/analysis/idle_evidence_semantic_rules.h"
#include "traceloom/core/sha256.h"
#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

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

void require_analysis_surface_queries_prepare(const std::string& path) {
  sqlite3* db = nullptr;
  traceloom::testing::require(
      sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) ==
      SQLITE_OK);
  sqlite3_stmt* catalog = nullptr;
  traceloom::testing::require(
      sqlite3_prepare_v2(db,
                         "SELECT surface_name, example_sql FROM "
                         "traceloom_analysis_surface ORDER BY surface_name",
                         -1, &catalog, nullptr) == SQLITE_OK);
  while (sqlite3_step(catalog) == SQLITE_ROW) {
    const unsigned char* surface_text = sqlite3_column_text(catalog, 0);
    const unsigned char* sql_text = sqlite3_column_text(catalog, 1);
    traceloom::testing::require(surface_text != nullptr && sql_text != nullptr);
    sqlite3_stmt* example = nullptr;
    const int rc = sqlite3_prepare_v2(
        db, reinterpret_cast<const char*>(sql_text), -1, &example, nullptr);
    if (example != nullptr) {
      sqlite3_finalize(example);
    }
    const std::string error_message =
        "analysis surface example SQL did not prepare: " +
        std::string(reinterpret_cast<const char*>(surface_text)) + ": " +
        sqlite3_errmsg(db);
    traceloom::testing::require(rc == SQLITE_OK, error_message.c_str());
  }
  sqlite3_finalize(catalog);
  sqlite3_close(db);
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

NativeIr build_idle_evidence_ir() {
  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "idle-sidecar", "TASK", 0);
  ir.streams.append(source, 0, 5);
  ir.streams.append(source, 0, 7);
  ir.streams.append(source, 0, 9);
  const SymbolId ai_core = ir.symbols.intern("AI_CORE");
  const SymbolId matmul = ir.symbols.intern("MatMul");
  const SymbolId wait = ir.symbols.intern("EVENT_WAIT");
  const SymbolId mystery = ir.symbols.intern("MysteryControl");
  const SymbolId mystery_type = ir.symbols.intern("MYSTERY");

  const auto add_task = [&](std::uint64_t source_row_id,
                            std::uint32_t stream_id,
                            std::int64_t start_ns,
                            std::int64_t end_ns,
                            SymbolId task_type,
                            SymbolId op_name) {
    const TraceEventId event = ir.trace_events.append(
        source, source_row_id, 0, stream_id, start_ns, end_ns, op_name);
    ir.tasks.append(source, event, source_row_id, source_row_id, -1, task_type,
                    op_name, op_name, task_type, SymbolId::invalid());
    return event;
  };
  const TraceEventId first = add_task(11, 5, 100, 200, ai_core, matmul);
  (void)add_task(12, 7, 200, 300, wait, wait);
  const TraceEventId second = add_task(13, 5, 400, 500, ai_core, matmul);
  (void)add_task(14, 9, 300, 350, mystery_type, mystery);

  const AnchorId first_anchor = ir.anchors.append(
      source, first, ReplayUnitId::invalid(), AnchorKind::kDeviceEvent,
      matmul, 0, 5, 100, 200);
  const AnchorId second_anchor = ir.anchors.append(
      source, second, ReplayUnitId::invalid(), AnchorKind::kDeviceEvent,
      matmul, 0, 5, 400, 500);
  ir.tokens.append(first_anchor, matmul, 0, 0, 100, 200);
  ir.tokens.append(second_anchor, matmul, 0, 1, 400, 500);
  return ir;
}

NativeIr build_graph_replay_ir() {
  NativeIr ir;
  const SourceRefId task_source =
      ir.source_refs.append("fixture", "graph", "TASK", 0);
  const SourceRefId graph_source = ir.source_refs.append(
      "fixture", "graph", "ACLGRAPH_REPLAY_UNIT", 0);
  const SymbolId ai_core = ir.symbols.intern("AI_CORE");
  const SymbolId matmul = ir.symbols.intern("MatMul");
  const SymbolId graph = ir.symbols.intern("GraphReplayUnit T1");

  const TraceEventId first =
      ir.trace_events.append(task_source, 11, 0, 7, 1100, 1600, ai_core);
  ir.tasks.append(task_source, first, 1, 1, 101, ai_core, matmul, matmul,
                  ai_core, SymbolId::invalid());
  const TraceEventId second =
      ir.trace_events.append(task_source, 12, 0, 8, 1500, 2100, ai_core);
  ir.tasks.append(task_source, second, 2, 2, 102, ai_core, matmul, matmul,
                  ai_core, SymbolId::invalid());
  const TraceEventId launch =
      ir.trace_events.append(graph_source, 1, 0, 7, 1000, 2000, graph);
  const GraphTemplateId graph_template =
      ir.graph_templates.append(graph_source, 12345, 2);
  ir.replay_units.append(graph_template, graph_source, AnchorId::invalid(),
                         AnchorId::invalid(), launch);
  return ir;
}

NativeIr build_exact_graph_replay_ir() {
  NativeIr ir;
  const SourceRefId source = ir.source_refs.append(
      "fixture", "exact-graph", "ACLGRAPH_REPLAY_UNIT", 0);
  const SymbolId graph = ir.symbols.intern("GraphReplayUnit ExactT1");
  const TraceEventId launch =
      ir.trace_events.append(source, 1, 0, 7, 1000, 2000, graph);
  const GraphLaunchOccurrenceId occurrence =
      ir.graph_launch_occurrences.append(
          source, source, 0, 1, 100, 9001, -1, StreamId::invalid(),
          StreamId::invalid(), CapturedGraphInstanceId::invalid(),
          TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), 1000,
          2000, 0, GraphLaunchMatchPolicy::kNotifyCompletionAdjacent);
  const GraphLaunchOccurrenceId incomplete_occurrence =
      ir.graph_launch_occurrences.append(
          source, source, 0, 1, 101, 9001, -1, StreamId::invalid(),
          StreamId::invalid(), CapturedGraphInstanceId::invalid(),
          TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), 2000,
          2500, 1, GraphLaunchMatchPolicy::kNotifyCompletionAdjacent);
  const GraphLaunchOccurrenceId mismatch_occurrence =
      ir.graph_launch_occurrences.append(
          source, source, 0, 1, 102, 9001, -1, StreamId::invalid(),
          StreamId::invalid(), CapturedGraphInstanceId::invalid(),
          TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), 2500,
          3000, 2, GraphLaunchMatchPolicy::kNotifyCompletionAdjacent);
  const GraphLaunchOccurrenceId leading_occurrence =
      ir.graph_launch_occurrences.append(
          source, source, 0, 1, 103, 9001, -1, StreamId::invalid(),
          StreamId::invalid(), CapturedGraphInstanceId::invalid(),
          TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), 3000,
          3500, 3, GraphLaunchMatchPolicy::kNotifyCompletionAdjacent);
  const GraphLaunchOccurrenceId missing_body_occurrence =
      ir.graph_launch_occurrences.append(
          source, source, 0, 1, 104, 9001, -1, StreamId::invalid(),
          StreamId::invalid(), CapturedGraphInstanceId::invalid(),
          TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), 3500,
          4000, 4, GraphLaunchMatchPolicy::kNotifyCompletionAdjacent);
  const ReplayBodyTemplateId body = ir.replay_body_templates.append(
      source, 11, ir.symbols.intern("HeadOp"), 1, 0, 1,
      ReplayBodyTopologyPolicy::kSingleModelStream);
  const ReplayCompositionCandidateId composition =
      ir.replay_composition_candidates.append(
          source, 0, occurrence, occurrence, 5, 0, 1, 1, 4, 22,
          ReplayCompositionIdentityPolicy::kGraphConnection,
          ReplayCompositionOrderPolicy::kHostSubmissionOrder,
          ReplayCompositionShapePolicy::kHeadRepeatedLayerTail,
          ReplayCompositionBoundaryPolicy::kExactPeriodicSuffix);
  const ReplayCompositionSlotId slot =
      ir.replay_composition_slots.append(
          composition, 0, CapturedGraphInstanceId::invalid(),
          GraphSlotTemplateId::invalid(), body,
          ReplayCompositionSlotRole::kHead, 9001);
  const ReplayCompositionRegionId region =
      ir.replay_composition_regions.append(
          composition, 0, occurrence, occurrence, 1000, 2000, 1, 1,
          ReplayCompositionRegionStatus::kRecognizedCompletePattern);
  ir.replay_composition_region_members.append(region, 0, occurrence, 0);
  const ReplayCompositionRegionId incomplete_region =
      ir.replay_composition_regions.append(
          composition, 1, incomplete_occurrence, incomplete_occurrence, 2000,
          2500, 1, 2,
          ReplayCompositionRegionStatus::kUnrecognizedIncompleteTail);
  ir.replay_composition_region_members.append(
      incomplete_region, 0, incomplete_occurrence, 0);
  const ReplayCompositionRegionId mismatch_region =
      ir.replay_composition_regions.append(
          composition, 2, mismatch_occurrence, mismatch_occurrence, 2500,
          3000, 1, 1,
          ReplayCompositionRegionStatus::kUnrecognizedBodyMismatch);
  ir.replay_composition_region_members.append(
      mismatch_region, 0, mismatch_occurrence, 0);
  const ReplayCompositionRegionId leading_region =
      ir.replay_composition_regions.append(
          composition, 3, leading_occurrence, leading_occurrence, 3000, 3500,
          1, 0,
          ReplayCompositionRegionStatus::kUnrecognizedLeadingContext);
  ir.replay_composition_region_members.append(
      leading_region, 0, leading_occurrence, -1);
  const ReplayCompositionRegionId missing_body_region =
      ir.replay_composition_regions.append(
          composition, 4, missing_body_occurrence, missing_body_occurrence,
          3500, 4000, 1, 1,
          ReplayCompositionRegionStatus::kUnrecognizedMissingBodyEvidence);
  ir.replay_composition_region_members.append(
      missing_body_region, 0, missing_body_occurrence, 0);
  const ReplayCompositionCandidateId incomplete_composition =
      ir.replay_composition_candidates.append(
          source, 0, missing_body_occurrence, missing_body_occurrence, 1, 0,
          0, 0, 0, 44, ReplayCompositionIdentityPolicy::kUnavailable,
          ReplayCompositionOrderPolicy::kHostSubmissionOrder,
          ReplayCompositionShapePolicy::kUnclassified,
          ReplayCompositionBoundaryPolicy::kIncompleteLaunchEvidence);
  const ReplayCompositionRegionId missing_completion_region =
      ir.replay_composition_regions.append(
          incomplete_composition, 0, missing_body_occurrence,
          missing_body_occurrence,
          3500, 4000, 1, 1,
          ReplayCompositionRegionStatus::
              kUnrecognizedMissingCompletionEvidence);
  ir.replay_composition_region_members.append(
      missing_completion_region, 0, missing_body_occurrence, -1);
  const ReplayCompositionRegionId missing_capability_region =
      ir.replay_composition_regions.append(
          composition, 6, missing_body_occurrence, missing_body_occurrence,
          3500, 4000, 1, 1,
          ReplayCompositionRegionStatus::
              kUnrecognizedMissingBodyCapability);
  ir.replay_composition_region_members.append(
      missing_capability_region, 0, missing_body_occurrence, -1);
  const SourceRefId task_source =
      ir.source_refs.append("fixture", "exact-graph", "TASK", 0);
  const SymbolId ai_core = ir.symbols.intern("AI_CORE");
  const SymbolId matmul = ir.symbols.intern("MatMul");
  const TraceEventId member_event =
      ir.trace_events.append(task_source, 21, 0, 7, 1050, 1600, ai_core);
  const TaskId member_task = ir.tasks.append(
      task_source, member_event, 21, 21, -1, ai_core, matmul, matmul,
      ai_core, SymbolId::invalid());
  const GraphLaunchBodyId body_id = ir.graph_launch_bodies.append(
      occurrence, body, member_task, member_task, 1, 0, 1);
  ir.graph_launch_body_members.append(
      body_id, member_task, 0, 0, GraphLaunchBodyMemberRow::Kind::kCompute);
  const GraphTemplateId graph_template =
      ir.graph_templates.append(source, 33, 1);
  const ReplayUnitId unit = ir.replay_units.append(
      graph_template, source, AnchorId::invalid(), AnchorId::invalid(),
      launch, region);
  ir.replay_unit_launch_members.append(unit, 0, occurrence, slot);
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
  return ir;
}

// Ascend-style exact ReplayUnit with two ordered slots, each mapping its own
// anchor to its own launch/body members. Provider-neutrality proof: the exact
// SQL surface must keep both launches and both bodies distinct without any
// temporal inference.
NativeIr build_ascend_multi_slot_exact_graph_sql_ir() {
  NativeIr ir;
  const SourceRefId source = ir.source_refs.append(
      "ascend", "acl-exact-graph", "ACLGRAPH_REPLAY_UNIT", 0);
  const SourceRefId task_source =
      ir.source_refs.append("ascend", "acl-exact-graph", "TASK", 0);
  const SymbolId ai_core = ir.symbols.intern("AI_CORE");
  const SymbolId head = ir.symbols.intern("HeadOp");
  const SymbolId tail = ir.symbols.intern("TailOp");
  const SymbolId graph = ir.symbols.intern("GraphReplayUnit ExactT1");

  const TraceEventId launch =
      ir.trace_events.append(source, 1, 0, 7, 1000, 3000, graph);
  const GraphLaunchOccurrenceId head_occurrence =
      ir.graph_launch_occurrences.append(
          source, source, 0, 1, 100, 9001, -1, StreamId::invalid(),
          StreamId::invalid(), CapturedGraphInstanceId::invalid(),
          TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), 1000,
          2000, -1, GraphLaunchMatchPolicy::kNotifyCompletionAdjacent);
  const GraphLaunchOccurrenceId tail_occurrence =
      ir.graph_launch_occurrences.append(
          source, source, 0, 1, 101, 9001, -1, StreamId::invalid(),
          StreamId::invalid(), CapturedGraphInstanceId::invalid(),
          TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), 2000,
          3000, -1, GraphLaunchMatchPolicy::kNotifyCompletionAdjacent);

  const TraceEventId head_event =
      ir.trace_events.append(task_source, 31, 0, 7, 1050, 1600, head);
  const TaskId head_task = ir.tasks.append(
      task_source, head_event, 31, 31, -1, ai_core, head, head, ai_core,
      SymbolId::invalid());
  const TraceEventId tail_event =
      ir.trace_events.append(task_source, 32, 0, 7, 2050, 2600, tail);
  const TaskId tail_task = ir.tasks.append(
      task_source, tail_event, 32, 32, -1, ai_core, tail, tail, ai_core,
      SymbolId::invalid());

  const ReplayBodyTemplateId head_template = ir.replay_body_templates.append(
      source, 11, ir.symbols.intern("HeadOp"), 1, 0, 1,
      ReplayBodyTopologyPolicy::kSingleModelStream);
  const ReplayBodyTemplateId tail_template = ir.replay_body_templates.append(
      source, 12, ir.symbols.intern("TailOp"), 1, 0, 1,
      ReplayBodyTopologyPolicy::kSingleModelStream);
  const GraphLaunchBodyId head_body = ir.graph_launch_bodies.append(
      head_occurrence, head_template, head_task, head_task, 1, 0, 1);
  ir.graph_launch_body_members.append(
      head_body, head_task, 0, 0, GraphLaunchBodyMemberRow::Kind::kCompute);
  const GraphLaunchBodyId tail_body = ir.graph_launch_bodies.append(
      tail_occurrence, tail_template, tail_task, tail_task, 1, 0, 1);
  ir.graph_launch_body_members.append(
      tail_body, tail_task, 0, 0, GraphLaunchBodyMemberRow::Kind::kCompute);

  const ReplayCompositionCandidateId composition =
      ir.replay_composition_candidates.append(
          source, 0, head_occurrence, tail_occurrence, 2, 0, 2, 1, 0, 22,
          ReplayCompositionIdentityPolicy::kGraphConnection,
          ReplayCompositionOrderPolicy::kHostSubmissionOrder,
          ReplayCompositionShapePolicy::kHeadRepeatedLayerTail,
          ReplayCompositionBoundaryPolicy::kExactPeriodicSuffix);
  const ReplayCompositionSlotId head_slot =
      ir.replay_composition_slots.append(
          composition, 0, CapturedGraphInstanceId::invalid(),
          GraphSlotTemplateId::invalid(), head_template,
          ReplayCompositionSlotRole::kHead, 9001);
  const ReplayCompositionSlotId tail_slot =
      ir.replay_composition_slots.append(
          composition, 1, CapturedGraphInstanceId::invalid(),
          GraphSlotTemplateId::invalid(), tail_template,
          ReplayCompositionSlotRole::kTail, 9002);
  const ReplayCompositionRegionId region =
      ir.replay_composition_regions.append(
          composition, 0, head_occurrence, tail_occurrence, 1000, 3000, 2, 2,
          ReplayCompositionRegionStatus::kRecognizedCompletePattern);
  ir.replay_composition_region_members.append(region, 0, head_occurrence, 0);
  ir.replay_composition_region_members.append(region, 1, tail_occurrence, 1);
  const GraphTemplateId graph_template =
      ir.graph_templates.append(source, 33, 2);
  const ReplayUnitId unit = ir.replay_units.append(
      graph_template, source, AnchorId::invalid(), AnchorId::invalid(),
      launch, region);
  const ReplayUnitLaunchMemberId head_member =
      ir.replay_unit_launch_members.append(unit, 0, head_occurrence,
                                           head_slot);
  const ReplayUnitLaunchMemberId tail_member =
      ir.replay_unit_launch_members.append(unit, 1, tail_occurrence,
                                           tail_slot);
  const SymbolId head_symbol = ir.symbols.intern("ACLH");
  const SymbolId tail_symbol = ir.symbols.intern("ACLT");
  const AnchorId head_anchor = ir.anchors.append(
      source, TraceEventId::invalid(), unit, AnchorKind::kGraphH, head_symbol,
      0, 7, 1000, 2000, head_member);
  const AnchorId tail_anchor = ir.anchors.append(
      source, TraceEventId::invalid(), unit, AnchorKind::kGraphT, tail_symbol,
      0, 7, 2000, 3000, tail_member);
  // Tokens drive the report-tree node coverage so both exact anchors get
  // real traceloom_viz_node_anchor occurrences for the node-view join.
  ir.tokens.append(head_anchor, head_symbol, 0, 0, 1000, 2000);
  ir.tokens.append(tail_anchor, tail_symbol, 0, 1, 2000, 3000);
  return ir;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "memory", "TASK", 0);
  const SymbolId task_type = ir.symbols.intern("AI_CORE");
  const SymbolId op_name = ir.symbols.intern("MatMul");
  const SymbolId op_type = ir.symbols.intern("Cube");

  const TraceEventId event =
      ir.trace_events.append(source, 12, 0, 5, 1000, 3000, task_type);
  ir.tasks.append(source, event, 77, 9001, -1, task_type, op_name, op_type,
                  task_type, SymbolId::invalid());
  const AnchorId anchor =
      ir.anchors.append(source, event, ReplayUnitId::invalid(),
                        AnchorKind::kDeviceEvent, op_type, 0, 5, 1000, 3000);
  ir.tokens.append(anchor, op_type, 0, 0, 1000, 3000);

  const std::string db_path = temp_db_path();
  compat::NativeCompatibilitySidecarOptions options;
  options.db_idx = 2;
  options.source_kind = "fixture";
  options.source_path = "memory";
  compat::write_basic_native_compatibility_sidecar(db_path, ir, options);

  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_metadata") == 14);
  require(run_scalar_text(db_path,
                          "SELECT value FROM traceloom_metadata "
                          "WHERE key = 'native_compatibility_materializer'") ==
          "basic_native_ir_v1");
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_event") == 1);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_event_source") == 1);
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_anchor") ==
          1);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM traceloom_anchor_cost_breakdown") == 1);
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_viz_node") ==
          2);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_viz_node_anchor") ==
          2);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_anchor_primary_node") == 1);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_semantic_tree") == 1);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_semantic_node") == 2);
  require(run_scalar_text(db_path,
                          "SELECT symbol FROM traceloom_event "
                          "WHERE event_id = 'event-0'") == "Cube");
  require(run_scalar_text(db_path,
                          "SELECT event_id FROM traceloom_anchor "
                          "WHERE anchor_id = 'anchor-0'") == "event-0");
  require(run_scalar_int(db_path,
                         "SELECT CAST(total_us * 1000 AS INTEGER) FROM "
                         "traceloom_anchor_cost_breakdown "
                         "WHERE anchor_idx = 1") == 2000);
  require(run_scalar_text(db_path,
                          "SELECT anchor_kind FROM "
                          "traceloom_anchor_cost_breakdown "
                          "WHERE anchor_idx = 1") == "exec");
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM traceloom_event_source s "
              "LEFT JOIN traceloom_event e ON e.event_id = s.event_id "
              "WHERE e.event_id IS NULL") == 0);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM traceloom_anchor a "
              "LEFT JOIN traceloom_event e ON e.event_id = a.event_id "
              "WHERE e.event_id IS NULL") == 0);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM sqlite_master "
              "WHERE type = 'view' AND name = 'traceloom_v_tree_node'") == 1);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_collective_global_link") == 0);

  std::remove(db_path.c_str());

  const std::string graph_db_path = temp_db_path();
  compat::NativeCompatibilitySidecarOptions graph_options;
  graph_options.db_idx = 4;
  graph_options.source_kind = "fixture";
  graph_options.source_path = "graph-smoke";
  compat::write_basic_native_compatibility_sidecar(
      graph_db_path, build_graph_replay_ir(), graph_options);
  require(run_scalar_int(graph_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_cuda_graph_replay") == 1);
  require(run_scalar_text(graph_db_path,
                          "SELECT graph_provider FROM "
                          "traceloom_cuda_graph_replay") == "aclgraph");
  require(run_scalar_text(graph_db_path,
                          "SELECT graph_id FROM "
                          "traceloom_cuda_graph_replay") ==
          "aclgraph-template-0");
  require(run_scalar_int(graph_db_path,
                         "SELECT enclosed_event_count FROM "
                         "traceloom_cuda_graph_replay") == 2);
  require(run_scalar_int(graph_db_path,
                         "SELECT enclosed_kernel_count FROM "
                         "traceloom_cuda_graph_replay") == 2);
  require(run_scalar_int(graph_db_path,
                         "SELECT json_extract(raw_json, "
                         "'$.capture_group_size') FROM "
                         "traceloom_cuda_graph_replay") == 2);
  require(run_scalar_int(graph_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_cuda_graph_envelope") == 2);
  require(run_scalar_int(graph_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_cuda_graph_envelope "
                         "WHERE relation = 'time_overlap'") == 1);
  require(run_scalar_int(graph_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_cuda_graph_envelope e "
                         "LEFT JOIN traceloom_event c "
                         "ON c.event_id = e.child_event_id "
                         "WHERE c.event_id IS NULL") == 0);
  require(run_scalar_text(graph_db_path,
                          "SELECT value FROM traceloom_metadata "
                          "WHERE key = 'replay_unit_count'") == "1");
  require(run_scalar_int(graph_db_path,
                         "SELECT COUNT(*) FROM traceloom_graph_launch") == 0);
  require(run_scalar_int(graph_db_path,
                         "SELECT COUNT(*) FROM traceloom_graph_body_member") ==
          0);
  std::remove(graph_db_path.c_str());

  const std::string exact_graph_db_path = temp_db_path();
  compat::write_basic_native_compatibility_sidecar(
      exact_graph_db_path, build_exact_graph_replay_ir(), graph_options);
  require(run_scalar_text(exact_graph_db_path,
                          "SELECT json_extract(raw_json, "
                          "'$.reconstruction') FROM "
                          "traceloom_cuda_graph_replay") ==
          "exact_replay_composition");
  require(run_scalar_int(exact_graph_db_path,
                         "SELECT json_extract(raw_json, "
                         "'$.replay_composition_region_id') FROM "
                         "traceloom_cuda_graph_replay") == 0);
  require(run_scalar_int(exact_graph_db_path,
                         "SELECT json_extract(raw_json, "
                         "'$.launch_member_count') FROM "
                         "traceloom_cuda_graph_replay") == 1);
  require(run_scalar_int(
              exact_graph_db_path,
              "SELECT COUNT(*) FROM "
              "traceloom_aclgraph_reconstruction_region") == 7);
  require(run_scalar_int(
              exact_graph_db_path,
              "SELECT COUNT(*) FROM "
              "traceloom_aclgraph_reconstruction_region "
              "WHERE status LIKE 'unrecognized_%'") == 6);
  require(run_scalar_text(
              exact_graph_db_path,
              "SELECT status FROM "
              "traceloom_aclgraph_reconstruction_region "
              "WHERE region_order = 1") ==
          "unrecognized_incomplete_tail");
  require(run_scalar_int(
              exact_graph_db_path,
              "SELECT expected_launch_count FROM "
              "traceloom_aclgraph_reconstruction_region "
              "WHERE region_order = 1") == 2);
  require(run_scalar_text(
              exact_graph_db_path,
              "SELECT boundary_policy FROM "
              "traceloom_aclgraph_reconstruction_region LIMIT 1") ==
          "exact_periodic_suffix");
  require(run_scalar_int(
              exact_graph_db_path,
              "SELECT COUNT(DISTINCT status) FROM "
              "traceloom_aclgraph_reconstruction_region") == 7);
  require(run_scalar_text(
              exact_graph_db_path,
              "SELECT boundary_policy FROM "
              "traceloom_aclgraph_reconstruction_region WHERE status = "
              "'unrecognized_missing_completion_evidence'") ==
          "incomplete_launch_evidence");
  require(run_scalar_text(
              exact_graph_db_path,
              "SELECT identity_policy FROM "
              "traceloom_aclgraph_reconstruction_region WHERE status = "
              "'unrecognized_missing_completion_evidence'") ==
          "unavailable");
  require(run_scalar_int(
              exact_graph_db_path,
              "SELECT COUNT(*) FROM traceloom_cuda_graph_replay") == 1);
  require(run_scalar_text(
              exact_graph_db_path,
              "SELECT value FROM traceloom_metadata "
              "WHERE key = 'replay_composition_region_count'") == "7");
  require(run_scalar_text(
              exact_graph_db_path,
              "SELECT value FROM traceloom_metadata "
              "WHERE key = 'unrecognized_replay_composition_region_count'") ==
          "6");
  // Exact launch without a tree anchor stays in the base table with a NULL
  // anchor_id and must not masquerade as a node-view row.
  require(run_scalar_int(exact_graph_db_path,
                         "SELECT COUNT(*) FROM traceloom_graph_launch") == 1);
  require(run_scalar_int(
              exact_graph_db_path,
              "SELECT COUNT(*) FROM traceloom_graph_launch "
              "WHERE anchor_id IS NULL") == 1);
  require(run_scalar_int(
              exact_graph_db_path,
              "SELECT COUNT(*) FROM "
              "traceloom_v_node_graph_body_member") == 0);
  std::remove(exact_graph_db_path.c_str());

  const std::string exact_cuda_graph_db_path = temp_db_path();
  compat::NativeCompatibilitySidecarOptions cuda_graph_options;
  cuda_graph_options.db_idx = 5;
  cuda_graph_options.source_kind = "cuda_nsys_sqlite";
  cuda_graph_options.source_path = "cuda-exact-graph";
  compat::write_basic_native_compatibility_sidecar(
      exact_cuda_graph_db_path, build_exact_cuda_graph_replay_ir(),
      cuda_graph_options);
  require(run_scalar_text(
              exact_cuda_graph_db_path,
              "SELECT graph_provider FROM traceloom_cuda_graph_replay") ==
          "cuda");
  require(run_scalar_text(
              exact_cuda_graph_db_path,
              "SELECT json_extract(raw_json, '$.reconstruction') FROM "
              "traceloom_cuda_graph_replay") ==
          "exact_replay_composition");
  require(run_scalar_text(
              exact_cuda_graph_db_path,
              "SELECT graph_provider FROM "
              "traceloom_aclgraph_reconstruction_region") ==
          "cuda");
  require(run_scalar_text(
              exact_cuda_graph_db_path,
              "SELECT boundary_policy FROM "
              "traceloom_aclgraph_reconstruction_region") ==
          "direct_observed_graph_launch");
  require(run_scalar_text(
              exact_cuda_graph_db_path,
              "SELECT identity_policy FROM "
              "traceloom_aclgraph_reconstruction_region") ==
          "cuda_graph_node_set");
  require(run_scalar_text(exact_cuda_graph_db_path,
                          "SELECT correlation_id FROM "
                          "traceloom_cuda_graph_replay") == "101");
  require(run_scalar_int(exact_cuda_graph_db_path,
                         "SELECT COUNT(*) FROM traceloom_graph_launch") == 1);
  require(run_scalar_int(exact_cuda_graph_db_path,
                         "SELECT COUNT(*) FROM traceloom_graph_body_member") ==
          3);
  require(run_scalar_text(exact_cuda_graph_db_path,
                          "SELECT correlation_id FROM traceloom_graph_launch") ==
          "101");
  require(run_scalar_text(exact_cuda_graph_db_path,
                          "SELECT anchor_id FROM traceloom_graph_launch") ==
          "anchor-0");
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM traceloom_graph_body_member "
              "WHERE kind = 'compute'") == 2);
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM traceloom_graph_body_member "
              "WHERE kind = 'data_move'") == 1);
  require(run_scalar_int(exact_cuda_graph_db_path,
                         "SELECT COUNT(*) FROM traceloom_replay_cost_unit") ==
          1);
  require(run_scalar_text(
              exact_cuda_graph_db_path,
              "SELECT support_status FROM traceloom_replay_cost_unit") ==
          "supported");
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT task_sum_ns FROM traceloom_replay_cost_launch") == 30);
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT busy_union_ns FROM traceloom_replay_cost_launch") ==
          30);
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT envelope_ns FROM traceloom_replay_cost_launch") == 50);
  require(run_scalar_int(exact_cuda_graph_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_replay_cost_member") == 3);
  require(run_scalar_int(exact_cuda_graph_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_replay_cost_aggregate") == 3);
  require(run_scalar_int(exact_cuda_graph_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_replay_cost_aggregate_member") == 3);
  require(run_scalar_int(exact_cuda_graph_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_v_node_replay_cost_member") == 3);
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT scheduled_work_share_ppm FROM "
              "traceloom_replay_cost_member "
              "WHERE member_id = 'graph-body-member-0'") == 333333);
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM traceloom_graph_body_member "
              "WHERE member_id = 'graph-body-member-0' "
              "AND graph_node_id = 8589934592") == 1);
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT original_graph_node_id FROM "
              "traceloom_graph_body_member "
              "WHERE member_id = 'graph-body-member-0'") == 5000);
  require(run_scalar_text(
              exact_cuda_graph_db_path,
              "SELECT match_policy FROM traceloom_graph_body_member "
              "WHERE member_id = 'graph-body-member-0'") ==
          "cuda_runtime_correlation");
  require(run_scalar_text(
              exact_cuda_graph_db_path,
              "SELECT association_policy FROM traceloom_graph_body_member "
              "WHERE member_id = 'graph-body-member-0'") ==
          "cuda_graph_node_set");
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM traceloom_v_node_graph_body_member "
              "WHERE node_event_id = 'event-0' AND coverage_kind = 'self'") ==
          3);
  require(run_scalar_text(
              exact_cuda_graph_db_path,
              "SELECT node_event_id FROM "
              "traceloom_v_node_graph_body_member "
              "WHERE event_id = 'event-1' AND coverage_kind = 'self'") ==
          "event-0");
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(DISTINCT node_launch_id) FROM "
              "traceloom_v_node_graph_body_member "
              "WHERE event_id = 'event-3'") == 1);
  require(run_scalar_text(
              exact_cuda_graph_db_path,
              "SELECT kind FROM traceloom_v_node_graph_body_member "
              "WHERE event_id = 'event-3' AND coverage_kind = 'self'") ==
          "data_move");
  require(run_scalar_text(
              exact_cuda_graph_db_path,
              "SELECT source_table FROM traceloom_graph_body_member "
              "WHERE event_id = 'event-3'") ==
          "CUPTI_ACTIVITY_KIND_MEMCPY");
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM traceloom_graph_launch l "
              "LEFT JOIN traceloom_anchor a ON a.anchor_id = l.anchor_id "
              "WHERE l.anchor_id != '' AND a.anchor_id IS NULL") == 0);
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM traceloom_graph_body_member m "
              "LEFT JOIN traceloom_graph_launch l "
              "ON l.launch_id = m.launch_id "
              "WHERE l.launch_id IS NULL") == 0);
  // Forward: tree node occurrence -> exact ordered members. The view joins
  // traceloom_viz_node_anchor on explicit anchor_id + db_idx/device_id, so
  // node_id + occurrence_idx identify the containing tree occurrence.
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM traceloom_v_node_graph_body_member v "
              "WHERE v.node_id = (SELECT node_id FROM "
              "traceloom_viz_node_anchor WHERE anchor_id = 'anchor-0' "
              "AND coverage_kind = 'self') "
              "AND v.occurrence_idx = 1") == 3);
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM traceloom_v_node_graph_body_member v "
              "WHERE v.event_id = 'event-1' AND v.occurrence_idx = 1 "
              "AND v.idx_in_occurrence = 0 AND v.anchor_order = 1 "
              "AND v.view_name = 'native_report_tree' "
              "AND v.node_anchor_id = 'anchor-0' "
              "AND v.coverage_kind = 'self'") == 1);
  // Reverse: member event -> containing tree occurrence.
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM traceloom_v_node_graph_body_member v "
              "WHERE v.event_id = 'event-1' AND v.node_id = "
              "(SELECT node_id FROM traceloom_viz_node_anchor WHERE "
              "anchor_id = 'anchor-0' AND coverage_kind = 'self')") == 1);
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM traceloom_graph_body_member m "
              "LEFT JOIN traceloom_event e ON e.event_id = m.event_id "
              "WHERE e.event_id IS NULL") == 0);
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM traceloom_graph_body_member m "
              "LEFT JOIN traceloom_event_source s "
              "ON s.event_id = m.event_id "
              "WHERE s.event_id IS NULL") == 0);
  std::remove(exact_cuda_graph_db_path.c_str());

  const std::string multi_slot_db_path = temp_db_path();
  compat::NativeCompatibilitySidecarOptions multi_slot_options;
  multi_slot_options.db_idx = 7;
  multi_slot_options.source_kind = "ascend";
  multi_slot_options.source_path = "acl-multi-slot-exact";
  compat::write_basic_native_compatibility_sidecar(
      multi_slot_db_path, build_ascend_multi_slot_exact_graph_sql_ir(),
      multi_slot_options);
  require(run_scalar_int(multi_slot_db_path,
                         "SELECT COUNT(*) FROM traceloom_graph_launch") == 2);
  require(run_scalar_int(multi_slot_db_path,
                         "SELECT COUNT(*) FROM traceloom_graph_body_member") ==
          2);
  require(run_scalar_text(
              multi_slot_db_path,
              "SELECT anchor_id FROM traceloom_graph_launch "
              "WHERE member_order = 0") == "anchor-0");
  require(run_scalar_text(
              multi_slot_db_path,
              "SELECT anchor_id FROM traceloom_graph_launch "
              "WHERE member_order = 1") == "anchor-1");
  require(run_scalar_int(
              multi_slot_db_path,
              "SELECT COUNT(DISTINCT body_id) FROM "
              "traceloom_graph_launch") == 2);
  require(run_scalar_text(
              multi_slot_db_path,
              "SELECT graph_provider FROM traceloom_graph_launch LIMIT 1") ==
          "aclgraph");
  require(run_scalar_int(
              multi_slot_db_path,
              "SELECT COUNT(*) FROM traceloom_v_node_graph_body_member "
              "WHERE node_event_id = 'event-0' AND node_member_order = 0 "
              "AND lane_ordinal = 0 AND task_ordinal = 0 "
              "AND coverage_kind = 'self'") == 1);
  require(run_scalar_text(
              multi_slot_db_path,
              "SELECT node_anchor_id FROM "
              "traceloom_v_node_graph_body_member "
              "WHERE node_member_order = 1 AND coverage_kind = 'self'") ==
          "anchor-1");
  require(run_scalar_int(
              multi_slot_db_path,
              "SELECT COUNT(DISTINCT node_launch_id) FROM "
              "traceloom_v_node_graph_body_member WHERE event_id = 'event-2'")
              == 1);
  require(run_scalar_text(
              multi_slot_db_path,
              "SELECT node_event_id FROM "
              "traceloom_v_node_graph_body_member "
              "WHERE event_id = 'event-2' AND coverage_kind = 'self'") ==
          "event-0");
  require(run_scalar_int(
              multi_slot_db_path,
              "SELECT COUNT(*) FROM traceloom_graph_launch l "
              "LEFT JOIN traceloom_anchor a ON a.anchor_id = l.anchor_id "
              "WHERE l.anchor_id != '' AND a.anchor_id IS NULL") == 0);
  require(run_scalar_int(
              multi_slot_db_path,
              "SELECT COUNT(*) FROM traceloom_graph_launch l "
              "LEFT JOIN traceloom_graph_body_member m "
              "ON m.launch_id = l.launch_id "
              "WHERE m.member_id IS NULL") == 0);
  // Both anchored exact launches appear under their promoted tree node
  // occurrences, one member each, at index 0 of their occurrence.
  require(run_scalar_int(
              multi_slot_db_path,
              "SELECT COUNT(*) FROM traceloom_v_node_graph_body_member v "
              "WHERE v.node_id = (SELECT node_id FROM "
              "traceloom_viz_node_anchor WHERE anchor_id = 'anchor-0' "
              "AND coverage_kind = 'self') "
              "AND v.occurrence_idx = 1 AND v.idx_in_occurrence = 0 "
              "AND v.node_anchor_id = 'anchor-0'") == 1);
  require(run_scalar_int(
              multi_slot_db_path,
              "SELECT COUNT(*) FROM traceloom_v_node_graph_body_member v "
              "WHERE v.node_id = (SELECT node_id FROM "
              "traceloom_viz_node_anchor WHERE anchor_id = 'anchor-1' "
              "AND coverage_kind = 'self') "
              "AND v.occurrence_idx = 1 AND v.idx_in_occurrence = 0 "
              "AND v.node_anchor_id = 'anchor-1'") == 1);
  std::remove(multi_slot_db_path.c_str());


  const std::string collective_db_path = temp_db_path();
  compat::NativeCompatibilitySidecarOptions collective_options;
  collective_options.db_idx = 3;
  collective_options.source_kind = "fixture";
  collective_options.source_path = "collective-smoke";
  collective_options.collective_run_name = "collective smoke";
  collective_options.collective_db_name = "db00.traceloom_augmented.db";
  collective_options.collective_expected_world_size = 1;
  compat::write_basic_native_compatibility_sidecar(
      collective_db_path, build_collective_repeat_ir(), collective_options);

  require(run_scalar_int(collective_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_collective_global_link") == 4);
  require(run_scalar_text(collective_db_path,
                          "SELECT op_type FROM "
                          "traceloom_collective_global_link "
                          "ORDER BY idx_in_occurrence LIMIT 1") ==
          "allReduce");
  require(run_scalar_text(collective_db_path,
                          "SELECT validation_status FROM "
                          "traceloom_collective_global_link "
                          "ORDER BY idx_in_occurrence LIMIT 1") ==
          "complete");
  require(run_scalar_text(collective_db_path,
                          "SELECT candidate_collective_key FROM "
                          "traceloom_collective_global_link "
                          "ORDER BY idx_in_occurrence LIMIT 1")
              .find("collective_smoke:LP_M002_01_") == 0);
  require(run_scalar_int(collective_db_path,
                         "SELECT COUNT(*) FROM traceloom_viz_node "
                         "WHERE kind = 'atom' AND symbol = 'HcclAllReduce'") ==
          1);
  require(run_scalar_int(collective_db_path,
                         "SELECT occurrence_count FROM traceloom_viz_node "
                         "WHERE kind = 'atom' AND symbol = 'HcclAllReduce'") ==
          4);

  std::remove(collective_db_path.c_str());

  const std::string idle_db_path = temp_db_path();
  NativeIr idle_ir = build_idle_evidence_ir();
  const SemanticTaskRuleset idle_rules =
      load_default_idle_evidence_semantic_ruleset();
  const IdleEvidencePipelineResult idle_pipeline =
      run_idle_evidence_pipeline(idle_ir, idle_rules);
  compat::NativeCompatibilitySidecarOptions idle_options;
  idle_options.db_idx = 9;
  idle_options.source_kind = "fixture";
  idle_options.source_path = "idle-sidecar";
  idle_options.materialize_grammar_report_tree = false;
  compat::write_basic_native_compatibility_sidecar(
      idle_db_path, idle_ir, idle_options, &idle_pipeline);

  require(run_scalar_int(idle_db_path,
                         "SELECT COUNT(*) FROM traceloom_run_metadata") == 1);
  require(run_scalar_text(idle_db_path,
                          "SELECT analysis_status FROM "
                          "traceloom_run_metadata") == "ok");
  require(run_scalar_text(idle_db_path,
                          "SELECT collection_status FROM "
                          "traceloom_run_metadata") == "unknown");
  require(run_scalar_int(idle_db_path,
                         "SELECT length(run_id) FROM "
                         "traceloom_run_metadata") == 64);
  require(run_scalar_int(idle_db_path,
                         "SELECT json_valid(metadata_json) FROM "
                         "traceloom_run_metadata") == 1);
  require(run_scalar_text(idle_db_path,
                          "SELECT run_id FROM traceloom_run_metadata") ==
          sha256_hex(run_scalar_text(
              idle_db_path,
              "SELECT metadata_json FROM traceloom_run_metadata")));
  require(run_scalar_int(idle_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_device_interval") == 3);
  require(run_scalar_int(idle_db_path,
                         "SELECT SUM(duration_ns) FROM "
                         "traceloom_device_interval") == 400);
  require(run_scalar_int(idle_db_path,
                         "SELECT COUNT(*) FROM traceloom_stream_state") == 9);
  require(run_scalar_int(
              idle_db_path,
              "SELECT COUNT(*) FROM traceloom_idle_explanation") == 3);
  require(run_scalar_int(
              idle_db_path,
              "SELECT duration_ns FROM traceloom_idle_explanation "
              "WHERE category = 'blocked_by_visible_wait'") == 100);
  require(run_scalar_text(
              idle_db_path,
              "SELECT evidence_relation FROM traceloom_idle_explanation "
              "WHERE category = 'blocked_by_visible_wait'") ==
          "device_event_coverage");
  require(run_scalar_text(
              idle_db_path,
              "SELECT evidence_level FROM traceloom_idle_explanation "
              "WHERE category = 'unattributed_visible_idle' LIMIT 1") ==
          "none");
  require(run_scalar_int(
              idle_db_path,
              "SELECT SUM(duration_ns) FROM traceloom_idle_explanation "
              "WHERE category = 'unattributed_visible_idle'") == 100);
  require(run_scalar_int(
              idle_db_path,
              "SELECT COUNT(*) FROM traceloom_evidence_link "
              "WHERE owner_kind = 'explanation' AND source_table = 'TASK' "
              "AND source_key = '12' AND matched_rule_id = "
              "'wait.event_wait'") == 1);
  require(run_scalar_int(
              idle_db_path,
              "SELECT COUNT(*) FROM traceloom_evidence_link "
              "WHERE owner_kind = 'explanation' AND source_key = '14' "
              "AND relation = 'none' AND evidence_level = 'none' "
              "AND overlap_start_ns IS NULL AND overlap_end_ns IS NULL") ==
          1);
  require(run_scalar_int(
              idle_db_path,
              "SELECT COUNT(*) FROM traceloom_idle_explanation e "
              "LEFT JOIN traceloom_device_interval g "
              "ON g.interval_id = e.gap_interval_id "
              "WHERE g.interval_id IS NULL OR "
              "g.interval_kind != 'visible_productive_idle'") == 0);
  require(run_scalar_int(
              idle_db_path,
              "SELECT SUM(duration_ns) FROM "
              "traceloom_anchor_idle_explanation") == 200);
  require(run_scalar_int(
              idle_db_path,
              "SELECT COUNT(*) FROM traceloom_node_idle_explanation") > 0);
  compat::write_basic_native_compatibility_sidecar(idle_db_path, idle_ir,
                                                    idle_options);
  require(run_scalar_int(idle_db_path,
                         "SELECT COUNT(*) FROM traceloom_run_metadata") == 0);
  require(run_scalar_int(
              idle_db_path,
              "SELECT COUNT(*) FROM traceloom_device_interval") == 0);
  require(run_scalar_int(
              idle_db_path,
              "SELECT COUNT(*) FROM traceloom_idle_explanation") == 0);
  require(run_scalar_int(idle_db_path,
                         "SELECT COUNT(*) FROM traceloom_evidence_link") ==
          0);
  std::remove(idle_db_path.c_str());

  // E3 inspects unknown intervals that E2 intentionally excludes from the
  // productive projection. A zero-duration unknown therefore makes E3/E4
  // invalid_input while E2 remains ok. This is an auditable negative result,
  // not a reason to abort sidecar materialization.
  const std::string invalid_idle_db_path = temp_db_path();
  NativeIr invalid_idle_ir = build_idle_evidence_ir();
  const SourceRefId idle_source(0);
  const SymbolId point_type = invalid_idle_ir.symbols.intern("POINT_UNKNOWN");
  const SymbolId point_name = invalid_idle_ir.symbols.intern("UnknownPoint");
  const TraceEventId point_event = invalid_idle_ir.trace_events.append(
      idle_source, 15, 0, 9, 250, 250, point_name);
  invalid_idle_ir.tasks.append(idle_source, point_event, 15, 15, -1,
                               point_type, point_name, point_name, point_type,
                               SymbolId::invalid());
  const IdleEvidencePipelineResult invalid_idle_pipeline =
      run_idle_evidence_pipeline(invalid_idle_ir, idle_rules);
  require(invalid_idle_pipeline.productive_timeline.status ==
              AnalysisStatus::kOk &&
              invalid_idle_pipeline.stream_states.status ==
                  AnalysisStatus::kInvalidInput &&
              invalid_idle_pipeline.idle_explanations.status ==
                  AnalysisStatus::kInvalidInput,
          "zero-duration unknown fixture did not exercise E2/E3 status join");
  compat::write_basic_native_compatibility_sidecar(
      invalid_idle_db_path, invalid_idle_ir, idle_options,
      &invalid_idle_pipeline);
  require(run_scalar_text(invalid_idle_db_path,
                          "SELECT analysis_status FROM "
                          "traceloom_run_metadata") == "invalid_input");
  require(run_scalar_int(invalid_idle_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_idle_explanation") == 3);
  require(run_scalar_text(invalid_idle_db_path,
                          "SELECT json_extract(metadata_json, "
                          "'$.analysis_status') FROM "
                          "traceloom_run_metadata") == "invalid_input");
  std::remove(invalid_idle_db_path.c_str());

  // The analysis database is a new snapshot, not a modified sidecar. It must
  // retain arbitrary raw profiler relations after the source is gone.
  const std::string raw_source_path = temp_db_path();
  const std::string augmented_path = temp_db_path();
  run_sql(raw_source_path,
          "CREATE TABLE RAW_SENTINEL(id INTEGER, payload TEXT);"
          "INSERT INTO RAW_SENTINEL VALUES(7, 'retained profiler evidence')");
  const std::string source_hash_before = sha256_file_hex(raw_source_path);
  compat::NativeCompatibilitySidecarOptions augmented_options;
  augmented_options.source_kind = "cuda_nsys_sqlite";
  compat::write_self_contained_augmented_database(
      augmented_path, raw_source_path, build_exact_cuda_graph_replay_ir(),
      augmented_options);
  require(sha256_file_hex(raw_source_path) == source_hash_before,
          "augmented DB construction modified the input profiler DB");
  require(run_scalar_text(augmented_path,
                          "SELECT value FROM traceloom_metadata WHERE key = "
                          "'artifact_kind'") ==
          "self_contained_augmented_database");
  require(run_scalar_text(augmented_path,
                          "SELECT value FROM traceloom_metadata WHERE key = "
                          "'source_sha256'") == source_hash_before);
  std::remove(raw_source_path.c_str());
  require(run_scalar_text(augmented_path,
                          "SELECT payload FROM RAW_SENTINEL WHERE id = 7") ==
          "retained profiler evidence");
  require(run_scalar_int(augmented_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_v_node_replay_cost_member") == 3);
  require(run_scalar_int(augmented_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_analysis_surface") >= 8);
  require_analysis_surface_queries_prepare(augmented_path);
  require(run_scalar_text(augmented_path,
                          "SELECT embedded_table_name FROM "
                          "traceloom_raw_table WHERE source_table = "
                          "'RAW_SENTINEL'") == "RAW_SENTINEL");
  std::remove(augmented_path.c_str());

  const std::string deterministic_raw_source = temp_db_path();
  run_sql(deterministic_raw_source,
          "CREATE TABLE RAW_SENTINEL(id INTEGER, payload TEXT);"
          "INSERT INTO RAW_SENTINEL VALUES(7, 'retained profiler evidence')");
  const std::string collective_augmented_a = temp_db_path();
  const std::string collective_augmented_b = temp_db_path();
  compat::NativeCompatibilitySidecarOptions deterministic_options;
  deterministic_options.source_kind = "fixture";
  deterministic_options.source_path = "/stable/logical/profile";
  deterministic_options.collective_expected_world_size = 1;
  compat::write_self_contained_augmented_database(
      collective_augmented_a, deterministic_raw_source,
      build_collective_repeat_ir(), deterministic_options);
  compat::write_self_contained_augmented_database(
      collective_augmented_b, deterministic_raw_source,
      build_collective_repeat_ir(), deterministic_options);
  require(sha256_file_hex(collective_augmented_a) ==
              sha256_file_hex(collective_augmented_b),
          "augmented DB bytes depend on output/temp path");
  require(run_scalar_text(collective_augmented_a,
                          "SELECT DISTINCT db_name FROM "
                          "traceloom_collective_global_link") ==
          "profile.traceloom.db");
  std::remove(collective_augmented_a.c_str());
  std::remove(collective_augmented_b.c_str());
  std::remove(deterministic_raw_source.c_str());

  // Split layouts package every raw DB in one portable artifact. Identical
  // vendor table names must remain distinct and preserve the original rowid
  // as an explicit queryable column.
  const std::string split_source_a = temp_db_path();
  const std::string split_source_b = temp_db_path();
  const std::string split_augmented = temp_db_path();
  run_sql(split_source_a,
          "CREATE TABLE RAW_SHARED(id INTEGER, payload TEXT);"
          "INSERT INTO RAW_SHARED VALUES(1, 'source-a')");
  run_sql(split_source_b,
          "CREATE TABLE RAW_SHARED(id INTEGER, payload TEXT);"
          "INSERT INTO RAW_SHARED VALUES(2, 'source-b')");
  const std::string split_source_a_hash = sha256_file_hex(split_source_a);
  const std::string split_source_b_hash = sha256_file_hex(split_source_b);
  compat::NativeCompatibilitySidecarOptions split_options;
  split_options.source_kind = "ascend_sqlite_split";
  split_options.source_path = "/portable/original/split-profile";
  compat::write_self_contained_augmented_database(
      split_augmented,
      std::vector<std::string>{split_source_b, split_source_a},
      build_exact_cuda_graph_replay_ir(), split_options);
  require(sha256_file_hex(split_source_a) == split_source_a_hash &&
              sha256_file_hex(split_source_b) == split_source_b_hash,
          "multi-source augmented DB construction modified a raw input");
  require(run_scalar_int(split_augmented,
                         "SELECT COUNT(*) FROM "
                         "traceloom_raw_source_database") == 2);
  require(run_scalar_int(split_augmented,
                         "SELECT COUNT(*) FROM traceloom_raw_table WHERE "
                         "source_table = 'RAW_SHARED'") == 2);
  require(run_scalar_int(split_augmented,
                         "SELECT COUNT(*) FROM "
                         "traceloom_raw_000__RAW_SHARED") == 1);
  require(run_scalar_int(split_augmented,
                         "SELECT COUNT(*) FROM "
                         "traceloom_raw_001__RAW_SHARED") == 1);
  require(run_scalar_text(split_augmented,
                          "SELECT source_rowid_column FROM "
                          "traceloom_raw_table WHERE source_id = "
                          "'raw-source-000' AND source_table = "
                          "'RAW_SHARED'") ==
          "__traceloom_source_rowid__");
  require(run_scalar_int(split_augmented,
                         "SELECT __traceloom_source_rowid__ FROM "
                         "traceloom_raw_000__RAW_SHARED") == 1);
  std::remove(split_source_a.c_str());
  std::remove(split_source_b.c_str());
  require(run_scalar_int(split_augmented,
                         "SELECT sum(id) FROM (SELECT id FROM "
                         "traceloom_raw_000__RAW_SHARED UNION ALL SELECT id "
                         "FROM traceloom_raw_001__RAW_SHARED)") == 3);
  std::remove(split_augmented.c_str());
  return 0;
}
