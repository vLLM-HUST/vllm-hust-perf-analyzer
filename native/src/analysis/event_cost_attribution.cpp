#include "traceloom/analysis/event_cost_attribution.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace traceloom {
namespace {

struct Interval {
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

std::uint64_t connection_key(std::uint32_t device_id,
                             std::int64_t connection_id) {
  return (static_cast<std::uint64_t>(device_id) << 32u) ^
         (static_cast<std::uint64_t>(connection_id) & 0xffffffffu);
}

bool overlaps(const TraceEventRow& event, const Interval& interval) {
  return event.start_ns <= interval.end_ns &&
         event.end_ns >= interval.start_ns;
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

std::unordered_set<TraceEventId::value_type> host_runtime_event_ids(
    const NativeIr& ir) {
  std::set<std::pair<SourceRefId::value_type, std::uint64_t>> sources;
  for (const RuntimeCallRow& call : ir.runtime_calls.rows()) {
    sources.emplace(call.source_ref_id.value(), call.source_row_id);
  }
  std::unordered_set<TraceEventId::value_type> out;
  for (const TraceEventRow& event : ir.trace_events.rows()) {
    if (sources.find({event.source_ref_id.value(), event.source_row_id}) !=
        sources.end()) {
      out.insert(event.id.value());
    }
  }
  return out;
}

std::unordered_set<TraceEventId::value_type>
reconciled_timing_envelope_event_ids(const NativeIr& ir) {
  std::unordered_set<TraceEventId::value_type> out;
  for (const EventReconciliationMemberRow& member :
       ir.event_reconciliation.members) {
    if (member.role != EventReconciliationMemberRole::kTimingEnvelope ||
        !member.decision_id.valid() ||
        member.decision_id.value() >=
            ir.event_reconciliation.decisions.size()) {
      continue;
    }
    const EventReconciliationDecisionRow& decision =
        ir.event_reconciliation.decisions[member.decision_id.value()];
    if (decision.status == EventReconciliationStatus::kReconciled &&
        member.event_id.valid()) {
      out.insert(member.event_id.value());
    }
  }
  return out;
}

}  // namespace

bool EventCostAttributionMask::includes(TraceEventId event_id) const {
  if (!event_id.valid() || event_id.value() >= included_.size()) {
    throw std::out_of_range("cost-attribution event id is out of range");
  }
  return included_[event_id.value()] != 0;
}

EventCostAttributionMask build_event_cost_attribution_mask(
    const NativeIr& ir, FlatAnchorBuildConfig config) {
  if (config.classification_rules.rules().empty()) {
    config.classification_rules = load_default_signal_classification_ruleset();
  }
  if (!config.classification_overrides.empty()) {
    config.classification_rules = override_signal_classification_ruleset(
        config.classification_rules, config.classification_overrides);
  }

  std::unordered_map<TraceEventId::value_type, const TaskRow*> tasks_by_event;
  for (const TaskRow& task : ir.tasks.rows()) {
    if (!task.trace_event_id.valid() ||
        task.trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument(
          "cost attribution: task event is out of range");
    }
    if (!tasks_by_event.emplace(task.trace_event_id.value(), &task).second) {
      throw std::invalid_argument(
          "cost attribution: multiple tasks reference one event");
    }
  }

  std::unordered_set<TraceEventId::value_type> communication_events;
  std::unordered_map<std::uint64_t, std::vector<Interval>>
      communication_by_connection;
  for (const CommunicationOpRow& comm : ir.communication_ops.rows()) {
    if (!comm.trace_event_id.valid() ||
        comm.trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument(
          "cost attribution: communication event is out of range");
    }
    communication_events.insert(comm.trace_event_id.value());
    const TraceEventRow& event = ir.trace_events.row(comm.trace_event_id);
    communication_by_connection[connection_key(event.device_id,
                                                comm.raw_connection_id)]
        .push_back({event.start_ns, event.end_ns});
  }

  std::unordered_map<std::uint32_t, std::vector<Interval>> replay_by_device;
  for (const ReplayUnitRow& replay : ir.replay_units.rows()) {
    if (!replay.launch_trace_event_id.valid() ||
        replay.launch_trace_event_id.value() >= ir.trace_events.size()) {
      continue;
    }
    const TraceEventRow& event =
        ir.trace_events.row(replay.launch_trace_event_id);
    replay_by_device[event.device_id].push_back(
        {event.start_ns, event.end_ns});
  }

  const auto host_events = host_runtime_event_ids(ir);
  const auto reconciled_envelopes =
      reconciled_timing_envelope_event_ids(ir);
  EventCostAttributionMask mask;
  mask.included_.assign(ir.trace_events.size(), 0);
  for (const TraceEventRow& event : ir.trace_events.rows()) {
    if (host_events.find(event.id.value()) != host_events.end() ||
        reconciled_envelopes.find(event.id.value()) !=
            reconciled_envelopes.end()) {
      continue;
    }
    if (communication_events.find(event.id.value()) !=
        communication_events.end()) {
      continue;
    }

    const auto task_found = tasks_by_event.find(event.id.value());
    const TaskRow* task =
        task_found == tasks_by_event.end() ? nullptr : task_found->second;

    const bool skip_replay_covered =
        config.skip_tasks_covered_by_replay_units ||
        config.skip_events_covered_by_replay_units;
    if (skip_replay_covered) {
      const auto replay = replay_by_device.find(event.device_id);
      if (replay != replay_by_device.end() &&
          std::any_of(replay->second.begin(), replay->second.end(),
                      [&event](const Interval& interval) {
                        return event.start_ns >= interval.start_ns &&
                               event.end_ns <= interval.end_ns;
                      })) {
        continue;
      }
    }

    if (task == nullptr) {
      // Non-task device observations without their own anchor remain eligible
      // evidence for local transition attribution.
      mask.included_[event.id.value()] = 1;
      continue;
    }

    if (task_type_is_explicitly_skipped(ir, *task, config)) {
      mask.included_[event.id.value()] = 1;
      continue;
    }

    if (config.skip_tasks_covered_by_communication_ops &&
        task->raw_connection_id >= 0) {
      const auto comm = communication_by_connection.find(
          connection_key(event.device_id, task->raw_connection_id));
      if (comm != communication_by_connection.end() &&
          std::any_of(comm->second.begin(), comm->second.end(),
                      [&event](const Interval& interval) {
                        return overlaps(event, interval);
                      })) {
        continue;
      }
    }

    if (!config.filter_auxiliary_task_anchors) {
      // With filtering disabled, task observations are structural anchors. An
      // unanchored task is therefore not silently reinterpreted as auxiliary.
      continue;
    }
    const SignalClassificationDecision decision =
        config.classification_rules.decide(
            signal_classification_input_for_task(ir, *task));
    if (decision.structural_participation ==
            SignalStructuralParticipation::kExcluded &&
        decision.cost_treatment ==
            SignalCostTreatment::kRetainedForAttribution) {
      mask.included_[event.id.value()] = 1;
    }
  }
  return mask;
}

}  // namespace traceloom
