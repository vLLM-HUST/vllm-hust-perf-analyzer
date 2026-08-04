#include "traceloom/analysis/idle_explanation.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace traceloom {
namespace {

struct ActiveState {
  std::uint64_t stream_id = 0;
  const StreamStateInterval* interval = nullptr;
};

bool source_less(const IdleExplanationSourceLink& lhs,
                 const IdleExplanationSourceLink& rhs) {
  if (lhs.stream_id != rhs.stream_id) {
    return lhs.stream_id < rhs.stream_id;
  }
  if (lhs.state != rhs.state) {
    return static_cast<int>(lhs.state) < static_cast<int>(rhs.state);
  }
  if (lhs.source.kind != rhs.source.kind) {
    return static_cast<int>(lhs.source.kind) <
           static_cast<int>(rhs.source.kind);
  }
  if (lhs.source.trace_event_id != rhs.source.trace_event_id) {
    return lhs.source.trace_event_id < rhs.source.trace_event_id;
  }
  if (lhs.source.task_id != rhs.source.task_id) {
    return lhs.source.task_id < rhs.source.task_id;
  }
  if (lhs.source.communication_op_id != rhs.source.communication_op_id) {
    return lhs.source.communication_op_id < rhs.source.communication_op_id;
  }
  if (lhs.source.source_ref_id != rhs.source.source_ref_id) {
    return lhs.source.source_ref_id < rhs.source.source_ref_id;
  }
  return lhs.source.matched_rule_id.value_or(std::string()) <
         rhs.source.matched_rule_id.value_or(std::string());
}

std::vector<IdleExplanationSourceLink> links_for_states(
    const std::vector<ActiveState>& active,
    const std::vector<StreamState>& selected_states) {
  std::vector<IdleExplanationSourceLink> links;
  for (const ActiveState& item : active) {
    if (std::find(selected_states.begin(), selected_states.end(),
                  item.interval->state) == selected_states.end()) {
      continue;
    }
    for (const StreamStateSourceLink& source : item.interval->source_links) {
      links.push_back(
          IdleExplanationSourceLink{item.stream_id, item.interval->state,
                                    source});
    }
  }
  std::sort(links.begin(), links.end(), source_less);
  return links;
}

bool contains_state(const std::vector<ActiveState>& active,
                    StreamState state) {
  return std::any_of(active.begin(), active.end(),
                     [state](const ActiveState& item) {
                       return item.interval->state == state;
                     });
}

IdleExplanationRow explain_slice(
    std::int64_t start_ns,
    std::int64_t end_ns,
    const std::vector<ActiveState>& active,
    bool can_attest_absence) {
  IdleExplanationRow row;
  row.start_ns = start_ns;
  row.end_ns = end_ns;

  if (contains_state(active, StreamState::kRunningWait)) {
    row.category = IdleExplanationCategory::kBlockedByVisibleWait;
    row.evidence_level = IdleEvidenceLevel::kDirect;
    row.evidence_relation = IdleEvidenceRelation::kDeviceEventCoverage;
    row.reason = "covered by a profiler-visible wait task";
    row.source_links = links_for_states(active, {StreamState::kRunningWait});
    return row;
  }
  if (contains_state(active, StreamState::kRunningCaptureControl)) {
    row.category = IdleExplanationCategory::kCaptureControlPresent;
    row.evidence_level = IdleEvidenceLevel::kDirect;
    row.evidence_relation = IdleEvidenceRelation::kDeviceEventCoverage;
    row.reason = "covered by a profiler-visible capture/control task";
    row.source_links =
        links_for_states(active, {StreamState::kRunningCaptureControl});
    return row;
  }
  if (contains_state(active, StreamState::kRunningRecord) ||
      contains_state(active, StreamState::kRunningRuntimeControl)) {
    row.category = IdleExplanationCategory::kRuntimeControlPresent;
    row.evidence_level = IdleEvidenceLevel::kDirect;
    row.evidence_relation = IdleEvidenceRelation::kDeviceEventCoverage;
    row.reason = "covered by a profiler-visible record/runtime-control task";
    row.source_links =
        links_for_states(active, {StreamState::kRunningRecord,
                                  StreamState::kRunningRuntimeControl});
    return row;
  }

  const bool every_observed_stream_empty =
      !active.empty() &&
      std::all_of(active.begin(), active.end(), [](const ActiveState& item) {
        return item.interval->state == StreamState::kEmptyObserved;
      });
  if (can_attest_absence && every_observed_stream_empty) {
    row.category = IdleExplanationCategory::kNoObservedDeviceWork;
    row.evidence_level = IdleEvidenceLevel::kDirect;
    row.evidence_relation = IdleEvidenceRelation::kCompleteAbsenceObservation;
    row.reason = "all streams in the complete observed universe are empty";
    return row;
  }

  row.category = IdleExplanationCategory::kUnattributedVisibleIdle;
  row.evidence_level = IdleEvidenceLevel::kNone;
  row.evidence_relation = IdleEvidenceRelation::kNone;
  row.reason = "insufficient device-side evidence for a stronger category";
  row.source_links = links_for_states(
      active, {StreamState::kUnknown, StreamState::kAmbiguousOverlap});
  return row;
}

bool same_explanation(const IdleExplanationRow& lhs,
                      const IdleExplanationRow& rhs) {
  return lhs.category == rhs.category &&
         lhs.evidence_level == rhs.evidence_level &&
         lhs.evidence_relation == rhs.evidence_relation &&
         lhs.alignment_status == rhs.alignment_status &&
         lhs.reason == rhs.reason && lhs.source_links == rhs.source_links;
}

void append_or_merge(IdleExplanationRow row,
                     std::vector<IdleExplanationRow>* output) {
  if (!output->empty() && output->back().end_ns == row.start_ns &&
      same_explanation(output->back(), row)) {
    output->back().end_ns = row.end_ns;
    return;
  }
  output->push_back(std::move(row));
}

const StreamStateInterval* state_at(const StreamStateTimeline& timeline,
                                    std::int64_t time_ns) {
  const auto found = std::upper_bound(
      timeline.intervals.begin(), timeline.intervals.end(), time_ns,
      [](std::int64_t value, const StreamStateInterval& interval) {
        return value < interval.start_ns;
      });
  if (found == timeline.intervals.begin()) {
    return nullptr;
  }
  const StreamStateInterval& interval = *std::prev(found);
  return interval.start_ns <= time_ns && time_ns < interval.end_ns
             ? &interval
             : nullptr;
}

void explain_gap(const DeviceIntervalRow& gap,
                 const StreamStateDeviceResult& stream_device,
                 CollectionStatus collection_status,
                 bool upstream_inputs_valid,
                 std::vector<TimelineDiagnostic>* diagnostics,
                 std::vector<IdleExplanationRow>* output) {
  std::vector<std::int64_t> boundaries{gap.start_ns, gap.end_ns};
  for (const StreamStateTimeline& timeline : stream_device.timelines) {
    // Timelines are sorted partitions. Start at the first interval whose end
    // exceeds the gap start rather than rescanning the full timeline for every
    // gap (large real profiles contain tens of thousands of gaps).
    auto interval_it = std::lower_bound(
        timeline.intervals.begin(), timeline.intervals.end(), gap.start_ns,
        [](const StreamStateInterval& interval, std::int64_t time_ns) {
          return interval.end_ns <= time_ns;
        });
    for (; interval_it != timeline.intervals.end() &&
           interval_it->start_ns < gap.end_ns;
         ++interval_it) {
      const StreamStateInterval& interval = *interval_it;
      boundaries.push_back(std::max(interval.start_ns, gap.start_ns));
      boundaries.push_back(std::min(interval.end_ns, gap.end_ns));
    }
  }
  std::sort(boundaries.begin(), boundaries.end());
  boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                   boundaries.end());

