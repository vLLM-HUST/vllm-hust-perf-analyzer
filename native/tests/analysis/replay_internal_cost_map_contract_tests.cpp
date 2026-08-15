#include "traceloom/analysis/replay_internal_cost_map.h"
#include "traceloom/testing/test_util.h"
#include "replay_internal_cost_map_test_cases.h"

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace traceloom;
using traceloom::testing::require;
struct MinimalIr {
  NativeIr ir;
  SourceRefId source;
  ReplayBodyTemplateId body_template;
  ReplayBodyTemplateId other_template;
  ReplayCompositionSlotId slot;
  ReplayUnitId unit;
  TaskId task;
};

// Minimal one-unit IR with a configurable launch sequence for fail-closed
// coverage. All variant state is fixed at append time (tables expose no
// mutable rows).
MinimalIr build_minimal_ir(bool with_body = true,
                           bool second_body = false,
                           ReplayBodyTemplateId observed_template =
                               ReplayBodyTemplateId::invalid(),
                           bool slot_valid = true,
                           bool slot_template_valid = true,
                           bool member_identity_valid = true,
                           std::vector<std::uint32_t> member_orders = {0},
                           bool with_body_member = true,
                           bool invalid_task_ref = false,
                           bool invalid_event_ref = false,
                           bool duplicate_position = false,
                           bool lane_inconsistent = false,
                           bool invalid_unit_template = false,
                           bool invalid_occurrence = false,
                           bool slot_template_out_of_range = false,
                           bool body_template_out_of_range = false,
                           bool identity_out_of_range = false,
                           std::uint32_t member_device_id = 0,
                           std::uint32_t observed_compute_count = 1,
                           std::uint32_t template_compute_count = 1) {
  MinimalIr out;
  out.source = out.ir.source_refs.append("fixture", "memory", "TASK", 0);
  const SymbolId compute = out.ir.symbols.intern("MatMul");
  out.body_template = out.ir.replay_body_templates.append(
      out.source, 1, out.ir.symbols.intern("MatMul"), template_compute_count, 0,
      1,
      ReplayBodyTopologyPolicy::kSingleModelStream);
  out.other_template = out.ir.replay_body_templates.append(
      out.source, 2, out.ir.symbols.intern("Relu"), 1, 0, 1,
      ReplayBodyTopologyPolicy::kSingleModelStream);
  const GraphTemplateId graph_template =
      invalid_unit_template
          ? GraphTemplateId::invalid()
          : out.ir.graph_templates.append(out.source, 7, 1);
  const ReplayCompositionCandidateId composition =
      out.ir.replay_composition_candidates.append(
          out.source, 0, GraphLaunchOccurrenceId::invalid(),
          GraphLaunchOccurrenceId::invalid(),
          static_cast<std::uint32_t>(member_orders.size()), 0, 1, 1, 0, 1,
          ReplayCompositionIdentityPolicy::kGraphConnection,
          ReplayCompositionOrderPolicy::kDeviceExecutionOrder,
          ReplayCompositionShapePolicy::kSingleGraph,
          ReplayCompositionBoundaryPolicy::kExactPeriodicSuffix);
  if (slot_valid) {
    ReplayBodyTemplateId slot_template = out.body_template;
    if (!slot_template_valid) {
      slot_template = ReplayBodyTemplateId::invalid();
    } else if (slot_template_out_of_range) {
      slot_template = ReplayBodyTemplateId(99);
    }
    out.slot = out.ir.replay_composition_slots.append(
        composition, 0, CapturedGraphInstanceId::invalid(),
        GraphSlotTemplateId::invalid(), slot_template,
        ReplayCompositionSlotRole::kGraph, -1);
  }
  out.unit = out.ir.replay_units.append(
      graph_template, out.source, AnchorId::invalid(), AnchorId::invalid(),
      TraceEventId::invalid());
  const TraceEventId event =
      invalid_event_ref
          ? TraceEventId::invalid()
          : out.ir.trace_events.append(out.source, 1, member_device_id, 0, 0,
                                       10, compute);
  SymbolId op_name = member_identity_valid ? compute : SymbolId::invalid();
  if (identity_out_of_range) {
    op_name = SymbolId(99);
  }
  out.task = out.ir.tasks.append(
      out.source, event, 1, 1, -1, op_name, op_name, op_name,
      SymbolId::invalid(), SymbolId::invalid());
  for (std::uint32_t order = 0;
       order < static_cast<std::uint32_t>(member_orders.size()); ++order) {
    const GraphLaunchOccurrenceId occurrence =
        invalid_occurrence
            ? GraphLaunchOccurrenceId::invalid()
            : out.ir.graph_launch_occurrences.append(
                  out.source, out.source, 0, 1, 1, 1, 1, StreamId::invalid(),
                  StreamId::invalid(), CapturedGraphInstanceId::invalid(),
                  TaskId::invalid(), TaskId::invalid(), TaskId::invalid(),
                  order * 20, order * 20 + 10, 0,
                  GraphLaunchMatchPolicy::kNotifyCompletionAdjacent,
                  GraphLaunchInstanceAssociationPolicy::kRecordModelId);
    if (with_body) {
      const ReplayBodyTemplateId template_id =
          body_template_out_of_range
              ? ReplayBodyTemplateId(99)
              : (observed_template.valid() ? observed_template
                                           : out.body_template);
      const GraphLaunchBodyId body = out.ir.graph_launch_bodies.append(
          occurrence, template_id, out.task, out.task,
          observed_compute_count, 0, 1);
      if (with_body_member) {
        out.ir.graph_launch_body_members.append(
            body, invalid_task_ref ? TaskId::invalid() : out.task, 0, 0,
            GraphLaunchBodyMemberRow::Kind::kCompute);
        if (duplicate_position) {
          out.ir.graph_launch_body_members.append(
              body, out.task, 0, 0, GraphLaunchBodyMemberRow::Kind::kCompute);
        }
        if (lane_inconsistent) {
          out.ir.graph_launch_body_members.append(
              body, out.task, 1, 1, GraphLaunchBodyMemberRow::Kind::kCompute);
        }
      }
      if (second_body) {
        out.ir.graph_launch_bodies.append(occurrence, out.other_template,
                                          out.task, out.task, 1, 0, 1);
      }
    }
    out.ir.replay_unit_launch_members.append(
        out.unit, member_orders[order], occurrence,
        slot_valid ? out.slot : ReplayCompositionSlotId::invalid());
  }
  return out;
}

