#include "traceloom/compat/native_graph_replay_rows.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "traceloom/compat/timeline_rows.h"

namespace traceloom::compat {
namespace {

double ns_to_us(std::int64_t ns) {
  return static_cast<double>(ns) / 1000.0;
}

bool overlaps(const TraceEventRow& lhs, const TraceEventRow& rhs) {
  return lhs.start_ns < rhs.end_ns && rhs.start_ns < lhs.end_ns;
}

std::string template_id(GraphTemplateId id) {
  return "aclgraph-template-" + std::to_string(id.value());
}

std::string replay_id(ReplayUnitId id) {
  return "aclgraph-replay-unit-" + std::to_string(id.value());
}

std::string composition_candidate_id(ReplayCompositionCandidateId id) {
  return "aclgraph-composition-candidate-" + std::to_string(id.value());
}

std::string composition_region_id(ReplayCompositionRegionId id) {
  return "aclgraph-reconstruction-region-" + std::to_string(id.value());
}

struct IndexedEvent {
  const TraceEventRow* event = nullptr;
};

struct DeviceEventIndex {
  std::vector<IndexedEvent> events;
};

struct DeviceEventSweep {
  std::size_t cursor = 0;
  std::vector<IndexedEvent> active;
};

std::unordered_map<std::uint32_t, DeviceEventIndex> build_device_event_index(
    const NativeIr& ir,
    const std::unordered_set<TraceEventId::value_type>& replay_event_ids) {
  std::unordered_map<std::uint32_t, DeviceEventIndex> index;
  for (const TraceEventRow& event : ir.trace_events.rows()) {
    if (replay_event_ids.find(event.id.value()) != replay_event_ids.end()) {
      continue;
    }
    index[event.device_id].events.push_back(IndexedEvent{&event});
  }
  for (auto& device_item : index) {
    DeviceEventIndex& device = device_item.second;
    std::sort(device.events.begin(), device.events.end(),
              [](const IndexedEvent& lhs, const IndexedEvent& rhs) {
                if (lhs.event->start_ns != rhs.event->start_ns) {
                  return lhs.event->start_ns < rhs.event->start_ns;
                }
                if (lhs.event->end_ns != rhs.event->end_ns) {
                  return lhs.event->end_ns < rhs.event->end_ns;
                }
                return lhs.event->id < rhs.event->id;
              });
  }
  return index;
}

template <typename Fn>
void for_each_overlapping_event(const DeviceEventIndex& index,
                                DeviceEventSweep& sweep,
                                const TraceEventRow& graph_event,
                                Fn&& fn) {
  sweep.active.erase(
      std::remove_if(sweep.active.begin(), sweep.active.end(),
                     [&](const IndexedEvent& event) {
                       return event.event->end_ns <= graph_event.start_ns;
                     }),
      sweep.active.end());
  while (sweep.cursor < index.events.size() &&
         index.events[sweep.cursor].event->start_ns < graph_event.end_ns) {
    const IndexedEvent& event = index.events[sweep.cursor++];
    if (event.event->end_ns > graph_event.start_ns) {
      sweep.active.push_back(event);
    }
  }
  for (const IndexedEvent& child : sweep.active) {
    if (overlaps(graph_event, *child.event)) {
      fn(child);
    }
  }
}

}  // namespace

GraphReplayEvidenceSqlRows build_native_graph_replay_evidence_sql_rows(
    const NativeIr& ir,
    const std::string& source_kind,
    std::uint32_t db_idx) {
  std::unordered_set<TraceEventId::value_type> kernel_event_ids;
  kernel_event_ids.reserve(ir.tasks.size());
  for (const TaskRow& task : ir.tasks.rows()) {
    if (task.trace_event_id.valid() &&
        (task.op_type_symbol_id.valid() || task.op_name_symbol_id.valid() ||
         task.comm_name_symbol_id.valid())) {
      kernel_event_ids.insert(task.trace_event_id.value());
    }
  }
  std::vector<const ReplayUnitRow*> replay_units;
  replay_units.reserve(ir.replay_units.size());
  std::unordered_map<ReplayUnitId::value_type, std::uint32_t>
      launch_member_count_by_unit;
  for (const ReplayUnitLaunchMemberRow& member :
       ir.replay_unit_launch_members.rows()) {
    ++launch_member_count_by_unit[member.replay_unit_id.value()];
  }
  std::unordered_set<TraceEventId::value_type> replay_event_ids;
  replay_event_ids.reserve(ir.replay_units.size());
  for (const ReplayUnitRow& replay_unit : ir.replay_units.rows()) {
    if (!replay_unit.launch_trace_event_id.valid() ||
        replay_unit.launch_trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument(
          "ReplayUnitRow launch_trace_event_id is out of range");
    }
    if (!replay_unit.graph_template_id.valid() ||
        replay_unit.graph_template_id.value() >= ir.graph_templates.size()) {
      throw std::invalid_argument(
          "ReplayUnitRow graph_template_id is out of range");
    }
    if (!replay_unit.source_ref_id.valid() ||
        replay_unit.source_ref_id.value() >= ir.source_refs.size()) {
      throw std::invalid_argument("ReplayUnitRow source_ref_id is out of range");
    }
    replay_units.push_back(&replay_unit);
    replay_event_ids.insert(replay_unit.launch_trace_event_id.value());
  }
  const auto events_by_device =
      build_device_event_index(ir, replay_event_ids);
  std::unordered_map<std::uint32_t, DeviceEventSweep> event_sweeps;
  std::sort(replay_units.begin(), replay_units.end(),
            [&](const ReplayUnitRow* lhs, const ReplayUnitRow* rhs) {
              const TraceEventRow& lhs_event =
                  ir.trace_events.row(lhs->launch_trace_event_id);
              const TraceEventRow& rhs_event =
                  ir.trace_events.row(rhs->launch_trace_event_id);
              if (lhs_event.device_id != rhs_event.device_id) {
                return lhs_event.device_id < rhs_event.device_id;
              }
              if (lhs_event.start_ns != rhs_event.start_ns) {
                return lhs_event.start_ns < rhs_event.start_ns;
              }
              return lhs->id < rhs->id;
            });

  GraphReplayEvidenceSqlRows rows;
  std::unordered_map<ReplayCompositionRegionId::value_type, std::uint32_t>
      member_count_by_region;
  member_count_by_region.reserve(ir.replay_composition_regions.size());
  for (const ReplayCompositionRegionMemberRow& member :
       ir.replay_composition_region_members.rows()) {
    if (!member.replay_composition_region_id.valid() ||
        member.replay_composition_region_id.value() >=
            ir.replay_composition_regions.size()) {
      throw std::invalid_argument(
          "ReplayCompositionRegionMemberRow region id is out of range");
    }
    if (!member.graph_launch_occurrence_id.valid() ||
        member.graph_launch_occurrence_id.value() >=
            ir.graph_launch_occurrences.size()) {
      throw std::invalid_argument(
          "ReplayCompositionRegionMemberRow launch id is out of range");
    }
    ++member_count_by_region[member.replay_composition_region_id.value()];
  }
  rows.reconstruction_regions.reserve(ir.replay_composition_regions.size());
  for (const ReplayCompositionRegionRow& region :
       ir.replay_composition_regions.rows()) {
    if (!region.replay_composition_candidate_id.valid() ||
        region.replay_composition_candidate_id.value() >=
            ir.replay_composition_candidates.size()) {
      throw std::invalid_argument(
          "ReplayCompositionRegionRow candidate id is out of range");
    }
    if (!region.first_launch_id.valid() ||
        region.first_launch_id.value() >= ir.graph_launch_occurrences.size() ||
        !region.last_launch_id.valid() ||
        region.last_launch_id.value() >= ir.graph_launch_occurrences.size()) {
      throw std::invalid_argument(
          "ReplayCompositionRegionRow launch bounds are out of range");
    }
    if (region.end_ns < region.start_ns) {
      throw std::invalid_argument(
          "ReplayCompositionRegionRow has a negative duration");
    }
    const std::uint32_t member_count = member_count_by_region[region.id.value()];
    if (member_count != region.observed_launch_count) {
      throw std::invalid_argument(
          "ReplayCompositionRegionRow observed launch count disagrees with "
          "region membership");
    }
    const ReplayCompositionCandidateRow& candidate =
        ir.replay_composition_candidates.row(
            region.replay_composition_candidate_id);

    GraphReconstructionRegionSqlRow sql_region;
    sql_region.region_id = composition_region_id(region.id);
    sql_region.db_idx = db_idx;
    sql_region.device_id = candidate.device_id;
    sql_region.graph_provider =
        candidate.identity_policy ==
                ReplayCompositionIdentityPolicy::kCudaGraphNodeSet
            ? "cuda"
            : "aclgraph";
    sql_region.candidate_id = composition_candidate_id(candidate.id);
    sql_region.region_order = region.region_order;
    sql_region.status = replay_composition_region_status_name(region.status);
    sql_region.boundary_policy =
        replay_composition_boundary_policy_name(candidate.boundary_policy);
    sql_region.order_policy =
        replay_composition_order_policy_name(candidate.order_policy);
    sql_region.identity_policy =
        replay_composition_identity_policy_name(candidate.identity_policy);
    sql_region.shape_policy =
        replay_composition_shape_policy_name(candidate.shape_policy);
    sql_region.first_launch_occurrence_id = region.first_launch_id.value();
    sql_region.last_launch_occurrence_id = region.last_launch_id.value();
    sql_region.observed_launch_count = region.observed_launch_count;
    sql_region.expected_launch_count = region.expected_launch_count;
    sql_region.start_ns = region.start_ns;
    sql_region.end_ns = region.end_ns;
    sql_region.dur_us = ns_to_us(region.end_ns - region.start_ns);
    sql_region.raw_json =
        "{\"native_region_id\":" + std::to_string(region.id.value()) +
        ",\"native_candidate_id\":" + std::to_string(candidate.id.value()) +
        ",\"region_member_count\":" + std::to_string(member_count) +
        ",\"segment_launch_count\":" +
        std::to_string(candidate.segment_launch_count) +
        ",\"leading_launch_count\":" +
        std::to_string(candidate.leading_launch_count) +
        ",\"pattern_length\":" + std::to_string(candidate.pattern_length) +
        ",\"full_repeat_count\":" +
        std::to_string(candidate.full_repeat_count) +
        ",\"trailing_launch_count\":" +
        std::to_string(candidate.trailing_launch_count) + "}";
    rows.reconstruction_regions.push_back(std::move(sql_region));
  }
  rows.graph_replays.reserve(ir.replay_units.size());
  std::uint32_t envelope_idx = 1;
  for (const ReplayUnitRow* replay_unit_ptr : replay_units) {
    const ReplayUnitRow& replay_unit = *replay_unit_ptr;
    const TraceEventRow& graph_event =
        ir.trace_events.row(replay_unit.launch_trace_event_id);
    const GraphTemplateRow& graph_template =
        ir.graph_templates.row(replay_unit.graph_template_id);
    const SourceRefRow& graph_source =
        ir.source_refs.row(replay_unit.source_ref_id);
    const bool is_aclgraph =
        graph_source.table_name == "ACLGRAPH_REPLAY_UNIT";
    const bool is_cuda = source_kind == "cuda_nsys_sqlite" ||
                         graph_source.table_name ==
                             "CUPTI_ACTIVITY_KIND_GRAPH_TRACE";

    GraphReplaySqlRow replay;
    replay.graph_event_id =
        trace_event_compat_id(replay_unit.launch_trace_event_id);
    replay.db_idx = db_idx;
    replay.device_id = graph_event.device_id;
    replay.graph_provider =
        is_aclgraph ? "aclgraph" : (is_cuda ? "cuda" : source_kind);
    replay.graph_kind = replay.graph_provider + "_graph_replay";
    replay.graph_event_idx = replay_unit.id.value();
    replay.event_id = replay.graph_event_id;
    replay.step_idx = graph_event.id.value();
    replay.stream_id = static_cast<std::int64_t>(graph_event.stream_id);
    replay.graph_id =
        is_aclgraph ? template_id(replay_unit.graph_template_id)
                    : std::to_string(graph_template.body_sequence_hash);
    replay.graph_exec_id =
        is_aclgraph
            ? replay_id(replay_unit.id)
            : "native-graph-template-" +
                  std::to_string(replay_unit.graph_template_id.value());
    replay.start_ns = graph_event.start_ns;
    replay.end_ns = graph_event.end_ns;
    replay.dur_us = ns_to_us(graph_event.end_ns - graph_event.start_ns);
    const bool is_exact_replay =
        replay_unit.replay_composition_region_id.valid();
    // Fill the existing replay correlation field for exact CUDA replays when
    // deterministically available: exactly one ordered launch member whose
    // occurrence carries a raw launch connection id. Ambiguous or missing
    // correlation stays empty rather than being guessed.
    if (is_exact_replay && is_cuda) {
      std::int64_t resolved_correlation = -1;
      bool deterministic = false;
      for (const ReplayUnitLaunchMemberRow& member :
           ir.replay_unit_launch_members.rows()) {
        if (member.replay_unit_id != replay_unit.id) {
          continue;
        }
        if (!member.graph_launch_occurrence_id.valid() ||
            member.graph_launch_occurrence_id.value() >=
                ir.graph_launch_occurrences.size()) {
          throw std::invalid_argument(
              "ReplayUnitLaunchMemberRow occurrence id is out of range");
        }
        const GraphLaunchOccurrenceRow& member_launch =
            ir.graph_launch_occurrences.row(
                member.graph_launch_occurrence_id);
        if (member_launch.raw_launch_connection_id < 0) {
          deterministic = false;
          break;
        }
        if (!deterministic) {
          resolved_correlation = member_launch.raw_launch_connection_id;
          deterministic = true;
        } else if (member_launch.raw_launch_connection_id !=
                   resolved_correlation) {
          deterministic = false;
          break;
        }
      }
      if (deterministic) {
        replay.correlation_id = std::to_string(resolved_correlation);
      }
    }
    replay.raw_json =
        "{\"reconstruction\":\"" +
        std::string(is_exact_replay
                        ? "exact_replay_composition"
                        : (is_aclgraph ? "capture_stream_task_overlap"
                                       : "graph_trace_interval_overlap")) +
        "\","
        "\"graph_template_id\":" +
        std::to_string(replay_unit.graph_template_id.value()) +
        ",\"body_sequence_hash\":" +
        std::to_string(graph_template.body_sequence_hash) +
        ",\"capture_group_size\":" +
        std::to_string(graph_template.slot_count);
    if (is_exact_replay) {
      replay.raw_json +=
          ",\"replay_composition_region_id\":" +
          std::to_string(
              replay_unit.replay_composition_region_id.value()) +
          ",\"launch_member_count\":" +
          std::to_string(
              launch_member_count_by_unit[replay_unit.id.value()]);
    }
    replay.raw_json += "}";

    const auto device_found = events_by_device.find(graph_event.device_id);
    if (device_found == events_by_device.end()) {
      rows.graph_replays.push_back(std::move(replay));
      continue;
    }
    for_each_overlapping_event(
        device_found->second, event_sweeps[graph_event.device_id], graph_event,
        [&](const IndexedEvent& indexed) {
      const TraceEventRow& child = *indexed.event;

      const std::int64_t overlap_start =
          std::max(graph_event.start_ns, child.start_ns);
      const std::int64_t overlap_end =
          std::min(graph_event.end_ns, child.end_ns);
      const std::int64_t overlap_ns =
          std::max<std::int64_t>(0, overlap_end - overlap_start);
      ++replay.enclosed_event_count;
      replay.enclosed_event_us += ns_to_us(overlap_ns);
      if (kernel_event_ids.find(child.id.value()) != kernel_event_ids.end()) {
        ++replay.enclosed_kernel_count;
        replay.enclosed_kernel_us += ns_to_us(overlap_ns);
      }

      GraphEnvelopeSqlRow envelope;
      envelope.envelope_id =
          replay.graph_provider + "-graph-envelope-" +
          std::to_string(envelope_idx);
      envelope.db_idx = db_idx;
      envelope.device_id = graph_event.device_id;
      envelope.graph_provider = replay.graph_provider;
      envelope.graph_kind = replay.graph_kind;
      envelope.envelope_idx = envelope_idx++;
      envelope.graph_event_id = replay.graph_event_id;
      envelope.child_event_id = trace_event_compat_id(child.id);
      envelope.graph_step_idx = graph_event.id.value();
      envelope.child_step_idx = child.id.value();
      envelope.relation =
          child.start_ns >= graph_event.start_ns &&
                  child.end_ns <= graph_event.end_ns
              ? "contains"
              : "time_overlap";
      envelope.stream_relation =
          child.stream_id == graph_event.stream_id ? "same_stream"
                                                   : "cross_stream";
      envelope.graph_id = replay.graph_id;
      envelope.graph_exec_id = replay.graph_exec_id;
      envelope.graph_start_ns = graph_event.start_ns;
      envelope.graph_end_ns = graph_event.end_ns;
      envelope.child_start_ns = child.start_ns;
      envelope.child_end_ns = child.end_ns;
      envelope.start_offset_us =
          ns_to_us(child.start_ns - graph_event.start_ns);
      envelope.end_offset_us =
          ns_to_us(graph_event.end_ns - child.end_ns);
      envelope.child_dur_us = ns_to_us(child.end_ns - child.start_ns);
      envelope.raw_json =
          "{\"source_ref_id\":" +
          std::to_string(child.source_ref_id.value()) +
          ",\"source_row_id\":" + std::to_string(child.source_row_id) +
          "}";
      rows.graph_envelopes.push_back(std::move(envelope));
    });
    rows.graph_replays.push_back(std::move(replay));
  }
  return rows;
}

}  // namespace traceloom::compat