  const bool can_attest_absence =
      collection_status == CollectionStatus::kComplete &&
      upstream_inputs_valid && stream_device.observed_universe_scan_complete;
  for (std::size_t index = 0; index + 1 < boundaries.size(); ++index) {
    const std::int64_t start_ns = boundaries[index];
    const std::int64_t end_ns = boundaries[index + 1];
    if (end_ns <= start_ns) {
      continue;
    }
    std::vector<ActiveState> active;
    active.reserve(stream_device.timelines.size());
    for (const StreamStateTimeline& timeline : stream_device.timelines) {
      const StreamStateInterval* interval = state_at(timeline, start_ns);
      if (interval == nullptr || interval->end_ns < end_ns) {
        diagnostics->push_back(TimelineDiagnostic{
            "incomplete_stream_partition: stream " +
                std::to_string(timeline.stream_id) +
                " does not cover an idle explanation slice",
            static_cast<std::int64_t>(timeline.stream_id)});
        continue;
      }
      active.push_back(ActiveState{timeline.stream_id, interval});
    }
    // An incomplete active set can never support an absence claim.
    const bool all_streams_scanned =
        active.size() == stream_device.timelines.size();
    if (contains_state(active, StreamState::kRunningCompute) ||
        contains_state(active, StreamState::kRunningComm) ||
        contains_state(active, StreamState::kRunningDataMove)) {
      diagnostics->push_back(TimelineDiagnostic{
          "productive_state_inside_gap: E2/E3 productive coverage disagrees",
          -1});
      IdleExplanationRow inconsistent;
      inconsistent.start_ns = start_ns;
      inconsistent.end_ns = end_ns;
      inconsistent.category =
          IdleExplanationCategory::kUnattributedVisibleIdle;
      inconsistent.evidence_level = IdleEvidenceLevel::kNone;
      inconsistent.evidence_relation = IdleEvidenceRelation::kNone;
      inconsistent.reason =
          "E2/E3 productive coverage disagrees; no explanation promoted";
      inconsistent.source_links = links_for_states(
          active, {StreamState::kRunningCompute, StreamState::kRunningComm,
                   StreamState::kRunningDataMove, StreamState::kRunningWait,
                   StreamState::kRunningCaptureControl,
                   StreamState::kRunningRecord,
                   StreamState::kRunningRuntimeControl,
                   StreamState::kUnknown, StreamState::kAmbiguousOverlap});
      append_or_merge(std::move(inconsistent), output);
      continue;
    }
    append_or_merge(
        explain_slice(start_ns, end_ns, active,
                      can_attest_absence && all_streams_scanned),
        output);
  }
}

}  // namespace

