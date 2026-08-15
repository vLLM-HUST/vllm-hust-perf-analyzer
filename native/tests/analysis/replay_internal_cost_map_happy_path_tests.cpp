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

struct MemberSpec {
  std::uint32_t lane;
  std::uint32_t ordinal;
  std::uint32_t stream;
  SymbolId op;
  GraphLaunchBodyMemberRow::Kind kind;
  std::int64_t start_ns;
  std::int64_t end_ns;
};

struct TestIr {
  NativeIr ir;
  SourceRefId source;
  SymbolId matmul;
  SymbolId allreduce;
  SymbolId relu;
  SymbolId h2d;
  ReplayBodyTemplateId head_template;
  ReplayBodyTemplateId layer_template;
  ReplayBodyTemplateId tail_template;
  GraphTemplateId graph_template;
  ReplayCompositionSlotId head_slot;
  ReplayCompositionSlotId layer1_slot;
  ReplayCompositionSlotId layer2_slot;
  ReplayCompositionSlotId tail_slot;
};

TaskId append_task(TestIr& test,
                   TraceEventId event,
                   std::uint64_t raw_task_id,
                   SymbolId op_name,
                   SymbolId comm_name) {
  const SymbolId task_type =
      comm_name.valid() ? test.allreduce : test.matmul;
  return test.ir.tasks.append(
      test.source, event, raw_task_id, static_cast<std::int64_t>(raw_task_id),
      -1, task_type, op_name, op_name, op_name, comm_name);
}

GraphLaunchOccurrenceId append_occurrence(
    TestIr& test,
    std::int64_t time_offset,
    const std::vector<MemberSpec>& members) {
  // One source event per member keeps task identity and timing exact.
  const GraphLaunchOccurrenceId occurrence =
      test.ir.graph_launch_occurrences.append(
          test.source, test.source, 0, 1, 1, 1, 1, StreamId::invalid(),
          StreamId::invalid(), CapturedGraphInstanceId::invalid(),
          TaskId::invalid(), TaskId::invalid(), TaskId::invalid(),
          time_offset + members.front().start_ns,
          time_offset + members.back().end_ns, 0,
          GraphLaunchMatchPolicy::kNotifyCompletionAdjacent,
          GraphLaunchInstanceAssociationPolicy::kRecordModelId);
  return occurrence;
}

GraphLaunchBodyId append_body(TestIr& test,
                              GraphLaunchOccurrenceId occurrence,
                              ReplayBodyTemplateId body_template,
                              std::int64_t time_offset,
                              const std::vector<MemberSpec>& members,
                              std::uint64_t raw_task_base) {
  std::uint32_t compute_count = 0;
  std::uint32_t communication_count = 0;
  std::uint32_t data_move_count = 0;
  for (const MemberSpec& member : members) {
    switch (member.kind) {
      case GraphLaunchBodyMemberRow::Kind::kCompute:
        ++compute_count;
        break;
      case GraphLaunchBodyMemberRow::Kind::kCommunication:
        ++communication_count;
        break;
      case GraphLaunchBodyMemberRow::Kind::kDataMove:
        ++data_move_count;
        break;
    }
  }
  std::set<std::uint32_t> lanes;
  for (const MemberSpec& member : members) {
    lanes.insert(member.lane);
  }
  const GraphLaunchBodyId body = test.ir.graph_launch_bodies.append(
      occurrence, body_template, TaskId::invalid(), TaskId::invalid(),
      compute_count, communication_count,
      static_cast<std::uint32_t>(lanes.size()), data_move_count);
  for (const MemberSpec& member : members) {
    const SymbolId raw_name =
        member.op.valid() ? member.op : test.ir.symbols.intern("raw");
    const TraceEventId event = test.ir.trace_events.append(
        test.source, raw_task_base + member.ordinal + 1, 0, member.stream,
        time_offset + member.start_ns, time_offset + member.end_ns, raw_name);
    const SymbolId comm_name =
        member.kind == GraphLaunchBodyMemberRow::Kind::kCommunication
            ? test.allreduce
            : SymbolId::invalid();
    const TaskId task = append_task(test, event, raw_task_base + member.ordinal,
                                    member.op, comm_name);
    test.ir.graph_launch_body_members.append(
        body, task, member.lane, member.ordinal, member.kind);
  }
  return body;
}

