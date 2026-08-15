#include "ascend_sqlite_internal.h"

#include "traceloom/analysis/exact_periodic_suffix.h"
#include "traceloom/runtime/thread_pool.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace traceloom::ascend_sqlite_detail {
ReplayBodyTemplateId replay_body_template_for_launch(
    const NativeIr& ir,
    GraphLaunchOccurrenceId launch_id) {
  for (const GraphLaunchBodyRow& body : ir.graph_launch_bodies.rows()) {
    if (body.graph_launch_occurrence_id == launch_id) {
      return body.replay_body_template_id;
    }
  }
  return ReplayBodyTemplateId::invalid();
}

void materialize_replay_composition_segment(
    NativeIr& ir,
    const std::vector<const GraphLaunchOccurrenceRow*>& segment,
    ReplayCompositionOrderPolicy order_policy,
    const std::set<GraphLaunchOccurrenceId>&
        missing_body_capability_launches) {
  if (segment.empty()) {
    return;
  }
  bool all_instances = true;
  std::vector<std::int64_t> identities;
  identities.reserve(segment.size());
  for (const GraphLaunchOccurrenceRow* launch : segment) {
    if (!launch->captured_graph_instance_id.valid()) {
      all_instances = false;
      break;
    }
    identities.push_back(
        static_cast<std::int64_t>(launch->captured_graph_instance_id.value()));
  }
  ReplayCompositionIdentityPolicy identity_policy =
      ReplayCompositionIdentityPolicy::kCapturedGraphInstance;
  if (!all_instances) {
    identity_policy = ReplayCompositionIdentityPolicy::kGraphConnection;
    identities.clear();
    for (const GraphLaunchOccurrenceRow* launch : segment) {
      if (launch->raw_graph_connection_id < 0) {
        return;
      }
      identities.push_back(launch->raw_graph_connection_id);
    }
  }

  const ExactPeriodicSuffixCandidate periodic =
      find_exact_periodic_suffix(identities);
  if (periodic.period == 0) {
    return;
  }
  std::string hash_input =
      identity_policy == ReplayCompositionIdentityPolicy::kCapturedGraphInstance
          ? "captured_graph_instance\n"
          : "graph_connection\n";
  hash_input +=
      order_policy == ReplayCompositionOrderPolicy::kHostSubmissionOrder
          ? "host_submission_order\n"
          : "device_execution_order\n";
  for (std::size_t index = 0; index < periodic.period; ++index) {
    hash_input += std::to_string(identities[periodic.start + index]);
    hash_input += "\n";
  }
  const GraphLaunchOccurrenceRow& first_pattern =
      *segment[periodic.start];
  std::vector<ReplayBodyTemplateId> body_templates;
  body_templates.reserve(periodic.period);
  for (std::size_t index = 0; index < periodic.period; ++index) {
    body_templates.push_back(replay_body_template_for_launch(
        ir, segment[periodic.start + index]->id));
  }
  ReplayCompositionShapePolicy shape_policy =
      ReplayCompositionShapePolicy::kUnclassified;
  if (body_templates.size() >= 3 && body_templates.front().valid() &&
      body_templates.back().valid() && body_templates[1].valid() &&
      body_templates.front() != body_templates[1] &&
      body_templates.back() != body_templates[1] &&
      body_templates.front() != body_templates.back() &&
      std::all_of(body_templates.begin() + 1, body_templates.end() - 1,
                  [&](ReplayBodyTemplateId id) {
                    return id == body_templates[1];
                  })) {
    shape_policy = ReplayCompositionShapePolicy::kHeadRepeatedLayerTail;
  }
  const auto range_has_body_capability = [&](std::size_t begin,
                                             std::size_t count) {
    for (std::size_t offset = 0; offset < count; ++offset) {
      if (missing_body_capability_launches.find(segment[begin + offset]->id) !=
          missing_body_capability_launches.end()) {
        return false;
      }
    }
    return true;
  };

  // A one-shot prefill cannot prove periodicity by repetition.  It can still
  // be recognized exactly when it occupies precisely one decode-sized leading
  // composition and independently has an H + L* + T body shape.  Requiring
  // the already-confirmed periodic suffix to have the same high-level shape
  // prevents arbitrary leading context from being promoted by this rule.
  bool recognized_one_shot_leading = false;
  if (range_has_body_capability(0, periodic.start + periodic.period) &&
      shape_policy ==
          ReplayCompositionShapePolicy::kHeadRepeatedLayerTail &&
      periodic.start == periodic.period && periodic.start >= 3) {
    std::vector<ReplayBodyTemplateId> leading_bodies;
    leading_bodies.reserve(periodic.start);
    for (std::size_t index = 0; index < periodic.start; ++index) {
      leading_bodies.push_back(
          replay_body_template_for_launch(ir, segment[index]->id));
    }
    const bool leading_hlt =
        leading_bodies.front().valid() && leading_bodies.back().valid() &&
        leading_bodies[1].valid() &&
        leading_bodies.front() != leading_bodies[1] &&
        leading_bodies.back() != leading_bodies[1] &&
        leading_bodies.front() != leading_bodies.back() &&
        std::all_of(leading_bodies.begin() + 1, leading_bodies.end() - 1,
                    [&](ReplayBodyTemplateId id) {
                      return id == leading_bodies[1];
                    });
    if (leading_hlt) {
      std::string leading_hash_input =
          identity_policy ==
                  ReplayCompositionIdentityPolicy::kCapturedGraphInstance
              ? "captured_graph_instance\n"
              : "graph_connection\n";
      leading_hash_input +=
          order_policy == ReplayCompositionOrderPolicy::kHostSubmissionOrder
              ? "host_submission_order\n"
              : "device_execution_order\n";
      leading_hash_input += "exact_one_shot_leading_composition\n";
      for (std::size_t index = 0; index < periodic.start; ++index) {
        leading_hash_input += std::to_string(identities[index]);
        leading_hash_input += "\n";
      }
      const ReplayCompositionCandidateId leading_candidate =
          ir.replay_composition_candidates.append(
              segment.front()->source_ref_id, segment.front()->device_id,
              segment.front()->id, segment.front()->id,
              static_cast<std::uint32_t>(periodic.start), 0,
              static_cast<std::uint32_t>(periodic.start), 1, 0,
              stable_hash64(leading_hash_input), identity_policy, order_policy,
              ReplayCompositionShapePolicy::kHeadRepeatedLayerTail,
              ReplayCompositionBoundaryPolicy::
                  kExactOneShotLeadingComposition);
      for (std::size_t index = 0; index < periodic.start; ++index) {
        const GraphLaunchOccurrenceRow& launch = *segment[index];
        GraphSlotTemplateId slot_template_id =
            GraphSlotTemplateId::invalid();
        if (launch.captured_graph_instance_id.valid()) {
          slot_template_id =
              ir.captured_graph_instances
                  .row(launch.captured_graph_instance_id)
                  .slot_template_id;
        }
        const ReplayCompositionSlotRole role =
            index == 0
                ? ReplayCompositionSlotRole::kHead
                : (index + 1 == periodic.start
                       ? ReplayCompositionSlotRole::kTail
                       : ReplayCompositionSlotRole::kLayer);
        ir.replay_composition_slots.append(
            leading_candidate, static_cast<std::uint32_t>(index),
            launch.captured_graph_instance_id, slot_template_id,
            leading_bodies[index], role, launch.raw_graph_connection_id);
      }
      std::int64_t leading_start_ns = segment.front()->start_ns;
      std::int64_t leading_end_ns = segment.front()->end_ns;
      for (std::size_t index = 1; index < periodic.start; ++index) {
        leading_start_ns =
            std::min(leading_start_ns, segment[index]->start_ns);
        leading_end_ns = std::max(leading_end_ns, segment[index]->end_ns);
      }
      const ReplayCompositionRegionId leading_region =
          ir.replay_composition_regions.append(
              leading_candidate, 0, segment.front()->id,
              segment[periodic.start - 1]->id, leading_start_ns,
              leading_end_ns, static_cast<std::uint32_t>(periodic.start),
              static_cast<std::uint32_t>(periodic.start),
              ReplayCompositionRegionStatus::kRecognizedCompletePattern);
      for (std::size_t index = 0; index < periodic.start; ++index) {
        ir.replay_composition_region_members.append(
            leading_region, static_cast<std::uint32_t>(index),
            segment[index]->id, static_cast<std::int64_t>(index));
      }
      recognized_one_shot_leading = true;
    }
  }

  const ReplayCompositionCandidateId candidate_id =
      ir.replay_composition_candidates.append(
          first_pattern.source_ref_id, first_pattern.device_id,
          segment.front()->id, first_pattern.id,
          static_cast<std::uint32_t>(segment.size()),
          static_cast<std::uint32_t>(periodic.start),
          static_cast<std::uint32_t>(periodic.period),
          static_cast<std::uint32_t>(periodic.full_repeats),
          static_cast<std::uint32_t>(periodic.trailing),
          stable_hash64(hash_input), identity_policy, order_policy,
          shape_policy,
          ReplayCompositionBoundaryPolicy::kExactPeriodicSuffix);
  for (std::size_t index = 0; index < periodic.period; ++index) {
    const GraphLaunchOccurrenceRow& launch =
        *segment[periodic.start + index];
    GraphSlotTemplateId slot_template_id = GraphSlotTemplateId::invalid();
    if (launch.captured_graph_instance_id.valid()) {
      slot_template_id =
          ir.captured_graph_instances
              .row(launch.captured_graph_instance_id)
              .slot_template_id;
    }
    ReplayCompositionSlotRole role = ReplayCompositionSlotRole::kGeneric;
    if (shape_policy ==
        ReplayCompositionShapePolicy::kHeadRepeatedLayerTail) {
      role = index == 0
                 ? ReplayCompositionSlotRole::kHead
                 : (index + 1 == periodic.period
                        ? ReplayCompositionSlotRole::kTail
                        : ReplayCompositionSlotRole::kLayer);
    }
    ir.replay_composition_slots.append(
        candidate_id, static_cast<std::uint32_t>(index),
        launch.captured_graph_instance_id, slot_template_id,
        body_templates[index], role,
        launch.raw_graph_connection_id);
  }

  const auto append_region = [&](std::uint32_t region_order,
                                 std::size_t begin,
                                 std::size_t count,
                                 std::uint32_t expected_launch_count,
                                 ReplayCompositionRegionStatus status,
                                 bool has_expected_slots) {
    std::int64_t start_ns = segment[begin]->start_ns;
    std::int64_t end_ns = segment[begin]->end_ns;
    for (std::size_t offset = 1; offset < count; ++offset) {
      start_ns = std::min(start_ns, segment[begin + offset]->start_ns);
      end_ns = std::max(end_ns, segment[begin + offset]->end_ns);
    }
    const ReplayCompositionRegionId region_id =
        ir.replay_composition_regions.append(
            candidate_id, region_order, segment[begin]->id,
            segment[begin + count - 1]->id, start_ns, end_ns,
            static_cast<std::uint32_t>(count), expected_launch_count, status);
    for (std::size_t offset = 0; offset < count; ++offset) {
      ir.replay_composition_region_members.append(
          region_id, static_cast<std::uint32_t>(offset),
          segment[begin + offset]->id,
          has_expected_slots ? static_cast<std::int64_t>(offset) : -1);
    }
  };
  const auto body_status = [&](std::size_t begin, std::size_t count,
                               ReplayCompositionRegionStatus matched_status) {
    if (!range_has_body_capability(begin, count)) {
      return ReplayCompositionRegionStatus::
          kUnrecognizedMissingBodyCapability;
    }
    for (std::size_t offset = 0; offset < count; ++offset) {
      const ReplayBodyTemplateId expected = body_templates[offset];
      const ReplayBodyTemplateId observed = replay_body_template_for_launch(
          ir, segment[begin + offset]->id);
      if (!expected.valid() || !observed.valid()) {
        return ReplayCompositionRegionStatus::
            kUnrecognizedMissingBodyEvidence;
      }
      if (expected != observed) {
        return ReplayCompositionRegionStatus::kUnrecognizedBodyMismatch;
      }
    }
    return matched_status;
  };

  std::uint32_t region_order = 0;
  if (periodic.start > 0 && !recognized_one_shot_leading) {
    append_region(region_order++, 0, periodic.start, 0,
                  range_has_body_capability(0, periodic.start)
                      ? ReplayCompositionRegionStatus::
                            kUnrecognizedLeadingContext
                      : ReplayCompositionRegionStatus::
                            kUnrecognizedMissingBodyCapability,
                  false);
  }
  for (std::size_t repeat = 0; repeat < periodic.full_repeats; ++repeat) {
    const std::size_t begin = periodic.start + repeat * periodic.period;
    append_region(
        region_order++, begin, periodic.period,
        static_cast<std::uint32_t>(periodic.period),
        body_status(
            begin, periodic.period,
            ReplayCompositionRegionStatus::kRecognizedCompletePattern),
        true);
  }
  if (periodic.trailing > 0) {
    const std::size_t begin =
        periodic.start + periodic.full_repeats * periodic.period;
    append_region(
        region_order++, begin, periodic.trailing,
        static_cast<std::uint32_t>(periodic.period),
        body_status(
            begin, periodic.trailing,
            ReplayCompositionRegionStatus::kUnrecognizedIncompleteTail),
        true);
  }
}

