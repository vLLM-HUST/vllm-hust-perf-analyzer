#include "traceloom/analysis/idle_gap_explanation.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace traceloom {
namespace {

constexpr const char* kAttributionRuleVersion = "idle-gap-partition-v1";

bool source_less(const IdleExplanationSourceLink& lhs,
                 const IdleExplanationSourceLink& rhs) {
  return std::tie(lhs.kind, lhs.trace_event_id, lhs.task_id,
                  lhs.communication_op_id, lhs.source_ref_id,
                  lhs.matched_rule_id, lhs.source_key) <
         std::tie(rhs.kind, rhs.trace_event_id, rhs.task_id,
                  rhs.communication_op_id, rhs.source_ref_id,
                  rhs.matched_rule_id, rhs.source_key);
}

void canonicalize_sources(std::vector<IdleExplanationSourceLink>* links) {
  std::sort(links->begin(), links->end(), source_less);
  links->erase(std::unique(links->begin(), links->end()), links->end());
}

IdleExplanationSourceLink convert_source(
    const StreamStateSourceLink& source) {
  IdleExplanationSourceLink result;
  result.kind = source.kind == StreamStateSourceLink::Kind::kTask
                    ? IdleExplanationSourceLink::Kind::kTask
                    : IdleExplanationSourceLink::Kind::kCommunicationOp;
  result.trace_event_id = source.trace_event_id;
  result.task_id = source.task_id;
  result.communication_op_id = source.communication_op_id;
  result.source_ref_id = source.source_ref_id;
  result.matched_rule_id = source.matched_rule_id;
  return result;
}

bool state_matches_category(StreamState state,
                            IdleExplanationCategory category) {
  switch (category) {
    case IdleExplanationCategory::kBlockedByVisibleWait:
      return state == StreamState::kRunningWait;
    case IdleExplanationCategory::kCaptureControlPresent:
      return state == StreamState::kRunningCaptureControl;
    case IdleExplanationCategory::kRuntimeControlPresent:
      return state == StreamState::kRunningRecord ||
             state == StreamState::kRunningRuntimeControl;
    case IdleExplanationCategory::kQueuedVisibleTaskDelay:
    case IdleExplanationCategory::kHostSyncApiPresent:
    case IdleExplanationCategory::kNoObservedDeviceWork:
    case IdleExplanationCategory::kUnattributedVisibleIdle:
      return false;
  }
  return false;
}

void append_matching_state_sources(
    const StreamStateInterval& interval,
    IdleExplanationCategory category,
    std::vector<IdleExplanationSourceLink>* output) {
  if (interval.state != StreamState::kAmbiguousOverlap) {
    if (!state_matches_category(interval.state, category)) {
      return;
    }
    for (const StreamStateSourceLink& source : interval.source_links) {
      output->push_back(convert_source(source));
    }
    return;
  }

  // E3 deliberately collapses overlapping same-stream events to one
  // ambiguous interval. observed_state on each source preserves enough
  // component semantics for E4's cross-category priority.
  for (const StreamStateSourceLink& source : interval.source_links) {
    if (state_matches_category(source.observed_state, category)) {
      output->push_back(convert_source(source));
    }
  }
}

void append_unknown_sources(
    const StreamStateInterval& interval,
    std::vector<IdleExplanationSourceLink>* output) {
  if (interval.state == StreamState::kUnknown) {
    for (const StreamStateSourceLink& source : interval.source_links) {
      output->push_back(convert_source(source));
    }
    return;
  }
  if (interval.state == StreamState::kAmbiguousOverlap) {
    for (const StreamStateSourceLink& source : interval.source_links) {
      if (source.observed_state == StreamState::kUnknown) {
        output->push_back(convert_source(source));
      }
    }
  }
}

void validate_device_intervals(const DeviceTimelineResult& device) {
  if (device.status != AnalysisStatus::kOk) {
    if (device.span_start_ns.has_value() || device.span_end_ns.has_value() ||
        !device.intervals.empty()) {
      throw std::invalid_argument(
          "non-ok E2 device must not carry a span or intervals");
    }
    return;
  }
  if (!device.span_start_ns.has_value() || !device.span_end_ns.has_value() ||
      *device.span_end_ns <= *device.span_start_ns) {
    throw std::invalid_argument("ok E2 device carries an invalid span");
  }
  if (device.intervals.empty()) {
    throw std::invalid_argument("ok E2 device has no interval partition");
  }
  std::int64_t cursor = *device.span_start_ns;
  for (const DeviceIntervalRow& interval : device.intervals) {
    if (interval.end_ns <= interval.start_ns ||
        interval.start_ns != cursor) {
      throw std::invalid_argument(
          "E2 device intervals are not a positive adjacent partition");
    }
    cursor = interval.end_ns;
  }
  if (cursor != *device.span_end_ns) {
    throw std::invalid_argument("E2 device intervals do not cover the span");
  }
}

void validate_stream_device(const DeviceTimelineResult& productive,
                            const StreamStateDeviceResult& streams) {
  if (productive.device_id != streams.device_id ||
      productive.status != streams.status) {
    throw std::invalid_argument("E2/E3 device identity or status mismatch");
  }
  if (productive.status != AnalysisStatus::kOk) {
    if (streams.span_start_ns.has_value() || streams.span_end_ns.has_value() ||
        !streams.timelines.empty() || streams.stream_universe_size != 0) {
      throw std::invalid_argument(
          "non-ok E3 device must not carry a span or stream universe");
    }
    return;
  }
  if (productive.span_start_ns != streams.span_start_ns ||
      productive.span_end_ns != streams.span_end_ns ||
      streams.stream_universe_size != streams.timelines.size()) {
    throw std::invalid_argument("E2/E3 span or stream-universe mismatch");
  }
  for (const StreamStateTimeline& timeline : streams.timelines) {
    if (timeline.device_id != streams.device_id ||
        timeline.span_start_ns != *streams.span_start_ns ||
        timeline.span_end_ns != *streams.span_end_ns ||
        timeline.intervals.empty()) {
      throw std::invalid_argument("malformed E3 stream timeline metadata");
    }
    std::int64_t cursor = timeline.span_start_ns;
    for (const StreamStateInterval& interval : timeline.intervals) {
      if (interval.end_ns <= interval.start_ns ||
          interval.start_ns != cursor) {
        throw std::invalid_argument(
            "E3 stream intervals are not a positive adjacent partition");
      }
      if (interval.state == StreamState::kEmptyObserved &&
          !interval.source_links.empty()) {
        throw std::invalid_argument("empty_observed carries source links");
      }
      if (interval.state != StreamState::kEmptyObserved &&
          interval.source_links.empty()) {
        throw std::invalid_argument(
            "non-empty E3 state carries no source lineage");
      }
      if (interval.state == StreamState::kAmbiguousOverlap &&
          interval.source_links.size() < 2) {
        throw std::invalid_argument(
            "ambiguous_overlap has fewer than two source links");
      }
      if (interval.state != StreamState::kAmbiguousOverlap &&
          interval.state != StreamState::kEmptyObserved) {
        for (const StreamStateSourceLink& source : interval.source_links) {
          if (source.observed_state != interval.state) {
            throw std::invalid_argument(
                "E3 source component state disagrees with its interval");
          }
        }
      }
      cursor = interval.end_ns;
    }
    if (cursor != timeline.span_end_ns) {
      throw std::invalid_argument("E3 stream intervals do not cover the span");
    }
  }
}

void validate_external_source(const IdleExplanationSourceLink& source) {
  if ((source.kind == IdleExplanationSourceLink::Kind::kHostApi ||
       source.kind == IdleExplanationSourceLink::Kind::kTaskApiLink) &&
      source.source_key.empty()) {
    throw std::invalid_argument(
        "external correlated source must carry a stable source_key");
  }
}

void validate_correlated_evidence(
    const ValidatedCorrelatedEvidenceInterval& evidence,
    const IdleGapExplanationOptions& options) {
  if (evidence.end_ns <= evidence.start_ns) {
    throw std::invalid_argument(
        "correlated evidence interval must have positive duration");
  }
  if (evidence.category !=
          IdleExplanationCategory::kQueuedVisibleTaskDelay &&
      evidence.category != IdleExplanationCategory::kHostSyncApiPresent) {
    throw std::invalid_argument(
        "E4 correlated input may contain only queued-delay or host-sync");
  }
  if ((evidence.alignment_status != AlignmentStatus::kCalibrated &&
       evidence.alignment_status != AlignmentStatus::kSyntheticOnly) ||
      evidence.alignment_status != options.alignment_status) {
    throw std::invalid_argument(
        "correlated evidence requires matching calibrated/synthetic alignment");
  }
  if (evidence.source_links.empty()) {
    throw std::invalid_argument(
        "correlated evidence must preserve at least one source link");
  }
  for (const IdleExplanationSourceLink& source : evidence.source_links) {
    validate_external_source(source);
  }
}

bool absence_claim_allowed(
    const CollectionCompletenessAttestation& collection,
    const StreamStateDeviceResult& streams) {
  return collection.status == CollectionStatus::kComplete &&
         collection.all_discovered_device_shards_imported &&
         collection.all_required_task_tables_readable &&
         collection.no_dropped_events_or_truncated_capture &&
         streams.observed_universe_scan_complete;
}

const StreamStateInterval& interval_covering(
    const StreamStateTimeline& timeline,
    std::size_t* cursor,
    std::int64_t start_ns,
    std::int64_t end_ns) {
  while (*cursor < timeline.intervals.size() &&
         timeline.intervals[*cursor].end_ns <= start_ns) {
    ++*cursor;
  }
  if (*cursor >= timeline.intervals.size()) {
    throw std::invalid_argument("E3 stream partition ended before gap slice");
  }
  const StreamStateInterval& interval = timeline.intervals[*cursor];
  if (interval.start_ns > start_ns || interval.end_ns < end_ns) {
    throw std::invalid_argument("E3 stream partition does not cover gap slice");
  }
  return interval;
}

struct SliceDecision {
  IdleExplanationCategory category =
      IdleExplanationCategory::kUnattributedVisibleIdle;
  EvidenceLevel level = EvidenceLevel::kNone;
  EvidenceRelation relation = EvidenceRelation::kNone;
  AlignmentStatus alignment = AlignmentStatus::kNotRequired;
  const char* reason = "no stronger explanation evidence covers this interval";
  std::vector<IdleExplanationSourceLink> sources;
};

SliceDecision decide_slice(
    std::int64_t start_ns,
    std::int64_t end_ns,
    const StreamStateDeviceResult& stream_device,
    std::vector<std::size_t>* stream_cursors,
    const std::vector<const ValidatedCorrelatedEvidenceInterval*>& correlated,
    const IdleGapExplanationOptions& options,
    bool allow_absence) {
  std::vector<IdleExplanationSourceLink> waits;
  std::vector<IdleExplanationSourceLink> capture;
  std::vector<IdleExplanationSourceLink> runtime;
  std::vector<IdleExplanationSourceLink> unknown;
  bool all_streams_empty = true;

  for (std::size_t index = 0; index < stream_device.timelines.size(); ++index) {
    const StreamStateInterval& interval = interval_covering(
        stream_device.timelines[index], &(*stream_cursors)[index], start_ns,
        end_ns);
    all_streams_empty =
        all_streams_empty && interval.state == StreamState::kEmptyObserved;
    append_matching_state_sources(
        interval, IdleExplanationCategory::kBlockedByVisibleWait, &waits);
    append_matching_state_sources(
        interval, IdleExplanationCategory::kCaptureControlPresent, &capture);
    append_matching_state_sources(
        interval, IdleExplanationCategory::kRuntimeControlPresent, &runtime);
    append_unknown_sources(interval, &unknown);
  }

  std::vector<IdleExplanationSourceLink> queued;
  std::vector<IdleExplanationSourceLink> host_sync;
  AlignmentStatus queued_alignment = AlignmentStatus::kInvalid;
  AlignmentStatus host_alignment = AlignmentStatus::kInvalid;
  for (const ValidatedCorrelatedEvidenceInterval* evidence : correlated) {
    if (evidence->start_ns <= start_ns && evidence->end_ns >= end_ns) {
      std::vector<IdleExplanationSourceLink>* target = nullptr;
      if (evidence->category ==
          IdleExplanationCategory::kQueuedVisibleTaskDelay) {
        target = &queued;
        queued_alignment = evidence->alignment_status;
      } else {
        target = &host_sync;
        host_alignment = evidence->alignment_status;
      }
      target->insert(target->end(), evidence->source_links.begin(),
                     evidence->source_links.end());
    }
  }

  SliceDecision result;
  if (!waits.empty()) {
    result.category = IdleExplanationCategory::kBlockedByVisibleWait;
    result.level = EvidenceLevel::kDirect;
    result.relation = EvidenceRelation::kDeviceEventCoverage;
    result.alignment = AlignmentStatus::kNotRequired;
    result.reason = "profiler-visible wait task covers this interval";
    result.sources = std::move(waits);
  } else if (!capture.empty()) {
    result.category = IdleExplanationCategory::kCaptureControlPresent;
    result.level = EvidenceLevel::kDirect;
    result.relation = EvidenceRelation::kDeviceEventCoverage;
    result.alignment = AlignmentStatus::kNotRequired;
    result.reason = "capture/control task covers this interval";
    result.sources = std::move(capture);
  } else if (!runtime.empty()) {
    result.category = IdleExplanationCategory::kRuntimeControlPresent;
    result.level = EvidenceLevel::kDirect;
    result.relation = EvidenceRelation::kDeviceEventCoverage;
    result.alignment = AlignmentStatus::kNotRequired;
    result.reason = "record or runtime-control task covers this interval";
    result.sources = std::move(runtime);
  } else if (!queued.empty()) {
    result.category = IdleExplanationCategory::kQueuedVisibleTaskDelay;
    result.level = EvidenceLevel::kCorrelated;
    result.relation = EvidenceRelation::kExactConnectionId;
    result.alignment = queued_alignment;
    result.reason =
        "validated unique enqueue-to-visible-task robust delay covers this interval";
    result.sources = std::move(queued);
  } else if (!host_sync.empty()) {
    result.category = IdleExplanationCategory::kHostSyncApiPresent;
    result.level = EvidenceLevel::kCorrelated;
    result.relation = EvidenceRelation::kTemporalOverlap;
    result.alignment = host_alignment;
    result.reason =
        "calibrated host-sync robust-overlap interval covers this interval";
    result.sources = std::move(host_sync);
  } else if (allow_absence && all_streams_empty) {
    result.category = IdleExplanationCategory::kNoObservedDeviceWork;
    result.level = EvidenceLevel::kDirect;
    result.relation = EvidenceRelation::kCompleteAbsenceObservation;
    result.alignment = AlignmentStatus::kNotRequired;
    result.reason =
        "all observed streams are empty and collection completeness is attested";
  } else {
    result.category = IdleExplanationCategory::kUnattributedVisibleIdle;
    result.level = EvidenceLevel::kNone;
    result.relation = EvidenceRelation::kNone;
    result.alignment = options.alignment_status;
    result.sources = std::move(unknown);
  }
  canonicalize_sources(&result.sources);
  return result;
}

bool rows_mergeable(const IdleExplanationRow& lhs,
                    const IdleExplanationRow& rhs) {
  return lhs.gap_interval_index == rhs.gap_interval_index &&
         lhs.end_ns == rhs.start_ns && lhs.category == rhs.category &&
         lhs.evidence_level == rhs.evidence_level &&
         lhs.evidence_relation == rhs.evidence_relation &&
         lhs.alignment_status == rhs.alignment_status &&
         lhs.collection_status == rhs.collection_status &&
         lhs.reason == rhs.reason && lhs.source_links == rhs.source_links;
}

void append_row(IdleExplanationRow row,
                std::vector<IdleExplanationRow>* rows) {
  if (!rows->empty() && rows_mergeable(rows->back(), row)) {
    rows->back().end_ns = row.end_ns;
    return;
  }
  rows->push_back(std::move(row));
}

void explain_gap(
    std::size_t gap_index,
    const DeviceIntervalRow& gap,
    const StreamStateDeviceResult& stream_device,
    const std::vector<const ValidatedCorrelatedEvidenceInterval*>& device_evidence,
    const IdleGapExplanationOptions& options,
    std::vector<IdleExplanationRow>* rows) {
  std::vector<std::int64_t> boundaries{gap.start_ns, gap.end_ns};
  std::vector<std::size_t> stream_cursors;
  stream_cursors.reserve(stream_device.timelines.size());
  for (const StreamStateTimeline& timeline : stream_device.timelines) {
    // End timestamps are strictly increasing in an adjacent partition, so
    // binary-searching the first overlap avoids rescanning the full span for
    // every gap.
    const auto first = std::lower_bound(
        timeline.intervals.begin(), timeline.intervals.end(), gap.start_ns,
        [](const StreamStateInterval& interval, std::int64_t timestamp) {
          return interval.end_ns <= timestamp;
        });
    stream_cursors.push_back(
        static_cast<std::size_t>(first - timeline.intervals.begin()));
    for (auto interval_it = first; interval_it != timeline.intervals.end();
         ++interval_it) {
      const StreamStateInterval& interval = *interval_it;
      if (interval.start_ns >= gap.end_ns) {
        break;
      }
      boundaries.push_back(std::max(interval.start_ns, gap.start_ns));
      boundaries.push_back(std::min(interval.end_ns, gap.end_ns));
    }
  }

  std::vector<const ValidatedCorrelatedEvidenceInterval*> overlapping_evidence;
  for (const ValidatedCorrelatedEvidenceInterval* evidence : device_evidence) {
    if (evidence->start_ns < gap.end_ns &&
        gap.start_ns < evidence->end_ns) {
      overlapping_evidence.push_back(evidence);
      boundaries.push_back(std::max(evidence->start_ns, gap.start_ns));
      boundaries.push_back(std::min(evidence->end_ns, gap.end_ns));
    }
  }
  std::sort(boundaries.begin(), boundaries.end());
  boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                   boundaries.end());

