#include "traceloom/analysis/host_correlation.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace traceloom {
namespace {

struct TaskTarget {
  TaskId task_id;
  std::uint32_t device_id = 0;
};

struct ApiResolution {
  bool has_device_id = false;
  std::uint32_t device_id = 0;
  TaskApiLinkStatus status = TaskApiLinkStatus::kUnresolved;
  std::vector<TaskTarget> targets;
};

using ConnectionKey = std::pair<std::int64_t, std::uint32_t>;

std::int64_t saturating_add(std::int64_t value, std::uint64_t amount) {
  if (amount > static_cast<std::uint64_t>(
                   std::numeric_limits<std::int64_t>::max()) ||
      value > std::numeric_limits<std::int64_t>::max() -
                  static_cast<std::int64_t>(amount)) {
    return std::numeric_limits<std::int64_t>::max();
  }
  return value + static_cast<std::int64_t>(amount);
}

std::int64_t saturating_sub(std::int64_t value, std::uint64_t amount) {
  if (amount > static_cast<std::uint64_t>(
                   std::numeric_limits<std::int64_t>::max()) ||
      value < std::numeric_limits<std::int64_t>::min() +
                  static_cast<std::int64_t>(amount)) {
    return std::numeric_limits<std::int64_t>::min();
  }
  return value - static_cast<std::int64_t>(amount);
}

bool intersection(std::int64_t lhs_start,
                  std::int64_t lhs_end,
                  std::int64_t rhs_start,
                  std::int64_t rhs_end,
                  std::int64_t* start,
                  std::int64_t* end) {
  *start = std::max(lhs_start, rhs_start);
  *end = std::min(lhs_end, rhs_end);
  return *end > *start;
}

std::vector<const DeviceIntervalRow*> device_gaps(
    const ProductiveTimelineRunResult& productive,
    std::uint32_t device_id) {
  std::vector<const DeviceIntervalRow*> gaps;
  for (const DeviceTimelineResult& device : productive.devices) {
    if (device.device_id != device_id || device.status != AnalysisStatus::kOk) {
      continue;
    }
    for (const DeviceIntervalRow& interval : device.intervals) {
      if (interval.kind == DeviceIntervalKind::kVisibleProductiveIdle) {
        gaps.push_back(&interval);
      }
    }
  }
  return gaps;
}

std::string api_name(const NativeIr& ir, const HostApiEventRow& api) {
  return api.api_name_symbol_id.valid()
             ? ir.symbols.value(api.api_name_symbol_id)
             : std::string();
}

std::map<HostApiEventId, ApiResolution> resolve_links(
    const NativeIr& ir,
    std::vector<TaskApiLinkRow>* output) {
  std::map<std::int64_t, std::vector<TaskTarget>> tasks_by_connection;
  for (const TaskRow& task : ir.tasks.rows()) {
    if (task.raw_connection_id < 0 || !task.trace_event_id.valid() ||
        task.trace_event_id.value() >= ir.trace_events.size()) {
      continue;
    }
    tasks_by_connection[task.raw_connection_id].push_back(
        TaskTarget{task.id,
                   ir.trace_events.row(task.trace_event_id).device_id});
  }

  std::map<HostApiEventId, ApiResolution> resolutions;
  std::map<ConnectionKey, std::size_t> host_count_by_scope;
  for (const HostApiEventRow& api : ir.host_api_events.rows()) {
    if (api.raw_connection_id < 0) {
      continue;
    }
    const auto target_it = tasks_by_connection.find(api.raw_connection_id);
    if (target_it == tasks_by_connection.end()) {
      continue;
    }
    std::set<std::uint32_t> devices;
    for (const TaskTarget& target : target_it->second) {
      if (!api.has_device_id || target.device_id == api.device_id) {
        devices.insert(target.device_id);
      }
    }
    if (api.has_device_id) {
      ++host_count_by_scope[{api.raw_connection_id, api.device_id}];
    } else if (devices.size() == 1) {
      ++host_count_by_scope[{api.raw_connection_id, *devices.begin()}];
    }
  }

  for (const HostApiEventRow& api : ir.host_api_events.rows()) {
    ApiResolution resolution;
    if (api.raw_connection_id >= 0) {
      const auto target_it = tasks_by_connection.find(api.raw_connection_id);
      if (target_it != tasks_by_connection.end()) {
        for (const TaskTarget& target : target_it->second) {
          if (!api.has_device_id || target.device_id == api.device_id) {
            resolution.targets.push_back(target);
          }
        }
      }
    }
    std::set<std::uint32_t> devices;
    for (const TaskTarget& target : resolution.targets) {
      devices.insert(target.device_id);
    }
    if (api.has_device_id) {
      resolution.has_device_id = true;
      resolution.device_id = api.device_id;
    } else if (devices.size() == 1) {
      resolution.has_device_id = true;
      resolution.device_id = *devices.begin();
    }

    if (resolution.targets.empty()) {
      resolution.status = TaskApiLinkStatus::kUnresolved;
    } else if (devices.size() > 1) {
      resolution.status = TaskApiLinkStatus::kAmbiguous;
    } else if (resolution.targets.size() > 1) {
      resolution.status = TaskApiLinkStatus::kOneToMany;
    } else if (host_count_by_scope[{api.raw_connection_id,
                                    resolution.device_id}] > 1) {
      // Many host records claiming the same scoped connection make the
      // reverse association ambiguous even if one task happens to exist.
      resolution.status = TaskApiLinkStatus::kAmbiguous;
    } else {
      resolution.status = TaskApiLinkStatus::kUnique;
    }

    if (resolution.targets.empty()) {
      output->push_back(TaskApiLinkRow{
          api.id, TaskId::invalid(), api.raw_connection_id, resolution.status,
          resolution.has_device_id, resolution.device_id});
    } else {
      for (const TaskTarget& target : resolution.targets) {
        output->push_back(TaskApiLinkRow{
            api.id, target.task_id, api.raw_connection_id, resolution.status,
            true, target.device_id});
      }
    }
    resolutions.emplace(api.id, std::move(resolution));
  }
  return resolutions;
}

HostEvidenceSourceLink source_link(HostApiEventId api_id,
                                   TaskId task_id,
                                   std::string relation,
                                   std::int64_t start_ns,
                                   std::int64_t end_ns) {
  return HostEvidenceSourceLink{api_id, task_id, std::move(relation), start_ns,
                                end_ns};
}

void add_host_sync_evidence(const HostApiEventRow& api,
                            std::uint32_t device_id,
                            const ClockModel& model,
                            const ProductiveTimelineRunResult& productive,
                            HostCorrelationRunResult* output) {
  const std::optional<std::int64_t> mapped_start =
      map_host_to_device_ns(model, api.start_ns);
  const std::optional<std::int64_t> mapped_end =
      map_host_to_device_ns(model, api.end_ns);
  if (!mapped_start.has_value() || !mapped_end.has_value() ||
      *mapped_end <= *mapped_start) {
    return;
  }
  const std::int64_t possible_start =
      saturating_sub(*mapped_start, model.epsilon_ns);
  const std::int64_t possible_end =
      saturating_add(*mapped_end, model.epsilon_ns);
  const std::int64_t robust_start =
      saturating_add(*mapped_start, model.epsilon_ns);
  const std::int64_t robust_end =
      saturating_sub(*mapped_end, model.epsilon_ns);
  for (const DeviceIntervalRow* gap : device_gaps(productive, device_id)) {
    std::int64_t possible_overlap_start = 0;
    std::int64_t possible_overlap_end = 0;
    if (!intersection(possible_start, possible_end, gap->start_ns, gap->end_ns,
                      &possible_overlap_start, &possible_overlap_end)) {
      continue;
    }
    std::int64_t robust_overlap_start = 0;
    std::int64_t robust_overlap_end = 0;
    if (robust_end > robust_start &&
        intersection(robust_start, robust_end, gap->start_ns, gap->end_ns,
                     &robust_overlap_start, &robust_overlap_end)) {
      HostEvidenceInterval evidence;
      evidence.device_id = device_id;
      evidence.start_ns = robust_overlap_start;
      evidence.end_ns = robust_overlap_end;
      evidence.category = HostEvidenceCategory::kHostSyncApiPresent;
      evidence.alignment_status = model.alignment_status;
      evidence.reason =
          "allowlisted host synchronization API robustly overlaps the gap";
      evidence.source_links.push_back(source_link(
          api.id, TaskId::invalid(), "temporal_overlap", robust_overlap_start,
          robust_overlap_end));
      output->evidence_intervals.push_back(std::move(evidence));
    } else {
      HostIdleCandidateRow candidate;
      candidate.device_id = device_id;
      candidate.gap_start_ns = gap->start_ns;
      candidate.gap_end_ns = gap->end_ns;
      candidate.category = HostEvidenceCategory::kHostSyncApiPresent;
      candidate.candidate_status = HostCandidateStatus::kPossibleOnly;
      candidate.candidate_relation = "temporal_overlap";
      candidate.alignment_status = model.alignment_status;
      candidate.reason =
          "host synchronization overlap exists only inside the uncertainty "
          "window";
      candidate.source_links.push_back(source_link(
          api.id, TaskId::invalid(), "temporal_overlap",
          possible_overlap_start, possible_overlap_end));
      output->candidates.push_back(std::move(candidate));
    }
  }
}

void add_enqueue_evidence(const NativeIr& ir,
                          const HostApiEventRow& api,
                          const ApiResolution& resolution,
                          const ClockModel& model,
                          const ProductiveTimelineRunResult& productive,
                          HostCorrelationRunResult* output) {
  if (resolution.status != TaskApiLinkStatus::kUnique ||
      resolution.targets.size() != 1) {
    return;
  }
  const TaskRow& task = ir.tasks.row(resolution.targets.front().task_id);
  if (!task.trace_event_id.valid() ||
      task.trace_event_id.value() >= ir.trace_events.size()) {
    return;
  }
  const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
  const std::optional<std::int64_t> mapped_end =
      map_host_to_device_ns(model, api.end_ns);
  if (!mapped_end.has_value()) {
    return;
  }
  const std::int64_t possible_start =
      saturating_sub(*mapped_end, model.epsilon_ns);
  const std::int64_t robust_start =
      saturating_add(*mapped_end, model.epsilon_ns);
  for (const DeviceIntervalRow* gap :
       device_gaps(productive, event.device_id)) {
    std::int64_t possible_overlap_start = 0;
    std::int64_t possible_overlap_end = 0;
    if (!intersection(possible_start, event.start_ns, gap->start_ns,
                      gap->end_ns, &possible_overlap_start,
                      &possible_overlap_end)) {
      continue;
    }
    std::int64_t robust_overlap_start = 0;
    std::int64_t robust_overlap_end = 0;
    if (event.start_ns > robust_start &&
        intersection(robust_start, event.start_ns, gap->start_ns, gap->end_ns,
                     &robust_overlap_start, &robust_overlap_end)) {
      HostEvidenceInterval evidence;
      evidence.device_id = event.device_id;
      evidence.start_ns = robust_overlap_start;
      evidence.end_ns = robust_overlap_end;
      evidence.category = HostEvidenceCategory::kQueuedVisibleTaskDelay;
      evidence.alignment_status = model.alignment_status;
      evidence.reason =
          "unique connectionId links an enqueue API to a later visible task "
          "with a robustly positive delay";
      evidence.source_links.push_back(source_link(
          api.id, task.id, "exact_connection_id", robust_overlap_start,
          robust_overlap_end));
      output->evidence_intervals.push_back(std::move(evidence));
    } else {
      HostIdleCandidateRow candidate;
      candidate.device_id = event.device_id;
      candidate.gap_start_ns = gap->start_ns;
      candidate.gap_end_ns = gap->end_ns;
      candidate.category = HostEvidenceCategory::kQueuedVisibleTaskDelay;
      candidate.candidate_status = HostCandidateStatus::kNonRobustDelay;
      candidate.candidate_relation = "exact_connection_id";
      candidate.alignment_status = model.alignment_status;
      candidate.reason =
          "enqueue-to-task delay is not positive outside clock uncertainty";
      candidate.source_links.push_back(source_link(
          api.id, task.id, "exact_connection_id", possible_overlap_start,
          possible_overlap_end));
      output->candidates.push_back(std::move(candidate));
    }
  }
}

}  // namespace

