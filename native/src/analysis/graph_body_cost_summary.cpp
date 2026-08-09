#include "traceloom/analysis/graph_body_cost_summary.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace traceloom {
namespace {

std::uint64_t duration_ns(const TraceEventRow& event) {
  return static_cast<std::uint64_t>(
      std::max<std::int64_t>(0, event.end_ns - event.start_ns));
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

GraphBodyCostDistributionRow distribution(
    ReplayBodyTemplateId template_id,
    GraphBodyCostScope scope,
    const std::vector<const GraphBodyOccurrenceCostRow*>& rows) {
  GraphBodyCostDistributionRow out;
  out.replay_body_template_id = template_id;
  out.scope = scope;
  out.occurrence_count = rows.size();
  std::vector<std::uint64_t> task_sum;
  std::vector<std::uint64_t> busy_union;
  std::vector<std::uint64_t> envelope;
  std::vector<std::uint64_t> compute;
  std::vector<std::uint64_t> communication;
  std::vector<std::uint64_t> data_move;
  for (const GraphBodyOccurrenceCostRow* row : rows) {
    task_sum.push_back(row->task_sum_ns);
    busy_union.push_back(row->busy_union_ns);
    envelope.push_back(row->envelope_ns);
    compute.push_back(row->compute_ns);
    communication.push_back(row->communication_ns);
    data_move.push_back(row->data_move_ns);
  }
  out.task_sum_p25_ns = quartile(task_sum, 1);
  out.task_sum_median_ns = median(task_sum);
  out.task_sum_p75_ns = quartile(task_sum, 3);
  out.busy_union_median_ns = median(busy_union);
  out.envelope_median_ns = median(envelope);
  out.compute_median_ns = median(compute);
  out.communication_median_ns = median(communication);
  out.data_move_median_ns = median(data_move);
  return out;
}

}  // namespace

std::string_view graph_body_cost_scope_name(GraphBodyCostScope scope) {
  switch (scope) {
    case GraphBodyCostScope::kAllObservedBodies:
      return "all_observed_bodies";
    case GraphBodyCostScope::kExactReplayUnits:
      return "exact_replay_units";
  }
  return "all_observed_bodies";
}

GraphBodyCostSummary build_graph_body_cost_summary(const NativeIr& ir) {
  std::set<GraphLaunchOccurrenceId::value_type> exact_occurrences;
  for (const ReplayUnitLaunchMemberRow& member :
       ir.replay_unit_launch_members.rows()) {
    exact_occurrences.insert(member.graph_launch_occurrence_id.value());
  }
  // Fail-closed membership indexing: body members whose body id or
  // task/event references are invalid or out of range are skipped rather than
  // dereferenced, so malformed IR never throws. For valid IR the behavior is
  // byte-identical to strict membership.
  std::vector<std::vector<const GraphLaunchBodyMemberRow*>> members_by_body(
      ir.graph_launch_bodies.size());
  for (const GraphLaunchBodyMemberRow& member :
       ir.graph_launch_body_members.rows()) {
    if (!member.graph_launch_body_id.valid() ||
        member.graph_launch_body_id.value() >= members_by_body.size()) {
      continue;
    }
    if (!member.task_id.valid() || member.task_id.value() >= ir.tasks.size()) {
      continue;
    }
    const TaskRow& task = ir.tasks.row(member.task_id);
    if (!task.trace_event_id.valid() ||
        task.trace_event_id.value() >= ir.trace_events.size()) {
      continue;
    }
    members_by_body[member.graph_launch_body_id.value()].push_back(&member);
  }

  GraphBodyCostSummary summary;
  summary.occurrences.reserve(ir.graph_launch_bodies.size());
  for (const GraphLaunchBodyRow& body : ir.graph_launch_bodies.rows()) {
    GraphBodyOccurrenceCostRow row;
    row.graph_launch_body_id = body.id;
    row.graph_launch_occurrence_id = body.graph_launch_occurrence_id;
    row.replay_body_template_id = body.replay_body_template_id;
    row.exact_replay_unit =
        exact_occurrences.find(body.graph_launch_occurrence_id.value()) !=
        exact_occurrences.end();
    const auto& members = members_by_body[body.id.value()];
    row.member_count = members.size();
    std::vector<std::pair<std::int64_t, std::int64_t>> intervals;
    intervals.reserve(members.size());
    for (const GraphLaunchBodyMemberRow* member : members) {
      const TaskRow& task = ir.tasks.row(member->task_id);
      const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
      const std::uint64_t duration = duration_ns(event);
      row.task_sum_ns += duration;
      switch (member->kind) {
        case GraphLaunchBodyMemberRow::Kind::kCompute:
          row.compute_ns += duration;
          break;
        case GraphLaunchBodyMemberRow::Kind::kCommunication:
          row.communication_ns += duration;
          break;
        case GraphLaunchBodyMemberRow::Kind::kDataMove:
          row.data_move_ns += duration;
          break;
      }
      intervals.emplace_back(event.start_ns, event.end_ns);
    }
    std::sort(intervals.begin(), intervals.end());
    if (!intervals.empty()) {
      std::int64_t union_start = intervals.front().first;
      std::int64_t union_end = intervals.front().second;
      const std::int64_t envelope_start = union_start;
      std::int64_t envelope_end = union_end;
      for (std::size_t index = 1; index < intervals.size(); ++index) {
        const auto& interval = intervals[index];
        envelope_end = std::max(envelope_end, interval.second);
        if (interval.first > union_end) {
          row.busy_union_ns += static_cast<std::uint64_t>(
              std::max<std::int64_t>(0, union_end - union_start));
          union_start = interval.first;
          union_end = interval.second;
        } else {
          union_end = std::max(union_end, interval.second);
        }
      }
      row.busy_union_ns += static_cast<std::uint64_t>(
          std::max<std::int64_t>(0, union_end - union_start));
      row.envelope_ns = static_cast<std::uint64_t>(
          std::max<std::int64_t>(0, envelope_end - envelope_start));
    }
    summary.occurrences.push_back(row);
  }

  std::map<ReplayBodyTemplateId::value_type,
           std::vector<const GraphBodyOccurrenceCostRow*>>
      by_template;
  for (const GraphBodyOccurrenceCostRow& row : summary.occurrences) {
    by_template[row.replay_body_template_id.value()].push_back(&row);
  }
  for (const auto& item : by_template) {
    const ReplayBodyTemplateId template_id(item.first);
    summary.distributions.push_back(distribution(
        template_id, GraphBodyCostScope::kAllObservedBodies, item.second));
    std::vector<const GraphBodyOccurrenceCostRow*> exact;
    for (const GraphBodyOccurrenceCostRow* row : item.second) {
      if (row->exact_replay_unit) {
        exact.push_back(row);
      }
    }
    if (!exact.empty()) {
      summary.distributions.push_back(distribution(
          template_id, GraphBodyCostScope::kExactReplayUnits, exact));
    }
  }
  return summary;
}

}  // namespace traceloom