MinimalIr build_stream_topology_ir(
    std::uint32_t declared_stream_count,
    const std::vector<std::pair<std::uint32_t, std::uint32_t>>&
        lane_streams) {
  MinimalIr out;
  out.source = out.ir.source_refs.append("fixture", "memory", "TASK", 0);
  const SymbolId compute = out.ir.symbols.intern("MatMul");
  const std::uint32_t member_count =
      static_cast<std::uint32_t>(lane_streams.size());
  out.body_template = out.ir.replay_body_templates.append(
      out.source, 1, compute, member_count, 0, declared_stream_count,
      ReplayBodyTopologyPolicy::kCapturedStreamSetUnordered);
  const GraphTemplateId graph_template =
      out.ir.graph_templates.append(out.source, 7, 1);
  const ReplayCompositionCandidateId composition =
      out.ir.replay_composition_candidates.append(
          out.source, 0, GraphLaunchOccurrenceId::invalid(),
          GraphLaunchOccurrenceId::invalid(), 1, 0, 1, 1, 0, 1,
          ReplayCompositionIdentityPolicy::kGraphConnection,
          ReplayCompositionOrderPolicy::kDeviceExecutionOrder,
          ReplayCompositionShapePolicy::kSingleGraph,
          ReplayCompositionBoundaryPolicy::kExactPeriodicSuffix);
  out.slot = out.ir.replay_composition_slots.append(
      composition, 0, CapturedGraphInstanceId::invalid(),
      GraphSlotTemplateId::invalid(), out.body_template,
      ReplayCompositionSlotRole::kGraph, -1);
  out.unit = out.ir.replay_units.append(
      graph_template, out.source, AnchorId::invalid(), AnchorId::invalid(),
      TraceEventId::invalid());
  const GraphLaunchOccurrenceId occurrence =
      out.ir.graph_launch_occurrences.append(
          out.source, out.source, 0, 1, 1, 1, 1, StreamId::invalid(),
          StreamId::invalid(), CapturedGraphInstanceId::invalid(),
          TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), 0, 10, 0,
          GraphLaunchMatchPolicy::kNotifyCompletionAdjacent,
          GraphLaunchInstanceAssociationPolicy::kRecordModelId);

  std::vector<TaskId> tasks;
  tasks.reserve(lane_streams.size());
  for (std::size_t index = 0; index < lane_streams.size(); ++index) {
    const TraceEventId event = out.ir.trace_events.append(
        out.source, index + 1, 0, lane_streams[index].second, 0, 10,
        compute);
    tasks.push_back(out.ir.tasks.append(
        out.source, event, index + 1, static_cast<std::int64_t>(index + 1),
        -1, compute, compute, compute, SymbolId::invalid(),
        SymbolId::invalid()));
  }
  require(!tasks.empty(), "stream-topology fixture needs a body member");
  out.task = tasks.front();
  const GraphLaunchBodyId body = out.ir.graph_launch_bodies.append(
      occurrence, out.body_template, tasks.front(), tasks.back(),
      member_count, 0, declared_stream_count);
  for (std::size_t index = 0; index < tasks.size(); ++index) {
    out.ir.graph_launch_body_members.append(
        body, tasks[index], lane_streams[index].first, 0,
        GraphLaunchBodyMemberRow::Kind::kCompute);
  }
  out.ir.replay_unit_launch_members.append(out.unit, 0, occurrence, out.slot);
  return out;
}

