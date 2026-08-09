#include "traceloom/compat/exact_graph_sql_rows.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "traceloom/testing/test_util.h"

namespace {

using namespace traceloom;

// Provider-neutral multi-slot exact ReplayUnit: one unit, two ordered slots,
// each with its own body (two members) and its own promoted anchor.
NativeIr build_multi_slot_exact_ir() {
  NativeIr ir;
  const SourceRefId source = ir.source_refs.append(
      "ascend", "acl-exact", "ACLGRAPH_REPLAY_UNIT", 0);
  const SourceRefId task_source =
      ir.source_refs.append("ascend", "acl-exact", "TASK", 0);
  const SymbolId ai_core = ir.symbols.intern("AI_CORE");
  const SymbolId head = ir.symbols.intern("HeadOp");
  const SymbolId tail = ir.symbols.intern("TailOp");
  const SymbolId graph = ir.symbols.intern("GraphReplayUnit ExactT1");

  const TraceEventId launch =
      ir.trace_events.append(source, 1, 0, 7, 1000, 3000, graph);
  const GraphLaunchOccurrenceId head_occurrence =
      ir.graph_launch_occurrences.append(
          source, source, 0, 1, -1, 9001, -1, StreamId::invalid(),
          StreamId::invalid(), CapturedGraphInstanceId::invalid(),
          TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), 1000,
          2000, -1, GraphLaunchMatchPolicy::kNotifyCompletionAdjacent);
  const GraphLaunchOccurrenceId tail_occurrence =
      ir.graph_launch_occurrences.append(
          source, source, 0, 1, -1, 9001, -1, StreamId::invalid(),
          StreamId::invalid(), CapturedGraphInstanceId::invalid(),
          TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), 2000,
          3000, -1, GraphLaunchMatchPolicy::kNotifyCompletionAdjacent);

  const auto add_member = [&](const SymbolId& symbol, std::uint64_t row_id,
                              std::int64_t start_ns) {
    const TraceEventId event =
        ir.trace_events.append(task_source, row_id, 0, 7, start_ns,
                               start_ns + 500, symbol);
    return ir.tasks.append(task_source, event, row_id, row_id, -1, ai_core,
                           symbol, symbol, ai_core, SymbolId::invalid());
  };
  const TaskId head_task_a = add_member(head, 31, 1050);
  const TaskId head_task_b = add_member(head, 32, 1200);
  const TaskId tail_task_a = add_member(tail, 33, 2050);
  const TaskId tail_task_b = add_member(tail, 34, 2200);

  const ReplayBodyTemplateId head_template = ir.replay_body_templates.append(
      source, 11, ir.symbols.intern("HeadOp"), 2, 0, 1,
      ReplayBodyTopologyPolicy::kSingleModelStream);
  const ReplayBodyTemplateId tail_template = ir.replay_body_templates.append(
      source, 12, ir.symbols.intern("TailOp"), 2, 0, 1,
      ReplayBodyTopologyPolicy::kSingleModelStream);
  const GraphLaunchBodyId head_body = ir.graph_launch_bodies.append(
      head_occurrence, head_template, head_task_a, head_task_b, 2, 0, 1);
  ir.graph_launch_body_members.append(
      head_body, head_task_a, 0, 0, GraphLaunchBodyMemberRow::Kind::kCompute);
  ir.graph_launch_body_members.append(
      head_body, head_task_b, 0, 1, GraphLaunchBodyMemberRow::Kind::kCompute);
  const GraphLaunchBodyId tail_body = ir.graph_launch_bodies.append(
      tail_occurrence, tail_template, tail_task_a, tail_task_b, 2, 0, 1);
  ir.graph_launch_body_members.append(
      tail_body, tail_task_a, 0, 0, GraphLaunchBodyMemberRow::Kind::kCompute);
  ir.graph_launch_body_members.append(
      tail_body, tail_task_b, 0, 1, GraphLaunchBodyMemberRow::Kind::kCompute);

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
  ir.anchors.append(source, TraceEventId::invalid(), unit,
                    AnchorKind::kGraphH, ir.symbols.intern("ACLH"), 0, 7,
                    1000, 2000, head_member);
  ir.anchors.append(source, TraceEventId::invalid(), unit,
                    AnchorKind::kGraphT, ir.symbols.intern("ACLT"), 0, 7,
                    2000, 3000, tail_member);
  return ir;
}

NativeIr build_best_effort_ir() {
  NativeIr ir;
  const SourceRefId source = ir.source_refs.append(
      "fixture", "best-effort", "ACLGRAPH_REPLAY_UNIT", 0);
  const TraceEventId launch =
      ir.trace_events.append(source, 1, 0, 7, 1000, 2000,
                             ir.symbols.intern("GraphReplayUnit T1"));
  const GraphTemplateId graph_template =
      ir.graph_templates.append(source, 33, 1);
  ir.replay_units.append(graph_template, source, AnchorId::invalid(),
                         AnchorId::invalid(), launch);
  return ir;
}