TestIr build_test_ir() {
  TestIr test;
  test.source = test.ir.source_refs.append("fixture", "memory", "TASK", 0);
  test.matmul = test.ir.symbols.intern("MatMul");
  test.allreduce = test.ir.symbols.intern("AllReduce");
  test.relu = test.ir.symbols.intern("Relu");
  test.h2d = test.ir.symbols.intern("H2D");

  const std::vector<MemberSpec> head_members = {
      {0, 0, 7, test.matmul, GraphLaunchBodyMemberRow::Kind::kCompute,
       100, 120},
      {1, 0, 9, test.allreduce, GraphLaunchBodyMemberRow::Kind::kCommunication,
       130, 160},
  };
  const std::vector<MemberSpec> layer_members = {
      {0, 0, 7, test.relu, GraphLaunchBodyMemberRow::Kind::kCompute, 200, 220},
      {0, 1, 7, test.matmul, GraphLaunchBodyMemberRow::Kind::kCompute,
       215, 250},
      {1, 0, 9, test.allreduce,
       GraphLaunchBodyMemberRow::Kind::kCommunication, 210, 260},
  };
  const std::vector<MemberSpec> tail_members = {
      {0, 0, 7, test.matmul, GraphLaunchBodyMemberRow::Kind::kCompute,
       300, 340},
      {1, 0, 9, test.h2d, GraphLaunchBodyMemberRow::Kind::kDataMove,
       310, 350},
  };

  test.head_template = test.ir.replay_body_templates.append(
      test.source, 111, test.ir.symbols.intern("head"), 1, 1, 2,
      ReplayBodyTopologyPolicy::kCapturedStreamSetUnordered);
  test.layer_template = test.ir.replay_body_templates.append(
      test.source, 222, test.ir.symbols.intern("layer"), 2, 1, 2,
      ReplayBodyTopologyPolicy::kCapturedStreamSetUnordered);
  test.tail_template = test.ir.replay_body_templates.append(
      test.source, 333, test.ir.symbols.intern("tail"), 1, 0, 2,
      ReplayBodyTopologyPolicy::kCapturedStreamSetUnordered, 1);

  test.graph_template = test.ir.graph_templates.append(test.source, 42, 4);
  const ReplayCompositionCandidateId composition =
      test.ir.replay_composition_candidates.append(
          test.source, 0, GraphLaunchOccurrenceId::invalid(),
          GraphLaunchOccurrenceId::invalid(), 8, 0, 4, 2, 0, 999,
          ReplayCompositionIdentityPolicy::kGraphConnection,
          ReplayCompositionOrderPolicy::kDeviceExecutionOrder,
          ReplayCompositionShapePolicy::kHeadRepeatedLayerTail,
          ReplayCompositionBoundaryPolicy::kExactPeriodicSuffix);
  test.head_slot = test.ir.replay_composition_slots.append(
      composition, 0, CapturedGraphInstanceId::invalid(),
      GraphSlotTemplateId::invalid(), test.head_template,
      ReplayCompositionSlotRole::kHead, -1);
  test.layer1_slot = test.ir.replay_composition_slots.append(
      composition, 1, CapturedGraphInstanceId::invalid(),
      GraphSlotTemplateId::invalid(), test.layer_template,
      ReplayCompositionSlotRole::kLayer, -1);
  test.layer2_slot = test.ir.replay_composition_slots.append(
      composition, 2, CapturedGraphInstanceId::invalid(),
      GraphSlotTemplateId::invalid(), test.layer_template,
      ReplayCompositionSlotRole::kLayer, -1);
  test.tail_slot = test.ir.replay_composition_slots.append(
      composition, 3, CapturedGraphInstanceId::invalid(),
      GraphSlotTemplateId::invalid(), test.tail_template,
      ReplayCompositionSlotRole::kTail, -1);

  const auto append_unit = [&](std::int64_t time_offset,
                               std::uint64_t raw_task_base) {
    const ReplayUnitId unit = test.ir.replay_units.append(
        test.graph_template, test.source, AnchorId::invalid(),
        AnchorId::invalid(), TraceEventId::invalid());
    const std::vector<std::pair<ReplayCompositionSlotId,
                                std::pair<ReplayBodyTemplateId,
                                          const std::vector<MemberSpec>*>>>
        slots = {
            {test.head_slot, {test.head_template, &head_members}},
            {test.layer1_slot, {test.layer_template, &layer_members}},
            {test.layer2_slot, {test.layer_template, &layer_members}},
            {test.tail_slot, {test.tail_template, &tail_members}},
        };
    std::uint64_t raw_base = raw_task_base;
    for (std::size_t order = 0; order < slots.size(); ++order) {
      const auto& item = slots[order];
      const GraphLaunchOccurrenceId occurrence =
          append_occurrence(test, time_offset, *item.second.second);
      append_body(test, occurrence, item.second.first, time_offset,
                  *item.second.second, raw_base);
      test.ir.replay_unit_launch_members.append(
          unit, static_cast<std::uint32_t>(order), occurrence, item.first);
      raw_base += 100;
    }
    return unit;
  };

  append_unit(0, 1);
  append_unit(1000, 1000);
  return test;
}