void test_fail_closed_missing_body() {
  const MinimalIr minimal = build_minimal_ir(/*with_body=*/false);
  const ReplayInternalCostMapResult map =
      build_replay_internal_cost_map(minimal.ir);
  require(map.units.size() == 1, "one unit");
  require(map.units[0].launch_members.size() == 1, "one launch member");
  require(!map.units[0].launch_members[0].supported &&
              map.units[0].launch_members[0].reason_code ==
                  "missing_graph_launch_body",
          "missing body is explicit, not an empty success");
  require(map.units[0].resolved_launch_count == 0 &&
              !map.units[0].supported && map.resolved_launch_count == 0 &&
              map.unsupported_launch_count == 1 &&
              map.unsupported_unit_count == 1,
          "missing body counts fail closed");
  require(map.members.empty() && map.aggregates.empty(),
          "no member or aggregate rows without body evidence");
}

void test_fail_closed_ambiguous_body() {
  const MinimalIr minimal = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/true);
  const ReplayInternalCostMapResult map =
      build_replay_internal_cost_map(minimal.ir);
  require(map.units.size() == 1 &&
              map.units[0].launch_members[0].reason_code ==
                  "ambiguous_graph_launch_body" &&
              !map.units[0].launch_members[0].supported,
          "ambiguous bodies fail closed");
}

void test_fail_closed_template_mismatch() {
  const MinimalIr minimal = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/false,
      /*observed_template=*/ReplayBodyTemplateId(1));
  const ReplayInternalCostMapResult map =
      build_replay_internal_cost_map(minimal.ir);
  require(map.units[0].launch_members[0].reason_code ==
              "body_template_mismatch" &&
              !map.units[0].launch_members[0].supported,
          "slot/body template mismatch fails closed");
}

void test_fail_closed_missing_slot() {
  const MinimalIr minimal = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/false,
      ReplayBodyTemplateId::invalid(), /*slot_valid=*/false);
  const ReplayInternalCostMapResult map =
      build_replay_internal_cost_map(minimal.ir);
  require(map.units[0].launch_members[0].reason_code ==
              "missing_replay_composition_slot" &&
              !map.units[0].launch_members[0].supported,
          "missing slot fails closed");
}

void test_fail_closed_slot_without_template() {
  const MinimalIr minimal = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/false,
      ReplayBodyTemplateId::invalid(), /*slot_valid=*/true,
      /*slot_template_valid=*/false);
  const ReplayInternalCostMapResult map =
      build_replay_internal_cost_map(minimal.ir);
  require(map.units[0].launch_members[0].reason_code ==
              "slot_missing_body_template" &&
              !map.units[0].launch_members[0].supported,
          "slot without body template fails closed");
}

void test_fail_closed_empty_body() {
  const MinimalIr minimal = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/false,
      ReplayBodyTemplateId::invalid(), /*slot_valid=*/true,
      /*slot_template_valid=*/true, /*member_identity_valid=*/true,
      /*member_orders=*/std::vector<std::uint32_t>{0},
      /*with_body_member=*/false);
  const ReplayInternalCostMapResult map =
      build_replay_internal_cost_map(minimal.ir);
  require(map.units[0].launch_members[0].reason_code ==
              "empty_graph_launch_body" &&
              !map.units[0].launch_members[0].supported,
          "empty body fails closed with explicit reason");
  require(map.members.empty() && map.aggregates.empty() &&
              map.resolved_launch_count == 0 &&
              map.unsupported_launch_count == 1,
          "empty body emits no member or aggregate rows");
}

