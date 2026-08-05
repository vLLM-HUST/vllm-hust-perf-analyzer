#include "traceloom/analysis/native_pipeline.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/materialize/native_result_json.h"
#include "traceloom/testing/test_util.h"

#include <sstream>
#include <string>

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "json_smoke", "TASK", 0);
  const SymbolId a = ir.symbols.intern("A");
  const SymbolId b = ir.symbols.intern("B");
  const TraceEventId event0 = ir.trace_events.append(source, 1, 0, 0, 0, 10, a);
  const TraceEventId event1 = ir.trace_events.append(source, 2, 0, 0, 10, 20, b);
  const TraceEventId event2 = ir.trace_events.append(source, 3, 0, 0, 20, 30, a);
  const TraceEventId event3 = ir.trace_events.append(source, 4, 0, 0, 30, 40, b);
  ir.tasks.append(source, event0, 1, 1001, -1, a, a, a,
                  SymbolId::invalid(), SymbolId::invalid());
  ir.tasks.append(source, event1, 2, 1002, -1, b, b, b,
                  SymbolId::invalid(), SymbolId::invalid());
  ir.tasks.append(source, event2, 3, 1003, -1, a, a, a,
                  SymbolId::invalid(), SymbolId::invalid());
  ir.tasks.append(source, event3, 4, 1004, -1, b, b, b,
                  SymbolId::invalid(), SymbolId::invalid());
  const GraphSlotTemplateId slot_template = ir.graph_slot_templates.append(
      source, 12345, ir.symbols.intern("aclnnMuls\naclnnAdds\n"));
  const CapturedGraphInstanceId captured_instance =
      ir.captured_graph_instances.append(
          source, 8, 0, 42, 99, 0, slot_template, 1,
          CaptureAssociationPolicy::kCaptureOrdinalAligned);
  ir.captured_graph_streams.append(captured_instance, source, 8, 0, 11);
  const GraphLaunchOccurrenceId graph_launch =
      ir.graph_launch_occurrences.append(
      source, source, 0, 7, 1001, 9001, 42, StreamId::invalid(),
      StreamId::invalid(), captured_instance, TaskId(0), TaskId(0), TaskId(0),
      0, 10, 0,
      GraphLaunchMatchPolicy::kNotifyCompletionAdjacent,
      GraphLaunchInstanceAssociationPolicy::kRecordModelStream);
  const GraphLaunchActivityId graph_activity =
      ir.graph_launch_activities.append(
          source, 123, 7, 7, 9,
          ir.symbols.intern("aclrtSynchronizeStreamWithTimeout"), 0, 12, 1,
          1, GraphLaunchActivityBoundaryPolicy::kHostBlockingSync);
  ir.graph_launch_activity_members.append(graph_activity, graph_launch, 0);
  const ReplayBodyTemplateId body_template = ir.replay_body_templates.append(
      source, 24680, ir.symbols.intern("Muls\nAdds\nRelu"), 3, 0, 1,
      ReplayBodyTopologyPolicy::kSingleModelStream);
  const GraphLaunchBodyId graph_body = ir.graph_launch_bodies.append(
      graph_launch, body_template, TaskId(0), TaskId(0), 3, 0, 1);
  ir.graph_launch_body_members.append(
      graph_body, TaskId(0), 0, 0,
      GraphLaunchBodyMemberRow::Kind::kCompute);
  const ReplayCompositionCandidateId composition =
      ir.replay_composition_candidates.append(
          source, 0, graph_launch, graph_launch, 4, 0, 1, 4, 0, 67890,
          ReplayCompositionIdentityPolicy::kCapturedGraphInstance,
          ReplayCompositionOrderPolicy::kHostSubmissionOrder,
          ReplayCompositionShapePolicy::kUnclassified,
          ReplayCompositionBoundaryPolicy::kExactPeriodicSuffix);
  ir.replay_composition_slots.append(composition, 0, captured_instance,
                                     slot_template, body_template,
                                     ReplayCompositionSlotRole::kGeneric,
                                     9001);
  ir.replay_composition_regions.append(
      composition, 0, graph_launch, graph_launch, 0, 10, 1, 1,
      ReplayCompositionRegionStatus::kRecognizedCompletePattern);
  ir.replay_composition_regions.append(
      composition, 1, graph_launch, graph_launch, 0, 10, 1, 4,
      ReplayCompositionRegionStatus::kUnrecognizedIncompleteTail);
  ir.replay_composition_regions.append(
      composition, 2, graph_launch, graph_launch, 0, 10, 1, 0,
      ReplayCompositionRegionStatus::kUnrecognizedLeadingContext);
  ir.replay_composition_regions.append(
      composition, 3, graph_launch, graph_launch, 0, 10, 1, 1,
      ReplayCompositionRegionStatus::kUnrecognizedMissingBodyEvidence);
  ir.replay_composition_regions.append(
      composition, 4, graph_launch, graph_launch, 0, 10, 1, 1,
      ReplayCompositionRegionStatus::kUnrecognizedBodyMismatch);
  ir.replay_composition_regions.append(
      composition, 5, graph_launch, graph_launch, 0, 10, 1, 1,
      ReplayCompositionRegionStatus::
          kUnrecognizedMissingCompletionEvidence);
  ir.replay_composition_regions.append(
      composition, 6, graph_launch, graph_launch, 0, 10, 1, 1,
      ReplayCompositionRegionStatus::kUnrecognizedMissingBodyCapability);
  ir.replay_composition_region_members.append(
      ReplayCompositionRegionId(0), 0, graph_launch, 0);
  ir.replay_composition_region_members.append(
      ReplayCompositionRegionId(1), 0, graph_launch, 0);
  ir.replay_composition_region_members.append(
      ReplayCompositionRegionId(2), 0, graph_launch, -1);
  ir.replay_composition_region_members.append(
      ReplayCompositionRegionId(3), 0, graph_launch, 0);
  ir.replay_composition_region_members.append(
      ReplayCompositionRegionId(4), 0, graph_launch, 0);
  ir.replay_composition_region_members.append(
      ReplayCompositionRegionId(5), 0, graph_launch, -1);
  ir.replay_composition_region_members.append(
      ReplayCompositionRegionId(6), 0, graph_launch, -1);
  const GraphTemplateId graph_template =
      ir.graph_templates.append(source, 97531, 1);
  const ReplayUnitId replay_unit = ir.replay_units.append(
      graph_template, source, AnchorId::invalid(), AnchorId::invalid(), event0,
      ReplayCompositionRegionId(0));
  ir.replay_unit_launch_members.append(replay_unit, 0, graph_launch,
                                       ReplayCompositionSlotId(0));

  NativePipelineOptions pipeline_options;
  pipeline_options.thread_count = 2;
  pipeline_options.partition_config = PartitionPlanConfig{2, 3};
  pipeline_options.candidate_scan_config = CandidateScanConfig{2, 3};
  const NativePipelineResult result =
      run_native_pipeline(ir, pipeline_options);

  NativeResultJsonOptions json_options;
  json_options.source_kind = "fixture";
  json_options.source_path = "json_smoke";
  json_options.thread_count = 2;
  json_options.top_candidate_limit = 2;
  json_options.native_ir = &ir;
  const SemanticTaskClassificationResult semantic_classification =
      classify_semantic_tasks(ir,
                              load_default_idle_evidence_semantic_ruleset());
  json_options.semantic_task_classification = &semantic_classification;

  AnchorInternalCostBreakdown breakdown;
  AnchorInternalCostBreakdownRow row;
  row.anchor_occurrence_id = ReportNodeOccurrenceId(7);
  row.anchor_def_id = ReportNodeDefId(3);
  row.anchor_idx = 2;
  row.symbol = "ACLL";
  row.anchor_kind = ReportAnchorKind::kGraphL;
  row.total_ns = 100;
  row.graph_child_ns = 100;
  row.raw_child_task_count = 20;
  row.top_ops = "MatMul:16";
  breakdown.rows.push_back(row);
  json_options.anchor_internal_cost_breakdown = &breakdown;

  std::ostringstream out;
  write_native_result_json(out, ir.symbols, result, json_options);
  const std::string json = out.str();

  require(json.find("\"schema_version\": \"native_in_memory_result_v1\"") !=
          std::string::npos);
  require(json.find("\"kind\": \"fixture\"") != std::string::npos);
  require(json.find("\"trace_event_count\": 4") != std::string::npos);
  require(json.find("\"graph_launch_occurrence_count\": 1") !=
          std::string::npos);
  require(json.find("\"graph_slot_template_count\": 1") !=
          std::string::npos);
  require(json.find("\"captured_graph_instance_count\": 1") !=
          std::string::npos);
  require(json.find("\"captured_graph_stream_count\": 1") !=
          std::string::npos);
  require(json.find("\"graph_launch_instance_linked_count\": 1") !=
          std::string::npos);
  require(json.find("\"graph_launch_completion_adjacent_count\": 1") !=
          std::string::npos);
  require(json.find("\"graph_launch_ordered_fallback_count\": 0") !=
          std::string::npos);
  require(json.find("\"graph_launch_unmatched_count\": 0") !=
          std::string::npos);
  require(json.find("\"replay_body_template_count\": 1") !=
          std::string::npos);
  require(json.find("\"graph_launch_body_count\": 1") !=
          std::string::npos);
  require(json.find("\"graph_launch_body_member_count\": 1") !=
          std::string::npos);
  require(json.find("\"graph_launch_body_members\": [") !=
          std::string::npos);
  require(json.find("\"operator\": \"A\"") != std::string::npos);
  require(json.find("\"duration_ns\": 10") != std::string::npos);
  require(json.find("\"semantic_role\": \"unknown\"") !=
          std::string::npos);
  require(json.find("\"semantic_operator_coverage\": {") !=
          std::string::npos);
  require(json.find("\"unique_unregistered_operator_count\": 2") !=
          std::string::npos);
  require(json.find("\"graph_body_cost_summary\": {") !=
          std::string::npos);
  require(json.find("\"task_sum_ns\": 10") != std::string::npos);
  require(json.find("\"busy_union_ns\": 10") != std::string::npos);
  require(json.find("\"topology_policy\": \"single_model_stream\"") !=
          std::string::npos);
  require(json.find("\"stream_count\": 1") != std::string::npos);
  require(json.find("\"communication_task_count\": 0") !=
          std::string::npos);
  require(json.find("\"normalized_task_count\": 3") !=
          std::string::npos);
  require(json.find("\"graph_launch_activity_count\": 1") !=
          std::string::npos);
  require(json.find("\"graph_launch_activity_member_count\": 1") !=
          std::string::npos);
  require(json.find("\"graph_launch_activity_host_sync_count\": 1") !=
          std::string::npos);
  require(json.find(
              "\"graph_launch_activity_unmatched_host_execute_count\": 0") !=
          std::string::npos);
  require(json.find("\"replay_composition_candidate_count\": 1") !=
          std::string::npos);
  require(json.find("\"replay_composition_body_confirmed_count\": 1") !=
          std::string::npos);
  require(json.find("\"replay_composition_slot_count\": 1") !=
          std::string::npos);
  require(json.find("\"replay_composition_region_count\": 7") !=
          std::string::npos);
  require(json.find("\"replay_composition_region_member_count\": 7") !=
          std::string::npos);
  require(json.find(
              "\"replay_composition_recognized_region_count\": 1") !=
          std::string::npos);
  require(json.find(
              "\"replay_composition_unrecognized_region_count\": 6") !=
          std::string::npos);
  require(json.find("\"replay_unit_count\": 1") != std::string::npos);
  require(json.find("\"exact_replay_unit_count\": 1") !=
          std::string::npos);
  require(json.find("\"replay_unit_launch_member_count\": 1") !=
          std::string::npos);
  require(json.find("\"graph_launch_occurrences\": [") !=
          std::string::npos);
  require(json.find("\"match_policy\": \"notify_completion_adjacent\"") !=
          std::string::npos);
  require(json.find(
              "\"instance_association_policy\": \"record_model_stream\"") !=
          std::string::npos);
  require(json.find("\"host_api_source_row_id\": 7") !=
          std::string::npos);
  require(json.find("\"graph_connection_id\": 9001") !=
          std::string::npos);
  require(json.find("\"model_id\": 42") != std::string::npos);
  require(json.find("\"association_policy\": \"capture_ordinal_aligned\"") !=
          std::string::npos);
  require(json.find("\"graph_launch_activities\": [") !=
          std::string::npos);
  require(json.find("\"replay_body_templates\": [") !=
          std::string::npos);
  require(json.find("\"op_sequence\": \"Muls\\nAdds\\nRelu\"") !=
          std::string::npos);
  require(json.find("\"replay_body_template_id\": 0") !=
          std::string::npos);
  require(json.find("\"boundary_policy\": \"host_blocking_sync\"") !=
          std::string::npos);
  require(json.find("\"boundary_host_api_source_row_id\": 9") !=
          std::string::npos);
  require(json.find("\"slot_template_id\": 0") != std::string::npos);
  require(json.find("\"boundary_policy\": \"exact_periodic_suffix\"") !=
          std::string::npos);
  require(json.find("\"order_policy\": \"host_submission_order\"") !=
          std::string::npos);
  require(json.find("\"shape_policy\": \"unclassified\"") !=
          std::string::npos);
  require(json.find("\"role\": \"generic_slot\"") !=
          std::string::npos);
  require(json.find("\"replay_composition_regions\": [") !=
          std::string::npos);
  require(json.find("\"replay_composition_region_members\": [") !=
          std::string::npos);
  require(json.find("\"expected_slot_order\": null") !=
          std::string::npos);
  require(json.find("\"status\": \"recognized_complete_pattern\"") !=
          std::string::npos);
  require(json.find("\"status\": \"unrecognized_incomplete_tail\"") !=
          std::string::npos);
  require(json.find("\"status\": \"unrecognized_leading_context\"") !=
          std::string::npos);
  require(json.find(
              "\"status\": \"unrecognized_missing_body_evidence\"") !=
          std::string::npos);
  require(json.find("\"status\": \"unrecognized_body_mismatch\"") !=
          std::string::npos);
  require(json.find(
              "\"status\": \"unrecognized_missing_completion_evidence\"") !=
          std::string::npos);
  require(json.find(
              "\"status\": \"unrecognized_missing_body_capability\"") !=
          std::string::npos);
  require(json.find("\"expected_launch_count\": null") !=
          std::string::npos);
  require(json.find("\"replay_units\": [") != std::string::npos);
  require(json.find("\"replay_composition_region_id\": 0") !=
          std::string::npos);
  require(json.find("\"replay_unit_launch_members\": [") !=
          std::string::npos);
  require(json.find("\"replay_internal_cost_map\": {") !=
              std::string::npos,
          "replay_internal_cost_map section");
  require(json.find("\"resolved_launch_count\": 1") !=
              std::string::npos,
          "resolved_launch_count");
  require(json.find("\"unsupported_launch_count\": 0") !=
              std::string::npos,
          "unsupported_launch_count");
  require(json.find("\"fully_supported_unit_count\": 1") !=
              std::string::npos,
          "fully_supported_unit_count");
  require(json.find("\"slot_role\": \"generic_slot\"") !=
              std::string::npos,
          "slot_role generic_slot");
  require(json.find("\"reason_code\": null") != std::string::npos,
          "reason_code null");
  require(json.find("\"aligned_aggregates\": [") !=
              std::string::npos,
          "aligned_aggregates");
  require(json.find("\"distribution_supported\": true") !=
              std::string::npos,
          "distribution_supported true");
  require(json.find("\"duration_median_ns\": 10") !=
              std::string::npos,
          "duration_median_ns 10");
  require(json.find("\"identity\": \"A\"") != std::string::npos,
          "identity A");
  require(json.find("\"graph_launch_body_member_id\": 0") !=
              std::string::npos,
          "graph_launch_body_member_id");
  require(json.find("\"aggregation_scope\": \"role_collapsed\"") !=
              std::string::npos,
          "aggregation_scope role_collapsed");
  require(json.find("\"scheduled_work_share_ppm\": 1000000") !=
              std::string::npos,
          "scheduled_work_share_ppm");
  require(json.find("\"scheduled_work_share_supported\": true") !=
              std::string::npos,
          "scheduled_work_share_supported");
  require(json.find(
              "\"scheduled_work_denominator_body_task_sum_ns\": 10") !=
              std::string::npos,
          "scheduled_work_denominator_body_task_sum_ns");
  require(json.find("\"scheduled_work_share_ppm\": 1000000") !=
              std::string::npos,
          "aggregate scheduled_work_share_ppm");
  require(json.find("\"full_repeat_count\": 4") != std::string::npos);
  require(json.find("\"candidate_distinct_count\"") != std::string::npos);
  require(json.find("\"anchor_internal_cost_breakdown\"") !=
          std::string::npos);

  NativeIr fixture_ir;
  const SourceRefId fixture_source = fixture_ir.source_refs.append(
      "fixture", "aclgraph_fixture", "ACLGRAPH_REPLAY_UNIT", 0);
  const GraphTemplateId fixture_template =
      fixture_ir.graph_templates.append(fixture_source, 1, 1);
  fixture_ir.replay_units.append(
      fixture_template, fixture_source, AnchorId::invalid(),
      AnchorId::invalid(), TraceEventId::invalid());
  NativeResultJsonOptions fixture_options;
  fixture_options.native_ir = &fixture_ir;
  std::ostringstream fixture_out;
  write_native_result_json(fixture_out, fixture_ir.symbols,
                           NativePipelineResult{}, fixture_options);
  const std::string fixture_json = fixture_out.str();
  require(fixture_json.find(
              "\"device_id\": null, \"stream_id\": null, "
              "\"start_ns\": null, \"end_ns\": null") !=
          std::string::npos);
  require(fixture_json.find("\"empty_replay_unit\"") !=
              std::string::npos,
          "fixture empty_replay_unit");
  require(fixture_json.find("\"unsupported_unit_count\": 1") !=
              std::string::npos,
          "fixture unsupported_unit_count");
  require(json.find("\"anchor_kind\": \"graph_l\"") != std::string::npos);
  require(json.find("\"graph_child_ns\": 100") != std::string::npos);
  require(json.find("\"raw_child_task_count\": 20") != std::string::npos);
  require(json.find("\"top_ops\": \"MatMul:16\"") != std::string::npos);
  require(json.find("\"candidates_preview\"") != std::string::npos);

  return 0;
}
