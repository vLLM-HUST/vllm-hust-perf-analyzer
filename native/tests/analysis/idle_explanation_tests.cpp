#include "traceloom/analysis/idle_explanation.h"
#include "traceloom/testing/test_util.h"

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

using traceloom::testing::require;

namespace {

using namespace traceloom;

StreamStateSourceLink source(std::uint32_t event_id,
                             std::uint32_t task_id,
                             StreamState observed_state =
                                 StreamState::kUnknown) {
  return StreamStateSourceLink{
      StreamStateSourceLink::Kind::kTask, TraceEventId(event_id),
      TaskId(task_id), CommunicationOpId::invalid(), SourceRefId(event_id),
      std::string("fixture-rule"), observed_state};
}

StreamStateInterval state(std::int64_t start_ns,
                          std::int64_t end_ns,
                          StreamState value,
                          std::vector<StreamStateSourceLink> links = {}) {
  return StreamStateInterval{start_ns, end_ns, value, std::move(links)};
}

struct Inputs {
  ProductiveTimelineRunResult productive;
  StreamStateRunResult streams;
};

Inputs make_inputs(bool scan_complete = true) {
  Inputs input;
  DeviceTimelineResult device;
  device.device_id = 7;
  device.status = AnalysisStatus::kOk;
  device.span_start_ns = 0;
  device.span_end_ns = 100;
  device.intervals = {
      DeviceIntervalRow{0, 10, DeviceIntervalKind::kProductiveActive, {}},
      DeviceIntervalRow{10, 90,
                        DeviceIntervalKind::kVisibleProductiveIdle, {}},
      DeviceIntervalRow{90, 100, DeviceIntervalKind::kProductiveActive, {}}};
  input.productive.devices.push_back(device);

  StreamStateTimeline first;
  first.device_id = 7;
  first.stream_id = 11;
  first.span_start_ns = 0;
  first.span_end_ns = 100;
  first.intervals = {
      state(0, 20, StreamState::kEmptyObserved),
      state(20, 40, StreamState::kRunningWait, {source(1, 1)}),
      state(40, 60, StreamState::kRunningCaptureControl, {source(2, 2)}),
      state(60, 70, StreamState::kRunningRecord, {source(3, 3)}),
      state(70, 80, StreamState::kUnknown, {source(4, 4)}),
      state(80, 90, StreamState::kAmbiguousOverlap,
            {source(5, 5), source(6, 6)}),
      state(90, 100, StreamState::kRunningCompute, {source(7, 7)})};

  StreamStateTimeline second;
  second.device_id = 7;
  second.stream_id = 22;
  second.span_start_ns = 0;
  second.span_end_ns = 100;
  second.intervals = {
      state(0, 30, StreamState::kEmptyObserved),
      state(30, 50, StreamState::kRunningRuntimeControl, {source(8, 8)}),
      state(50, 90, StreamState::kEmptyObserved),
      state(90, 100, StreamState::kEmptyObserved)};

  StreamStateDeviceResult stream_device;
  stream_device.device_id = 7;
  stream_device.status = AnalysisStatus::kOk;
  stream_device.span_start_ns = 0;
  stream_device.span_end_ns = 100;
  stream_device.timelines = {first, second};
  stream_device.stream_universe_size = 2;
  stream_device.observed_universe_scan_complete = scan_complete;
  input.streams.devices.push_back(std::move(stream_device));
  input.streams.stream_universe_size = 2;
  input.streams.observed_universe_scan_complete = scan_complete;
  return input;
}

std::int64_t total_duration(const IdleExplanationDeviceResult& device) {
  std::int64_t total = 0;
  for (const IdleExplanationRow& row : device.explanations) {
    require(row.end_ns > row.start_ns, "explanation interval is positive");
    total += row.end_ns - row.start_ns;
  }
  return total;
}

bool throws_invalid_argument(const std::function<void()>& callback) {
  try {
    callback();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

}  // namespace

int main() {
  using namespace traceloom;

  require(idle_explanation_category_name(
              IdleExplanationCategory::kBlockedByVisibleWait) ==
              "blocked_by_visible_wait" &&
              idle_explanation_category_name(
                  IdleExplanationCategory::kCaptureControlPresent) ==
                  "capture_control_present" &&
              idle_explanation_category_name(
                  IdleExplanationCategory::kRuntimeControlPresent) ==
                  "runtime_control_present" &&
              idle_explanation_category_name(
                  IdleExplanationCategory::kNoObservedDeviceWork) ==
                  "no_observed_device_work" &&
              idle_explanation_category_name(
                  IdleExplanationCategory::kUnattributedVisibleIdle) ==
                  "unattributed_visible_idle",
          "category names are frozen contract strings");
  require(idle_evidence_level_name(IdleEvidenceLevel::kDirect) == "direct" &&
              idle_evidence_level_name(IdleEvidenceLevel::kNone) == "none" &&
              idle_evidence_relation_name(
                  IdleEvidenceRelation::kDeviceEventCoverage) ==
                  "device_event_coverage" &&
              idle_evidence_relation_name(
                  IdleEvidenceRelation::kCompleteAbsenceObservation) ==
                  "complete_absence_observation" &&
              collection_status_name(CollectionStatus::kUnknown) == "unknown",
          "evidence metadata names are frozen contract strings");

  // Complete synthetic capture: direct categories obey the frozen priority,
  // all-stream empty slices become no_observed_device_work, and unknown or
  // ambiguous states remain explicitly unattributed with diagnostic lineage.
  {
    const Inputs input = make_inputs();
    IdleExplanationOptions options;
    options.collection_status = CollectionStatus::kComplete;
    const IdleExplanationRunResult run =
        build_idle_explanations(input.productive, input.streams, options);
    require(run.status == AnalysisStatus::kOk && run.devices.size() == 1,
            "complete fixture produces one ok device");
    const IdleExplanationDeviceResult& device = run.devices.front();
    require(device.explanations.size() == 6,
            "state boundaries produce six conservative slices");
    const std::vector<IdleExplanationCategory> expected = {
        IdleExplanationCategory::kNoObservedDeviceWork,
        IdleExplanationCategory::kBlockedByVisibleWait,
        IdleExplanationCategory::kCaptureControlPresent,
        IdleExplanationCategory::kRuntimeControlPresent,
        IdleExplanationCategory::kUnattributedVisibleIdle,
        IdleExplanationCategory::kUnattributedVisibleIdle};
    const std::vector<std::int64_t> starts = {10, 20, 40, 60, 70, 80};
    const std::vector<std::int64_t> ends = {20, 40, 60, 70, 80, 90};
    for (std::size_t index = 0; index < expected.size(); ++index) {
      const IdleExplanationRow& row = device.explanations[index];
      require(row.start_ns == starts[index] && row.end_ns == ends[index] &&
                  row.category == expected[index],
              "exact explanation boundary and category");
      require(row.alignment_status == "not_required",
              "device-only evidence needs no clock alignment");
      if (index <= 3) {
        require(row.evidence_level == IdleEvidenceLevel::kDirect,
                "direct category carries direct evidence");
      } else {
        require(row.evidence_level == IdleEvidenceLevel::kNone &&
                    row.evidence_relation == IdleEvidenceRelation::kNone,
                "unattributed slice carries no claim evidence");
      }
    }
    require(device.explanations[1].source_links.size() == 1 &&
                device.explanations[1].source_links[0].stream_id == 11 &&
                device.explanations[1].source_links[0].source.task_id ==
                    TaskId(1),
            "wait lineage is exact");
    require(device.explanations[2].source_links.size() == 1 &&
                device.explanations[2].source_links[0].source.task_id ==
                    TaskId(2),
            "capture wins over runtime-control on another stream");
    require(device.explanations[4].source_links.size() == 1 &&
                device.explanations[5].source_links.size() == 2,
            "unknown and ambiguous diagnostic lineage is preserved");
    require(total_duration(device) == 80,
            "explanation union equals the visible productive gap");
    for (std::size_t index = 1; index < device.explanations.size(); ++index) {
      require(device.explanations[index - 1].end_ns ==
                  device.explanations[index].start_ns,
              "explanation slices are adjacent and non-overlapping");
    }
  }

  // Real captures default to unknown completeness: identical empty evidence
  // stays unattributed rather than making an absence claim.
  {
    const Inputs input = make_inputs();
    const IdleExplanationRunResult run =
        build_idle_explanations(input.productive, input.streams);
    const IdleExplanationRow& first = run.devices.front().explanations.front();
    require(first.start_ns == 10 && first.end_ns == 20 &&
                first.category ==
                    IdleExplanationCategory::kUnattributedVisibleIdle &&
                first.evidence_level == IdleEvidenceLevel::kNone,
            "unknown collection completeness gates absence attribution");
  }

  // Even a complete collection attestation cannot establish absence when an
  // observed event could not be assigned/scanned per stream.
  {
    const Inputs input = make_inputs(false);
    IdleExplanationOptions options;
    options.collection_status = CollectionStatus::kComplete;
    const IdleExplanationRunResult run =
        build_idle_explanations(input.productive, input.streams, options);
    require(run.devices.front().explanations.front().category ==
                IdleExplanationCategory::kUnattributedVisibleIdle,
            "incomplete observed-universe scan gates absence attribution");
  }

  // Run-level input damage also voids a nominal collection attestation, even
  // when one device's local scan flag happens to remain true.
  {
    Inputs input = make_inputs();
    input.streams.status = AnalysisStatus::kInvalidInput;
    IdleExplanationOptions options;
    options.collection_status = CollectionStatus::kComplete;
    const IdleExplanationRunResult run =
        build_idle_explanations(input.productive, input.streams, options);
    require(run.status == AnalysisStatus::kInvalidInput &&
                run.devices.front().explanations.front().category ==
                    IdleExplanationCategory::kUnattributedVisibleIdle,
            "run-level damage gates absence attribution");
  }

  // Damage that E3 cannot attribute to any device is a global absence veto,
  // even if the run status and the current device's local scan flag are ok.
  {
    Inputs input = make_inputs();
    input.streams.diagnostics.push_back(
        TimelineDiagnostic{"unattributable damaged event", -1});
    input.streams.observed_universe_scan_complete = false;
    IdleExplanationOptions options;
    options.collection_status = CollectionStatus::kComplete;
    const IdleExplanationRunResult run =
        build_idle_explanations(input.productive, input.streams, options);
    require(run.devices.front().explanations.front().category ==
                IdleExplanationCategory::kUnattributedVisibleIdle,
            "device-unattributable run damage gates absence attribution");
  }

  // A different device's incomplete scan does not veto a locally complete
  // device. Only device-unattributable run diagnostics are global.
  {
    Inputs input = make_inputs();
    DeviceTimelineResult second_productive = input.productive.devices.front();
    second_productive.device_id = 8;
    input.productive.devices.push_back(std::move(second_productive));
    StreamStateDeviceResult second_stream = input.streams.devices.front();
    second_stream.device_id = 8;
    second_stream.observed_universe_scan_complete = false;
    for (StreamStateTimeline& timeline : second_stream.timelines) {
      timeline.device_id = 8;
    }
    input.streams.devices.push_back(std::move(second_stream));
    input.streams.stream_universe_size = 4;
    input.streams.observed_universe_scan_complete = false;
    IdleExplanationOptions options;
    options.collection_status = CollectionStatus::kComplete;
    const IdleExplanationRunResult run =
        build_idle_explanations(input.productive, input.streams, options);
    require(run.devices.size() == 2 &&
                run.devices[0].explanations.front().category ==
                    IdleExplanationCategory::kNoObservedDeviceWork &&
                run.devices[1].explanations.front().category ==
                    IdleExplanationCategory::kUnattributedVisibleIdle,
            "absence completeness remains device-local when damage is attributable");
  }

  // E2 damage propagates to E4 and prevents an absence claim even when E3 is
  // otherwise complete and empty on every observed stream.
  {
    Inputs input = make_inputs();
    input.productive.status = AnalysisStatus::kInvalidInput;
    IdleExplanationOptions options;
    options.collection_status = CollectionStatus::kComplete;
    const IdleExplanationRunResult run =
        build_idle_explanations(input.productive, input.streams, options);
    require(run.status == AnalysisStatus::kInvalidInput &&
                run.devices.front().explanations.front().category ==
                    IdleExplanationCategory::kUnattributedVisibleIdle,
            "E2 invalid input propagates and gates absence attribution");
  }

  // Ambiguous E3 intervals retain component state. The frozen wait priority
  // selects only the wait lineage rather than treating the whole overlap as
  // unattributed.
  {
    Inputs input = make_inputs();
    input.streams.devices.front().timelines.front().intervals[1] = state(
        20, 40, StreamState::kAmbiguousOverlap,
        {source(1, 1, StreamState::kRunningWait),
         source(9, 9, StreamState::kRunningCaptureControl)});
    const IdleExplanationRunResult run =
        build_idle_explanations(input.productive, input.streams);
    const IdleExplanationRow& row = run.devices.front().explanations[1];
    require(row.start_ns == 20 && row.end_ns == 40 &&
                row.category ==
                    IdleExplanationCategory::kBlockedByVisibleWait &&
                row.source_links.size() == 1 &&
                row.source_links.front().state ==
                    StreamState::kRunningWait &&
                row.source_links.front().source.task_id == TaskId(1),
            "ambiguous wait component obeys priority with exact lineage");
  }

  // Non-ok device results propagate status and never invent explanations.
  {
    ProductiveTimelineRunResult productive;
    DeviceTimelineResult device;
    device.device_id = 3;
    device.status = AnalysisStatus::kNoProductiveSpan;
    productive.devices.push_back(device);
    StreamStateRunResult streams;
    StreamStateDeviceResult stream_device;
    stream_device.device_id = 3;
    stream_device.status = AnalysisStatus::kNoProductiveSpan;
    streams.devices.push_back(stream_device);
    const IdleExplanationRunResult run =
        build_idle_explanations(productive, streams);
    require(run.devices.size() == 1 &&
                run.devices[0].status == AnalysisStatus::kNoProductiveSpan &&
                run.devices[0].explanations.empty(),
            "no productive span produces no explanations");
  }

  // E4 refuses mismatched E2/E3 inputs instead of silently projecting across
  // different spans or universes.
  {
    Inputs input = make_inputs();
    input.streams.devices.front().span_end_ns = 99;
    require(throws_invalid_argument([&input]() {
              (void)build_idle_explanations(input.productive, input.streams);
            }),
            "span mismatch fails fast");
  }
  {
    Inputs input = make_inputs();
    input.streams.devices.front().stream_universe_size = 1;
    require(throws_invalid_argument([&input]() {
              (void)build_idle_explanations(input.productive, input.streams);
            }),
            "universe metadata mismatch fails fast");
  }

  // Productive E3 coverage inside an E2 visible gap is a semantic stage
  // mismatch, including when productive work is one ambiguous component.
  {
    Inputs input = make_inputs();
    input.streams.devices.front().timelines.front().intervals[1] =
        state(20, 40, StreamState::kRunningCompute, {source(1, 1)});
    require(throws_invalid_argument([&input]() {
              (void)build_idle_explanations(input.productive, input.streams);
            }),
            "productive state inside an E2 gap fails fast");
  }
  {
    Inputs input = make_inputs();
    input.streams.devices.front().timelines.front().intervals[1] = state(
        20, 40, StreamState::kAmbiguousOverlap,
        {source(1, 1, StreamState::kRunningCompute),
         source(9, 9, StreamState::kRunningWait)});
    require(throws_invalid_argument([&input]() {
              (void)build_idle_explanations(input.productive, input.streams);
            }),
            "ambiguous productive component inside an E2 gap fails fast");
  }

  return 0;
}