NativeIr build_member_without_body_ir() {
  NativeIr ir;
  const SourceRefId source = ir.source_refs.append(
      "fixture", "bad-chain", "ACLGRAPH_REPLAY_UNIT", 0);
  const TraceEventId launch =
      ir.trace_events.append(source, 1, 0, 7, 1000, 2000,
                             ir.symbols.intern("GraphReplayUnit ExactT1"));
  const GraphLaunchOccurrenceId occurrence =
      ir.graph_launch_occurrences.append(
          source, source, 0, 1, -1, 9001, -1, StreamId::invalid(),
          StreamId::invalid(), CapturedGraphInstanceId::invalid(),
          TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), 1000,
          2000, -1, GraphLaunchMatchPolicy::kNotifyCompletionAdjacent);
  const ReplayBodyTemplateId body = ir.replay_body_templates.append(
      source, 11, ir.symbols.intern("HeadOp"), 1, 0, 1,
      ReplayBodyTopologyPolicy::kSingleModelStream);
  const ReplayCompositionCandidateId composition =
      ir.replay_composition_candidates.append(
          source, 0, occurrence, occurrence, 1, 0, 1, 1, 0, 22,
          ReplayCompositionIdentityPolicy::kGraphConnection,
          ReplayCompositionOrderPolicy::kHostSubmissionOrder,
          ReplayCompositionShapePolicy::kSingleGraph,
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
  const GraphTemplateId graph_template =
      ir.graph_templates.append(source, 33, 1);
  const ReplayUnitId unit = ir.replay_units.append(
      graph_template, source, AnchorId::invalid(), AnchorId::invalid(),
      launch, region);
  ir.replay_unit_launch_members.append(unit, 0, occurrence, slot);
  return ir;
}

NativeIr build_duplicate_anchor_mapping_ir() {
  NativeIr ir = build_multi_slot_exact_ir();
  // A second anchor claiming the same (replay unit, launch member) as the
  // fixture's head anchor: the exact anchor mapping becomes ambiguous.
  const ReplayUnitId unit = ir.replay_units.rows().front().id;
  const ReplayUnitLaunchMemberId head_member =
      ir.replay_unit_launch_members.rows().front().id;
  ir.anchors.append(ir.source_refs.rows().front().id,
                    TraceEventId::invalid(), unit, AnchorKind::kGraphH,
                    ir.symbols.intern("ACLH2"), 0, 7, 1000, 2000,
                    head_member);
  return ir;
}

NativeIr build_slot_template_mismatch_ir() {
  NativeIr ir = build_multi_slot_exact_ir();
  // Rewrite the slots so the head slot claims a body template that does not
  // match the unique body of its launch occurrence.
  const ReplayCompositionCandidateId composition =
      ir.replay_composition_candidates.rows().front().id;
  const ReplayBodyTemplateId other_template =
      ir.replay_body_templates.append(
          ir.source_refs.rows().front().id, 99,
          ir.symbols.intern("OtherOp"), 2, 0, 1,
          ReplayBodyTopologyPolicy::kSingleModelStream);
  ir.replay_composition_slots = ReplayCompositionSlotTable{};
  ir.replay_composition_slots.append(
      composition, 0, CapturedGraphInstanceId::invalid(),
      GraphSlotTemplateId::invalid(), other_template,
      ReplayCompositionSlotRole::kHead, 9001);
  ir.replay_composition_slots.append(
      composition, 1, CapturedGraphInstanceId::invalid(),
      GraphSlotTemplateId::invalid(),
      ir.replay_body_templates.rows()[1].id,
      ReplayCompositionSlotRole::kTail, 9002);
  return ir;
}

NativeIr build_non_contiguous_order_ir() {
  NativeIr ir = build_multi_slot_exact_ir();
  // Rewrite the unit membership to a non-contiguous order {0, 2}; the two
  // occurrences and slots are the fixture's original id 0 and 1.
  ir.replay_unit_launch_members = ReplayUnitLaunchMemberTable{};
  const ReplayUnitId unit = ir.replay_units.rows().front().id;
  ir.replay_unit_launch_members.append(unit, 0, GraphLaunchOccurrenceId(0),
                                       ReplayCompositionSlotId(0));
  ir.replay_unit_launch_members.append(unit, 2, GraphLaunchOccurrenceId(1),
                                       ReplayCompositionSlotId(1));
  return ir;
}