const ReplayAlignedCostAggregateRow* find_aggregate(
    const ReplayInternalCostMapResult& map,
    ReplayCompositionSlotRole role,
    ReplayBodyTemplateId body_template,
    std::uint32_t stream_id,
    std::uint32_t position,
    SymbolId identity) {
  for (const ReplayAlignedCostAggregateRow& row : map.aggregates) {
    if (row.slot_role == role &&
        row.replay_body_template_id == body_template &&
        row.stream_id == stream_id &&
        row.within_stream_position == position &&
        row.identity_symbol_id == identity) {
      return &row;
    }
  }
  return nullptr;
}

std::string member_fingerprint(const ReplayInternalCostMapResult& map) {
  std::string out;
  for (const ReplayMemberCostRow& member : map.members) {
    out += std::to_string(member.replay_unit_id.value()) + ":" +
           std::to_string(member.replay_unit_launch_member_id.value()) + ":" +
           std::to_string(member.member_order) + ":" +
           std::to_string(member.replay_composition_slot_id.value()) + ":" +
           std::to_string(static_cast<std::uint32_t>(member.slot_role)) + ":" +
           std::to_string(member.lane_ordinal) + ":" +
           std::to_string(member.within_stream_position) + ":" +
           std::to_string(member.stream_id) + ":" +
           std::to_string(member.identity_symbol_id.value()) + ":" +
           std::to_string(member.duration_ns) + ":" +
           std::to_string(member.relative_start_ns) + ";";
  }
  return out;
}

std::string aggregate_fingerprint(const ReplayInternalCostMapResult& map) {
  std::string out;
  for (const ReplayAlignedCostAggregateRow& row : map.aggregates) {
    out += std::to_string(row.graph_template_id.value()) + ":" +
           std::to_string(row.device_id) + ":" +
           std::to_string(static_cast<std::uint32_t>(row.slot_role)) + ":" +
           std::to_string(row.replay_body_template_id.value()) + ":" +
           std::to_string(row.stream_id) + ":" +
           std::to_string(row.within_stream_position) + ":" +
           std::to_string(row.identity_symbol_id.value()) + ":" +
           std::to_string(row.member_occurrence_count) + ":" +
           std::to_string(row.duration_median_ns) + ";";
  }
  return out;
}