void materialize_incomplete_launch_segment(
    NativeIr& ir,
    const std::vector<const GraphLaunchOccurrenceRow*>& segment,
    ReplayCompositionOrderPolicy order_policy) {
  if (segment.empty()) {
    return;
  }
  std::string hash_input = "incomplete_launch_evidence\n";
  hash_input +=
      order_policy == ReplayCompositionOrderPolicy::kHostSubmissionOrder
          ? "host_submission_order\n"
          : "device_execution_order\n";
  std::int64_t start_ns = segment.front()->start_ns;
  std::int64_t end_ns = segment.front()->end_ns;
  for (const GraphLaunchOccurrenceRow* launch : segment) {
    hash_input += std::to_string(launch->raw_launch_connection_id);
    hash_input += "\n";
    start_ns = std::min(start_ns, launch->start_ns);
    end_ns = std::max(end_ns, launch->end_ns);
  }
  const ReplayCompositionCandidateId candidate_id =
      ir.replay_composition_candidates.append(
          segment.front()->source_ref_id, segment.front()->device_id,
          segment.front()->id, segment.front()->id,
          static_cast<std::uint32_t>(segment.size()), 0, 0, 0, 0,
          stable_hash64(hash_input),
          ReplayCompositionIdentityPolicy::kUnavailable, order_policy,
          ReplayCompositionShapePolicy::kUnclassified,
          ReplayCompositionBoundaryPolicy::kIncompleteLaunchEvidence);
  const ReplayCompositionRegionId region_id =
      ir.replay_composition_regions.append(
          candidate_id, 0, segment.front()->id, segment.back()->id, start_ns,
          end_ns, static_cast<std::uint32_t>(segment.size()),
          static_cast<std::uint32_t>(segment.size()),
          ReplayCompositionRegionStatus::
              kUnrecognizedMissingCompletionEvidence);
  for (std::size_t index = 0; index < segment.size(); ++index) {
    ir.replay_composition_region_members.append(
        region_id, static_cast<std::uint32_t>(index), segment[index]->id, -1);
  }
}

