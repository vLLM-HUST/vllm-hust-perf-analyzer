#include "evidence_role_sql_internal.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/aux_attribution_rows.h"
#include "traceloom/compat/timeline_rows.h"

namespace traceloom::compat::detail {
namespace {

struct ReplayMembership {
  ReplayUnitId replay_unit_id;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  bool exact = false;
};

std::string join_strings(const std::set<std::string>& values) {
  std::string out;
  for (const std::string& value : values) {
    if (!out.empty()) {
      out += ',';
    }
    out += value;
  }
  return out;
}

std::string replay_unit_id_text(ReplayUnitId id) {
  return "replay-unit-" + std::to_string(id.value());
}

std::string protected_interval_id_text(ProtectedIntervalId id) {
  return "protected-interval-" + std::to_string(id.value());
}

std::string graph_body_member_id_text(GraphLaunchBodyMemberId id) {
  return "graph-body-member-" + std::to_string(id.value());
}

const char* protected_interval_kind_name(ProtectedIntervalKind kind) {
  switch (kind) {
    case ProtectedIntervalKind::kGraphReplayUnit:
      return "graph_replay_unit";
    case ProtectedIntervalKind::kUserWindow:
      return "user_window";
    case ProtectedIntervalKind::kUnknown:
      return "unknown";
  }
  return "unknown";
}

const char* boundary_policy_name(BoundaryPolicy policy) {
  switch (policy) {
    case BoundaryPolicy::kNoCross:
      return "no_cross";
    case BoundaryPolicy::kAllowEnclosing:
      return "allow_enclosing";
    case BoundaryPolicy::kBlockAnyOverlap:
      return "block_any_overlap";
  }
  return "unknown";
}

bool task_type_is_explicitly_skipped(const NativeIr& ir,
                                     const TaskRow& task,
                                     const FlatAnchorBuildConfig& config) {
  if (!task.task_type_symbol_id.valid()) {
    return false;
  }
  const std::string& task_type = ir.symbols.value(task.task_type_symbol_id);
  return std::find(config.skipped_task_type_symbols.begin(),
                   config.skipped_task_type_symbols.end(),
                   task_type) != config.skipped_task_type_symbols.end();
}

std::uint64_t connection_key(std::uint32_t device_id,
                             std::int64_t connection_id) {
  return (static_cast<std::uint64_t>(device_id) << 32u) ^
         (static_cast<std::uint64_t>(connection_id) & 0xffffffffu);
}

bool overlaps(std::int64_t lhs_start, std::int64_t lhs_end,
              std::int64_t rhs_start, std::int64_t rhs_end) {
  return lhs_start <= rhs_end && lhs_end >= rhs_start;
}

std::vector<ReplayMembership> replay_memberships_for_event(
    const NativeIr& ir,
    const TraceEventRow& event,
    const std::unordered_map<TraceEventId::value_type,
                             std::set<ReplayUnitId::value_type>>&
        exact_replays_by_event) {
  std::map<ReplayUnitId::value_type, ReplayMembership> memberships;
  const auto exact = exact_replays_by_event.find(event.id.value());
  if (exact != exact_replays_by_event.end()) {
    for (const ReplayUnitId::value_type value : exact->second) {
      const ReplayUnitRow& replay = ir.replay_units.row(ReplayUnitId(value));
      const TraceEventRow& replay_event =
          ir.trace_events.row(replay.launch_trace_event_id);
      memberships.emplace(
          value, ReplayMembership{replay.id, replay_event.start_ns,
                                  replay_event.end_ns, true});
    }
  }
  for (const ReplayUnitRow& replay : ir.replay_units.rows()) {
    if (!replay.launch_trace_event_id.valid() ||
        replay.launch_trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument(
          "evidence-role SQL: ReplayUnit launch event is out of range");
    }
    const TraceEventRow& replay_event =
        ir.trace_events.row(replay.launch_trace_event_id);
    if (replay.launch_trace_event_id == event.id ||
        (event.device_id == replay_event.device_id &&
         event.start_ns >= replay_event.start_ns &&
         event.end_ns <= replay_event.end_ns)) {
      const bool exact_membership = replay.replay_composition_region_id.valid();
      auto inserted = memberships.emplace(
          replay.id.value(),
          ReplayMembership{replay.id, replay_event.start_ns,
                           replay_event.end_ns, exact_membership});
      inserted.first->second.exact =
          inserted.first->second.exact || exact_membership;
    }
  }
  std::vector<ReplayMembership> result;
  for (const auto& item : memberships) {
    result.push_back(item.second);
  }
  return result;
}

void add_placement(EvidenceRoleDecisionRow& row, std::string kind, std::string id,
                   std::string owner, std::string relation,
                   std::string reason,
                   std::string support = "supported") {
  EvidenceRolePlacementRow placement;
  placement.decision_id = row.decision_id;
  placement.placement_order =
      static_cast<std::uint32_t>(row.placements.size());
  placement.placement_kind = std::move(kind);
  placement.placement_id = std::move(id);
  placement.owner_id = std::move(owner);
  placement.relation_name = std::move(relation);
  placement.support_state = std::move(support);
  placement.reason_code = std::move(reason);
  row.placements.push_back(std::move(placement));
}

void add_issue(EvidenceRoleDecisionRow& row, std::string code, std::string support,
               std::string related) {
  row.issues.push_back(
      EvidenceRoleIssueRow{row.decision_id, std::move(code), std::move(support),
               std::move(related)});
}

void apply_policy_decision(EvidenceRoleDecisionRow& row,
                           const SignalClassificationDecision& decision) {
  row.policy_role = signal_role_name(decision.role);
  row.final_role = row.policy_role;
  row.rule_id = decision.rule_id;
  row.rule_class = decision.matched_rule ? "positive_policy" : "fallback";
  row.matched_rule = decision.matched_rule;
  row.priority = decision.priority;
  row.declaration_order = decision.declaration_order;
  row.policy_structural_participation =
      signal_structural_participation_name(decision.structural_participation);
  row.effective_structural_participation =
      row.policy_structural_participation;
  row.support_state = decision.support_state;
  row.reason_code = decision.reason_code;
  row.available_fields = decision.available_fields;
  row.required_fields = decision.required_fields;
  row.missing_required_fields = decision.missing_required_fields;
  row.missing_capability_rule_ids =
      decision.missing_capability_rule_ids;
  row.cost_treatment = signal_cost_treatment_name(decision.cost_treatment);
  row.context_treatment =
      signal_context_treatment_name(decision.context_treatment);
  row.provenance_treatment =
      signal_provenance_treatment_name(decision.provenance_treatment);
}

void apply_system_decision(EvidenceRoleDecisionRow& row, const std::string& role,
                           const std::string& participation,
                           const std::string& rule_id,
                           const std::string& rule_class,
                           const std::string& support_state,
                           const std::string& reason_code,
                           const std::string& required_fields) {
  row.final_role = role;
  row.rule_id = rule_id;
  row.rule_class = rule_class;
  row.matched_rule = true;
  row.priority = 0;
  row.declaration_order = 0;
  row.effective_structural_participation = participation;
  row.support_state = support_state;
  row.reason_code = reason_code;
  row.required_fields = required_fields;
  row.cost_treatment = "retained_for_attribution";
  row.context_treatment = "retained";
  row.provenance_treatment = "retained";
}

std::vector<EvidenceRoleDecisionRow> build_decisions(
    const NativeIr& ir,
    const FlatAnchorBuildConfig& config,
    std::uint32_t db_idx,
    bool materialize_aux_attribution) {
  std::unordered_map<TraceEventId::value_type, const TaskRow*> task_by_event;
  for (const TaskRow& task : ir.tasks.rows()) {
    if (!task.trace_event_id.valid() ||
        task.trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument(
          "evidence-role SQL: TaskRow trace_event_id is out of range");
    }
    if (!task_by_event.emplace(task.trace_event_id.value(), &task).second) {
      throw std::invalid_argument(
          "evidence-role SQL: multiple tasks reference one normalized event");
    }
  }

  std::unordered_map<TraceEventId::value_type,
                     std::vector<const CommunicationOpRow*>>
      comm_by_event;
  std::unordered_map<std::uint64_t,
                     std::vector<const CommunicationOpRow*>>
      comm_by_connection;
  for (const CommunicationOpRow& comm : ir.communication_ops.rows()) {
    if (!comm.trace_event_id.valid() ||
        comm.trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument(
          "evidence-role SQL: communication event is out of range");
    }
    comm_by_event[comm.trace_event_id.value()].push_back(&comm);
    const TraceEventRow& event = ir.trace_events.row(comm.trace_event_id);
    comm_by_connection[connection_key(event.device_id, comm.raw_connection_id)]
        .push_back(&comm);
  }

  std::unordered_map<TaskId::value_type,
                     std::vector<GraphLaunchBodyMemberId>>
      body_members_by_task;
  std::unordered_map<GraphLaunchOccurrenceId::value_type,
                     std::set<ReplayUnitId::value_type>>
      replay_units_by_occurrence;
  for (const ReplayUnitLaunchMemberRow& member :
       ir.replay_unit_launch_members.rows()) {
    if (!member.replay_unit_id.valid() ||
        member.replay_unit_id.value() >= ir.replay_units.size() ||
        !member.graph_launch_occurrence_id.valid() ||
        member.graph_launch_occurrence_id.value() >=
            ir.graph_launch_occurrences.size()) {
      throw std::invalid_argument(
          "evidence-role SQL: replay launch membership is out of range");
    }
    replay_units_by_occurrence[member.graph_launch_occurrence_id.value()]
        .insert(member.replay_unit_id.value());
  }
  std::unordered_map<TraceEventId::value_type,
                     std::set<ReplayUnitId::value_type>>
      exact_replays_by_event;
  for (const GraphLaunchBodyMemberRow& member :
       ir.graph_launch_body_members.rows()) {
    if (!member.task_id.valid() || member.task_id.value() >= ir.tasks.size() ||
        !member.graph_launch_body_id.valid() ||
        member.graph_launch_body_id.value() >= ir.graph_launch_bodies.size()) {
      throw std::invalid_argument(
          "evidence-role SQL: graph body membership is out of range");
    }
    body_members_by_task[member.task_id.value()].push_back(member.id);
    const GraphLaunchBodyRow& body =
        ir.graph_launch_bodies.row(member.graph_launch_body_id);
    const auto replay_found = replay_units_by_occurrence.find(
        body.graph_launch_occurrence_id.value());
    if (replay_found == replay_units_by_occurrence.end()) {
      continue;
    }
    const TaskRow& task = ir.tasks.row(member.task_id);
    exact_replays_by_event[task.trace_event_id.value()].insert(
        replay_found->second.begin(), replay_found->second.end());
  }

  std::unordered_map<TraceEventId::value_type, std::vector<AnchorId>>
      anchors_by_event;
  for (const AnchorRow& anchor : ir.anchors.rows()) {
    if (anchor.trace_event_id.valid()) {
      anchors_by_event[anchor.trace_event_id.value()].push_back(anchor.id);
    }
  }

  struct ReconciliationPlacement {
    const EventReconciliationDecisionRow* decision = nullptr;
    const EventReconciliationMemberRow* member = nullptr;
  };
  std::unordered_map<TraceEventId::value_type, ReconciliationPlacement>
      reconciliation_by_event;
  for (const EventReconciliationMemberRow& member :
       ir.event_reconciliation.members) {
    if (!member.decision_id.valid() ||
        member.decision_id.value() >=
            ir.event_reconciliation.decisions.size()) {
      continue;
    }
    const EventReconciliationDecisionRow& decision =
        ir.event_reconciliation.decisions[member.decision_id.value()];
    if (decision.status == EventReconciliationStatus::kReconciled &&
        member.event_id.valid()) {
      reconciliation_by_event.emplace(
          member.event_id.value(),
          ReconciliationPlacement{&decision, &member});
    }
  }

  std::unordered_map<ReplayUnitId::value_type, ProtectedIntervalId>
      protected_by_replay;
  for (const ReplayUnitRow& replay : ir.replay_units.rows()) {
    if (!replay.first_anchor_id.valid() || !replay.last_anchor_id.valid()) {
      continue;
    }
    for (const ProtectedIntervalRow& interval :
         ir.protected_intervals.rows()) {
      if (interval.first_anchor_id == replay.first_anchor_id &&
          interval.last_anchor_id == replay.last_anchor_id) {
        if (!protected_by_replay.emplace(replay.id.value(), interval.id)
                 .second) {
          throw std::invalid_argument(
              "evidence-role SQL: replay maps to multiple protected intervals");
        }
      }
    }
  }

  std::unordered_map<TraceEventId::value_type,
                     std::vector<const AuxLinkSqlRow*>>
      aux_by_event;
  AuxAttributionSqlRows aux_rows;
  if (materialize_aux_attribution) {
    aux_rows = build_aux_attribution_sql_rows(ir, config, db_idx);
    for (const AuxLinkSqlRow& link : aux_rows.aux_links) {
      const std::string prefix = "event-";
      if (link.aux_event_id.rfind(prefix, 0) != 0) {
        throw std::invalid_argument(
            "evidence-role SQL: malformed auxiliary event id");
      }
      const auto event_value = static_cast<TraceEventId::value_type>(
          std::stoul(link.aux_event_id.substr(prefix.size())));
      aux_by_event[event_value].push_back(&link);
    }
  }

  std::vector<EvidenceRoleDecisionRow> rows;
  rows.reserve(ir.trace_events.size());
  for (const TraceEventRow& event : ir.trace_events.rows()) {
    if (!event.source_ref_id.valid() ||
        event.source_ref_id.value() >= ir.source_refs.size()) {
      throw std::invalid_argument(
          "evidence-role SQL: event source_ref_id is out of range");
    }
    const SourceRefRow& source = ir.source_refs.row(event.source_ref_id);
    EvidenceRoleDecisionRow row;
    row.decision_id = "role-decision-" + std::to_string(event.id.value());
    row.db_idx = db_idx;
    row.device_id = event.device_id;
    row.event_id = trace_event_compat_id(event.id);
    row.source_ref_id = event.source_ref_id.value();
    row.policy_id = config.classification_rules.metadata().policy_id;
    row.policy_version =
        config.classification_rules.metadata().policy_version;
    row.manifest_sha256 =
        config.classification_rules.metadata().manifest_sha256;
    row.source_table = source.table_name;
    row.source_key = std::to_string(event.source_row_id);
    row.start_ns = event.start_ns;
    row.end_ns = event.end_ns;
    row.duration_ns = std::max<std::int64_t>(0, event.end_ns - event.start_ns);
    add_placement(row, "normalized_event", row.event_id, "",
                  "traceloom_event", "normalized_event_retained");

    const auto task_found = task_by_event.find(event.id.value());
    const TaskRow* task =
        task_found == task_by_event.end() ? nullptr : task_found->second;
    if (task != nullptr) {
      row.task_id = task->id.value();
      row.source_domain = "task";
      const SignalClassificationInput input =
          signal_classification_input_for_task(ir, *task);
      row.input_provider_scope = input.provider_scope;
      const SignalClassificationDecision policy_decision =
          config.classification_rules.decide(input);
      apply_policy_decision(row, policy_decision);
      if (!policy_decision.missing_required_fields.empty()) {
        add_issue(
            row, "missing_required_capability", policy_decision.support_state,
            policy_decision.missing_required_fields + ":" +
                policy_decision.missing_capability_rule_ids);
      }
    } else {
      row.input_provider_scope = "any";
      row.policy_structural_participation = "not_applicable";
      row.available_fields = "provider_scope,source_domain";
    }

    const std::vector<ReplayMembership> replay_memberships =
        replay_memberships_for_event(ir, event, exact_replays_by_event);
    std::map<CommunicationOpId::value_type, const CommunicationOpRow*>
        matching_communications;
    const auto direct_communications = comm_by_event.find(event.id.value());
    if (direct_communications != comm_by_event.end()) {
      for (const CommunicationOpRow* comm : direct_communications->second) {
        matching_communications.emplace(comm->id.value(), comm);
      }
    }
    if (task != nullptr && task->raw_connection_id >= 0) {
      const auto found = comm_by_connection.find(
          connection_key(event.device_id, task->raw_connection_id));
      if (found != comm_by_connection.end()) {
        for (const CommunicationOpRow* comm : found->second) {
          const TraceEventRow& comm_event =
              ir.trace_events.row(comm->trace_event_id);
          if (overlaps(event.start_ns, event.end_ns, comm_event.start_ns,
                       comm_event.end_ns)) {
            matching_communications.emplace(comm->id.value(), comm);
          }
        }
      }
    }
    const bool communication_covered = !matching_communications.empty();

    const bool skip_replay_covered =
        config.skip_tasks_covered_by_replay_units ||
        config.skip_events_covered_by_replay_units;
    if (!replay_memberships.empty() &&
        (task == nullptr || skip_replay_covered)) {
      const bool conflict = replay_memberships.size() > 1;
      apply_system_decision(
          row, "protected_boundary", "atomic_boundary",
          "system.protected_composite", "protected_membership",
          conflict ? "conflict" : "supported",
          conflict ? "multiple_protected_composites"
                   : "provider_composite_membership",
          "replay_unit_membership");
      std::set<std::string> replay_ids;
      for (const ReplayMembership& membership : replay_memberships) {
        replay_ids.insert(replay_unit_id_text(membership.replay_unit_id));
        add_placement(row, "replay_unit",
                      replay_unit_id_text(membership.replay_unit_id), "",
                      "traceloom_graph_launch",
                      membership.exact ? "exact_composite_membership"
                                       : "typed_composite_membership");
        const auto interval =
            protected_by_replay.find(membership.replay_unit_id.value());
        if (interval != protected_by_replay.end()) {
          add_placement(row, "protected_interval",
                        protected_interval_id_text(interval->second),
                        replay_unit_id_text(membership.replay_unit_id),
                        "traceloom_protected_interval",
                        "generic_discovery_no_cross_boundary");
        }
      }
      if (conflict) {
        add_issue(row, "multiple_protected_composites", "conflict",
                  join_strings(replay_ids));
      }
    } else if (task != nullptr &&
               task_type_is_explicitly_skipped(ir, *task, config)) {
      apply_system_decision(row, "auxiliary", "excluded",
                            "system.analysis_config_exclusion",
                            "analysis_config", "supported",
                            "explicit_analysis_config_exclusion",
                            "task_type");
    } else if (communication_covered &&
               (task == nullptr ||
                config.skip_tasks_covered_by_communication_ops)) {
      apply_system_decision(row, "anchor", "identity",
                            "system.communication_anchor",
                            "provider_relation", "supported",
                            "represented_by_communication_anchor",
                            "communication_membership");
      std::set<std::string> representative_anchor_ids;
      for (const auto& item : matching_communications) {
        const CommunicationOpRow& comm = *item.second;
        // The communication observation itself receives its direct placement
        // below.  Only add a representative placement when a distinct task
        // observation was suppressed in favor of the communication anchor.
        if (comm.trace_event_id == event.id) {
          continue;
        }
        const auto representative =
            anchors_by_event.find(comm.trace_event_id.value());
        if (representative == anchors_by_event.end()) {
          continue;
        }
        for (const AnchorId anchor : representative->second) {
          representative_anchor_ids.insert(anchor_compat_id(anchor));
        }
      }
      for (const std::string& anchor_id : representative_anchor_ids) {
        add_placement(row, "anchor", anchor_id, "",
                      "traceloom_anchor",
                      "represented_by_communication_anchor");
      }
      if (representative_anchor_ids.size() > 1) {
        row.support_state = "conflict";
        row.reason_code = "multiple_representative_communication_anchors";
        add_issue(row, "multiple_representative_communication_anchors",
                  "conflict", join_strings(representative_anchor_ids));
      }
    } else if (task != nullptr &&
               !config.filter_auxiliary_task_anchors) {
      apply_system_decision(row, "anchor", "identity",
                            "system.unfiltered_task_anchor",
                            "analysis_config", "supported",
                            "auxiliary_filter_disabled", "task_event");
    } else if (task == nullptr) {
      const bool has_direct_anchor =
          anchors_by_event.find(event.id.value()) != anchors_by_event.end();
      if (has_direct_anchor) {
        row.source_domain = "event";
        apply_system_decision(row, "anchor", "identity",
                              "system.existing_event_anchor",
                              "structural_membership", "supported",
                              "existing_anchor_membership", "anchor");
      } else {
        row.source_domain = "event";
        apply_system_decision(row, "unsupported", "not_applicable",
                              "system.unsupported_event_kind", "unsupported",
                              "unsupported", "event_not_projection_candidate",
                              "normalized_event");
        add_issue(row, "event_not_projection_candidate", "unsupported",
                  row.event_id);
      }
    }

    const auto reconciliation =
        reconciliation_by_event.find(event.id.value());
    if (reconciliation != reconciliation_by_event.end()) {
      const EventReconciliationDecisionRow& decision =
          *reconciliation->second.decision;
      const EventReconciliationMemberRow& member =
          *reconciliation->second.member;
      std::string canonical_anchor;
      const auto canonical =
          anchors_by_event.find(decision.canonical_event_id.value());
      if (canonical != anchors_by_event.end() &&
          canonical->second.size() == 1) {
        canonical_anchor = anchor_compat_id(canonical->second.front());
      }
      if (member.role == EventReconciliationMemberRole::kTimingEnvelope) {
        apply_system_decision(
            row, "anchor", "identity", "system.event_reconciliation",
            "provider_relation", "supported",
            "represented_by_reconciled_canonical_anchor",
            "event_reconciliation_membership");
      }
      add_placement(
          row, "reconciliation_member",
          "reconciliation-decision-" +
              std::to_string(decision.id.value()),
          canonical_anchor, "traceloom_event_reconciliation_member",
          member.role == EventReconciliationMemberRole::kTimingEnvelope
              ? "timing_envelope_for_canonical_anchor"
              : "semantic_detail_for_canonical_anchor");
    }

    const auto direct_anchors = anchors_by_event.find(event.id.value());
    if (direct_anchors != anchors_by_event.end()) {
      if (direct_anchors->second.size() > 1) {
        row.support_state = "conflict";
        row.reason_code = "multiple_direct_anchors";
        std::set<std::string> ids;
        for (const AnchorId anchor : direct_anchors->second) {
          ids.insert(anchor_compat_id(anchor));
        }
        add_issue(row, "multiple_direct_anchors", "conflict",
                  join_strings(ids));
      }
      for (const AnchorId anchor : direct_anchors->second) {
        add_placement(row, "anchor", anchor_compat_id(anchor), "",
                      "traceloom_anchor", "direct_event_anchor");
      }
    }

    if (task != nullptr) {
      const auto members = body_members_by_task.find(task->id.value());
      if (members != body_members_by_task.end()) {
        if (members->second.size() > 1) {
          row.support_state = "conflict";
          row.reason_code = "multiple_graph_body_memberships";
          std::set<std::string> ids;
          for (const GraphLaunchBodyMemberId member : members->second) {
            ids.insert(graph_body_member_id_text(member));
          }
          add_issue(row, "multiple_graph_body_memberships", "conflict",
                    join_strings(ids));
        }
        for (const GraphLaunchBodyMemberId member : members->second) {
          add_placement(row, "graph_body_member",
                        graph_body_member_id_text(member), "",
                        "traceloom_graph_body_member",
                        "exact_graph_body_membership");
        }
      }
    }

    const auto aux = aux_by_event.find(event.id.value());
    if (aux != aux_by_event.end()) {
      for (const AuxLinkSqlRow* link : aux->second) {
        add_placement(row, "auxiliary_link",
                      link->anchor_id + "/aux-" +
                          std::to_string(link->aux_order),
                      link->anchor_id, "traceloom_aux_link",
                      "retained_between_anchor_evidence");
      }
    }

    const bool identity_role = row.final_role == "anchor" ||
                               row.final_role == "unknown_anchor";
    const bool excluded_role = row.final_role == "auxiliary" ||
                               row.final_role == "transparent";
    const bool has_identity_placement = std::any_of(
        row.placements.begin(), row.placements.end(),
        [](const EvidenceRolePlacementRow& placement) {
          return placement.placement_kind == "anchor" ||
                 placement.placement_kind == "graph_body_member" ||
                 placement.placement_kind == "reconciliation_member";
        });
    const bool has_aux_placement = std::any_of(
        row.placements.begin(), row.placements.end(),
        [](const EvidenceRolePlacementRow& placement) {
          return placement.placement_kind == "auxiliary_link";
        });
    if (identity_role && !has_identity_placement &&
        row.final_role != "protected_boundary") {
      row.support_state = "orphan";
      row.reason_code = "identity_event_without_anchor";
      add_issue(row, "identity_event_without_anchor", "orphan", row.event_id);
    } else if (excluded_role && materialize_aux_attribution &&
               !has_aux_placement) {
      row.support_state = "retained_unplaced";
      row.reason_code = "omitted_event_without_auxiliary_link";
      add_issue(row, "omitted_event_without_auxiliary_link",
                "retained_unplaced", row.event_id);
    }

    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<ProtectedIntervalSqlRow> build_protected_intervals(
    const NativeIr& ir, std::uint32_t db_idx) {
  std::map<std::pair<AnchorId::value_type, AnchorId::value_type>, ReplayUnitId>
      replay_by_bounds;
  for (const ReplayUnitRow& replay : ir.replay_units.rows()) {
    if (replay.first_anchor_id.valid() && replay.last_anchor_id.valid()) {
      const auto inserted = replay_by_bounds.emplace(
          std::make_pair(replay.first_anchor_id.value(),
                         replay.last_anchor_id.value()),
          replay.id);
      if (!inserted.second) {
        throw std::invalid_argument(
            "evidence-role SQL: multiple replay units share anchor bounds");
      }
    }
  }
  std::vector<ProtectedIntervalSqlRow> rows;
  rows.reserve(ir.protected_intervals.size());
  for (const ProtectedIntervalRow& interval : ir.protected_intervals.rows()) {
    if (!interval.first_anchor_id.valid() ||
        interval.first_anchor_id.value() >= ir.anchors.size() ||
        !interval.last_anchor_id.valid() ||
        interval.last_anchor_id.value() >= ir.anchors.size() ||
        !interval.evidence_source_ref_id.valid() ||
        interval.evidence_source_ref_id.value() >= ir.source_refs.size()) {
      throw std::invalid_argument(
          "evidence-role SQL: protected interval identity is out of range");
    }
    const AnchorRow& first = ir.anchors.row(interval.first_anchor_id);
    const AnchorRow& last = ir.anchors.row(interval.last_anchor_id);
    ProtectedIntervalSqlRow row;
    row.protected_interval_id = protected_interval_id_text(interval.id);
    row.db_idx = db_idx;
    row.device_id = first.device_id;
    row.kind = protected_interval_kind_name(interval.kind);
    row.boundary_policy = boundary_policy_name(interval.boundary_policy);
    row.first_anchor_id = anchor_compat_id(interval.first_anchor_id);
    row.last_anchor_id = anchor_compat_id(interval.last_anchor_id);
    row.evidence_source_ref_id = interval.evidence_source_ref_id.value();
    row.start_ns = first.start_ns;
    row.end_ns = last.end_ns;
    const auto replay = replay_by_bounds.find(
        {interval.first_anchor_id.value(), interval.last_anchor_id.value()});
    if (replay != replay_by_bounds.end()) {
      row.replay_unit_id = replay_unit_id_text(replay->second);
      row.support_state = "supported";
      row.reason_code = "exact_replay_anchor_bounds";
    } else {
      row.support_state = "typed_open";
      row.reason_code = "protected_bounds_without_replay_unit";
    }
    rows.push_back(std::move(row));
  }
  return rows;
}


}  // namespace

EvidenceRoleSqlRows build_evidence_role_sql_rows(
    const NativeIr& ir, const FlatAnchorBuildConfig& config,
    std::uint32_t db_idx, bool materialize_aux_attribution) {
  EvidenceRoleSqlRows rows;
  rows.decisions =
      build_decisions(ir, config, db_idx, materialize_aux_attribution);
  rows.protected_intervals = build_protected_intervals(ir, db_idx);
  rows.replay_unit_anchors.reserve(ir.anchors.size());
  for (const AnchorRow& anchor : ir.anchors.rows()) {
    if (!anchor.replay_unit_id.valid()) {
      continue;
    }
    if (anchor.replay_unit_id.value() >= ir.replay_units.size()) {
      throw std::invalid_argument(
          "evidence-role SQL: AnchorRow replay_unit_id is out of range");
    }
    rows.replay_unit_anchors.push_back(
        {replay_unit_id_text(anchor.replay_unit_id), anchor_compat_id(anchor.id),
         db_idx, anchor.device_id});
  }
  return rows;
}

}  // namespace traceloom::compat::detail
