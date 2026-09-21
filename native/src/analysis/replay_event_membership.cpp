#include "traceloom/analysis/replay_event_membership.h"

#include <algorithm>

namespace traceloom {

ReplayEventMembershipIndex::ReplayEventMembershipIndex(const NativeIr& ir) {
  for (const auto& replay : ir.replay_units.rows()) {
    const auto& event = ir.trace_events.row(replay.launch_trace_event_id);
    exact_[event.id.value()].push_back(replay.id);
    if (!replay.replay_composition_region_id.valid()) {
      legacy_[event.device_id].push_back({replay.id, event.start_ns, event.end_ns});
    }
  }
  std::unordered_map<GraphLaunchOccurrenceId::value_type,
                     std::vector<ReplayUnitId>> by_launch;
  for (const auto& member : ir.replay_unit_launch_members.rows()) {
    const auto& replay = ir.replay_units.row(member.replay_unit_id);
    if (!replay.replay_composition_region_id.valid()) continue;
    by_launch[member.graph_launch_occurrence_id.value()].push_back(replay.id);
    const auto& launch = ir.graph_launch_occurrences.row(member.graph_launch_occurrence_id);
    // These IDs come from provider-aware matching, not proximity at this stage.
    for (const auto task : {launch.model_execute_task_id,
                            launch.notify_wait_task_id, launch.notify_record_task_id}) {
      if (task.valid()) {
        const auto event = ir.tasks.row(task).trace_event_id;
        if (event.valid()) exact_[event.value()].push_back(replay.id);
      }
    }
  }
  for (const auto& member : ir.graph_launch_body_members.rows()) {
    const auto& body = ir.graph_launch_bodies.row(member.graph_launch_body_id);
    const auto found = by_launch.find(body.graph_launch_occurrence_id.value());
    if (found == by_launch.end()) continue;
    const auto event = ir.tasks.row(member.task_id).trace_event_id;
    auto& owners = exact_[event.value()];
    owners.insert(owners.end(), found->second.begin(), found->second.end());
  }
  for (auto& entry : exact_) {
    auto& owners = entry.second;
    std::sort(owners.begin(), owners.end());
    owners.erase(std::unique(owners.begin(), owners.end()), owners.end());
  }
}

std::vector<ReplayUnitId> ReplayEventMembershipIndex::memberships(
    const TraceEventRow& event) const {
  std::vector<ReplayUnitId> result;
  const auto exact = exact_.find(event.id.value());
  if (exact != exact_.end()) result = exact->second;
  const auto legacy = legacy_.find(event.device_id);
  if (legacy != legacy_.end()) {
    for (const auto& span : legacy->second) {
      if (event.start_ns >= span.start && event.end_ns <= span.end)
        result.push_back(span.replay);
    }
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

bool ReplayEventMembershipIndex::contains(const TraceEventRow& event) const {
  if (exact_.count(event.id.value())) return true;
  const auto found = legacy_.find(event.device_id);
  if (found == legacy_.end()) return false;
  return std::any_of(found->second.begin(), found->second.end(), [&](const auto& span) {
    return event.start_ns >= span.start && event.end_ns <= span.end;
  });
}

}  // namespace traceloom