bool graph_launches_in_host_submission_order(
    const NativeIr& ir,
    std::vector<const GraphLaunchOccurrenceRow*>& ordered) {
  if (ir.graph_launch_occurrences.empty() ||
      ir.graph_launch_activity_members.empty()) {
    return false;
  }
  std::vector<std::uint32_t> launch_membership_counts(
      ir.graph_launch_occurrences.size(), 0);
  std::vector<std::uint32_t> activity_member_counts(
      ir.graph_launch_activities.size(), 0);
  ordered.clear();
  ordered.reserve(ir.graph_launch_occurrences.size());
  for (const GraphLaunchActivityMemberRow& member :
       ir.graph_launch_activity_members.rows()) {
    if (!member.graph_launch_activity_id.valid() ||
        !member.graph_launch_occurrence_id.valid()) {
      return false;
    }
    ++activity_member_counts[member.graph_launch_activity_id.value()];
    ++launch_membership_counts[member.graph_launch_occurrence_id.value()];
    ordered.push_back(
        &ir.graph_launch_occurrences.row(member.graph_launch_occurrence_id));
  }
  for (const GraphLaunchActivityRow& activity :
       ir.graph_launch_activities.rows()) {
    const std::uint32_t member_count = activity_member_counts[activity.id.value()];
    if (member_count == 0) {
      continue;
    }
    if (activity.boundary_policy !=
            GraphLaunchActivityBoundaryPolicy::kHostBlockingSync ||
        activity.host_execute_count != activity.matched_launch_count ||
        activity.matched_launch_count != member_count) {
      ordered.clear();
      return false;
    }
  }
  if (ordered.size() != ir.graph_launch_occurrences.size() ||
      std::any_of(launch_membership_counts.begin(),
                  launch_membership_counts.end(),
                  [](std::uint32_t count) { return count != 1; })) {
    ordered.clear();
    return false;
  }
  return true;
}

