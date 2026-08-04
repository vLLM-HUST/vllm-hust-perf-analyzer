#include "traceloom/compat/native_sidecar_materializer.h"
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
  const GraphTemplateId graph_template =
      ir.graph_templates.append(source, 33, 1);
  const ReplayUnitId unit = ir.replay_units.append(
      graph_template, source, AnchorId::invalid(), AnchorId::invalid(),
      launch, region);
  ir.replay_unit_launch_members.append(unit, 0, occurrence, slot);
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
                         "SELECT COUNT(*) FROM traceloom_metadata") == 10);
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
              "traceloom_aclgraph_reconstruction_region") == 6);
  require(run_scalar_int(
              exact_graph_db_path,
              "SELECT COUNT(*) FROM "
              "traceloom_aclgraph_reconstruction_region "
              "WHERE status LIKE 'unrecognized_%'") == 5);
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
              "traceloom_aclgraph_reconstruction_region") == 6);
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
              "WHERE key = 'replay_composition_region_count'") == "6");
  require(run_scalar_text(
              exact_graph_db_path,
              "SELECT value FROM traceloom_metadata "
              "WHERE key = 'unrecognized_replay_composition_region_count'") ==
          "5");
  std::remove(exact_graph_db_path.c_str());

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
  return 0;
}