void test_fail_closed_invalid_member_reference() {
  // Invalid task reference on a body member: launch unsupported, no partial
  // rows, no exception.
  const MinimalIr bad_task = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/false,
      ReplayBodyTemplateId::invalid(), /*slot_valid=*/true,
      /*slot_template_valid=*/true, /*member_identity_valid=*/true,
      /*member_orders=*/std::vector<std::uint32_t>{0},
      /*with_body_member=*/true, /*invalid_task_ref=*/true);
  const ReplayInternalCostMapResult task_map =
      build_replay_internal_cost_map(bad_task.ir);
  require(task_map.units[0].launch_members[0].reason_code ==
              "missing_body_member_evidence" &&
              !task_map.units[0].launch_members[0].supported,
          "invalid member task reference fails closed");
  require(task_map.members.empty() && task_map.aggregates.empty(),
          "invalid member task reference suppresses partial rows");

  // Invalid trace-event reference through the task row.
  const MinimalIr bad_event = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/false,
      ReplayBodyTemplateId::invalid(), /*slot_valid=*/true,
      /*slot_template_valid=*/true, /*member_identity_valid=*/true,
      /*member_orders=*/std::vector<std::uint32_t>{0},
      /*with_body_member=*/true, /*invalid_task_ref=*/false,
      /*invalid_event_ref=*/true);
  const ReplayInternalCostMapResult event_map =
      build_replay_internal_cost_map(bad_event.ir);
  require(event_map.units[0].launch_members[0].reason_code ==
              "missing_body_member_evidence" &&
              !event_map.units[0].launch_members[0].supported &&
              event_map.members.empty() && event_map.aggregates.empty(),
          "invalid member event reference fails closed");

  // Orphaned member row (invalid body id) is excluded from every body and
  // reported; the valid body remains supported with complete evidence.
  MinimalIr orphan = build_minimal_ir();
  orphan.ir.graph_launch_body_members.append(
      GraphLaunchBodyId(99), orphan.task, 0, 0,
      GraphLaunchBodyMemberRow::Kind::kCompute);
  const ReplayInternalCostMapResult orphan_map =
      build_replay_internal_cost_map(orphan.ir);
  require(orphan_map.units[0].launch_members[0].supported &&
              orphan_map.members.size() == 1 &&
              orphan_map.aggregates.size() == 1,
          "orphan member row never enters body membership or aggregates");
  bool orphan_issue = false;
  for (const ReplayInternalCostIssue& issue : orphan_map.issues) {
    if (issue.code == "invalid_body_member_reference" &&
        !issue.replay_unit_id.valid()) {
      orphan_issue = true;
    }
  }
  require(orphan_issue, "orphan member reference is an explicit issue");
}

void test_fail_closed_duplicate_position() {
  const MinimalIr minimal = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/false,
      ReplayBodyTemplateId::invalid(), /*slot_valid=*/true,
      /*slot_template_valid=*/true, /*member_identity_valid=*/true,
      /*member_orders=*/std::vector<std::uint32_t>{0},
      /*with_body_member=*/true, /*invalid_task_ref=*/false,
      /*invalid_event_ref=*/false, /*duplicate_position=*/true);
  const ReplayInternalCostMapResult map =
      build_replay_internal_cost_map(minimal.ir);
  require(map.units[0].launch_members[0].reason_code ==
              "duplicate_within_stream_position" &&
              !map.units[0].launch_members[0].supported &&
              map.members.empty() && map.aggregates.empty() &&
              map.unsupported_launch_count == 1,
          "duplicate (stream, position) is unsupported with no partial "
          "aggregates");
}

void test_fail_closed_lane_inconsistency() {
  const MinimalIr minimal = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/false,
      ReplayBodyTemplateId::invalid(), /*slot_valid=*/true,
      /*slot_template_valid=*/true, /*member_identity_valid=*/true,
      /*member_orders=*/std::vector<std::uint32_t>{0},
      /*with_body_member=*/true, /*invalid_task_ref=*/false,
      /*invalid_event_ref=*/false, /*duplicate_position=*/false,
      /*lane_inconsistent=*/true);
  const ReplayInternalCostMapResult map =
      build_replay_internal_cost_map(minimal.ir);
  require(map.units[0].launch_members[0].reason_code ==
              "stream_lane_inconsistency" &&
              !map.units[0].launch_members[0].supported &&
              map.members.empty() && map.aggregates.empty(),
          "lane-inconsistent stream is unsupported (per-stream sequence "
          "ambiguous)");
}