void materialize_replay_composition_candidates_for_order(
    NativeIr& ir,
    const std::vector<const GraphLaunchOccurrenceRow*>& ordered_launches,
    ReplayCompositionOrderPolicy order_policy,
    const std::set<GraphLaunchOccurrenceId>&
        missing_body_capability_launches) {
  std::map<std::uint32_t, std::vector<const GraphLaunchOccurrenceRow*>>
      launches_by_device;
  for (const GraphLaunchOccurrenceRow* launch : ordered_launches) {
    launches_by_device[launch->device_id].push_back(launch);
  }
  for (const auto& item : launches_by_device) {
    std::vector<const GraphLaunchOccurrenceRow*> segment;
    std::vector<const GraphLaunchOccurrenceRow*> incomplete_segment;
    for (const GraphLaunchOccurrenceRow* launch : item.second) {
      if (launch->raw_graph_connection_id < 0) {
        materialize_replay_composition_segment(
            ir, segment, order_policy, missing_body_capability_launches);
        segment.clear();
        incomplete_segment.push_back(launch);
        continue;
      }
      materialize_incomplete_launch_segment(
          ir, incomplete_segment, order_policy);
      incomplete_segment.clear();
      segment.push_back(launch);
    }
    materialize_replay_composition_segment(
        ir, segment, order_policy, missing_body_capability_launches);
    materialize_incomplete_launch_segment(
        ir, incomplete_segment, order_policy);
  }
}