  const bool allow_absence =
      absence_claim_allowed(options.collection, stream_device);
  for (std::size_t index = 0; index + 1 < boundaries.size(); ++index) {
    const std::int64_t start_ns = boundaries[index];
    const std::int64_t end_ns = boundaries[index + 1];
    if (end_ns <= start_ns) {
      continue;
    }
    SliceDecision decision = decide_slice(
        start_ns, end_ns, stream_device, &stream_cursors,
        overlapping_evidence, options, allow_absence);
    IdleExplanationRow row;
    row.gap_interval_index = gap_index;
    row.start_ns = start_ns;
    row.end_ns = end_ns;
    row.category = decision.category;
    row.evidence_level = decision.level;
    row.evidence_relation = decision.relation;
    row.alignment_status = decision.alignment;
    row.collection_status = options.collection.status;
    row.reason = decision.reason;
    row.source_links = std::move(decision.sources);
    append_row(std::move(row), rows);
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
    case IdleExplanationCategory::kQueuedVisibleTaskDelay:
      return "queued_visible_task_delay";
    case IdleExplanationCategory::kHostSyncApiPresent:
      return "host_sync_api_present";
    case IdleExplanationCategory::kNoObservedDeviceWork:
      return "no_observed_device_work";
    case IdleExplanationCategory::kUnattributedVisibleIdle:
      return "unattributed_visible_idle";
  }
  return "unattributed_visible_idle";
}