void test_fail_closed_body_shape_and_device() {
  const MinimalIr observed_shape = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/false,
      ReplayBodyTemplateId::invalid(), /*slot_valid=*/true,
      /*slot_template_valid=*/true, /*member_identity_valid=*/true,
      /*member_orders=*/std::vector<std::uint32_t>{0},
      /*with_body_member=*/true, /*invalid_task_ref=*/false,
      /*invalid_event_ref=*/false, /*duplicate_position=*/false,
      /*lane_inconsistent=*/false, /*invalid_unit_template=*/false,
      /*invalid_occurrence=*/false, /*slot_template_out_of_range=*/false,
      /*body_template_out_of_range=*/false, /*identity_out_of_range=*/false,
      /*member_device_id=*/0, /*observed_compute_count=*/2,
      /*template_compute_count=*/1);
  const ReplayInternalCostMapResult observed_shape_map =
      build_replay_internal_cost_map(observed_shape.ir);
  require(observed_shape_map.units[0].launch_members[0].reason_code ==
              "body_membership_summary_mismatch" &&
              observed_shape_map.members.empty() &&
              observed_shape_map.aggregates.empty(),
          "missing body member fails the observed-body summary contract");

  const MinimalIr template_shape = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/false,
      ReplayBodyTemplateId::invalid(), /*slot_valid=*/true,
      /*slot_template_valid=*/true, /*member_identity_valid=*/true,
      /*member_orders=*/std::vector<std::uint32_t>{0},
      /*with_body_member=*/true, /*invalid_task_ref=*/false,
      /*invalid_event_ref=*/false, /*duplicate_position=*/false,
      /*lane_inconsistent=*/false, /*invalid_unit_template=*/false,
      /*invalid_occurrence=*/false, /*slot_template_out_of_range=*/false,
      /*body_template_out_of_range=*/false, /*identity_out_of_range=*/false,
      /*member_device_id=*/0, /*observed_compute_count=*/1,
      /*template_compute_count=*/2);
  const ReplayInternalCostMapResult template_shape_map =
      build_replay_internal_cost_map(template_shape.ir);
  require(template_shape_map.units[0].launch_members[0].reason_code ==
              "body_template_shape_mismatch" &&
              template_shape_map.members.empty() &&
              template_shape_map.aggregates.empty(),
          "body members must match the referenced exact template shape");

  const MinimalIr cross_device = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/false,
      ReplayBodyTemplateId::invalid(), /*slot_valid=*/true,
      /*slot_template_valid=*/true, /*member_identity_valid=*/true,
      /*member_orders=*/std::vector<std::uint32_t>{0},
      /*with_body_member=*/true, /*invalid_task_ref=*/false,
      /*invalid_event_ref=*/false, /*duplicate_position=*/false,
      /*lane_inconsistent=*/false, /*invalid_unit_template=*/false,
      /*invalid_occurrence=*/false, /*slot_template_out_of_range=*/false,
      /*body_template_out_of_range=*/false, /*identity_out_of_range=*/false,
      /*member_device_id=*/1);
  const ReplayInternalCostMapResult cross_device_map =
      build_replay_internal_cost_map(cross_device.ir);
  require(cross_device_map.units[0].launch_members[0].reason_code ==
              "body_device_mismatch" &&
              cross_device_map.members.empty() &&
              cross_device_map.aggregates.empty(),
          "a replay body cannot cross its graph-launch device domain");
}

void test_captured_stream_topology_allows_empty_lanes() {
  // The captured topology declares lanes 0 and 1, while this exact launch has
  // scheduled work only on lane 1. Task-kind counts still prove complete body
  // membership, so the empty captured lane must not make cost evidence
  // unsupported.
  const MinimalIr empty_lane =
      build_stream_topology_ir(2, {{1, 7}});
  const ReplayInternalCostMapResult empty_lane_map =
      build_replay_internal_cost_map(empty_lane.ir);
  require(empty_lane_map.units.size() == 1 &&
              empty_lane_map.units[0].supported &&
              empty_lane_map.fully_supported_unit_count == 1 &&
              empty_lane_map.resolved_launch_count == 1 &&
              empty_lane_map.unsupported_launch_count == 0 &&
              empty_lane_map.members.size() == 1 &&
              empty_lane_map.members[0].lane_ordinal == 1 &&
              empty_lane_map.members[0].stream_id == 7,
          "empty captured lane preserves exact replay cost support");

  const MinimalIr lane_out_of_range =
      build_stream_topology_ir(1, {{1, 7}});
  const ReplayInternalCostMapResult out_of_range_map =
      build_replay_internal_cost_map(lane_out_of_range.ir);
  require(out_of_range_map.units.size() == 1 &&
              !out_of_range_map.units[0].supported &&
              out_of_range_map.units[0].launch_members[0].reason_code ==
                  "body_membership_summary_mismatch" &&
              out_of_range_map.members.empty() &&
              out_of_range_map.aggregates.empty(),
          "nonempty lane outside declared topology fails closed");

  const MinimalIr lane_collision =
      build_stream_topology_ir(2, {{1, 7}, {1, 9}});
  const ReplayInternalCostMapResult collision_map =
      build_replay_internal_cost_map(lane_collision.ir);
  require(collision_map.units.size() == 1 &&
              !collision_map.units[0].supported &&
              collision_map.units[0].launch_members[0].reason_code ==
                  "stream_lane_inconsistency" &&
              collision_map.members.empty() &&
              collision_map.aggregates.empty(),
          "two nonempty streams cannot collapse onto one captured lane");
}

