#include "ascend_sqlite_internal.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace traceloom::ascend_sqlite_detail {

// A captured graph invocation is an observed composite even without three
// repetitions of a larger launch pattern. Do not turn bank alternation or a
// one-shot mixed graph into one profile-wide capture-stream envelope.
// Existing periodic multi-launch compositions retain their original contract.
void materialize_direct_aclgraph_candidates(NativeIr& ir) {
  std::set<std::uint32_t> composed_devices;
  for (const auto& candidate : ir.replay_composition_candidates.rows()) {
    composed_devices.insert(candidate.device_id);
  }
  std::map<std::uint32_t, std::vector<const GraphLaunchOccurrenceRow*>> devices;
  for (const auto& launch : ir.graph_launch_occurrences.rows()) {
    if (!composed_devices.count(launch.device_id)) {
      devices[launch.device_id].push_back(&launch);
    }
  }
  std::map<GraphLaunchOccurrenceId, const GraphLaunchBodyRow*> bodies;
  std::set<GraphLaunchOccurrenceId> ambiguous;
  for (const auto& body : ir.graph_launch_bodies.rows()) {
    if (!bodies.emplace(body.graph_launch_occurrence_id, &body).second) {
      ambiguous.insert(body.graph_launch_occurrence_id);
    }
  }
  for (auto& entry : devices) {
    auto& launches = entry.second;
    std::sort(launches.begin(), launches.end(), [](const auto* a, const auto* b) {
      return a->start_ns != b->start_ns ? a->start_ns < b->start_ns : a->id < b->id;
    });
    bool complete = true;
    auto previous_end = std::numeric_limits<std::int64_t>::min();
    for (const auto* launch : launches) {
      const auto body = bodies.find(launch->id);
      if (launch->match_policy !=
              GraphLaunchMatchPolicy::kNotifyCompletionAdjacent ||
          launch->instance_association_policy !=
              GraphLaunchInstanceAssociationPolicy::kRecordModelId ||
          !launch->captured_graph_instance_id.valid() ||
          !launch->model_execute_task_id.valid() ||
          !launch->notify_wait_task_id.valid() ||
          !launch->notify_record_task_id.valid() || launch->raw_graph_connection_id < 0 ||
          launch->start_ns >= launch->end_ns || launch->start_ns < previous_end ||
          body == bodies.end() || ambiguous.count(launch->id) ||
          !body->second->replay_body_template_id.valid()) {
        complete = false;
        break;
      }
      previous_end = launch->end_ns;
    }
    // No partially evidenced asynchronous batch is promoted. Exact-body
    // membership was independently established by capture streams + model ID
    // + matched execution/completion controls, not temporal overlap alone.
    if (!complete) continue;
    for (const auto* launch : launches) {
      const auto& body = *bodies.at(launch->id);
      const auto& instance = ir.captured_graph_instances.row(
          launch->captured_graph_instance_id);
      const auto candidate = ir.replay_composition_candidates.append(
          launch->source_ref_id, launch->device_id, launch->id, launch->id,
          1, 0, 1, 1, 0,
          stable_hash64("direct_captured_graph\n" +
                        std::to_string(instance.id.value())),
          ReplayCompositionIdentityPolicy::kCapturedGraphInstance,
          ReplayCompositionOrderPolicy::kDeviceExecutionOrder,
          ReplayCompositionShapePolicy::kSingleGraph,
          ReplayCompositionBoundaryPolicy::kDirectObservedGraphLaunch);
      ir.replay_composition_slots.append(
          candidate, 0, launch->captured_graph_instance_id, instance.slot_template_id,
          body.replay_body_template_id, ReplayCompositionSlotRole::kGraph,
          launch->raw_graph_connection_id);
      const auto region = ir.replay_composition_regions.append(
          candidate, 0, launch->id, launch->id, launch->start_ns, launch->end_ns,
          1, 1, ReplayCompositionRegionStatus::kRecognizedCompletePattern);
      ir.replay_composition_region_members.append(region, 0, launch->id, 0);
    }
  }
}

}  // namespace traceloom::ascend_sqlite_detail
