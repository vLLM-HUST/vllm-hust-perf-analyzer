#include "traceloom/analysis/replay_internal_cost_map.h"

#include "traceloom/analysis/graph_body_cost_summary.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace traceloom {
namespace {

std::uint64_t duration_ns(const TraceEventRow& event) {
  return static_cast<std::uint64_t>(
      std::max<std::int64_t>(0, event.end_ns - event.start_ns));
}

std::uint64_t interval_union_ns(
    const std::vector<std::pair<std::int64_t, std::int64_t>>& intervals) {
  if (intervals.empty()) {
    return 0;
  }
  std::vector<std::pair<std::int64_t, std::int64_t>> sorted = intervals;
  std::sort(sorted.begin(), sorted.end());
  std::uint64_t total = 0;
  std::int64_t union_start = sorted.front().first;
  std::int64_t union_end = sorted.front().second;
  for (std::size_t index = 1; index < sorted.size(); ++index) {
    const auto& interval = sorted[index];
    if (interval.first > union_end) {
      total += static_cast<std::uint64_t>(
          std::max<std::int64_t>(0, union_end - union_start));
      union_start = interval.first;
      union_end = interval.second;
    } else {
      union_end = std::max(union_end, interval.second);
    }
  }
  total += static_cast<std::uint64_t>(
      std::max<std::int64_t>(0, union_end - union_start));
  return total;
}

std::uint64_t median(std::vector<std::uint64_t> values) {
  if (values.empty()) {
    return 0;
  }
  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2;
  if (values.size() % 2 != 0) {
    return values[middle];
  }
  return values[middle - 1] / 2 + values[middle] / 2 +
         (values[middle - 1] % 2 + values[middle] % 2) / 2;
}

std::uint64_t quartile(std::vector<std::uint64_t> values,
                       std::size_t numerator) {
  if (values.empty()) {
    return 0;
  }
  std::sort(values.begin(), values.end());
  return values[((values.size() - 1) * numerator + 2) / 4];
}

void add_issue(ReplayInternalCostMapResult& result,
               std::string code,
               ReplayUnitId replay_unit_id,
               ReplayUnitLaunchMemberId replay_unit_launch_member_id,
               std::string detail = {}) {
  ReplayInternalCostIssue issue;
  issue.code = std::move(code);
  issue.replay_unit_id = replay_unit_id;
  issue.replay_unit_launch_member_id = replay_unit_launch_member_id;
  issue.detail = std::move(detail);
  result.issues.push_back(std::move(issue));
}

struct LaunchMemberRef {
  const ReplayUnitLaunchMemberRow* row;
};

struct BodyMemberRef {
  const GraphLaunchBodyMemberRow* row;
  const TaskRow* task;
  const TraceEventRow* event;
};

bool slot_row_for(const NativeIr& ir,
                  ReplayCompositionSlotId slot_id,
                  const ReplayCompositionSlotRow** slot) {
  if (!slot_id.valid() || slot_id.value() >= ir.replay_composition_slots.size()) {
    return false;
  }
  *slot = &ir.replay_composition_slots.row(slot_id);
  return true;
}

bool task_event_for(const NativeIr& ir,
                    TaskId task_id,
                    const TaskRow** task,
                    const TraceEventRow** event) {
  if (!task_id.valid() || task_id.value() >= ir.tasks.size()) {
    return false;
  }
  const TaskRow& task_row = ir.tasks.row(task_id);
  if (!task_row.trace_event_id.valid() ||
      task_row.trace_event_id.value() >= ir.trace_events.size()) {
    return false;
  }
  *task = &task_row;
  *event = &ir.trace_events.row(task_row.trace_event_id);
  return true;
}

SymbolId member_identity(const TaskRow& task) {
  if (task.op_name_symbol_id.valid()) {
    return task.op_name_symbol_id;
  }
  if (task.comm_name_symbol_id.valid()) {
    return task.comm_name_symbol_id;
  }
  return SymbolId::invalid();
}

void append_stream_cost_row(
    ReplayLaunchMemberCostRow& launch_member,
    const std::vector<BodyMemberRef>& members) {
  ReplayStreamCostRow stream;
  std::vector<std::pair<std::int64_t, std::int64_t>> intervals;
  bool first = true;
  for (const BodyMemberRef& member : members) {
    if (first) {
      stream.stream_id = member.event->stream_id;
      stream.lane_ordinal = member.row->lane_ordinal;
      first = false;
    }
    if (stream.lane_ordinal != member.row->lane_ordinal) {
      stream.lane_consistent = false;
    }
    ++stream.member_count;
    const std::uint64_t duration = duration_ns(*member.event);
    stream.task_sum_ns += duration;
    switch (member.row->kind) {
      case GraphLaunchBodyMemberRow::Kind::kCompute:
        stream.compute_ns += duration;
        break;
      case GraphLaunchBodyMemberRow::Kind::kCommunication:
        stream.communication_ns += duration;
        break;
      case GraphLaunchBodyMemberRow::Kind::kDataMove:
        stream.data_move_ns += duration;
        break;
    }
    intervals.emplace_back(member.event->start_ns, member.event->end_ns);
  }
  stream.busy_union_ns = interval_union_ns(intervals);
  launch_member.streams.push_back(std::move(stream));
}

struct AggregateKey {
  std::uint32_t graph_template_id = 0;
  std::uint32_t device_id = 0;
  std::uint32_t slot_role = 0;
  std::uint32_t body_template_id = 0;
  std::uint32_t stream_id = 0;
  std::uint32_t within_stream_position = 0;
  std::uint32_t identity_symbol_id = 0;
};

bool operator<(const AggregateKey& lhs, const AggregateKey& rhs) {
  return std::tie(lhs.graph_template_id, lhs.device_id, lhs.slot_role,
                  lhs.body_template_id, lhs.stream_id,
                  lhs.within_stream_position, lhs.identity_symbol_id) <
         std::tie(rhs.graph_template_id, rhs.device_id, rhs.slot_role,
                  rhs.body_template_id, rhs.stream_id,
                  rhs.within_stream_position, rhs.identity_symbol_id);
}

struct AggregateAccumulator {
  std::set<ReplayUnitId::value_type> replay_units;
  std::set<ReplayUnitLaunchMemberId::value_type> launch_members;
  std::vector<std::uint64_t> durations;
  GraphLaunchBodyMemberRow::Kind kind =
      GraphLaunchBodyMemberRow::Kind::kCompute;
  std::uint32_t lane_ordinal = 0;
  bool kind_consistent = true;
  bool lane_consistent = true;
};

void accumulate(AggregateAccumulator& accumulator,
                const ReplayMemberCostRow& member) {
  accumulator.replay_units.insert(member.replay_unit_id.value());
  accumulator.launch_members.insert(
      member.replay_unit_launch_member_id.value());
  accumulator.durations.push_back(member.duration_ns);
  if (accumulator.launch_members.size() == 1 &&
      accumulator.durations.size() == 1) {
    accumulator.kind = member.kind;
    accumulator.lane_ordinal = member.lane_ordinal;
  } else {
    if (accumulator.kind != member.kind) {
      accumulator.kind_consistent = false;
    }
    if (accumulator.lane_ordinal != member.lane_ordinal) {
      accumulator.lane_consistent = false;
    }
  }
}

}  // namespace