void test_fail_closed_invalid_foreign_keys() {
  // Unit graph template invalid.
  const MinimalIr bad_template = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/false,
      ReplayBodyTemplateId::invalid(), /*slot_valid=*/true,
      /*slot_template_valid=*/true, /*member_identity_valid=*/true,
      /*member_orders=*/std::vector<std::uint32_t>{0},
      /*with_body_member=*/true, /*invalid_task_ref=*/false,
      /*invalid_event_ref=*/false, /*duplicate_position=*/false,
      /*lane_inconsistent=*/false, /*invalid_unit_template=*/true);
  const ReplayInternalCostMapResult template_map =
      build_replay_internal_cost_map(bad_template.ir);
  require(template_map.units[0].launch_members[0].reason_code ==
              "invalid_unit_graph_template" &&
              !template_map.units[0].launch_members[0].supported &&
              template_map.members.empty() && template_map.aggregates.empty(),
          "invalid unit graph template never enters aggregate keys");

  // Launch member occurrence invalid.
  const MinimalIr bad_occurrence = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/false,
      ReplayBodyTemplateId::invalid(), /*slot_valid=*/true,
      /*slot_template_valid=*/true, /*member_identity_valid=*/true,
      /*member_orders=*/std::vector<std::uint32_t>{0},
      /*with_body_member=*/true, /*invalid_task_ref=*/false,
      /*invalid_event_ref=*/false, /*duplicate_position=*/false,
      /*lane_inconsistent=*/false, /*invalid_unit_template=*/false,
      /*invalid_occurrence=*/true);
  const ReplayInternalCostMapResult occurrence_map =
      build_replay_internal_cost_map(bad_occurrence.ir);
  require(occurrence_map.units[0].launch_members[0].reason_code ==
              "invalid_launch_occurrence" &&
              !occurrence_map.units[0].launch_members[0].supported &&
              occurrence_map.members.empty() &&
              occurrence_map.aggregates.empty(),
          "invalid launch occurrence fails closed");

  // Slot body template out of range.
  const MinimalIr bad_slot_template = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/false,
      ReplayBodyTemplateId::invalid(), /*slot_valid=*/true,
      /*slot_template_valid=*/true, /*member_identity_valid=*/true,
      /*member_orders=*/std::vector<std::uint32_t>{0},
      /*with_body_member=*/true, /*invalid_task_ref=*/false,
      /*invalid_event_ref=*/false, /*duplicate_position=*/false,
      /*lane_inconsistent=*/false, /*invalid_unit_template=*/false,
      /*invalid_occurrence=*/false, /*slot_template_out_of_range=*/true);
  const ReplayInternalCostMapResult slot_template_map =
      build_replay_internal_cost_map(bad_slot_template.ir);
  require(slot_template_map.units[0].launch_members[0].reason_code ==
              "slot_missing_body_template" &&
              !slot_template_map.units[0].launch_members[0].supported,
          "out-of-range slot body template fails closed");

  // Observed body template out of range.
  const MinimalIr bad_body_template = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/false,
      ReplayBodyTemplateId::invalid(), /*slot_valid=*/true,
      /*slot_template_valid=*/true, /*member_identity_valid=*/true,
      /*member_orders=*/std::vector<std::uint32_t>{0},
      /*with_body_member=*/true, /*invalid_task_ref=*/false,
      /*invalid_event_ref=*/false, /*duplicate_position=*/false,
      /*lane_inconsistent=*/false, /*invalid_unit_template=*/false,
      /*invalid_occurrence=*/false, /*slot_template_out_of_range=*/false,
      /*body_template_out_of_range=*/true);
  const ReplayInternalCostMapResult body_template_map =
      build_replay_internal_cost_map(bad_body_template.ir);
  require(body_template_map.units[0].launch_members[0].reason_code ==
              "body_template_mismatch" &&
              !body_template_map.units[0].launch_members[0].supported,
          "out-of-range observed body template fails closed");

  // Identity symbol out of range never enters aggregate keys.
  const MinimalIr bad_identity = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/false,
      ReplayBodyTemplateId::invalid(), /*slot_valid=*/true,
      /*slot_template_valid=*/true, /*member_identity_valid=*/true,
      /*member_orders=*/std::vector<std::uint32_t>{0},
      /*with_body_member=*/true, /*invalid_task_ref=*/false,
      /*invalid_event_ref=*/false, /*duplicate_position=*/false,
      /*lane_inconsistent=*/false, /*invalid_unit_template=*/false,
      /*invalid_occurrence=*/false, /*slot_template_out_of_range=*/false,
      /*body_template_out_of_range=*/false, /*identity_out_of_range=*/true);
  const ReplayInternalCostMapResult identity_map =
      build_replay_internal_cost_map(bad_identity.ir);
  require(identity_map.units[0].launch_members[0].supported &&
              identity_map.members.size() == 1 &&
              !identity_map.members[0].identity_symbol_id.valid() &&
              identity_map.aggregates.empty(),
          "out-of-range identity never enters aggregate keys");
  bool identity_issue = false;
  for (const ReplayInternalCostIssue& issue : identity_map.issues) {
    if (issue.code == "invalid_member_identity") {
      identity_issue = true;
    }
  }
  require(identity_issue, "out-of-range identity is an explicit issue");
}