void materialize_replay_composition_candidates(
    NativeIr& ir,
    const std::set<GraphLaunchOccurrenceId>&
        missing_body_capability_launches) {
  std::vector<const GraphLaunchOccurrenceRow*> device_order;
  device_order.reserve(ir.graph_launch_occurrences.size());
  for (const GraphLaunchOccurrenceRow& launch :
       ir.graph_launch_occurrences.rows()) {
    device_order.push_back(&launch);
  }

  std::vector<const GraphLaunchOccurrenceRow*> host_order;
  if (!graph_launches_in_host_submission_order(ir, host_order)) {
    materialize_replay_composition_candidates_for_order(
        ir, device_order, ReplayCompositionOrderPolicy::kDeviceExecutionOrder,
        missing_body_capability_launches);
    return;
  }
  const bool identical_order =
      std::equal(device_order.begin(), device_order.end(), host_order.begin(),
                 host_order.end(),
                 [](const GraphLaunchOccurrenceRow* lhs,
                    const GraphLaunchOccurrenceRow* rhs) {
                   return lhs->id == rhs->id;
                 });
  if (!identical_order) {
    materialize_replay_composition_candidates_for_order(
        ir, device_order, ReplayCompositionOrderPolicy::kDeviceExecutionOrder,
        missing_body_capability_launches);
  }
  materialize_replay_composition_candidates_for_order(
      ir, host_order, ReplayCompositionOrderPolicy::kHostSubmissionOrder,
      missing_body_capability_launches);
}

