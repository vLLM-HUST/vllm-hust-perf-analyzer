#include "traceloom/ir/anchor_table.h"
#include "traceloom/ir/capture_slot_table.h"
#include "traceloom/ir/captured_graph_instance_table.h"
#include "traceloom/ir/communication_op_table.h"
#include "traceloom/ir/graph_launch_activity_table.h"
#include "traceloom/ir/graph_launch_body_table.h"
#include "traceloom/ir/graph_launch_occurrence_table.h"
#include "traceloom/ir/graph_slot_template_table.h"
#include "traceloom/ir/graph_template_table.h"
#include "traceloom/ir/protected_interval_table.h"
#include "traceloom/ir/replay_composition_candidate_table.h"
#include "traceloom/ir/replay_unit_table.h"
#include "traceloom/ir/source_ref_table.h"
#include "traceloom/ir/stream_table.h"
#include "traceloom/ir/task_table.h"
#include "traceloom/ir/token_table.h"
#include "traceloom/ir/trace_event_table.h"
#include "traceloom/testing/test_util.h"

#include <stdexcept>

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  SourceRefTable sources;
  const SourceRefId source0 =
      sources.append("ascend_sqlite_hot_path", "msprof.db", "TASK", 42);

  require(source0.valid());
  require(source0.value() == 0);
  require(sources.size() == 1);
  require(sources.row(source0).table_name == "TASK");
  require(sources.row(source0).row_id == 42);

  StreamTable streams;
  const StreamId stream0 = streams.append(source0, 0, 3);
  require(stream0.valid());
  require(streams.row(stream0).source_ref_id == source0);
  require(streams.row(stream0).device_id == 0);
  require(streams.row(stream0).raw_stream_id == 3);

  TraceEventTable events;
  const SymbolId raw_name0(7);
  const TraceEventId event0 =
      events.append(source0, 42, 0, streams.row(stream0).raw_stream_id, 100,
                    160, raw_name0);

  require(event0.valid());
  require(event0.value() == 0);
  require(events.size() == 1);

  const TraceEventRow& row = events.row(event0);
  require(row.id == event0);
  require(row.source_ref_id == source0);
  require(row.source_row_id == 42);
  require(row.device_id == 0);
  require(row.stream_id == 3);
  require(row.start_ns == 100);
  require(row.end_ns == 160);
  require(row.raw_name_symbol_id == raw_name0);

  TaskTable tasks;
  const SymbolId task_type0(11);
  const SymbolId op_name0(12);
  const SymbolId op_type0(13);
  const SymbolId compute_task_type0(14);
  const SymbolId comm_name0(15);
  const SymbolId communication_task_type0(16);
  const TaskId task0 =
      tasks.append(source0, event0, 99, 123456, 777, task_type0, op_name0,
                   op_type0, compute_task_type0, comm_name0, 88,
                   communication_task_type0);
  require(task0.valid());
  require(tasks.row(task0).trace_event_id == event0);
  require(tasks.row(task0).raw_task_id == 99);
  require(tasks.row(task0).raw_global_task_id == 123456);
  require(tasks.row(task0).raw_connection_id == 777);
  require(tasks.row(task0).task_type_symbol_id == task_type0);
  require(tasks.row(task0).op_name_symbol_id == op_name0);
  require(tasks.row(task0).op_type_symbol_id == op_type0);
  require(tasks.row(task0).compute_task_type_symbol_id == compute_task_type0);
  require(tasks.row(task0).comm_name_symbol_id == comm_name0);
  require(tasks.row(task0).raw_model_id == 88);
  require(tasks.row(task0).communication_task_type_symbol_id ==
          communication_task_type0);

  CommunicationOpTable communication_ops;
  const CommunicationOpId comm0 =
      communication_ops.append(source0, event0, 777, 55, 3, 2, SymbolId(31),
                               SymbolId(32), SymbolId(33), SymbolId(34));
  require(comm0.valid());
  require(communication_ops.row(comm0).source_ref_id == source0);
  require(communication_ops.row(comm0).trace_event_id == event0);
  require(communication_ops.row(comm0).raw_connection_id == 777);
  require(communication_ops.row(comm0).raw_op_id == 55);
  require(communication_ops.row(comm0).linked_task_count == 3);
  require(communication_ops.row(comm0).linked_stream_count == 2);
  require(communication_ops.row(comm0).op_name_symbol_id == SymbolId(31));
  require(communication_ops.row(comm0).op_type_symbol_id == SymbolId(32));
  require(communication_ops.row(comm0).linked_task_name_symbol_id ==
          SymbolId(33));
  require(communication_ops.row(comm0).linked_task_type_symbol_id ==
          SymbolId(34));

  GraphTemplateTable graph_templates;
  const GraphTemplateId template0 =
      graph_templates.append(source0, 0xabcdu, 2);
  require(template0.valid());
  require(graph_templates.row(template0).source_ref_id == source0);
  require(graph_templates.row(template0).body_sequence_hash == 0xabcdu);
  require(graph_templates.row(template0).slot_count == 2);

  CaptureSlotTable capture_slots;
  const CaptureSlotId slot0 =
      capture_slots.append(template0, source0, 0, RoleId(1), SymbolId(21),
                           event0, event0);
  const CaptureSlotId slot1 =
      capture_slots.append(template0, source0, 1, RoleId(2), SymbolId(22),
                           TraceEventId::invalid(), TraceEventId::invalid());
  require(slot0.valid());
  require(slot1.valid());
  require(capture_slots.row(slot0).graph_template_id == template0);
  require(capture_slots.row(slot0).slot_order == 0);
  require(capture_slots.row(slot0).slot_role_id == RoleId(1));
  require(capture_slots.row(slot0).slot_symbol_id == SymbolId(21));
  require(capture_slots.row(slot0).first_event_id == event0);
  require(capture_slots.row(slot1).slot_order == 1);
  require(!capture_slots.row(slot1).first_event_id.valid());

  GraphSlotTemplateTable slot_templates;
  const GraphSlotTemplateId slot_template0 =
      slot_templates.append(source0, 0x1234u, SymbolId(23));
  require(slot_template0.valid());
  require(slot_templates.row(slot_template0).body_sequence_hash == 0x1234u);

  CapturedGraphInstanceTable captured_instances;
  const CapturedGraphInstanceId captured_instance0 =
      captured_instances.append(
          source0, 52, 0, 88, 1234, 0, slot_template0, 1,
          CaptureAssociationPolicy::kCaptureOrdinalAligned);
  require(captured_instance0.valid());
  require(captured_instances.row(captured_instance0).raw_model_id == 88);
  require(captured_instances.row(captured_instance0).slot_template_id ==
          slot_template0);

  CapturedGraphStreamTable captured_streams;
  const CapturedGraphStreamId captured_stream0 = captured_streams.append(
      captured_instance0, source0, 52, 3, 11);
  require(captured_stream0.valid());
  require(captured_streams.row(captured_stream0).raw_original_stream_id == 3);
  require(captured_streams.row(captured_stream0).raw_model_stream_id == 11);

  GraphLaunchOccurrenceTable graph_launches;
  const GraphLaunchOccurrenceId launch0 = graph_launches.append(
      source0, source0, 0, 51, 777, 9001, 88, stream0, stream0,
      captured_instance0, task0, task0, task0, 100, 160, -4,
      GraphLaunchMatchPolicy::kNotifyCompletionAdjacent,
      GraphLaunchInstanceAssociationPolicy::kRecordModelId);
  require(launch0.valid());
  require(graph_launches.row(launch0).source_ref_id == source0);
  require(graph_launches.row(launch0).host_api_source_ref_id == source0);
  require(graph_launches.row(launch0).raw_host_api_row_id == 51);
  require(graph_launches.row(launch0).raw_launch_connection_id == 777);
  require(graph_launches.row(launch0).raw_graph_connection_id == 9001);
  require(graph_launches.row(launch0).raw_model_id == 88);
  require(graph_launches.row(launch0).execute_stream_id == stream0);
  require(graph_launches.row(launch0).captured_graph_instance_id ==
          captured_instance0);
  require(graph_launches.row(launch0).model_execute_task_id == task0);
  require(graph_launches.row(launch0).wait_record_end_delta_ns == -4);
  require(graph_launches.row(launch0).match_policy ==
          GraphLaunchMatchPolicy::kNotifyCompletionAdjacent);
  require(graph_launches.row(launch0).instance_association_policy ==
          GraphLaunchInstanceAssociationPolicy::kRecordModelId);

  ReplayBodyTemplateTable replay_body_templates;
  const ReplayBodyTemplateId body_template0 = replay_body_templates.append(
      source0, 0x2345u, SymbolId(25), 3, 0, 1,
      ReplayBodyTopologyPolicy::kSingleModelStream);
  require(body_template0.valid());
  require(replay_body_templates.row(body_template0).compute_task_count == 3);
  require(replay_body_templates.row(body_template0).stream_count == 1);
  require(replay_body_templates.row(body_template0).topology_policy ==
          ReplayBodyTopologyPolicy::kSingleModelStream);

  GraphLaunchBodyTable graph_launch_bodies;
  const GraphLaunchBodyId launch_body0 = graph_launch_bodies.append(
      launch0, body_template0, task0, task0, 3, 0, 1);
  require(launch_body0.valid());
  require(graph_launch_bodies.row(launch_body0).replay_body_template_id ==
          body_template0);
  require(graph_launch_bodies.row(launch_body0).stream_count == 1);

  GraphLaunchActivityTable graph_launch_activities;
  const GraphLaunchActivityId activity0 = graph_launch_activities.append(
      source0, 123, 50, 51, 52, SymbolId(24), 90, 170, 1, 1,
      GraphLaunchActivityBoundaryPolicy::kHostBlockingSync);
  require(activity0.valid());
  require(graph_launch_activities.row(activity0).raw_global_tid == 123);
  require(graph_launch_activities.row(activity0).boundary_host_api_row_id ==
          52);

  GraphLaunchActivityMemberTable graph_launch_activity_members;
  const GraphLaunchActivityMemberId activity_member0 =
      graph_launch_activity_members.append(activity0, launch0, 0);
  require(activity_member0.valid());
  require(graph_launch_activity_members.row(activity_member0)
              .graph_launch_occurrence_id == launch0);

  ReplayCompositionCandidateTable composition_candidates;
  const ReplayCompositionCandidateId composition0 =
      composition_candidates.append(
          source0, 0, launch0, launch0, 7, 0, 2, 3, 1, 0x5678u,
          ReplayCompositionIdentityPolicy::kCapturedGraphInstance,
          ReplayCompositionOrderPolicy::kHostSubmissionOrder,
          ReplayCompositionShapePolicy::kHeadRepeatedLayerTail,
          ReplayCompositionBoundaryPolicy::kExactPeriodicSuffix);
  require(composition0.valid());
  require(composition_candidates.row(composition0).pattern_length == 2);
  require(composition_candidates.row(composition0).full_repeat_count == 3);
  require(composition_candidates.row(composition0).order_policy ==
          ReplayCompositionOrderPolicy::kHostSubmissionOrder);
  require(composition_candidates.row(composition0).shape_policy ==
          ReplayCompositionShapePolicy::kHeadRepeatedLayerTail);
  ReplayCompositionCandidateRow generic_exact =
      composition_candidates.row(composition0);
  generic_exact.shape_policy = ReplayCompositionShapePolicy::kUnclassified;
  require(replay_composition_candidate_has_exact_structure(generic_exact));
  ReplayCompositionCandidateRow unavailable_identity = generic_exact;
  unavailable_identity.identity_policy =
      ReplayCompositionIdentityPolicy::kUnavailable;
  require(!replay_composition_candidate_has_exact_structure(
      unavailable_identity));
  ReplayCompositionCandidateRow incomplete_evidence = generic_exact;
  incomplete_evidence.boundary_policy =
      ReplayCompositionBoundaryPolicy::kIncompleteLaunchEvidence;
  require(!replay_composition_candidate_has_exact_structure(
      incomplete_evidence));
  ReplayCompositionCandidateRow inconsistent_coverage = generic_exact;
  ++inconsistent_coverage.segment_launch_count;
  require(!replay_composition_candidate_has_exact_structure(
      inconsistent_coverage));
  ReplayCompositionCandidateRow insufficient_repeats = generic_exact;
  insufficient_repeats.segment_launch_count = 5;
  insufficient_repeats.full_repeat_count = 2;
  require(!replay_composition_candidate_has_exact_structure(
      insufficient_repeats));

  ReplayCompositionSlotTable composition_slots;
  const ReplayCompositionSlotId composition_slot0 = composition_slots.append(
      composition0, 0, captured_instance0, slot_template0, body_template0,
      ReplayCompositionSlotRole::kHead, 9001);
  require(composition_slot0.valid());
  require(composition_slots.row(composition_slot0)
              .replay_composition_candidate_id == composition0);
  require(composition_slots.row(composition_slot0).role ==
          ReplayCompositionSlotRole::kHead);

  ReplayCompositionRegionTable composition_regions;
  const ReplayCompositionRegionId composition_region0 =
      composition_regions.append(
          composition0, 0, launch0, launch0, 100, 160, 1, 2,
          ReplayCompositionRegionStatus::kUnrecognizedIncompleteTail);
  require(composition_region0.valid());
  require(composition_regions.row(composition_region0)
              .replay_composition_candidate_id == composition0);
  require(composition_regions.row(composition_region0).status ==
          ReplayCompositionRegionStatus::kUnrecognizedIncompleteTail);

  ReplayCompositionRegionMemberTable composition_region_members;
  const ReplayCompositionRegionMemberId composition_region_member0 =
      composition_region_members.append(composition_region0, 0, launch0, 0);
  require(composition_region_member0.valid());
  require(composition_region_members.row(composition_region_member0)
              .graph_launch_occurrence_id == launch0);

  AnchorTable anchors;
  const SymbolId anchor_symbol0(21);
  const AnchorId anchor0 =
      anchors.append(source0, event0, ReplayUnitId::invalid(),
                     AnchorKind::kDeviceEvent, anchor_symbol0, 0, 3, 100, 160);
  const AnchorId anchor1 =
      anchors.append(source0, TraceEventId::invalid(), ReplayUnitId(0),
                     AnchorKind::kGraphReplayUnit, SymbolId(22), 0, 3, 160,
                     220);
  require(anchor0.valid());
  require(anchor1.valid());
  require(anchors.row(anchor0).kind == AnchorKind::kDeviceEvent);
  require(anchors.row(anchor0).trace_event_id == event0);
  require(!anchors.row(anchor0).replay_unit_id.valid());
  require(anchors.row(anchor1).kind == AnchorKind::kGraphReplayUnit);
  require(!anchors.row(anchor1).trace_event_id.valid());
  require(anchors.row(anchor1).replay_unit_id == ReplayUnitId(0));

  TokenTable tokens;
  const TokenId token0 = tokens.append(anchor0, anchor_symbol0, 0, 0, 100, 160);
  const TokenId token1 = tokens.append(anchor1, SymbolId(22), 0, 1, 160, 220);
  require(token0.valid());
  require(token1.valid());
  require(tokens.row(token0).anchor_id == anchor0);
  require(tokens.row(token0).sequence_index == 0);
  require(tokens.row(token1).anchor_id == anchor1);
  require(tokens.row(token1).sequence_index == 1);

  ReplayUnitTable replay_units;
  const ReplayUnitId replay0 =
      replay_units.append(template0, source0, anchor0, anchor1, event0,
                          composition_region0);
  require(replay0.valid());
  require(replay_units.row(replay0).graph_template_id == template0);
  require(replay_units.row(replay0).first_anchor_id == anchor0);
  require(replay_units.row(replay0).last_anchor_id == anchor1);
  require(replay_units.row(replay0).launch_trace_event_id == event0);
  require(replay_units.row(replay0).replay_composition_region_id ==
          composition_region0);

  ReplayUnitLaunchMemberTable replay_unit_launch_members;
  const ReplayUnitLaunchMemberId replay_member0 =
      replay_unit_launch_members.append(replay0, 0, launch0,
                                        composition_slot0);
  require(replay_member0.valid());
  require(replay_unit_launch_members.row(replay_member0).replay_unit_id ==
          replay0);

  ProtectedIntervalTable intervals;
  const ProtectedIntervalId interval0 = intervals.append(
      ProtectedIntervalKind::kGraphReplayUnit, BoundaryPolicy::kNoCross,
      token0, token1, anchor0, anchor1, source0);
  require(interval0.valid());
  require(intervals.row(interval0).kind ==
         ProtectedIntervalKind::kGraphReplayUnit);
  require(intervals.row(interval0).boundary_policy == BoundaryPolicy::kNoCross);
  require(intervals.row(interval0).first_token_id == token0);
  require(intervals.row(interval0).last_token_id == token1);
  require(intervals.row(interval0).first_anchor_id == anchor0);
  require(intervals.row(interval0).last_anchor_id == anchor1);
  require(intervals.row(interval0).evidence_source_ref_id == source0);

  bool caught_bad_source = false;
  try {
    (void)sources.row(SourceRefId(999));
  } catch (const std::out_of_range&) {
    caught_bad_source = true;
  }
  require(caught_bad_source);

  bool caught_bad_event = false;
  try {
    (void)events.row(TraceEventId::invalid());
  } catch (const std::out_of_range&) {
    caught_bad_event = true;
  }
  require(caught_bad_event);

  bool caught_bad_interval = false;
  try {
    (void)intervals.row(ProtectedIntervalId(999));
  } catch (const std::out_of_range&) {
    caught_bad_interval = true;
  }
  require(caught_bad_interval);

  bool caught_bad_template = false;
  try {
    (void)graph_templates.row(GraphTemplateId(999));
  } catch (const std::out_of_range&) {
    caught_bad_template = true;
  }
  require(caught_bad_template);

  bool caught_bad_graph_launch = false;
  try {
    (void)graph_launches.row(GraphLaunchOccurrenceId(999));
  } catch (const std::out_of_range&) {
    caught_bad_graph_launch = true;
  }
  require(caught_bad_graph_launch);

  return 0;
}