std::string_view evidence_level_name(EvidenceLevel level) {
  switch (level) {
    case EvidenceLevel::kDirect:
      return "direct";
    case EvidenceLevel::kCorrelated:
      return "correlated";
    case EvidenceLevel::kInferred:
      return "inferred";
    case EvidenceLevel::kNone:
      return "none";
  }
  return "none";
}

std::string_view evidence_relation_name(EvidenceRelation relation) {
  switch (relation) {
    case EvidenceRelation::kDeviceEventCoverage:
      return "device_event_coverage";
    case EvidenceRelation::kCompleteAbsenceObservation:
      return "complete_absence_observation";
    case EvidenceRelation::kExactConnectionId:
      return "exact_connection_id";
    case EvidenceRelation::kTemporalOverlap:
      return "temporal_overlap";
    case EvidenceRelation::kPatternContext:
      return "pattern_context";
    case EvidenceRelation::kNone:
      return "none";
  }
  return "none";
}

std::string_view alignment_status_name(AlignmentStatus status) {
  switch (status) {
    case AlignmentStatus::kNotRequired:
      return "not_required";
    case AlignmentStatus::kCalibrated:
      return "calibrated";
    case AlignmentStatus::kSyntheticOnly:
      return "synthetic_only";
    case AlignmentStatus::kUncalibrated:
      return "uncalibrated";
    case AlignmentStatus::kInvalid:
      return "invalid";
  }
  return "invalid";
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

IdleExplanationRunResult build_idle_gap_explanations(
    const ProductiveTimelineRunResult& productive,
    const StreamStateRunResult& streams,
    const IdleGapExplanationOptions& options) {
  for (const ValidatedCorrelatedEvidenceInterval& evidence :
       options.correlated_evidence) {
    validate_correlated_evidence(evidence, options);
  }

  std::map<std::uint32_t, const StreamStateDeviceResult*> stream_by_device;
  for (const StreamStateDeviceResult& device : streams.devices) {
    if (!stream_by_device.emplace(device.device_id, &device).second) {
      throw std::invalid_argument("duplicate device in E3 result");
    }
  }
  if (stream_by_device.size() != productive.devices.size()) {
    throw std::invalid_argument("E2/E3 device set mismatch");
  }

  std::map<std::uint32_t,
           std::vector<const ValidatedCorrelatedEvidenceInterval*>>
      evidence_by_device;
  for (const ValidatedCorrelatedEvidenceInterval& evidence :
       options.correlated_evidence) {
    evidence_by_device[evidence.device_id].push_back(&evidence);
  }

  IdleExplanationRunResult run;
  run.status = streams.status;
  run.attribution_rule_version = kAttributionRuleVersion;
  for (const DeviceTimelineResult& productive_device : productive.devices) {
    validate_device_intervals(productive_device);
    const auto stream_it = stream_by_device.find(productive_device.device_id);
    if (stream_it == stream_by_device.end()) {
      throw std::invalid_argument("E2 device missing from E3 result");
    }
    const StreamStateDeviceResult& stream_device = *stream_it->second;
    validate_stream_device(productive_device, stream_device);

    IdleExplanationDeviceResult result;
    result.device_id = productive_device.device_id;
    result.status = productive_device.status;
    result.span_start_ns = productive_device.span_start_ns;
    result.span_end_ns = productive_device.span_end_ns;
    if (result.status == AnalysisStatus::kOk) {
      const auto evidence_it = evidence_by_device.find(result.device_id);
      static const std::vector<const ValidatedCorrelatedEvidenceInterval*>
          kNoEvidence;
      const auto& device_evidence =
          evidence_it == evidence_by_device.end() ? kNoEvidence
                                                  : evidence_it->second;
      for (std::size_t index = 0;
           index < productive_device.intervals.size(); ++index) {
        const DeviceIntervalRow& interval = productive_device.intervals[index];
        if (interval.kind == DeviceIntervalKind::kVisibleProductiveIdle) {
          explain_gap(index, interval, stream_device, device_evidence, options,
                      &result.rows);
        }
      }
    }
    run.devices.push_back(std::move(result));
  }

  // Evidence for a nonexistent logical device is almost certainly a scope
  // error; rejecting it prevents accidental cross-device attribution.
  for (const auto& item : evidence_by_device) {
    bool found = false;
    for (const DeviceTimelineResult& device : productive.devices) {
      found = found || device.device_id == item.first;
    }
    if (!found) {
      throw std::invalid_argument(
          "correlated evidence references a device outside E2/E3 results");
    }
  }
  return run;
}

}  // namespace traceloom
