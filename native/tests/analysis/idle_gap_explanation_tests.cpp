#include "traceloom/analysis/idle_gap_explanation.h"
#include "traceloom/testing/test_util.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

using traceloom::testing::require;

namespace {

using namespace traceloom;

StreamStateSourceLink task_source(std::uint32_t id, StreamState state) {
  return StreamStateSourceLink{
      StreamStateSourceLink::Kind::kTask,
      TraceEventId(id),
      TaskId(id),
      CommunicationOpId::invalid(),
      SourceRefId(id),
      std::string("rule.") + std::to_string(id),
      state};
}

StreamStateInterval state_interval(std::int64_t start_ns,
                                   std::int64_t end_ns,
                                   StreamState state,
                                   std::uint32_t source_id = 0) {
  StreamStateInterval interval{start_ns, end_ns, state, {}};
  if (state != StreamState::kEmptyObserved) {
    interval.source_links.push_back(task_source(source_id, state));
  }
  return interval;
}

ProductiveTimelineRunResult one_gap_productive(std::int64_t start_ns = 0,
                                               std::int64_t end_ns = 100) {
  ProductiveTimelineRunResult run;
  DeviceTimelineResult device;
  device.device_id = 0;
  device.status = AnalysisStatus::kOk;
  device.span_start_ns = start_ns;
  device.span_end_ns = end_ns;
  device.intervals.push_back(DeviceIntervalRow{
      start_ns, end_ns, DeviceIntervalKind::kVisibleProductiveIdle, {}});
  run.devices.push_back(std::move(device));
  return run;
}

StreamStateRunResult stream_result(
    const std::vector<StreamStateTimeline>& timelines,
    bool scan_complete = true,
    std::int64_t start_ns = 0,
    std::int64_t end_ns = 100) {
  StreamStateRunResult run;
  StreamStateDeviceResult device;
  device.device_id = 0;
  device.status = AnalysisStatus::kOk;
  device.span_start_ns = start_ns;
  device.span_end_ns = end_ns;
  device.timelines = timelines;
  device.stream_universe_size = timelines.size();
  device.observed_universe_scan_complete = scan_complete;
  run.devices.push_back(std::move(device));
  run.stream_universe_size = timelines.size();
  run.observed_universe_scan_complete = scan_complete;
  return run;
}

StreamStateTimeline timeline(
    std::uint64_t stream_id,
    std::vector<StreamStateInterval> intervals,
    std::int64_t start_ns = 0,
    std::int64_t end_ns = 100) {
  StreamStateTimeline result;
  result.device_id = 0;
  result.stream_id = stream_id;
  result.span_start_ns = start_ns;
  result.span_end_ns = end_ns;
  result.intervals = std::move(intervals);
  return result;
}

IdleExplanationSourceLink external_source(
    IdleExplanationSourceLink::Kind kind,
    const char* key) {
  IdleExplanationSourceLink result;
  result.kind = kind;
  result.source_key = key;
  return result;
}

CollectionCompletenessAttestation complete_collection() {
  CollectionCompletenessAttestation result;
  result.status = CollectionStatus::kComplete;
  result.all_discovered_device_shards_imported = true;
  result.all_required_task_tables_readable = true;
  result.no_dropped_events_or_truncated_capture = true;
  return result;
}

DeviceTimelineResult gap_device(std::uint32_t device_id,
                                std::int64_t start_ns = 0,
                                std::int64_t end_ns = 10) {
  DeviceTimelineResult result;
  result.device_id = device_id;
  result.status = AnalysisStatus::kOk;
  result.span_start_ns = start_ns;
  result.span_end_ns = end_ns;
  result.intervals.push_back(DeviceIntervalRow{
      start_ns, end_ns, DeviceIntervalKind::kVisibleProductiveIdle, {}});
  return result;
}

StreamStateDeviceResult empty_stream_device(std::uint32_t device_id,
                                            bool scan_complete,
                                            std::int64_t start_ns = 0,
                                            std::int64_t end_ns = 10) {
  StreamStateDeviceResult result;
  result.device_id = device_id;
  result.status = AnalysisStatus::kOk;
  result.span_start_ns = start_ns;
  result.span_end_ns = end_ns;
  result.timelines.push_back(
      timeline(0, {state_interval(start_ns, end_ns,
                                  StreamState::kEmptyObserved)},
               start_ns, end_ns));
  result.timelines.back().device_id = device_id;
  result.stream_universe_size = 1;
  result.observed_universe_scan_complete = scan_complete;
  return result;
}

bool throws_invalid_argument(const std::function<void()>& fn) {
  try {
    fn();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

void check_gap_partition(const IdleExplanationDeviceResult& device,
                         std::int64_t start_ns,
                         std::int64_t end_ns) {
  require(!device.rows.empty(), "gap explanation is non-empty");
  std::int64_t cursor = start_ns;
  for (const IdleExplanationRow& row : device.rows) {
    require(row.start_ns == cursor, "explanation slices are adjacent");
    require(row.end_ns > row.start_ns, "explanation slice is positive");
    cursor = row.end_ns;
  }
  require(cursor == end_ns, "explanation union equals the gap");
}

}  // namespace

int main() {
  using namespace traceloom;

  // Frozen external spellings are part of the output contract.
  require(idle_explanation_category_name(
              IdleExplanationCategory::kBlockedByVisibleWait) ==
              "blocked_by_visible_wait",
          "wait category spelling");
  require(idle_explanation_category_name(
              IdleExplanationCategory::kCaptureControlPresent) ==
              "capture_control_present",
          "capture category spelling");
  require(idle_explanation_category_name(
              IdleExplanationCategory::kRuntimeControlPresent) ==
              "runtime_control_present",
          "runtime category spelling");
  require(idle_explanation_category_name(
              IdleExplanationCategory::kQueuedVisibleTaskDelay) ==
              "queued_visible_task_delay",
          "queued category spelling");
  require(idle_explanation_category_name(
              IdleExplanationCategory::kHostSyncApiPresent) ==
              "host_sync_api_present",
          "host category spelling");
  require(idle_explanation_category_name(
              IdleExplanationCategory::kNoObservedDeviceWork) ==
              "no_observed_device_work",
          "absence category spelling");
  require(idle_explanation_category_name(
              IdleExplanationCategory::kUnattributedVisibleIdle) ==
              "unattributed_visible_idle",
          "residual category spelling");
  require(evidence_level_name(EvidenceLevel::kDirect) == "direct" &&
              evidence_level_name(EvidenceLevel::kCorrelated) ==
                  "correlated" &&
              evidence_level_name(EvidenceLevel::kInferred) == "inferred" &&
              evidence_level_name(EvidenceLevel::kNone) == "none",
          "evidence level spellings");
  require(evidence_relation_name(EvidenceRelation::kDeviceEventCoverage) ==
                  "device_event_coverage" &&
              evidence_relation_name(
                  EvidenceRelation::kCompleteAbsenceObservation) ==
                  "complete_absence_observation" &&
              evidence_relation_name(EvidenceRelation::kExactConnectionId) ==
                  "exact_connection_id" &&
              evidence_relation_name(EvidenceRelation::kTemporalOverlap) ==
                  "temporal_overlap" &&
              evidence_relation_name(EvidenceRelation::kPatternContext) ==
                  "pattern_context" &&
              evidence_relation_name(EvidenceRelation::kNone) == "none",
          "evidence relation spellings");

  // One gap exercises all seven categories. Broad lower-priority evidence
  // overlaps stronger device evidence, proving priority and clipping rather
  // than merely testing seven disjoint inputs.
  {
    const ProductiveTimelineRunResult productive = one_gap_productive();
    const StreamStateTimeline stream0 = timeline(
        0,
        {state_interval(0, 20, StreamState::kRunningWait, 1),
         state_interval(20, 35, StreamState::kRunningCaptureControl, 2),
         state_interval(35, 50, StreamState::kRunningRecord, 3),
         state_interval(50, 70, StreamState::kEmptyObserved),
         state_interval(70, 80, StreamState::kUnknown, 4),
         state_interval(80, 100, StreamState::kEmptyObserved)});
    const StreamStateTimeline stream1 = timeline(
        1, {state_interval(0, 100, StreamState::kEmptyObserved)});
    const StreamStateRunResult streams = stream_result({stream0, stream1});

    IdleGapExplanationOptions options;
    options.collection = complete_collection();
    options.alignment_status = AlignmentStatus::kSyntheticOnly;
    options.correlated_evidence.push_back(
        ValidatedCorrelatedEvidenceInterval{
            0,
            10,
            60,
            IdleExplanationCategory::kQueuedVisibleTaskDelay,
            CorrelatedEvidenceProof::kUniqueExactConnectionRobustDelay,
            AlignmentStatus::kSyntheticOnly,
            {external_source(IdleExplanationSourceLink::Kind::kTaskApiLink,
                             "TASK_API_LINK:1")}});
    options.correlated_evidence.push_back(
        ValidatedCorrelatedEvidenceInterval{
            0,
            0,
            70,
            IdleExplanationCategory::kHostSyncApiPresent,
            CorrelatedEvidenceProof::kRobustTemporalOverlap,
            AlignmentStatus::kSyntheticOnly,
            {external_source(IdleExplanationSourceLink::Kind::kHostApi,
                             "CANN_API:2")}});

    const IdleExplanationRunResult run =
        build_idle_gap_explanations(productive, streams, options);
    require(run.status == AnalysisStatus::kOk, "E4 run status");
    require(run.attribution_rule_version == "idle-gap-partition-v1",
            "attribution version");
    require(run.devices.size() == 1, "one E4 device");
    const IdleExplanationDeviceResult& device = run.devices[0];
    require(device.rows.size() == 7, "all seven explanation slices");
    check_gap_partition(device, 0, 100);

    const std::vector<std::int64_t> ends{20, 35, 50, 60, 70, 80, 100};
    const std::vector<IdleExplanationCategory> categories{
        IdleExplanationCategory::kBlockedByVisibleWait,
        IdleExplanationCategory::kCaptureControlPresent,
        IdleExplanationCategory::kRuntimeControlPresent,
        IdleExplanationCategory::kQueuedVisibleTaskDelay,
        IdleExplanationCategory::kHostSyncApiPresent,
        IdleExplanationCategory::kUnattributedVisibleIdle,
        IdleExplanationCategory::kNoObservedDeviceWork};
    for (std::size_t index = 0; index < categories.size(); ++index) {
      require(device.rows[index].end_ns == ends[index], "exact slice boundary");
      require(device.rows[index].category == categories[index],
              "fixed category priority");
      require(device.rows[index].gap_interval_index == 0,
              "gap interval identity");
      require(device.rows[index].collection_status ==
                  CollectionStatus::kComplete,
              "collection status copied");
    }
    require(device.rows[0].evidence_level == EvidenceLevel::kDirect &&
                device.rows[0].evidence_relation ==
                    EvidenceRelation::kDeviceEventCoverage &&
                device.rows[0].alignment_status ==
                    AlignmentStatus::kNotRequired,
            "direct wait evidence tuple");
    require(device.rows[3].evidence_level == EvidenceLevel::kCorrelated &&
                device.rows[3].evidence_relation ==
                    EvidenceRelation::kExactConnectionId &&
                device.rows[3].alignment_status ==
                    AlignmentStatus::kSyntheticOnly,
            "queued evidence tuple");
    require(device.rows[4].evidence_relation ==
                EvidenceRelation::kTemporalOverlap,
            "host evidence tuple");
    require(device.rows[5].evidence_level == EvidenceLevel::kNone &&
                device.rows[5].evidence_relation == EvidenceRelation::kNone &&
                device.rows[5].source_links.size() == 1 &&
                device.rows[5].source_links[0].task_id == TaskId(4),
            "unknown stays residual with diagnostic lineage");
    require(device.rows[6].evidence_level == EvidenceLevel::kDirect &&
                device.rows[6].evidence_relation ==
                    EvidenceRelation::kCompleteAbsenceObservation &&
                device.rows[6].source_links.empty(),
            "absence evidence tuple");
  }

  // Same-stream ambiguity preserves component roles. Wait wins over capture
  // and only the selected wait source becomes explanation evidence.
  {
    const ProductiveTimelineRunResult productive = one_gap_productive(0, 10);
    StreamStateInterval ambiguous{0, 10, StreamState::kAmbiguousOverlap, {}};
    ambiguous.source_links.push_back(
        task_source(10, StreamState::kRunningCaptureControl));
    ambiguous.source_links.push_back(
        task_source(11, StreamState::kRunningWait));
    const StreamStateRunResult streams =
        stream_result({timeline(0, {ambiguous}, 0, 10)}, true, 0, 10);
    const IdleExplanationRunResult run =
        build_idle_gap_explanations(productive, streams);
    require(run.devices[0].rows.size() == 1 &&
                run.devices[0].rows[0].category ==
                    IdleExplanationCategory::kBlockedByVisibleWait,
            "wait wins inside ambiguous overlap");
    require(run.devices[0].rows[0].source_links.size() == 1 &&
                run.devices[0].rows[0].source_links[0].task_id == TaskId(11),
            "selected ambiguous component lineage only");
  }

  // Empty stream states are residual unless every collection attestation
  // gate passes. E3 scan incompleteness independently vetoes absence.
  {
    const ProductiveTimelineRunResult productive = one_gap_productive(0, 10);
    const StreamStateTimeline empty =
        timeline(0, {state_interval(0, 10, StreamState::kEmptyObserved)}, 0,
                 10);
    IdleGapExplanationOptions options;
    options.collection = complete_collection();
    options.collection.all_required_task_tables_readable = false;
    IdleExplanationRunResult run = build_idle_gap_explanations(
        productive, stream_result({empty}, true, 0, 10), options);
    require(run.devices[0].rows[0].category ==
                IdleExplanationCategory::kUnattributedVisibleIdle,
            "partial attestation vetoes absence");

    options.collection = complete_collection();
    run = build_idle_gap_explanations(
        productive, stream_result({empty}, false, 0, 10), options);
    require(run.devices[0].rows[0].category ==
                IdleExplanationCategory::kUnattributedVisibleIdle,
            "E3 incomplete scan vetoes absence");

    run = build_idle_gap_explanations(
        productive, stream_result({empty}, true, 0, 10), options);
    require(run.devices[0].rows[0].category ==
                IdleExplanationCategory::kNoObservedDeviceWork,
            "complete attestation enables absence");
  }

  // Per-device damage does not poison a healthy sibling device, while
  // device-unattributable run damage vetoes absence for every device.
  {
    ProductiveTimelineRunResult productive;
    productive.devices.push_back(gap_device(0));
    productive.devices.push_back(gap_device(1));

    StreamStateRunResult streams;
    streams.devices.push_back(empty_stream_device(0, true));
    streams.devices.push_back(empty_stream_device(1, false));
    streams.stream_universe_size = 2;
    streams.observed_universe_scan_complete = false;

    IdleGapExplanationOptions options;
    options.collection = complete_collection();
    IdleExplanationRunResult run =
        build_idle_gap_explanations(productive, streams, options);
    require(run.devices[0].rows[0].category ==
                IdleExplanationCategory::kNoObservedDeviceWork,
            "other device damage does not veto healthy device absence");
    require(run.devices[1].rows[0].category ==
                IdleExplanationCategory::kUnattributedVisibleIdle,
            "damaged device cannot claim absence");

    streams.diagnostics.push_back(TimelineDiagnostic{
        "invalid_trace_event_reference: device cannot be resolved", -1});
    streams.status = AnalysisStatus::kInvalidInput;
    run = build_idle_gap_explanations(productive, streams, options);
    require(run.devices[0].rows[0].category ==
                IdleExplanationCategory::kUnattributedVisibleIdle,
            "device-unattributable run damage vetoes healthy device absence");
  }

  // Either upstream stage may skip damaged input while retaining valid
  // per-device partitions. invalid_input must propagate and independently
  // veto absence even when every observed stream is empty.
  {
    ProductiveTimelineRunResult productive = one_gap_productive(0, 10);
    productive.status = AnalysisStatus::kInvalidInput;
    StreamStateRunResult streams = stream_result(
        {timeline(0, {state_interval(0, 10, StreamState::kEmptyObserved)}, 0,
                  10)},
        true, 0, 10);
    IdleGapExplanationOptions options;
    options.collection = complete_collection();
    const IdleExplanationRunResult run =
        build_idle_gap_explanations(productive, streams, options);
    require(run.status == AnalysisStatus::kInvalidInput,
            "E2 invalid_input propagates to E4 run status");
    require(run.devices[0].rows.size() == 1 &&
                run.devices[0].rows[0].category ==
                    IdleExplanationCategory::kUnattributedVisibleIdle,
            "E2 invalid_input vetoes no_observed_device_work");

    productive.status = AnalysisStatus::kOk;
    streams.status = AnalysisStatus::kInvalidInput;
    const IdleExplanationRunResult e3_invalid_run =
        build_idle_gap_explanations(productive, streams, options);
    require(e3_invalid_run.status == AnalysisStatus::kInvalidInput,
            "E3 invalid_input propagates to E4 run status");
    require(e3_invalid_run.devices[0].rows[0].category ==
                IdleExplanationCategory::kUnattributedVisibleIdle,
            "E3 invalid_input vetoes no_observed_device_work without diagnostics");
  }

  // An E2 gap cannot intersect any E3 productive component. Reject both a
  // direct productive state and an ambiguous interval containing one.
  {
    const ProductiveTimelineRunResult productive = one_gap_productive(0, 10);
    StreamStateRunResult streams = stream_result(
        {timeline(0,
                  {state_interval(0, 10, StreamState::kRunningCompute, 1)},
                  0, 10)},
        true, 0, 10);
    require(throws_invalid_argument([&]() {
              (void)build_idle_gap_explanations(productive, streams);
            }),
            "E2 gap plus E3 running_compute is rejected");

    StreamStateInterval ambiguous{0, 10, StreamState::kAmbiguousOverlap, {}};
    ambiguous.source_links.push_back(
        task_source(2, StreamState::kRunningCompute));
    ambiguous.source_links.push_back(
        task_source(3, StreamState::kRunningWait));
    streams = stream_result({timeline(0, {ambiguous}, 0, 10)}, true, 0, 10);
    require(throws_invalid_argument([&]() {
              (void)build_idle_gap_explanations(productive, streams);
            }),
            "E2 gap plus ambiguous productive component is rejected");
  }

  // Productive intervals never receive explanations; gap linkage uses the
  // source DeviceIntervalRow index and each distinct gap is covered exactly.
  {
    ProductiveTimelineRunResult productive;
    DeviceTimelineResult device;
    device.device_id = 0;
    device.status = AnalysisStatus::kOk;
    device.span_start_ns = 0;
    device.span_end_ns = 30;
    device.intervals = {
        DeviceIntervalRow{0, 10, DeviceIntervalKind::kProductiveActive, {}},
        DeviceIntervalRow{10, 20,
                          DeviceIntervalKind::kVisibleProductiveIdle, {}},
        DeviceIntervalRow{20, 25, DeviceIntervalKind::kProductiveActive, {}},
        DeviceIntervalRow{25, 30,
                          DeviceIntervalKind::kVisibleProductiveIdle, {}}};
    productive.devices.push_back(std::move(device));
    const StreamStateRunResult streams = stream_result(
        {timeline(0,
                  {state_interval(0, 10, StreamState::kRunningCompute, 1),
                   state_interval(10, 20, StreamState::kRunningWait, 2),
                   state_interval(20, 25, StreamState::kRunningCompute, 3),
                   state_interval(25, 30, StreamState::kEmptyObserved)},
                  0, 30)},
        true, 0, 30);
    const IdleExplanationRunResult run =
        build_idle_gap_explanations(productive, streams);
    require(run.devices[0].rows.size() == 2, "only gaps explained");
    require(run.devices[0].rows[0].gap_interval_index == 1 &&
                run.devices[0].rows[0].start_ns == 10 &&
                run.devices[0].rows[0].end_ns == 20,
            "first gap linkage");
    require(run.devices[0].rows[1].gap_interval_index == 3 &&
                run.devices[0].rows[1].start_ns == 25 &&
                run.devices[0].rows[1].end_ns == 30,
            "second gap linkage");
  }

  // Correlated evidence cannot bypass its upstream robustness/alignment or
  // frozen category gates.
  {
    const ProductiveTimelineRunResult productive = one_gap_productive(0, 10);
    const StreamStateRunResult streams = stream_result(
        {timeline(0, {state_interval(0, 10, StreamState::kEmptyObserved)}, 0,
                  10)},
        true, 0, 10);
    IdleGapExplanationOptions options;
    options.alignment_status = AlignmentStatus::kUncalibrated;
    options.correlated_evidence.push_back(
        ValidatedCorrelatedEvidenceInterval{
            0,
            0,
            10,
            IdleExplanationCategory::kHostSyncApiPresent,
            CorrelatedEvidenceProof::kRobustTemporalOverlap,
            AlignmentStatus::kUncalibrated,
            {external_source(IdleExplanationSourceLink::Kind::kHostApi,
                             "CANN_API:1")}});
    require(throws_invalid_argument([&]() {
              (void)build_idle_gap_explanations(productive, streams, options);
            }),
            "uncalibrated correlated evidence rejected");

    options.alignment_status = AlignmentStatus::kCalibrated;
    options.correlated_evidence[0].alignment_status =
        AlignmentStatus::kCalibrated;
    options.correlated_evidence[0].category =
        IdleExplanationCategory::kBlockedByVisibleWait;
    require(throws_invalid_argument([&]() {
              (void)build_idle_gap_explanations(productive, streams, options);
            }),
            "direct category injection rejected");

    options.correlated_evidence[0].category =
        IdleExplanationCategory::kQueuedVisibleTaskDelay;
    options.correlated_evidence[0].proof =
        CorrelatedEvidenceProof::kUniqueExactConnectionRobustDelay;
    options.correlated_evidence[0].source_links = {
        external_source(IdleExplanationSourceLink::Kind::kTask, "TASK:1")};
    require(throws_invalid_argument([&]() {
              (void)build_idle_gap_explanations(productive, streams, options);
            }),
            "queued evidence without task-api link source rejected");

    options.correlated_evidence[0].category =
        IdleExplanationCategory::kHostSyncApiPresent;
    options.correlated_evidence[0].proof =
        CorrelatedEvidenceProof::kUniqueExactConnectionRobustDelay;
    options.correlated_evidence[0].source_links = {
        external_source(IdleExplanationSourceLink::Kind::kHostApi,
                        "CANN_API:1")};
    require(throws_invalid_argument([&]() {
              (void)build_idle_gap_explanations(productive, streams, options);
            }),
            "category/proof mismatch rejected");

    options.correlated_evidence[0].proof =
        CorrelatedEvidenceProof::kRobustTemporalOverlap;
    options.correlated_evidence[0].source_links = {
        external_source(IdleExplanationSourceLink::Kind::kTaskApiLink,
                        "TASK_API_LINK:1")};
    require(throws_invalid_argument([&]() {
              (void)build_idle_gap_explanations(productive, streams, options);
            }),
            "host-sync evidence without host API source rejected");
  }

  // Host synchronization evidence cannot invent a gap when the E2 span is
  // fully productive (the contract's host-wait/zero-visible-idle boundary).
  {
    ProductiveTimelineRunResult productive;
    DeviceTimelineResult productive_device;
    productive_device.device_id = 0;
    productive_device.status = AnalysisStatus::kOk;
    productive_device.span_start_ns = 0;
    productive_device.span_end_ns = 10;
    productive_device.intervals.push_back(DeviceIntervalRow{
        0, 10, DeviceIntervalKind::kProductiveActive, {}});
    productive.devices.push_back(std::move(productive_device));
    const StreamStateRunResult streams = stream_result(
        {timeline(0,
                  {state_interval(0, 10, StreamState::kRunningCompute, 1)},
                  0, 10)},
        true, 0, 10);
    IdleGapExplanationOptions options;
    options.alignment_status = AlignmentStatus::kCalibrated;
    options.correlated_evidence.push_back(
        ValidatedCorrelatedEvidenceInterval{
            0,
            0,
            10,
            IdleExplanationCategory::kHostSyncApiPresent,
            CorrelatedEvidenceProof::kRobustTemporalOverlap,
            AlignmentStatus::kCalibrated,
            {external_source(IdleExplanationSourceLink::Kind::kHostApi,
                             "CANN_API:host_wait")}});
    const IdleExplanationRunResult run =
        build_idle_gap_explanations(productive, streams, options);
    require(run.devices.size() == 1 && run.devices[0].rows.empty(),
            "fully productive span stays explanation-free despite host wait");
  }

  // Non-ok stage results propagate without inventing rows.
  {
    ProductiveTimelineRunResult productive;
    DeviceTimelineResult productive_device;
    productive_device.device_id = 0;
    productive_device.status = AnalysisStatus::kNoProductiveSpan;
    productive.devices.push_back(productive_device);
    StreamStateRunResult streams;
    StreamStateDeviceResult stream_device;
    stream_device.device_id = 0;
    stream_device.status = AnalysisStatus::kNoProductiveSpan;
    streams.devices.push_back(stream_device);
    const IdleExplanationRunResult run =
        build_idle_gap_explanations(productive, streams);
    require(run.devices.size() == 1 && run.devices[0].rows.empty() &&
                run.devices[0].status == AnalysisStatus::kNoProductiveSpan,
            "non-ok result propagates without slices");
  }

  return 0;
}