std::string_view task_api_link_status_name(TaskApiLinkStatus status) {
  switch (status) {
    case TaskApiLinkStatus::kUnique:
      return "unique";
    case TaskApiLinkStatus::kOneToMany:
      return "one_to_many";
    case TaskApiLinkStatus::kAmbiguous:
      return "ambiguous";
    case TaskApiLinkStatus::kUnresolved:
      return "unresolved";
  }
  return "unresolved";
}

std::string_view host_evidence_category_name(HostEvidenceCategory category) {
  switch (category) {
    case HostEvidenceCategory::kQueuedVisibleTaskDelay:
      return "queued_visible_task_delay";
    case HostEvidenceCategory::kHostSyncApiPresent:
      return "host_sync_api_present";
  }
  return "host_sync_api_present";
}

std::string_view host_candidate_status_name(HostCandidateStatus status) {
  switch (status) {
    case HostCandidateStatus::kPossibleOnly:
      return "possible_only";
    case HostCandidateStatus::kNonRobustDelay:
      return "non_robust_delay";
  }
  return "possible_only";
}

HostCorrelationRunResult build_host_correlation(
    const NativeIr& ir,
    const ProductiveTimelineRunResult& productive,
    const ClockAlignmentRunResult& alignment,
    const HostApiRuleset& ruleset) {
  HostCorrelationRunResult output;
  output.host_api_rules_version = ruleset.version();
  output.host_api_rules_sha256 = ruleset.sha256();
  const std::map<HostApiEventId, ApiResolution> resolutions =
      resolve_links(ir, &output.task_api_links);

  for (const HostApiEventRow& api : ir.host_api_events.rows()) {
    const std::optional<HostApiMatch> match =
        ruleset.classify(api_name(ir, api));
    output.host_api_classification.push_back(
        HostCorrelationRunResult::HostApiClassificationRow{
            api.id, match.has_value(),
            match.has_value() ? match->family : HostApiFamily::kHostSync,
            match.has_value() ? match->api_pattern : std::string()});
    if (!match.has_value()) {
      continue;
    }
    const ApiResolution& resolution = resolutions.at(api.id);
    bool has_device_id = api.has_device_id;
    std::uint32_t device_id = api.device_id;
    if (!has_device_id && resolution.status == TaskApiLinkStatus::kUnique &&
        resolution.has_device_id) {
      has_device_id = true;
      device_id = resolution.device_id;
    }
    if (!has_device_id) {
      continue;
    }
    const ClockModel* model = alignment.find_device(device_id);
    if (model == nullptr ||
        !alignment_supports_cross_clock_evidence(model->alignment_status)) {
      continue;
    }
    if (match->family == HostApiFamily::kHostSync) {
      add_host_sync_evidence(api, device_id, *model, productive, &output);
    } else {
      add_enqueue_evidence(ir, api, resolution, *model, productive, &output);
    }
  }
  std::stable_sort(
      output.evidence_intervals.begin(), output.evidence_intervals.end(),
      [](const HostEvidenceInterval& lhs, const HostEvidenceInterval& rhs) {
        return std::tie(lhs.device_id, lhs.start_ns, lhs.end_ns, lhs.category) <
               std::tie(rhs.device_id, rhs.start_ns, rhs.end_ns, rhs.category);
      });
  std::stable_sort(
      output.candidates.begin(), output.candidates.end(),
      [](const HostIdleCandidateRow& lhs, const HostIdleCandidateRow& rhs) {
        return std::tie(lhs.device_id, lhs.gap_start_ns, lhs.gap_end_ns,
                        lhs.category, lhs.candidate_status) <
               std::tie(rhs.device_id, rhs.gap_start_ns, rhs.gap_end_ns,
                        rhs.category, rhs.candidate_status);
      });
  return output;
}

}  // namespace traceloom