std::string_view idle_explanation_category_name(
    IdleExplanationCategory category) {
  switch (category) {
    case IdleExplanationCategory::kBlockedByVisibleWait:
      return "blocked_by_visible_wait";
    case IdleExplanationCategory::kCaptureControlPresent:
      return "capture_control_present";
    case IdleExplanationCategory::kRuntimeControlPresent:
      return "runtime_control_present";
    case IdleExplanationCategory::kNoObservedDeviceWork:
      return "no_observed_device_work";
    case IdleExplanationCategory::kUnattributedVisibleIdle:
      return "unattributed_visible_idle";
  }
  return "unattributed_visible_idle";
}

std::string_view idle_evidence_level_name(IdleEvidenceLevel level) {
  switch (level) {
    case IdleEvidenceLevel::kDirect:
      return "direct";
    case IdleEvidenceLevel::kNone:
      return "none";
  }
  return "none";
}

std::string_view idle_evidence_relation_name(IdleEvidenceRelation relation) {
  switch (relation) {
    case IdleEvidenceRelation::kDeviceEventCoverage:
      return "device_event_coverage";
    case IdleEvidenceRelation::kCompleteAbsenceObservation:
      return "complete_absence_observation";
    case IdleEvidenceRelation::kNone:
      return "none";
  }
  return "none";
}