template <typename Fn>
void require_throws_with(const std::string& needle, const Fn& fn) {
  try {
    fn();
  } catch (const std::invalid_argument& ex) {
    const std::string detail =
        "exception did not explain the invalid exact graph chain: " +
        std::string(ex.what());
    traceloom::testing::require(
        std::string(ex.what()).find(needle) != std::string::npos,
        detail.c_str());
    return;
  }
  traceloom::testing::require(false,
                              "expected exact graph SQL builder to reject "
                              "the invalid chain");
}

}  // namespace

int main() {
  using namespace traceloom;

  // Happy path: provider-neutral multi-slot exact chain.
  {
    const NativeIr ir = build_multi_slot_exact_ir();
    const compat::ExactGraphSqlRows rows =
        compat::build_exact_graph_sql_rows(ir, "ascend", 3);
    traceloom::testing::require(rows.launches.size() == 2,
                                "multi-slot exact unit lost a launch");
    traceloom::testing::require(rows.members.size() == 4,
                                "multi-slot exact bodies lost members");
    const compat::GraphLaunchSqlRow& head_launch = rows.launches[0];
    const compat::GraphLaunchSqlRow& tail_launch = rows.launches[1];
    traceloom::testing::require(
        head_launch.graph_provider == "aclgraph" &&
            head_launch.member_order == 0 && head_launch.slot_order == 0 &&
            head_launch.anchor_id == "anchor-0" &&
            head_launch.body_id != tail_launch.body_id &&
            head_launch.graph_event_id == "event-0" &&
            head_launch.correlation_id.empty(),
        "head slot launch lost exact anchor/order/provider identity");
    traceloom::testing::require(
        tail_launch.member_order == 1 && tail_launch.slot_order == 1 &&
            tail_launch.anchor_id == "anchor-1",
        "tail slot launch lost its exact anchor/order identity");
    for (const compat::GraphBodyMemberSqlRow& member : rows.members) {
      const bool launch_matches =
          member.launch_id == (member.member_order == 0
                                   ? head_launch.launch_id
                                   : tail_launch.launch_id);
      if (!launch_matches ||
          member.graph_provider != "aclgraph" ||
          member.evidence_level != "exact_direct" ||
          member.graph_node_id != -1 ||
          member.original_graph_node_id != -1 ||
          member.source_table != "TASK") {
        std::cerr << "member_id=" << member.member_id
                  << " launch=" << member.launch_id
                  << " expected="
                  << (member.member_order == 0 ? head_launch.launch_id
                                               : tail_launch.launch_id)
                  << " order=" << member.member_order
                  << " provider=" << member.graph_provider
                  << " evidence=" << member.evidence_level
                  << " node=" << member.graph_node_id
                  << " orig=" << member.original_graph_node_id
                  << " src=" << member.source_table << '\n';
      }
      traceloom::testing::require(
          launch_matches &&
              member.graph_provider == "aclgraph" &&
              member.evidence_level == "exact_direct" &&
              member.graph_node_id == -1 &&
              member.original_graph_node_id == -1 &&
              member.source_table == "TASK",
          "member row lost exact provenance or provider identity");
    }
  }

  // Best-effort replay units stay outside the exact surface.
  {
    const NativeIr ir = build_best_effort_ir();
    const compat::ExactGraphSqlRows rows =
        compat::build_exact_graph_sql_rows(ir, "fixture", 3);
    traceloom::testing::require(rows.launches.empty() && rows.members.empty(),
                                "best-effort replay unit leaked into the "
                                "exact SQL surface");
  }

  // Fail closed: an exact launch member without a body is invalid.
  {
    const NativeIr ir = build_member_without_body_ir();
    require_throws_with("has no body", [&]() {
      (void)compat::build_exact_graph_sql_rows(ir, "fixture", 3);
    });
  }

  // Fail closed: non-contiguous member order is invalid.
  {
    const NativeIr ir = build_non_contiguous_order_ir();
    require_throws_with("not contiguous", [&]() {
      (void)compat::build_exact_graph_sql_rows(ir, "ascend", 3);
    });
  }

  // Fail closed: two anchors mapping the same (replay unit, launch member)
  // make the exact anchor mapping ambiguous.
  {
    const NativeIr ir = build_duplicate_anchor_mapping_ir();
    require_throws_with("multiple anchors map to the same replay unit "
                        "launch member",
                        [&]() {
                          (void)compat::build_exact_graph_sql_rows(ir,
                                                                   "ascend",
                                                                   3);
                        });
  }

  // Fail closed: a composition slot whose replay_body_template_id is not the
  // unique body's template is invalid exact evidence.
  {
    const NativeIr ir = build_slot_template_mismatch_ir();
    require_throws_with("does not match the launch body template", [&]() {
      (void)compat::build_exact_graph_sql_rows(ir, "ascend", 3);
    });
  }
  return 0;
}