const char* replay_internal_cost_map_slot_role_name(
    ReplayCompositionSlotRole role) noexcept {
  switch (role) {
    case ReplayCompositionSlotRole::kUnclassified:
      return "unclassified";
    case ReplayCompositionSlotRole::kHead:
      return "head";
    case ReplayCompositionSlotRole::kLayer:
      return "layer";
    case ReplayCompositionSlotRole::kTail:
      return "tail";
    case ReplayCompositionSlotRole::kGraph:
      return "graph";
    case ReplayCompositionSlotRole::kCudaGraph:
      return "cuda_graph";
    case ReplayCompositionSlotRole::kGeneric:
      return "generic_slot";
  }
  return "unclassified";
}

const char* replay_internal_cost_map_member_kind_name(
    GraphLaunchBodyMemberRow::Kind kind) noexcept {
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

ReplayInternalCostMapResult build_replay_internal_cost_map(
    const NativeIr& ir) {
  ReplayInternalCostMapResult result;

  if (ir.replay_units.empty()) {
    result.result_reason_codes.push_back("no_replay_units");
    return result;
  }

  // Body index by occurrence, and whole-body cost lenses reused from the
  // existing overlap-aware summary (task_sum / busy_union / envelope /
  // compute / communication / data_move).
  std::map<GraphLaunchOccurrenceId::value_type,
           std::vector<const GraphLaunchBodyRow*>>
      bodies_by_occurrence;
  for (const GraphLaunchBodyRow& body : ir.graph_launch_bodies.rows()) {
    bodies_by_occurrence[body.graph_launch_occurrence_id.value()].push_back(
        &body);
  }
  const GraphBodyCostSummary summary = build_graph_body_cost_summary(ir);
  std::map<GraphLaunchBodyId::value_type, const GraphBodyOccurrenceCostRow*>
      occurrence_cost_by_body;
  for (const GraphBodyOccurrenceCostRow& row : summary.occurrences) {
    occurrence_cost_by_body.emplace(row.graph_launch_body_id.value(), &row);
  }

  // Launch members grouped by unit, preserving recorded order with a stable
  // (member_order, member id) emission order.
  std::map<ReplayUnitId::value_type, std::vector<LaunchMemberRef>>
      members_by_unit;
  for (const ReplayUnitLaunchMemberRow& member :
       ir.replay_unit_launch_members.rows()) {
    if (!member.replay_unit_id.valid() ||
        member.replay_unit_id.value() >= ir.replay_units.size()) {
      add_issue(result, "orphan_replay_unit_launch_member",
                member.replay_unit_id, member.id);
      continue;
    }
    LaunchMemberRef ref;
    ref.row = &member;
    members_by_unit[member.replay_unit_id.value()].push_back(ref);
  }

  std::map<AggregateKey, AggregateAccumulator> aggregate_accumulators;

  for (const ReplayUnitRow& unit : ir.replay_units.rows()) {
    ReplayUnitCostBlock block;
    block.replay_unit_id = unit.id;
    block.graph_template_id = unit.graph_template_id;
    block.source_ref_id = unit.source_ref_id;

    auto members_it = members_by_unit.find(unit.id.value());
    if (members_it == members_by_unit.end() || members_it->second.empty()) {
      block.unit_reason_codes.push_back("empty_replay_unit");
      ++result.unsupported_unit_count;
      result.units.push_back(std::move(block));
      continue;
    }
    std::vector<LaunchMemberRef> ordered = members_it->second;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const LaunchMemberRef& lhs,
                        const LaunchMemberRef& rhs) {
                       return std::make_tuple(lhs.row->member_order,
                                               lhs.row->id.value()) <
                              std::make_tuple(rhs.row->member_order,
                                              rhs.row->id.value());
                     });
    bool member_order_gap = false;
    for (std::size_t index = 0; index < ordered.size(); ++index) {
      if (ordered[index].row->member_order != index) {
        member_order_gap = true;
        break;
      }
    }
    if (member_order_gap) {
      add_issue(result, "member_order_gap", unit.id,
                ReplayUnitLaunchMemberId::invalid(),
                "recorded member_order is not a contiguous 0..n-1 sequence");
    }

    block.launch_member_count = static_cast<std::uint32_t>(ordered.size());
    bool all_supported = true;
    std::set<std::string> unit_reasons;
    for (const LaunchMemberRef& ref : ordered) {
      const ReplayUnitLaunchMemberRow& member = *ref.row;
      ReplayLaunchMemberCostRow cost;
      cost.replay_unit_launch_member_id = member.id;
      cost.member_order = member.member_order;
      cost.graph_launch_occurrence_id = member.graph_launch_occurrence_id;
      cost.replay_composition_slot_id = member.replay_composition_slot_id;

      const ReplayCompositionSlotRow* slot = nullptr;
      if (!slot_row_for(ir, member.replay_composition_slot_id, &slot)) {
        cost.reason_code = "missing_replay_composition_slot";
      } else {
        cost.slot_role = slot->role;
        cost.slot_order = slot->slot_order;
        cost.replay_body_template_id = slot->replay_body_template_id;
        if (!slot->replay_body_template_id.valid()) {
          cost.reason_code = "slot_missing_body_template";
        } else {
          const auto bodies = bodies_by_occurrence.find(
              member.graph_launch_occurrence_id.value());
          if (bodies == bodies_by_occurrence.end() ||
              bodies->second.empty()) {
            cost.reason_code = "missing_graph_launch_body";
          } else if (bodies->second.size() > 1) {
            cost.reason_code = "ambiguous_graph_launch_body";
            cost.replay_body_template_id = slot->replay_body_template_id;
          } else {
            const GraphLaunchBodyRow& body = *bodies->second.front();
            if (body.replay_body_template_id !=
                slot->replay_body_template_id) {
              cost.reason_code = "body_template_mismatch";
              cost.replay_body_template_id = slot->replay_body_template_id;
            } else if (occurrence_cost_by_body.find(body.id.value()) ==
                       occurrence_cost_by_body.end()) {
              cost.reason_code = "missing_occurrence_cost_evidence";
            } else {
              cost.supported = true;
              cost.graph_launch_body_id = body.id;
              const GraphBodyOccurrenceCostRow& occurrence_cost =
                  *occurrence_cost_by_body.at(body.id.value());
              cost.member_count = occurrence_cost.member_count;
              cost.task_sum_ns = occurrence_cost.task_sum_ns;
              cost.busy_union_ns = occurrence_cost.busy_union_ns;
              cost.envelope_ns = occurrence_cost.envelope_ns;
              cost.compute_ns = occurrence_cost.compute_ns;
              cost.communication_ns = occurrence_cost.communication_ns;
              cost.data_move_ns = occurrence_cost.data_move_ns;

              // Per-stream ordered member evidence.
              std::vector<const GraphLaunchBodyMemberRow*> body_members;
              for (const GraphLaunchBodyMemberRow& body_member :
                   ir.graph_launch_body_members.rows()) {
                if (body_member.graph_launch_body_id == body.id) {
                  body_members.push_back(&body_member);
                }
              }
              std::stable_sort(
                  body_members.begin(), body_members.end(),
                  [&](const GraphLaunchBodyMemberRow* lhs,
                      const GraphLaunchBodyMemberRow* rhs) {
                    const TraceEventRow& lhs_event =
                        ir.trace_events.row(
                            ir.tasks.row(lhs->task_id).trace_event_id);
                    const TraceEventRow& rhs_event =
                        ir.trace_events.row(
                            ir.tasks.row(rhs->task_id).trace_event_id);
                    return std::make_tuple(lhs->lane_ordinal,
                                            lhs->task_ordinal,
                                            lhs_event.start_ns,
                                            lhs->id.value()) <
                           std::make_tuple(rhs->lane_ordinal,
                                           rhs->task_ordinal,
                                           rhs_event.start_ns,
                                           rhs->id.value());
                  });

              std::int64_t body_min_start = 0;
              bool has_min_start = false;
              std::vector<BodyMemberRef> valid_members;
              std::set<std::pair<std::uint32_t, std::uint32_t>> positions;
              bool duplicate_position = false;
              bool invalid_reference = false;
              for (const GraphLaunchBodyMemberRow* body_member :
                   body_members) {
                const TaskRow* task = nullptr;
                const TraceEventRow* event = nullptr;
                if (!task_event_for(ir, body_member->task_id, &task,
                                    &event)) {
                  invalid_reference = true;
                  continue;
                }
                if (!positions
                         .emplace(event->stream_id,
                                  body_member->task_ordinal)
                         .second) {
                  duplicate_position = true;
                }
                BodyMemberRef ref;
                ref.row = body_member;
                ref.task = task;
                ref.event = event;
                valid_members.push_back(ref);
                if (!has_min_start || event->start_ns < body_min_start) {
                  body_min_start = event->start_ns;
                  has_min_start = true;
                }
              }
              if (invalid_reference) {
                add_issue(result, "invalid_body_member_reference", unit.id,
                          member.id,
                          "body " + std::to_string(body.id.value()) +
                              " references an invalid task or trace event");
              }
              if (duplicate_position) {
                add_issue(result, "duplicate_within_stream_position",
                          unit.id, member.id,
                          "body " + std::to_string(body.id.value()) +
                              " repeats a (stream, position) key");
              }

              std::map<std::uint32_t, std::vector<BodyMemberRef>>
                  members_by_stream;
              for (const BodyMemberRef& body_member : valid_members) {
                members_by_stream[body_member.event->stream_id].push_back(
                    body_member);
                ReplayMemberCostRow member_row;
                member_row.replay_unit_id = unit.id;
                member_row.replay_unit_launch_member_id = member.id;
                member_row.member_order = member.member_order;
                member_row.graph_launch_occurrence_id =
                    member.graph_launch_occurrence_id;
                member_row.replay_composition_slot_id =
                    member.replay_composition_slot_id;
                member_row.slot_role = slot->role;
                member_row.slot_order = slot->slot_order;
                member_row.replay_body_template_id =
                    body.replay_body_template_id;
                member_row.graph_launch_body_id = body.id;
                member_row.graph_launch_body_member_id =
                    body_member.row->id;
                member_row.device_id = body_member.event->device_id;
                member_row.stream_id = body_member.event->stream_id;
                member_row.lane_ordinal = body_member.row->lane_ordinal;
                member_row.within_stream_position =
                    body_member.row->task_ordinal;
                member_row.kind = body_member.row->kind;
                member_row.task_id = body_member.row->task_id;
                member_row.trace_event_id = body_member.event->id;
                member_row.source_ref_id = body_member.task->source_ref_id;
                member_row.identity_symbol_id =
                    member_identity(*body_member.task);
                member_row.raw_task_id = body_member.task->raw_task_id;
                member_row.start_ns = body_member.event->start_ns;
                member_row.end_ns = body_member.event->end_ns;
                member_row.duration_ns = duration_ns(*body_member.event);
                member_row.relative_start_ns =
                    body_member.event->start_ns - body_min_start;
                member_row.relative_end_ns =
                    body_member.event->end_ns - body_min_start;
                if (!member_row.identity_symbol_id.valid()) {
                  add_issue(result, "missing_member_identity", unit.id,
                            member.id,
                            "body member " +
                                std::to_string(
                                    body_member.row->id.value()) +
                                " has no op or communication identity");
                }
                result.members.push_back(std::move(member_row));
              }

              bool lane_inconsistency = false;
              for (auto& stream_item : members_by_stream) {
                append_stream_cost_row(cost, stream_item.second);
                if (!cost.streams.back().lane_consistent) {
                  lane_inconsistency = true;
                }
              }
              if (lane_inconsistency) {
                add_issue(result, "stream_lane_inconsistency", unit.id,
                          member.id,
                          "streams of body " +
                              std::to_string(body.id.value()) +
                              " map to multiple lane ordinals");
              }
            }
          }
        }
      }
      if (!cost.supported) {
        all_supported = false;
        ++result.unsupported_launch_count;
        unit_reasons.insert(cost.reason_code);
      } else {
        ++result.resolved_launch_count;
      }
      block.launch_members.push_back(std::move(cost));
    }

    block.resolved_launch_count = static_cast<std::uint32_t>(
        block.launch_members.size() -
        std::count_if(block.launch_members.begin(),
                      block.launch_members.end(),
                      [](const ReplayLaunchMemberCostRow& row) {
                        return !row.supported;
                      }));
    block.supported = all_supported;
    block.unit_reason_codes.assign(unit_reasons.begin(), unit_reasons.end());
    if (block.supported) {
      ++result.fully_supported_unit_count;
    } else if (block.resolved_launch_count == 0) {
      ++result.unsupported_unit_count;
    } else {
      ++result.partially_supported_unit_count;
    }
    result.units.push_back(std::move(block));
  }

  // Aligned aggregates over the stable structural key. Members without a
  // valid identity cannot be aligned and are excluded (explicit issue above).
  for (const ReplayMemberCostRow& member : result.members) {
    if (!member.identity_symbol_id.valid()) {
      continue;
    }
    AggregateKey key;
    // ReplayUnit ids are contiguous 0..n-1 (checked_next_id on append) and
    // result.units holds exactly one block per unit in table order, so the
    // unit id is a valid block index.
    key.graph_template_id =
        result.units[member.replay_unit_id.value()].graph_template_id.value();
    key.device_id = member.device_id;
    key.slot_role = static_cast<std::uint32_t>(member.slot_role);
    key.body_template_id = member.replay_body_template_id.value();
    key.stream_id = member.stream_id;
    key.within_stream_position = member.within_stream_position;
    key.identity_symbol_id = member.identity_symbol_id.value();
    accumulate(aggregate_accumulators[key], member);
  }

  for (const auto& item : aggregate_accumulators) {
    const AggregateKey& key = item.first;
    const AggregateAccumulator& accumulator = item.second;
    ReplayAlignedCostAggregateRow row;
    row.graph_template_id = GraphTemplateId(key.graph_template_id);
    row.device_id = key.device_id;
    row.slot_role = static_cast<ReplayCompositionSlotRole>(key.slot_role);
    row.replay_body_template_id = ReplayBodyTemplateId(key.body_template_id);
    row.stream_id = key.stream_id;
    row.within_stream_position = key.within_stream_position;
    row.identity_symbol_id = SymbolId(key.identity_symbol_id);
    row.kind = accumulator.kind;
    row.member_occurrence_count =
        static_cast<std::uint32_t>(accumulator.durations.size());
    row.replay_unit_count =
        static_cast<std::uint32_t>(accumulator.replay_units.size());
    row.launch_member_count =
        static_cast<std::uint32_t>(accumulator.launch_members.size());
    row.kind_consistent = accumulator.kind_consistent;
    row.lane_consistent = accumulator.lane_consistent;
    row.distribution_supported =
        accumulator.kind_consistent && accumulator.lane_consistent;
    if (row.distribution_supported) {
      row.duration_p25_ns = quartile(accumulator.durations, 1);
      row.duration_median_ns = median(accumulator.durations);
      row.duration_p75_ns = quartile(accumulator.durations, 3);
    }
    result.aggregates.push_back(std::move(row));
  }

  return result;
}

}  // namespace traceloom
