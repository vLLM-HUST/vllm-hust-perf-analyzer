#include "event_reconciliation_internal.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "traceloom/ir/native_ir.h"

namespace traceloom {
namespace {

std::string lower_ascii(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

std::string provider_scope(const NativeIr& ir, SourceRefId source_ref_id) {
  if (!source_ref_id.valid() ||
      source_ref_id.value() >= ir.source_refs.size()) {
    return "any";
  }
  const std::string kind =
      lower_ascii(ir.source_refs.row(source_ref_id).source_kind);
  if (kind.find("ascend") != std::string::npos ||
      kind.find("cann") != std::string::npos) {
    return "ascend";
  }
  if (kind.find("cuda") != std::string::npos ||
      kind.find("nsys") != std::string::npos) {
    return "cuda";
  }
  if (kind.find("hygon") != std::string::npos ||
      kind.find("hip") != std::string::npos) {
    return "hygon";
  }
  return "any";
}

bool matches_provider(const NativeIr& ir,
                      SourceRefId source_ref_id,
                      const EventReconciliationRule& rule) {
  return rule.provider_scope == "any" ||
         rule.provider_scope == provider_scope(ir, source_ref_id);
}

std::string symbol_text(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

double contained_fraction(const TraceEventRow& outer,
                          const TraceEventRow& inner) {
  const std::int64_t inner_duration =
      std::max<std::int64_t>(0, inner.end_ns - inner.start_ns);
  if (inner_duration == 0) {
    return outer.start_ns <= inner.start_ns && outer.end_ns >= inner.end_ns
               ? 1.0
               : 0.0;
  }
  const std::int64_t intersection = std::max<std::int64_t>(
      0, std::min(outer.end_ns, inner.end_ns) -
             std::max(outer.start_ns, inner.start_ns));
  return static_cast<double>(intersection) /
         static_cast<double>(inner_duration);
}

std::vector<ReplayUnitId::value_type> replay_membership(
    const NativeIr& ir,
    const TraceEventRow& event) {
  std::vector<ReplayUnitId::value_type> ids;
  for (const ReplayUnitRow& replay : ir.replay_units.rows()) {
    if (!replay.launch_trace_event_id.valid()) {
      continue;
    }
    const TraceEventRow& envelope =
        ir.trace_events.row(replay.launch_trace_event_id);
    if (event.device_id == envelope.device_id &&
        event.start_ns >= envelope.start_ns &&
        event.end_ns <= envelope.end_ns) {
      ids.push_back(replay.id.value());
    }
  }
  return ids;
}

void append_communication_member(
    EventReconciliationState& state,
    EventReconciliationDecisionId decision_id,
    const CommunicationOpRow& communication,
    EventReconciliationMemberRole role) {
  state.members.push_back(
      {decision_id, TaskId::invalid(), communication.trace_event_id, role,
       false, false, false, true, communication.id});
}

struct Candidate {
  const TaskRow* task = nullptr;
  double contained = 0.0;
};

struct CommunicationCandidates {
  const CommunicationOpRow* communication = nullptr;
  std::vector<Candidate> candidates;
};

}  // namespace

void reconcile_task_communication_observations(
    const NativeIr& ir,
    const EventReconciliationRuleset& ruleset,
    EventReconciliationState& state) {
  std::unordered_map<CommunicationOpId::value_type, bool>
      claimed_communications;
  for (const EventReconciliationRule& rule : ruleset.rules()) {
    if (rule.source_domain != "task+communication_op") {
      continue;
    }

    std::vector<const TaskRow*> tasks;
    for (const TaskRow& task : ir.tasks.rows()) {
      if (!task.trace_event_id.valid() ||
          task.trace_event_id.value() >= ir.trace_events.size() ||
          !matches_provider(ir, task.source_ref_id, rule) ||
          symbol_text(ir, task.op_type_symbol_id) != rule.task_op_type) {
        continue;
      }
      tasks.push_back(&task);
    }

    std::vector<CommunicationCandidates> groups;
    std::unordered_map<TaskId::value_type, std::size_t> task_degree;
    for (const CommunicationOpRow& communication :
         ir.communication_ops.rows()) {
      if (!communication.trace_event_id.valid() ||
          communication.trace_event_id.value() >= ir.trace_events.size() ||
          !matches_provider(ir, communication.source_ref_id, rule) ||
          claimed_communications.find(communication.id.value()) !=
              claimed_communications.end()) {
        continue;
      }
      const std::string name =
          symbol_text(ir, communication.op_name_symbol_id);
      if (name.rfind(rule.communication_op_name_prefix, 0) != 0) {
        continue;
      }
      claimed_communications.emplace(communication.id.value(), true);
      const TraceEventRow& communication_event =
          ir.trace_events.row(communication.trace_event_id);
      CommunicationCandidates group;
      group.communication = &communication;
      for (const TaskRow* task : tasks) {
        const TraceEventRow& task_event =
            ir.trace_events.row(task->trace_event_id);
        if (task_event.device_id != communication_event.device_id ||
            task_event.start_ns > communication_event.end_ns ||
            task_event.end_ns < communication_event.start_ns) {
          continue;
        }
        group.candidates.push_back(
            {task, contained_fraction(task_event, communication_event)});
        ++task_degree[task->id.value()];
      }
      groups.push_back(std::move(group));
    }

    for (const CommunicationCandidates& group : groups) {
      const auto decision_id = checked_next_id<EventReconciliationDecisionId>(
          state.decisions.size());
      EventReconciliationDecisionRow decision;
      decision.id = decision_id;
      decision.rule_id = rule.rule_id;
      append_communication_member(
          state, decision_id, *group.communication,
          group.candidates.empty()
              ? EventReconciliationMemberRole::kIndependentCandidate
              : EventReconciliationMemberRole::kProviderDetail);
      for (const Candidate& candidate : group.candidates) {
        state.members.push_back(
            {decision_id, candidate.task->id,
             candidate.task->trace_event_id,
             group.candidates.size() == 1
                 ? EventReconciliationMemberRole::kSemanticDetail
                 : EventReconciliationMemberRole::kConflictingCandidate,
             false, false, false, true, CommunicationOpId::invalid()});
      }

      if (group.candidates.empty()) {
        decision.status = EventReconciliationStatus::kIndependent;
        decision.reason_code = "missing_containing_task_peer";
        state.decisions.push_back(std::move(decision));
        continue;
      }
      if (group.candidates.size() != 1 ||
          task_degree[group.candidates.front().task->id.value()] != 1) {
        decision.status = EventReconciliationStatus::kAmbiguous;
        decision.reason_code = "non_unique_containment_candidates";
        state.decisions.push_back(std::move(decision));
        continue;
      }

      const Candidate& candidate = group.candidates.front();
      decision.contained_fraction = candidate.contained;
      if (candidate.contained < rule.min_contained_fraction) {
        decision.status = EventReconciliationStatus::kConflict;
        decision.reason_code = "insufficient_interval_containment";
        state.decisions.push_back(std::move(decision));
        continue;
      }
      const TraceEventRow& task_event =
          ir.trace_events.row(candidate.task->trace_event_id);
      const TraceEventRow& communication_event =
          ir.trace_events.row(group.communication->trace_event_id);
      if (replay_membership(ir, task_event) !=
          replay_membership(ir, communication_event)) {
        decision.status = EventReconciliationStatus::kConflict;
        decision.reason_code = "protected_replay_membership_mismatch";
        state.decisions.push_back(std::move(decision));
        continue;
      }

      decision.status = EventReconciliationStatus::kReconciled;
      decision.reason_code =
          "unique_provider_observation_with_containing_task";
      decision.canonical_task_id = candidate.task->id;
      decision.canonical_event_id = candidate.task->trace_event_id;
      decision.envelope_event_id = candidate.task->trace_event_id;
      decision.canonical_start_ns = task_event.start_ns;
      decision.canonical_end_ns = task_event.end_ns;
      EventReconciliationMemberRow& communication_member =
          state.members[state.members.size() - 2];
      EventReconciliationMemberRow& task_member = state.members.back();
      communication_member.role =
          EventReconciliationMemberRole::kProviderDetail;
      task_member.role = EventReconciliationMemberRole::kSemanticDetail;
      task_member.contributes_timing = true;
      task_member.contributes_symbol = true;
      task_member.contributes_cost = true;
      state.decisions.push_back(std::move(decision));
    }
  }
}

}  // namespace traceloom
