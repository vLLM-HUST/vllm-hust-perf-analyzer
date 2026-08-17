#include "native_sidecar_materializer_test_support.h"

#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/testing/test_util.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace traceloom::testing::sidecar_materializer {
namespace {

using namespace traceloom;

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

void run_graph_materializer_tests() {
  using namespace traceloom;
  using traceloom::testing::require;

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
  cuda_graph_options.evidence_role_config
      .skip_tasks_covered_by_communication_ops = true;
  cuda_graph_options.evidence_role_config.skip_events_covered_by_replay_units =
      true;
  cuda_graph_options.evidence_role_config.filter_auxiliary_task_anchors = true;
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
  require(run_scalar_text(
              exact_cuda_graph_db_path,
              "SELECT support_status FROM "
              "traceloom_replay_body_pattern_run") == "supported");
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM "
              "traceloom_replay_body_pattern_domain WHERE support_status = "
              "'supported' AND position_count = 3") == 1);
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM traceloom_replay_body_position") == 3);
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM "
              "traceloom_replay_body_pattern_occurrence WHERE "
              "parent_occurrence_id IS NULL AND position_start = 0 AND "
              "position_end_exclusive = 3 AND "
              "duration_median_sum_ns = 30") == 1);
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM "
              "traceloom_v_replay_body_pattern_member WHERE occurrence_id "
              "= 'replay-body-domain-0-occurrence-0'") == 3);
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM "
              "traceloom_v_replay_body_pattern_source_locator WHERE "
              "occurrence_id = 'replay-body-domain-0-occurrence-0'") == 3);
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
  // Atomic graph protection stays visible as final_role without erasing each
  // member's nested policy role.  This view is launch-scoped, so tree
  // ancestors cannot duplicate the Position realization rows.
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM "
              "traceloom_v_replay_position_realization_member") == 3);
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM "
              "traceloom_v_replay_position_realization_member WHERE "
              "position_anchor_id = 'anchor-0' AND policy_role IS NOT NULL "
              "AND final_role = 'protected_boundary' AND "
              "effective_structural_participation = 'atomic_boundary' AND "
              "membership_relation = 'exact_graph_body_member' AND "
              "interval_relation = 'contained' AND "
              "observation_semantics = 'timestamp_order_not_dependency' "
              "AND evidence_level = 'exact_direct'") == 3);
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT COUNT(*) FROM "
              "traceloom_v_replay_position_realization_member position "
              "JOIN traceloom_evidence_role_decision role ON "
              "role.decision_id = position.role_decision_id WHERE "
              "position.policy_role IS role.policy_role AND "
              "position.final_role = role.final_role") == 3);
  // Force a lane-major order that differs from timestamp order.  The formal
  // plane must remain graph_a_gemm(0), graph_a_gemm(1), memcpy rather than
  // grouping the two lane-0 members ahead of the lane-1 member.
  run_sql(exact_cuda_graph_db_path,
          "UPDATE traceloom_replay_cost_member SET lane_ordinal = 1, "
          "task_ordinal = 0 WHERE member_id = 'graph-body-member-1'; "
          "UPDATE traceloom_replay_cost_member SET lane_ordinal = 0, "
          "task_ordinal = 1 WHERE member_id = 'graph-body-member-2';");
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT observed_order FROM "
              "traceloom_v_replay_position_realization_member WHERE "
              "member_id = 'graph-body-member-1'") == 1);
  require(run_scalar_int(
              exact_cuda_graph_db_path,
              "SELECT observed_order FROM "
              "traceloom_v_replay_position_realization_member WHERE "
              "member_id = 'graph-body-member-2'") == 2);
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



}

}  // namespace traceloom::testing::sidecar_materializer