void test_scheduled_work_share_zero_denominator() {
  // A body with a nonempty membership but zero total duration has a zero
  // task_sum: the launch keeps cost evidence, but no share value is
  // manufactured (supported = false, share = 0).
  MinimalIr zero;
  zero.source = zero.ir.source_refs.append("fixture", "memory", "TASK", 0);
  const SymbolId compute = zero.ir.symbols.intern("MatMul");
  const ReplayBodyTemplateId body_template =
      zero.ir.replay_body_templates.append(
          zero.source, 1, zero.ir.symbols.intern("MatMul"), 1, 0, 1,
          ReplayBodyTopologyPolicy::kSingleModelStream);
  const GraphTemplateId graph_template =
      zero.ir.graph_templates.append(zero.source, 7, 1);
  const ReplayCompositionCandidateId composition =
      zero.ir.replay_composition_candidates.append(
          zero.source, 0, GraphLaunchOccurrenceId::invalid(),
          GraphLaunchOccurrenceId::invalid(), 1, 0, 1, 1, 0, 1,
          ReplayCompositionIdentityPolicy::kGraphConnection,
          ReplayCompositionOrderPolicy::kDeviceExecutionOrder,
          ReplayCompositionShapePolicy::kSingleGraph,
          ReplayCompositionBoundaryPolicy::kExactPeriodicSuffix);
  zero.slot = zero.ir.replay_composition_slots.append(
      composition, 0, CapturedGraphInstanceId::invalid(),
      GraphSlotTemplateId::invalid(), body_template,
      ReplayCompositionSlotRole::kGraph, -1);
  zero.unit = zero.ir.replay_units.append(
      graph_template, zero.source, AnchorId::invalid(), AnchorId::invalid(),
      TraceEventId::invalid());
  const TraceEventId event =
      zero.ir.trace_events.append(zero.source, 1, 0, 0, 5, 5, compute);
  zero.task = zero.ir.tasks.append(
      zero.source, event, 1, 1, -1, compute, compute, compute,
      SymbolId::invalid(), SymbolId::invalid());
  const GraphLaunchOccurrenceId occurrence =
      zero.ir.graph_launch_occurrences.append(
          zero.source, zero.source, 0, 1, 1, 1, 1, StreamId::invalid(),
          StreamId::invalid(), CapturedGraphInstanceId::invalid(),
          TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), 0, 10, 0,
          GraphLaunchMatchPolicy::kNotifyCompletionAdjacent,
          GraphLaunchInstanceAssociationPolicy::kRecordModelId);
  const GraphLaunchBodyId body = zero.ir.graph_launch_bodies.append(
      occurrence, body_template, zero.task, zero.task, 1, 0, 1);
  zero.ir.graph_launch_body_members.append(
      body, zero.task, 0, 0, GraphLaunchBodyMemberRow::Kind::kCompute);
  zero.ir.replay_unit_launch_members.append(
      zero.unit, 0, occurrence, zero.slot);

  const ReplayInternalCostMapResult map =
      build_replay_internal_cost_map(zero.ir);
  require(map.units[0].launch_members[0].supported &&
              map.units[0].launch_members[0].task_sum_ns == 0 &&
              map.members.size() == 1 &&
              !map.members[0].scheduled_work_share_supported &&
              map.members[0].scheduled_work_share_ppm == 0 &&
              map.members[0].scheduled_work_denominator_body_task_sum_ns == 0,
          "zero denominator never manufactures a share");
  require(map.aggregates.size() == 1 &&
              !map.aggregates[0].scheduled_work_share_supported &&
              map.aggregates[0].scheduled_work_share_ppm == 0 &&
              map.aggregates[0].scheduled_work_denominator_body_task_sum_ns ==
                  0,
          "aggregate share unsupported when every owning body task_sum is "
          "zero");
}