std::string_view collection_status_name(CollectionStatus status) {
  switch (status) {
    case CollectionStatus::kComplete:
      return "complete";
    case CollectionStatus::kIncomplete:
      return "incomplete";
    case CollectionStatus::kUnknown:
      return "unknown";
    case CollectionStatus::kInvalid:
      return "invalid";
  }
  return "invalid";
}

IdleExplanationRunResult build_idle_explanations(
    const ProductiveTimelineRunResult& productive,
    const StreamStateRunResult& streams,
    const IdleExplanationOptions& options) {
  IdleExplanationRunResult run;
  run.collection_status = options.collection_status;
  run.status = productive.status == AnalysisStatus::kInvalidInput ||
                       streams.status == AnalysisStatus::kInvalidInput
                   ? AnalysisStatus::kInvalidInput
                   : productive.status;
  const bool upstream_inputs_valid =
      productive.status == AnalysisStatus::kOk &&
      streams.status == AnalysisStatus::kOk;

  std::map<std::uint32_t, const StreamStateDeviceResult*> streams_by_device;
  for (const StreamStateDeviceResult& device : streams.devices) {
    if (!streams_by_device.emplace(device.device_id, &device).second) {
      throw std::invalid_argument(
          "duplicate stream-state device result for device " +
          std::to_string(device.device_id));
    }
  }
  if (productive.devices.size() != streams.devices.size()) {
    throw std::invalid_argument(
        "productive/stream-state device count mismatch");
  }

  for (const DeviceTimelineResult& device : productive.devices) {
    const auto stream_it = streams_by_device.find(device.device_id);
    if (stream_it == streams_by_device.end()) {
      throw std::invalid_argument(
          "missing stream-state result for productive device " +
          std::to_string(device.device_id));
    }
    const StreamStateDeviceResult& stream_device = *stream_it->second;
    if (device.status != stream_device.status ||
        device.span_start_ns != stream_device.span_start_ns ||
        device.span_end_ns != stream_device.span_end_ns) {
      throw std::invalid_argument(
          "productive/stream-state status or span mismatch on device " +
          std::to_string(device.device_id));
    }
    if (stream_device.stream_universe_size !=
        stream_device.timelines.size()) {
      throw std::invalid_argument(
          "stream universe size/timeline count mismatch on device " +
          std::to_string(device.device_id));
    }
    for (const StreamStateTimeline& timeline : stream_device.timelines) {
      if (timeline.device_id != device.device_id ||
          !device.span_start_ns.has_value() ||
          !device.span_end_ns.has_value() ||
          timeline.span_start_ns != *device.span_start_ns ||
          timeline.span_end_ns != *device.span_end_ns ||
          timeline.intervals.empty() ||
          timeline.intervals.front().start_ns != timeline.span_start_ns ||
          timeline.intervals.back().end_ns != timeline.span_end_ns) {
        throw std::invalid_argument(
            "malformed stream timeline on device " +
            std::to_string(device.device_id) + ", stream " +
            std::to_string(timeline.stream_id));
      }
      std::int64_t cursor = timeline.span_start_ns;
      for (const StreamStateInterval& interval : timeline.intervals) {
        if (interval.end_ns <= interval.start_ns ||
            interval.start_ns != cursor) {
          throw std::invalid_argument(
              "non-partitioning stream timeline on device " +
              std::to_string(device.device_id) + ", stream " +
              std::to_string(timeline.stream_id));
        }
        cursor = interval.end_ns;
      }
    }

    IdleExplanationDeviceResult result;
    result.device_id = device.device_id;
    result.status = device.status;
    result.collection_status = options.collection_status;
    if (device.status == AnalysisStatus::kOk) {
      for (const DeviceIntervalRow& interval : device.intervals) {
        if (interval.kind != DeviceIntervalKind::kVisibleProductiveIdle) {
          continue;
        }
        explain_gap(interval, stream_device, options.collection_status,
                    upstream_inputs_valid,
                    &result.diagnostics, &result.explanations);
      }
    }
    run.devices.push_back(std::move(result));
  }
  return run;
}

}  // namespace traceloom