std::set<std::uint32_t> materialize_exact_aclgraph_replay_units(
    NativeIr& ir,
    const std::string& source_kind,
    const std::string& source_path) {
  std::map<std::uint32_t,
           std::vector<const ReplayCompositionCandidateRow*>>
      host_candidates_by_device;
  std::map<std::uint32_t,
           std::vector<const ReplayCompositionCandidateRow*>>
      device_candidates_by_device;
  for (const ReplayCompositionCandidateRow& candidate :
       ir.replay_composition_candidates.rows()) {
    if (!replay_composition_candidate_has_exact_structure(candidate)) {
      continue;
    }
    auto& destination =
        candidate.order_policy ==
                ReplayCompositionOrderPolicy::kHostSubmissionOrder
            ? host_candidates_by_device
            : device_candidates_by_device;
    destination[candidate.device_id].push_back(&candidate);
  }

  std::map<std::uint32_t,
           std::vector<const ReplayCompositionCandidateRow*>>
      candidates_by_device;
  for (const auto& item : host_candidates_by_device) {
    candidates_by_device.emplace(item.first, item.second);
  }
  for (const auto& item : device_candidates_by_device) {
    if (candidates_by_device.find(item.first) == candidates_by_device.end()) {
      candidates_by_device.emplace(item.first, item.second);
    }
  }
  if (candidates_by_device.empty()) {
    return {};
  }

  // Once structurally exact composition evidence exists for a device, do not
  // fall back to the older capture-cardinality heuristic on that device.
  // Optional H/L/T labels do not strengthen identity, order, boundaries, or
  // body evidence. Ambiguous, missing, or contradictory exact evidence must
  // remain explicit unknowns rather than being silently reclassified by a
  // weaker projector.
  std::set<std::uint32_t> exact_claimed_devices;
  for (const auto& device_candidates : candidates_by_device) {
    exact_claimed_devices.insert(device_candidates.first);
  }

  const SourceRefId source_ref = ir.source_refs.append(
      source_kind, source_path, "ACLGRAPH_REPLAY_UNIT", 0);
  std::map<std::string, GraphTemplateId> templates_by_signature;
  for (const auto& device_candidates : candidates_by_device) {
    std::int64_t previous_end_ns = std::numeric_limits<std::int64_t>::min();
    for (const ReplayCompositionCandidateRow* candidate_ptr :
         device_candidates.second) {
      const ReplayCompositionCandidateRow& candidate = *candidate_ptr;
      std::vector<const ReplayCompositionSlotRow*> slots(
          candidate.pattern_length, nullptr);
      for (const ReplayCompositionSlotRow& slot :
           ir.replay_composition_slots.rows()) {
        if (slot.replay_composition_candidate_id != candidate.id) {
          continue;
        }
        if (slot.slot_order >= slots.size() ||
            slots[slot.slot_order] != nullptr) {
          throw std::logic_error(
              "exact replay composition has invalid slot membership");
        }
        slots[slot.slot_order] = &slot;
      }
      if (std::any_of(slots.begin(), slots.end(),
                      [](const ReplayCompositionSlotRow* slot) {
                        return slot == nullptr ||
                               !slot->replay_body_template_id.valid();
                      })) {
        continue;
      }

      std::string template_signature = "exact_replay_composition_v1\n";
      for (const ReplayCompositionSlotRow* slot : slots) {
        const ReplayBodyTemplateRow& body = ir.replay_body_templates.row(
            slot->replay_body_template_id);
        template_signature += std::to_string(slot->slot_order);
        template_signature += ":";
        template_signature += std::to_string(body.exact_sequence_hash);
        template_signature += ":";
        template_signature += std::to_string(static_cast<unsigned>(slot->role));
        template_signature += "\n";
      }
      GraphTemplateId graph_template = GraphTemplateId::invalid();
      const auto existing_template =
          templates_by_signature.find(template_signature);
      if (existing_template == templates_by_signature.end()) {
        graph_template = ir.graph_templates.append(
            source_ref, stable_hash64(template_signature),
            candidate.pattern_length);
        templates_by_signature.emplace(template_signature, graph_template);
      } else {
        graph_template = existing_template->second;
      }

      std::vector<std::vector<const ReplayCompositionRegionMemberRow*>>
          members_by_region(ir.replay_composition_regions.size());
      for (const ReplayCompositionRegionMemberRow& member :
           ir.replay_composition_region_members.rows()) {
        if (member.replay_composition_region_id.valid() &&
            member.replay_composition_region_id.value() <
                members_by_region.size()) {
          members_by_region[member.replay_composition_region_id.value()]
              .push_back(&member);
        }
      }

      for (const ReplayCompositionRegionRow& region :
           ir.replay_composition_regions.rows()) {
        if (region.replay_composition_candidate_id != candidate.id ||
            region.status !=
                ReplayCompositionRegionStatus::kRecognizedCompletePattern) {
          continue;
        }
        std::vector<const ReplayCompositionRegionMemberRow*>& members =
            members_by_region[region.id.value()];
        std::sort(members.begin(), members.end(),
                  [](const ReplayCompositionRegionMemberRow* lhs,
                     const ReplayCompositionRegionMemberRow* rhs) {
                    return lhs->member_order < rhs->member_order;
                  });
        if (members.size() != candidate.pattern_length ||
            region.observed_launch_count != candidate.pattern_length ||
            region.expected_launch_count != candidate.pattern_length ||
            region.start_ns >= region.end_ns ||
            region.start_ns < previous_end_ns) {
          throw std::logic_error(
              "recognized exact replay region violates projection invariants");
        }

        std::uint32_t raw_stream_id =
            std::numeric_limits<std::uint32_t>::max();
        for (std::size_t index = 0; index < members.size(); ++index) {
          const ReplayCompositionRegionMemberRow& member = *members[index];
          if (member.member_order != index ||
              member.expected_slot_order != static_cast<std::int64_t>(index)) {
            throw std::logic_error(
                "recognized exact replay region lost ordered slot membership");
          }
          const GraphLaunchOccurrenceRow& launch =
              ir.graph_launch_occurrences.row(
                  member.graph_launch_occurrence_id);
          if (launch.device_id != candidate.device_id ||
              replay_body_template_for_launch(ir, launch.id) !=
                  slots[index]->replay_body_template_id) {
            throw std::logic_error(
                "recognized exact replay region body no longer matches slot");
          }
          if (index == 0) {
            const StreamId stream_id = launch.model_stream_id.valid()
                                           ? launch.model_stream_id
                                           : launch.execute_stream_id;
            if (stream_id.valid()) {
              raw_stream_id = static_cast<std::uint32_t>(
                  ir.streams.row(stream_id).raw_stream_id);
            }
          }
        }

        const std::string symbol =
            "GraphReplayUnit ExactT" +
            std::to_string(graph_template.value() +
                           static_cast<std::uint32_t>(1));
        const TraceEventId event_id = ir.trace_events.append(
            source_ref, region.id.value() + 1, candidate.device_id,
            raw_stream_id, region.start_ns, region.end_ns,
            ir.symbols.intern(symbol));
        const ReplayUnitId replay_unit = ir.replay_units.append(
            graph_template, source_ref, AnchorId::invalid(),
            AnchorId::invalid(), event_id, region.id);
        for (std::size_t index = 0; index < members.size(); ++index) {
          ir.replay_unit_launch_members.append(
              replay_unit, static_cast<std::uint32_t>(index),
              members[index]->graph_launch_occurrence_id, slots[index]->id);
        }
        previous_end_ns = region.end_ns;
      }
    }
  }
  return exact_claimed_devices;
}