void test_missing_identity_member() {
  const MinimalIr minimal = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/false,
      ReplayBodyTemplateId::invalid(), /*slot_valid=*/true,
      /*slot_template_valid=*/true, /*member_identity_valid=*/false);
  const ReplayInternalCostMapResult map =
      build_replay_internal_cost_map(minimal.ir);
  require(map.units[0].launch_members[0].supported,
          "identity-less member still has cost evidence");
  require(map.members.size() == 1 && !map.members[0].identity_symbol_id.valid(),
          "identity-less member emitted without identity");
  require(map.aggregates.empty(), "identity-less member never aligned");
  bool found = false;
  for (const ReplayInternalCostIssue& issue : map.issues) {
    if (issue.code == "missing_member_identity") {
      found = true;
    }
  }
  require(found, "missing identity is an explicit issue");
}

void test_no_replay_units() {
  NativeIr ir;
  const ReplayInternalCostMapResult map = build_replay_internal_cost_map(ir);
  require(map.units.empty() && map.members.empty() && map.aggregates.empty() &&
              map.result_reason_codes.size() == 1 &&
              map.result_reason_codes[0] == "no_replay_units" &&
              map.issues.size() == 1 &&
              map.issues[0].code == "no_replay_units",
          "no replay units is an explicit reason, not an empty success");
}

void test_member_order_gap_and_empty_unit() {
  const MinimalIr minimal = build_minimal_ir(
      /*with_body=*/true, /*second_body=*/false,
      ReplayBodyTemplateId::invalid(), /*slot_valid=*/true,
      /*slot_template_valid=*/true, /*member_identity_valid=*/true,
      /*member_orders=*/std::vector<std::uint32_t>{0, 1, 3});
  const ReplayInternalCostMapResult map =
      build_replay_internal_cost_map(minimal.ir);
  require(map.units[0].launch_members.size() == 3, "three members kept");
  require(map.units[0].launch_members[0].member_order == 0 &&
              map.units[0].launch_members[1].member_order == 1 &&
              map.units[0].launch_members[2].member_order == 3,
          "stable member_order emission");
  bool gap_issue = false;
  for (const ReplayInternalCostIssue& issue : map.issues) {
    if (issue.code == "member_order_gap") {
      gap_issue = true;
    }
  }
  require(gap_issue, "member order gap is reported");

  // A unit without launch members is an explicit unsupported result.
  NativeIr empty_ir;
  const SourceRefId source =
      empty_ir.source_refs.append("fixture", "memory", "TASK", 0);
  const GraphTemplateId graph_template =
      empty_ir.graph_templates.append(source, 9, 1);
  empty_ir.replay_units.append(graph_template, source, AnchorId::invalid(),
                               AnchorId::invalid(), TraceEventId::invalid());
  const ReplayInternalCostMapResult empty_map =
      build_replay_internal_cost_map(empty_ir);
  require(empty_map.units.size() == 1 && !empty_map.units[0].supported &&
              empty_map.units[0].unit_reason_codes.size() == 1 &&
              empty_map.units[0].unit_reason_codes[0] == "empty_replay_unit" &&
              empty_map.unsupported_unit_count == 1,
          "empty unit fails closed with explicit reason");
}

}  // namespace

namespace traceloom::testing::replay_internal_cost_map {

void run_contract_tests() {
  test_fail_closed_missing_body();
  test_fail_closed_ambiguous_body();
  test_fail_closed_template_mismatch();
  test_fail_closed_missing_slot();
  test_fail_closed_slot_without_template();
  test_fail_closed_empty_body();
  test_fail_closed_invalid_member_reference();
  test_fail_closed_duplicate_position();
  test_fail_closed_lane_inconsistency();
  test_fail_closed_body_shape_and_device();
  test_captured_stream_topology_allows_empty_lanes();
  test_fail_closed_invalid_foreign_keys();
  test_scheduled_work_share_zero_denominator();
  test_missing_identity_member();
  test_no_replay_units();
  test_member_order_gap_and_empty_unit();
}

}  // namespace traceloom::testing::replay_internal_cost_map
