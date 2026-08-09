#include "traceloom/compat/exact_graph_sql_rows.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/timeline_rows.h"

namespace traceloom::compat {
namespace {

double ns_to_us(std::int64_t ns) {
  return static_cast<double>(ns) / 1000.0;
}

const char* graph_launch_match_policy_name(GraphLaunchMatchPolicy policy) {
  switch (policy) {
    case GraphLaunchMatchPolicy::kNotifyCompletionAdjacent:
      return "notify_completion_adjacent";
    case GraphLaunchMatchPolicy::kNotifyOrderedFallback:
      return "notify_ordered_fallback";
    case GraphLaunchMatchPolicy::kUnmatched:
      return "unmatched";
    case GraphLaunchMatchPolicy::kCudaRuntimeCorrelation:
      return "cuda_runtime_correlation";
  }
  return "unmatched";
}

const char* graph_launch_instance_association_policy_name(
    GraphLaunchInstanceAssociationPolicy policy) {
  switch (policy) {
    case GraphLaunchInstanceAssociationPolicy::kNone:
      return "none";
    case GraphLaunchInstanceAssociationPolicy::kRecordModelId:
      return "record_model_id";
    case GraphLaunchInstanceAssociationPolicy::kRecordModelStream:
      return "record_model_stream";
    case GraphLaunchInstanceAssociationPolicy::kCudaGraphNodeSet:
      return "cuda_graph_node_set";
  }
  return "none";
}

const char* graph_body_member_kind_name(GraphLaunchBodyMemberRow::Kind kind) {
  switch (kind) {
    case GraphLaunchBodyMemberRow::Kind::kCompute:
      return "compute";
    case GraphLaunchBodyMemberRow::Kind::kCommunication:
      return "communication";
    case GraphLaunchBodyMemberRow::Kind::kDataMove:
      return "data_move";
  }
  return "compute";
}

void require_unit_ids_in_range(const NativeIr& ir,
                               const ReplayUnitRow& replay_unit) {
  if (!replay_unit.graph_template_id.valid() ||
      replay_unit.graph_template_id.value() >= ir.graph_templates.size()) {
    throw std::invalid_argument(
        "exact graph SQL: ReplayUnitRow graph_template_id is out of range");
  }
  if (!replay_unit.source_ref_id.valid() ||
      replay_unit.source_ref_id.value() >= ir.source_refs.size()) {
    throw std::invalid_argument(
        "exact graph SQL: ReplayUnitRow source_ref_id is out of range");
  }
  if (!replay_unit.launch_trace_event_id.valid() ||
      replay_unit.launch_trace_event_id.value() >= ir.trace_events.size()) {
    throw std::invalid_argument(
        "exact graph SQL: ReplayUnitRow launch_trace_event_id is out of "
        "range");
  }
}

GraphLaunchBodyId require_single_body_for_occurrence(
    const NativeIr& ir, GraphLaunchOccurrenceId occurrence_id) {
  GraphLaunchBodyId body_id = GraphLaunchBodyId::invalid();
  for (const GraphLaunchBodyRow& body : ir.graph_launch_bodies.rows()) {
    if (body.graph_launch_occurrence_id != occurrence_id) {
      continue;
    }
    if (body_id.valid()) {
      throw std::invalid_argument(
          "exact graph SQL: exact launch occurrence has multiple bodies");
    }
    body_id = body.id;
  }
  if (!body_id.valid()) {
    throw std::invalid_argument(
        "exact graph SQL: exact ReplayUnitLaunchMember occurrence has no "
        "body");
  }
  const GraphLaunchBodyRow& body = ir.graph_launch_bodies.row(body_id);
  if (!body.replay_body_template_id.valid() ||
      body.replay_body_template_id.value() >=
          ir.replay_body_templates.size()) {
    throw std::invalid_argument(
        "exact graph SQL: GraphLaunchBodyRow replay_body_template_id is out "
        "of range");
  }
  return body_id;
}

std::string correlation_text(std::int64_t raw_launch_connection_id) {
  return raw_launch_connection_id >= 0
             ? std::to_string(raw_launch_connection_id)
             : std::string();
}

}  // namespace

ExactGraphSqlRows build_exact_graph_sql_rows(const NativeIr& ir,
                                             const std::string& source_kind,
                                             std::uint32_t db_idx) {
  ExactGraphSqlRows rows;

  // Exact anchor mapping: (replay unit, launch member) -> promoted anchor.
  // Anchors created for exact members carry replay_unit_launch_member_id, so
  // this mapping is exact and never inferred from time.
  std::map<std::pair<std::uint32_t, std::uint32_t>, AnchorId> anchor_by_member;
  for (const AnchorRow& anchor : ir.anchors.rows()) {
    if (!anchor.replay_unit_launch_member_id.valid() ||
        !anchor.replay_unit_id.valid() ||
        anchor.replay_unit_id.value() >= ir.replay_units.size()) {
      continue;
    }
    const auto inserted = anchor_by_member.emplace(
        std::make_pair(anchor.replay_unit_id.value(),
                       anchor.replay_unit_launch_member_id.value()),
        anchor.id);
    if (!inserted.second) {
      throw std::invalid_argument(
          "exact graph SQL: multiple anchors map to the same replay unit "
          "launch member");
    }
  }

  for (const ReplayUnitRow& replay_unit : ir.replay_units.rows()) {
    require_unit_ids_in_range(ir, replay_unit);

    std::vector<const ReplayUnitLaunchMemberRow*> members;
    for (const ReplayUnitLaunchMemberRow& member :
         ir.replay_unit_launch_members.rows()) {
      if (member.replay_unit_id == replay_unit.id) {
        members.push_back(&member);
      }
    }
    // Best-effort replay units (graph-trace overlap, capture-stream task
    // overlap) have no ordered launch membership and stay outside this exact
    // surface.
    if (members.empty()) {
      continue;
    }
    std::sort(members.begin(), members.end(),
              [](const ReplayUnitLaunchMemberRow* lhs,
                 const ReplayUnitLaunchMemberRow* rhs) {
                return lhs->member_order < rhs->member_order;
              });
    for (std::size_t index = 0; index < members.size(); ++index) {
      if (members[index]->member_order != index) {
        throw std::invalid_argument(
            "exact graph SQL: ReplayUnit launch membership order is not "
            "contiguous");
      }
    }

    const SourceRefRow& unit_source =
        ir.source_refs.row(replay_unit.source_ref_id);
    const bool is_aclgraph = unit_source.table_name == "ACLGRAPH_REPLAY_UNIT";
    const bool is_cuda =
        source_kind == "cuda_nsys_sqlite" ||
        unit_source.table_name == "CUPTI_ACTIVITY_KIND_GRAPH_TRACE";
    const std::string provider =
        is_aclgraph ? "aclgraph" : (is_cuda ? "cuda" : source_kind);
    const std::string graph_event_id =
        trace_event_compat_id(replay_unit.launch_trace_event_id);

    for (const ReplayUnitLaunchMemberRow* member_ptr : members) {
      const ReplayUnitLaunchMemberRow& member = *member_ptr;
      if (!member.graph_launch_occurrence_id.valid() ||
          member.graph_launch_occurrence_id.value() >=
              ir.graph_launch_occurrences.size()) {
        throw std::invalid_argument(
            "exact graph SQL: ReplayUnitLaunchMemberRow occurrence id is out "
            "of range");
      }
      if (!member.replay_composition_slot_id.valid() ||
          member.replay_composition_slot_id.value() >=
              ir.replay_composition_slots.size()) {
        throw std::invalid_argument(
            "exact graph SQL: ReplayUnitLaunchMemberRow slot id is out of "
            "range");
      }
      const GraphLaunchOccurrenceRow& occurrence =
          ir.graph_launch_occurrences.row(
              member.graph_launch_occurrence_id);
      if (occurrence.end_ns < occurrence.start_ns) {
        throw std::invalid_argument(
            "exact graph SQL: GraphLaunchOccurrenceRow has a negative "
            "duration");
      }
      const ReplayCompositionSlotRow& slot =
          ir.replay_composition_slots.row(member.replay_composition_slot_id);
      const GraphLaunchBodyId body_id =
          require_single_body_for_occurrence(ir, member.graph_launch_occurrence_id);
      const GraphLaunchBodyRow& body = ir.graph_launch_bodies.row(body_id);
      if (!slot.replay_body_template_id.valid() ||
          slot.replay_body_template_id.value() >=
              ir.replay_body_templates.size()) {
        throw std::invalid_argument(
            "exact graph SQL: ReplayCompositionSlotRow "
            "replay_body_template_id is out of range");
      }
      if (slot.replay_body_template_id != body.replay_body_template_id) {
        throw std::invalid_argument(
            "exact graph SQL: composition slot replay_body_template_id does "
            "not match the launch body template");
      }

      GraphLaunchSqlRow launch_row;
      launch_row.launch_id = "graph-launch-" +
                             std::to_string(member.id.value());
      launch_row.db_idx = db_idx;
      launch_row.device_id = occurrence.device_id;
      launch_row.graph_provider = provider;
      launch_row.graph_event_id = graph_event_id;
      const auto anchor_found = anchor_by_member.find(
          std::make_pair(replay_unit.id.value(), member.id.value()));
      launch_row.anchor_id = anchor_found == anchor_by_member.end()
                                 ? std::string()
                                 : anchor_compat_id(anchor_found->second);
      launch_row.replay_unit_id = replay_unit.id.value();
      launch_row.graph_template_id = replay_unit.graph_template_id.value();
      launch_row.graph_launch_occurrence_id =
          member.graph_launch_occurrence_id.value();
      launch_row.replay_body_template_id =
          body.replay_body_template_id.value();
      launch_row.body_id = body.id.value();
      launch_row.member_order = member.member_order;
      launch_row.slot_order = slot.slot_order;
      launch_row.correlation_id =
          correlation_text(occurrence.raw_launch_connection_id);
      launch_row.match_policy =
          graph_launch_match_policy_name(occurrence.match_policy);
      launch_row.association_policy =
          graph_launch_instance_association_policy_name(
              occurrence.instance_association_policy);
      launch_row.start_ns = occurrence.start_ns;
      launch_row.end_ns = occurrence.end_ns;
      launch_row.dur_us = ns_to_us(occurrence.end_ns - occurrence.start_ns);
      // Capture values needed for member rows before launch_row is moved
      // into the result (moved-from std::string members are unspecified).
      const std::string launch_key = launch_row.launch_id;
      const std::string match_policy = launch_row.match_policy;
      const std::string association_policy = launch_row.association_policy;
      rows.launches.push_back(std::move(launch_row));

      for (const GraphLaunchBodyMemberRow& body_member :
           ir.graph_launch_body_members.rows()) {
        if (body_member.graph_launch_body_id != body_id) {
          continue;
        }
        if (!body_member.task_id.valid() ||
            body_member.task_id.value() >= ir.tasks.size()) {
          throw std::invalid_argument(
              "exact graph SQL: GraphLaunchBodyMemberRow task id is out of "
              "range");
        }
        const TaskRow& task = ir.tasks.row(body_member.task_id);
        if (!task.trace_event_id.valid() ||
            task.trace_event_id.value() >= ir.trace_events.size()) {
          throw std::invalid_argument(
              "exact graph SQL: member task has no in-range trace event");
        }
        const TraceEventRow& member_event =
            ir.trace_events.row(task.trace_event_id);
        if (member_event.end_ns < member_event.start_ns) {
          throw std::invalid_argument(
              "exact graph SQL: member trace event has a negative duration");
        }
        if (!task.source_ref_id.valid() ||
            task.source_ref_id.value() >= ir.source_refs.size()) {
          throw std::invalid_argument(
              "exact graph SQL: member task source_ref_id is out of range");
        }
        const SourceRefRow& member_source =
            ir.source_refs.row(task.source_ref_id);

        GraphBodyMemberSqlRow member_row;
        member_row.member_id =
            "graph-body-member-" + std::to_string(body_member.id.value());
        member_row.launch_id = launch_key;
        member_row.db_idx = db_idx;
        member_row.device_id = occurrence.device_id;
        member_row.graph_provider = provider;
        member_row.graph_event_id = graph_event_id;
        member_row.replay_unit_id = replay_unit.id.value();
        member_row.graph_template_id = replay_unit.graph_template_id.value();
        member_row.graph_launch_occurrence_id =
            member.graph_launch_occurrence_id.value();
        member_row.body_id = body.id.value();
        member_row.replay_body_template_id =
            body.replay_body_template_id.value();
        member_row.member_order = member.member_order;
        member_row.slot_order = slot.slot_order;
        member_row.lane_ordinal = body_member.lane_ordinal;
        member_row.task_ordinal = body_member.task_ordinal;
        member_row.kind = graph_body_member_kind_name(body_member.kind);
        member_row.event_id = trace_event_compat_id(task.trace_event_id);
        member_row.task_id = body_member.task_id.value();
        member_row.source_table = member_source.table_name;
        member_row.source_row_id = member_event.source_row_id;
        member_row.raw_task_id = task.raw_task_id;
        member_row.start_ns = member_event.start_ns;
        member_row.end_ns = member_event.end_ns;
        member_row.dur_us =
            ns_to_us(member_event.end_ns - member_event.start_ns);
        member_row.correlation_id =
            correlation_text(occurrence.raw_launch_connection_id);
        member_row.graph_node_id = body_member.raw_graph_node_id;
        member_row.original_graph_node_id =
            body_member.original_graph_node_id;
        member_row.match_policy = match_policy;
        member_row.association_policy = association_policy;
        rows.members.push_back(std::move(member_row));
      }
    }
  }
  return rows;
}

}  // namespace traceloom::compat