void materialize_aclgraph_replay_units(
    NativeIr& ir,
    const std::unordered_map<std::uint32_t,
                             std::unordered_set<std::uint64_t>>&
        model_streams_by_device,
    const std::vector<GraphLaunchView>& execute_launches,
    SourceRefId source_ref,
    std::uint32_t capture_group_size,
    const std::string& capture_replay_unit_signature) {
  if (model_streams_by_device.empty()) {
    return;
  }

  const std::unordered_set<std::uint64_t> model_stream_keys =
      flatten_model_stream_keys(model_streams_by_device);
  const GraphTaskSymbolSets graph_symbols =
      build_graph_task_symbol_sets(ir.symbols);
  ir.trace_events.reserve(ir.trace_events.size() + ir.tasks.size());
  std::vector<GraphTaskView> model_rows;
  std::vector<GraphTaskView> control_rows;
  model_rows.reserve(ir.tasks.size() / 2u);
  control_rows.reserve(ir.tasks.size() / 8u);
  for (const TaskRow& task : ir.tasks.rows()) {
    if (!task.trace_event_id.valid()) {
      continue;
    }
    const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
    const bool is_model_stream =
        model_stream_keys.find(stream_key(event.device_id, event.stream_id)) !=
        model_stream_keys.end();
    if (is_model_stream && event.end_ns > event.start_ns) {
      model_rows.push_back(GraphTaskView{&task, &event});
    }
    if (symbol_in_set(graph_symbols.graph_control, task.task_type_symbol_id) &&
        event.end_ns > event.start_ns) {
      control_rows.push_back(GraphTaskView{&task, &event});
    }
  }
  if (model_rows.empty()) {
    return;
  }
  std::sort(model_rows.begin(), model_rows.end(),
            [](const GraphTaskView& lhs, const GraphTaskView& rhs) {
              if (lhs.event->start_ns != rhs.event->start_ns) {
                return lhs.event->start_ns < rhs.event->start_ns;
              }
              if (lhs.event->end_ns != rhs.event->end_ns) {
                return lhs.event->end_ns < rhs.event->end_ns;
              }
              if (lhs.event->stream_id != rhs.event->stream_id) {
                return lhs.event->stream_id < rhs.event->stream_id;
              }
              return lhs.task->raw_task_id < rhs.task->raw_task_id;
            });
  std::sort(control_rows.begin(), control_rows.end(),
            [](const GraphTaskView& lhs, const GraphTaskView& rhs) {
              if (lhs.event->start_ns != rhs.event->start_ns) {
                return lhs.event->start_ns < rhs.event->start_ns;
              }
              if (lhs.event->end_ns != rhs.event->end_ns) {
                return lhs.event->end_ns < rhs.event->end_ns;
              }
              return lhs.task->raw_task_id < rhs.task->raw_task_id;
            });
  const std::vector<GraphTaskView> notify_wait_rows =
      controls_with_symbol_set(control_rows, graph_symbols.notify_wait);
  const std::vector<GraphTaskView> model_execute_rows =
      controls_with_symbol_set(control_rows, graph_symbols.model_execute);

  std::vector<GraphReplayUnitView> units;
  if (!execute_launches.empty()) {
    const std::vector<GraphLaunchView> device_launches =
        device_backed_execute_launches(execute_launches, model_execute_rows,
                                       capture_group_size);
    units = split_rows_by_execute_waves(model_rows, device_launches,
                                        capture_group_size);
  }

  static constexpr std::int64_t kGapNs = 5'000'000;
  if (units.empty()) {
    std::vector<std::vector<GraphTaskView>> activities;
    std::vector<GraphTaskView> current;
    std::int64_t last_end = model_rows.front().event->end_ns;
    for (const GraphTaskView& row : model_rows) {
      if (!current.empty() && row.event->start_ns - last_end > kGapNs) {
        activities.push_back(std::move(current));
        current = {};
      }
      current.push_back(row);
      last_end = std::max(last_end, row.event->end_ns);
    }
    if (!current.empty()) {
      activities.push_back(std::move(current));
    }

    std::size_t notify_wait_cursor = 0;
    std::size_t model_execute_cursor = 0;
    for (const std::vector<GraphTaskView>& activity : activities) {
      const std::int64_t start_ns = activity.front().event->start_ns;
      std::int64_t end_ns = activity.front().event->end_ns;
      for (const GraphTaskView& row : activity) {
        end_ns = std::max(end_ns, row.event->end_ns);
      }
      std::vector<GraphTaskView> notify_waits =
          controls_in_interval_from_sorted(notify_wait_rows, start_ns, end_ns,
                                           notify_wait_cursor);
      std::vector<GraphTaskView> model_execs =
          controls_in_interval_from_sorted(model_execute_rows, start_ns, end_ns,
                                           model_execute_cursor);
      std::vector<GraphReplayUnitView> split =
          split_activity(ir, activity, notify_waits, model_execs,
                         capture_group_size);
      units.insert(units.end(), split.begin(), split.end());
    }
  }
  if (units.empty()) {
    return;
  }
  if (!capture_replay_unit_signature.empty()) {
    for (GraphReplayUnitView& unit : units) {
      unit.template_signature = capture_replay_unit_signature;
    }
  }

  std::vector<ReplayUnitWindow> windows;
  windows.reserve(units.size());
  for (const GraphReplayUnitView& unit : units) {
    if (unit.rows.empty()) {
      windows.push_back(ReplayUnitWindow{});
      continue;
    }
    if (unit.has_window) {
      windows.push_back(ReplayUnitWindow{unit.start_ns, unit.end_ns});
      continue;
    }
    std::int64_t start_ns = unit.rows.front().event->start_ns;
    std::int64_t end_ns = unit.rows.front().event->end_ns;
    for (const GraphTaskView& row : unit.rows) {
      start_ns = std::min(start_ns, row.event->start_ns);
      end_ns = std::max(end_ns, row.event->end_ns);
    }
    windows.push_back(ReplayUnitWindow{start_ns, end_ns});
  }

  std::map<std::string, GraphTemplateId> templates_by_signature;
  for (std::size_t unit_index = 0; unit_index < units.size(); ++unit_index) {
    const GraphReplayUnitView& unit = units[unit_index];
    if (unit.rows.empty()) {
      continue;
    }
    std::int64_t start_ns = windows[unit_index].start_ns;
    std::int64_t end_ns = windows[unit_index].end_ns;
    std::uint32_t device_id = unit.rows.front().event->device_id;
    std::map<std::uint64_t, std::uint64_t> duration_by_stream;
    for (const GraphTaskView& row : unit.rows) {
      device_id = row.event->device_id;
      duration_by_stream[row.event->stream_id] += static_cast<std::uint64_t>(
          std::max<std::int64_t>(0, row.event->end_ns - row.event->start_ns));
    }
    std::uint64_t primary_stream = unit.rows.front().event->stream_id;
    std::uint64_t primary_duration = 0;
    for (const auto& item : duration_by_stream) {
      if (item.second > primary_duration) {
        primary_stream = item.first;
        primary_duration = item.second;
      }
    }

    const std::string signature = unit.template_signature.empty()
                                      ? body_signature(ir, unit.rows)
                                      : unit.template_signature;
    const std::uint64_t hash = stable_hash64(signature);
    auto template_found = templates_by_signature.find(signature);
    GraphTemplateId graph_template;
    if (template_found == templates_by_signature.end()) {
      graph_template =
          ir.graph_templates.append(source_ref, hash, capture_group_size);
      templates_by_signature.emplace(signature, graph_template);
    } else {
      graph_template = template_found->second;
    }

    const std::string symbol =
        "GraphReplayUnit T" +
        std::to_string(graph_template.value() + static_cast<std::uint32_t>(1));
    const SymbolId symbol_id = ir.symbols.intern(symbol);
    const TraceEventId event_id = ir.trace_events.append(
        source_ref, ir.replay_units.size() + 1, device_id, primary_stream,
        start_ns, end_ns, symbol_id);
    ir.replay_units.append(graph_template, source_ref, AnchorId::invalid(),
                           AnchorId::invalid(), event_id);
  }
}

}  // namespace traceloom::ascend_sqlite_detail