void test_full_multi_launch_map() {
  TestIr test = build_test_ir();
  const ReplayInternalCostMapResult map =
      build_replay_internal_cost_map(test.ir);

  require(map.result_reason_codes.empty(), "no result-level reasons");
  require(map.units.size() == 2, "two replay unit occurrences");
  require(map.fully_supported_unit_count == 2, "both units fully supported");
  require(map.resolved_launch_count == 8, "all launches resolved");
  require(map.unsupported_launch_count == 0, "no unsupported launches");
  require(map.issues.empty(), "no issues in the exact fixture");

  // Exact ReplayUnit identity and ordered launch membership, including
  // repeated slot roles (two layer slots).
  for (std::size_t unit_index = 0; unit_index < map.units.size();
       ++unit_index) {
    const ReplayUnitCostBlock& block = map.units[unit_index];
    require(block.replay_unit_id == ReplayUnitId(unit_index),
            "unit identity preserved");
    require(block.graph_template_id == test.graph_template,
            "replay template identity");
    require(block.launch_member_count == 4, "multi-launch membership kept");
    require(block.resolved_launch_count == 4, "all members resolved");
    require(block.supported, "unit supported");
    require(block.unit_reason_codes.empty(), "no unit reasons");
    for (std::size_t order = 0; order < block.launch_members.size();
         ++order) {
      const ReplayLaunchMemberCostRow& member =
          block.launch_members[order];
      require(member.member_order == order, "member order preserved");
      require(member.supported, "launch member supported");
      require(member.replay_composition_slot_id ==
                  ReplayCompositionSlotId(order),
              "slot ownership exact");
      require(member.slot_order == order, "slot order preserved");
    }
    require(block.launch_members[0].slot_role ==
                ReplayCompositionSlotRole::kHead,
            "head role");
    require(block.launch_members[1].slot_role ==
                ReplayCompositionSlotRole::kLayer &&
                block.launch_members[2].slot_role ==
                    ReplayCompositionSlotRole::kLayer,
            "repeated layer role");
    require(block.launch_members[3].slot_role ==
                ReplayCompositionSlotRole::kTail,
            "tail role");
  }

  // Body ownership and cost-lens distinctions (task_sum vs busy_union vs
  // envelope vs kind lenses) for the first occurrence.
  const ReplayUnitCostBlock& unit0 = map.units[0];
  const ReplayLaunchMemberCostRow& head = unit0.launch_members[0];
  require(head.replay_body_template_id == test.head_template &&
              head.graph_launch_body_id.valid(),
          "head body template/body ownership");
  require(head.member_count == 2 && head.task_sum_ns == 50 &&
              head.busy_union_ns == 50 && head.envelope_ns == 60 &&
              head.compute_ns == 20 && head.communication_ns == 30 &&
              head.data_move_ns == 0,
          "head lens distinction: task_sum 50, busy 50, envelope 60");
  const ReplayLaunchMemberCostRow& layer = unit0.launch_members[1];
  require(layer.replay_body_template_id == test.layer_template &&
              layer.member_count == 3 && layer.task_sum_ns == 105 &&
              layer.busy_union_ns == 60 && layer.envelope_ns == 60 &&
              layer.compute_ns == 55 && layer.communication_ns == 50 &&
              layer.data_move_ns == 0,
          "layer overlap: task_sum 105, busy union 60, envelope 60");
  const ReplayLaunchMemberCostRow& tail = unit0.launch_members[3];
  require(tail.replay_body_template_id == test.tail_template &&
              tail.member_count == 2 && tail.task_sum_ns == 80 &&
              tail.busy_union_ns == 50 && tail.envelope_ns == 50 &&
              tail.compute_ns == 40 && tail.communication_ns == 0 &&
              tail.data_move_ns == 40,
          "tail data-move lens");

  // Per-stream ordered cost evidence.
  require(head.streams.size() == 2 && head.streams[0].stream_id == 7 &&
              head.streams[0].member_count == 1 &&
              head.streams[0].task_sum_ns == 20 &&
              head.streams[0].busy_union_ns == 20 &&
              head.streams[0].compute_ns == 20 &&
              head.streams[1].stream_id == 9 &&
              head.streams[1].task_sum_ns == 30 &&
              head.streams[1].communication_ns == 30,
          "head per-stream lenses");
  require(layer.streams.size() == 2 && layer.streams[0].stream_id == 7 &&
              layer.streams[0].member_count == 2 &&
              layer.streams[0].task_sum_ns == 55 &&
              layer.streams[0].busy_union_ns == 50 &&
              layer.streams[0].compute_ns == 55 &&
              layer.streams[1].stream_id == 9 &&
              layer.streams[1].task_sum_ns == 50 &&
              layer.streams[1].busy_union_ns == 50 &&
              layer.streams[1].communication_ns == 50,
          "layer per-stream overlap-safe busy union");
  require(tail.streams.size() == 2 && tail.streams[1].data_move_ns == 40,
          "tail per-stream data move");

  // Fine-grained member rows: 7 members per launch sequence, per-stream
  // within-body order preserved, relative timing re-labeled from body min
  // start, provenance carried.
  const std::string member_count_message =
      "two occurrences x 20 members (10 per unit); actual=" +
      std::to_string(map.members.size());
  require(map.members.size() == 20, member_count_message.c_str());
  const ReplayMemberCostRow& first = map.members[0];
  require(first.replay_unit_id == ReplayUnitId(0) &&
              first.member_order == 0 &&
              first.replay_composition_slot_id == test.head_slot &&
              first.lane_ordinal == 0 && first.within_stream_position == 0 &&
              first.stream_id == 7 && first.identity_symbol_id == test.matmul &&
              first.kind == GraphLaunchBodyMemberRow::Kind::kCompute &&
              first.duration_ns == 20 && first.start_ns == 100 &&
              first.relative_start_ns == 0 && first.relative_end_ns == 20 &&
              first.source_ref_id == test.source &&
              first.raw_task_id == 1,
          "first member identity/timing/provenance");
  const ReplayMemberCostRow& second = map.members[1];
  require(second.lane_ordinal == 1 && second.within_stream_position == 0 &&
              second.stream_id == 9 && second.duration_ns == 30 &&
              second.relative_start_ns == 30 &&
              second.identity_symbol_id == test.allreduce &&
              second.kind == GraphLaunchBodyMemberRow::Kind::kCommunication,
          "second head member on stream 9 with relative offset");
  // Within-stream order: lane 0 keeps Relu (pos 0) before MatMul (pos 1).
  require(map.members[2].identity_symbol_id == test.relu &&
              map.members[2].lane_ordinal == 0 &&
              map.members[2].within_stream_position == 0 &&
              map.members[3].identity_symbol_id == test.matmul &&
              map.members[3].within_stream_position == 1 &&
              map.members[4].identity_symbol_id == test.allreduce &&
              map.members[4].lane_ordinal == 1,
          "per-stream member sequence order");
  require(map.members[10].replay_unit_id == ReplayUnitId(1),
          "second occurrence starts after first");

  // Deterministic ordering across independent runs.
  const ReplayInternalCostMapResult rerun =
      build_replay_internal_cost_map(test.ir);
  require(member_fingerprint(map) == member_fingerprint(rerun),
          "member emission order deterministic");
  require(aggregate_fingerprint(map) == aggregate_fingerprint(rerun),
          "aggregate order deterministic");

  // Aligned aggregates under the stable structural key.
  require(map.aggregates.size() == 7, "seven structural aggregate keys");
  require(map.aggregates[0].slot_role == ReplayCompositionSlotRole::kHead &&
              map.aggregates[0].replay_body_template_id ==
                  test.head_template &&
              map.aggregates[0].stream_id == 7 &&
              map.aggregates[0].within_stream_position == 0 &&
              map.aggregates[0].identity_symbol_id == test.matmul,
          "aggregates sorted by structural key");

  const ReplayAlignedCostAggregateRow* head_compute = find_aggregate(
      map, ReplayCompositionSlotRole::kHead, test.head_template, 7, 0,
      test.matmul);
  require(head_compute != nullptr &&
              head_compute->member_occurrence_count == 2 &&
              head_compute->replay_unit_count == 2 &&
              head_compute->launch_member_count == 2 &&
              head_compute->kind ==
                  GraphLaunchBodyMemberRow::Kind::kCompute &&
              head_compute->kind_consistent &&
              head_compute->lane_consistent &&
              head_compute->distribution_supported &&
              head_compute->duration_median_ns == 20 &&
              head_compute->duration_p25_ns == 20 &&
              head_compute->duration_p75_ns == 20,
          "head compute aggregate aligned across occurrences");
  const ReplayAlignedCostAggregateRow* head_comm = find_aggregate(
      map, ReplayCompositionSlotRole::kHead, test.head_template, 9, 0,
      test.allreduce);
  require(head_comm != nullptr && head_comm->member_occurrence_count == 2 &&
              head_comm->duration_median_ns == 30,
          "head communication aggregate");

  // Repeated slot roles align under the same structural key.
  const ReplayAlignedCostAggregateRow* layer_relu = find_aggregate(
      map, ReplayCompositionSlotRole::kLayer, test.layer_template, 7, 0,
      test.relu);
  require(layer_relu != nullptr && layer_relu->member_occurrence_count == 4 &&
              layer_relu->replay_unit_count == 2 &&
              layer_relu->launch_member_count == 4 &&
              layer_relu->duration_median_ns == 20,
          "repeated layer slots align");
  const ReplayAlignedCostAggregateRow* layer_matmul = find_aggregate(
      map, ReplayCompositionSlotRole::kLayer, test.layer_template, 7, 1,
      test.matmul);
  require(layer_matmul != nullptr &&
              layer_matmul->member_occurrence_count == 4 &&
              layer_matmul->duration_median_ns == 35,
          "layer second position aligns");
  const ReplayAlignedCostAggregateRow* layer_comm = find_aggregate(
      map, ReplayCompositionSlotRole::kLayer, test.layer_template, 9, 0,
      test.allreduce);
  require(layer_comm != nullptr && layer_comm->member_occurrence_count == 4 &&
              layer_comm->duration_median_ns == 50,
          "layer communication aligns");
  const ReplayAlignedCostAggregateRow* tail_compute = find_aggregate(
      map, ReplayCompositionSlotRole::kTail, test.tail_template, 7, 0,
      test.matmul);
  require(tail_compute != nullptr && tail_compute->member_occurrence_count == 2 &&
              tail_compute->duration_median_ns == 40,
          "tail compute aggregate");
  const ReplayAlignedCostAggregateRow* tail_move = find_aggregate(
      map, ReplayCompositionSlotRole::kTail, test.tail_template, 9, 0,
      test.h2d);
  require(tail_move != nullptr && tail_move->member_occurrence_count == 2 &&
              tail_move->duration_median_ns == 40 &&
              tail_move->kind == GraphLaunchBodyMemberRow::Kind::kDataMove,
          "tail data-move aggregate keeps kind lens");

  // Role-collapsed aggregation scope: repeated layer slots merge at the map
  // surface with multiplicity preserved, and the scope is explicit.
  require(layer_relu != nullptr &&
              layer_relu->aggregation_scope ==
                  ReplayAggregationScope::kRoleCollapsed &&
              layer_relu->member_occurrence_count == 4 &&
              layer_relu->replay_unit_count == 2 &&
              layer_relu->launch_member_count == 4,
          "role-collapsed layer family keeps multiplicity");
  for (const ReplayAlignedCostAggregateRow& row : map.aggregates) {
    require(row.aggregation_scope == ReplayAggregationScope::kRoleCollapsed,
            "every aggregate carries the explicit role_collapsed scope");
  }

  // Drill-down contract: exact member rows retain replay-unit occurrence,
  // slot id and slot_order for every slot, so a particular L position is
  // always recoverable from the map surface.
  require(map.members[2].replay_composition_slot_id == test.layer1_slot &&
              map.members[2].slot_order == 1 &&
              map.members[5].replay_composition_slot_id == test.layer2_slot &&
              map.members[5].slot_order == 2 &&
              map.members[15].replay_unit_id == ReplayUnitId(1) &&
              map.members[15].replay_composition_slot_id ==
                  test.layer2_slot &&
              map.members[15].slot_order == 2,
          "member rows keep exact slot ids and slot_order for drill-down");

  // Scheduled-work share contract: integer ppm of the owning body task_sum
  // with the exact denominator recorded; shares partition the body's
  // task_sum (sum over one body is 1,000,000 within truncation).
  require(map.members[0].scheduled_work_share_supported &&
              map.members[0].scheduled_work_share_ppm == 400000 &&
              map.members[0].scheduled_work_denominator_body_task_sum_ns ==
                  50 &&
              map.members[1].scheduled_work_share_ppm == 600000 &&
              map.members[1].scheduled_work_denominator_body_task_sum_ns ==
                  50,
          "head member shares partition head task_sum 50");
  require(map.members[2].scheduled_work_share_ppm == 190476 &&
              map.members[3].scheduled_work_share_ppm == 333333 &&
              map.members[4].scheduled_work_share_ppm == 476190 &&
              map.members[4].scheduled_work_denominator_body_task_sum_ns ==
                  105,
          "layer member shares partition layer task_sum 105");
  require(map.members[8].scheduled_work_share_ppm == 500000 &&
              map.members[9].scheduled_work_share_ppm == 500000 &&
              map.members[9].scheduled_work_denominator_body_task_sum_ns ==
                  80,
          "tail member shares partition tail task_sum 80");
  require(head_compute->scheduled_work_share_supported &&
              head_compute->scheduled_work_share_ppm == 400000 &&
              head_compute->scheduled_work_denominator_body_task_sum_ns ==
                  100 &&
              head_comm->scheduled_work_share_ppm == 600000,
          "head aggregate shares over summed owning-body task_sums");
  require(layer_relu->scheduled_work_share_ppm == 190476 &&
              layer_relu->scheduled_work_denominator_body_task_sum_ns == 420 &&
              layer_matmul->scheduled_work_share_ppm == 333333 &&
              layer_comm->scheduled_work_share_ppm == 476190,
          "layer aggregate shares preserve role-collapsed multiplicity");
  require(tail_compute->scheduled_work_share_ppm == 500000 &&
              tail_move->scheduled_work_share_ppm == 500000 &&
              tail_move->scheduled_work_denominator_body_task_sum_ns == 160,
          "tail aggregate shares");
}

}  // namespace

namespace traceloom::testing::replay_internal_cost_map {

void run_happy_path_tests() { test_full_multi_launch_map(); }

}  // namespace traceloom::testing::replay_internal_cost_map
