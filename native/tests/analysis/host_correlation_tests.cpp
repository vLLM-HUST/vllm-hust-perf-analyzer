#include "traceloom/analysis/host_correlation.h"
#include "traceloom/analysis/idle_explanation.h"
#include "traceloom/testing/test_util.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace traceloom;
using traceloom::testing::require;

TaskId append_task(NativeIr& ir,
                   SourceRefId source,
                   std::uint64_t row_id,
                   std::uint32_t device_id,
                   std::int64_t start_ns,
                   std::int64_t end_ns,
                   std::int64_t connection_id) {
  const SymbolId type = ir.symbols.intern("AI_CORE");
  const TraceEventId event = ir.trace_events.append(
      source, row_id, device_id, 3, start_ns, end_ns, type);
  return ir.tasks.append(source, event, row_id, row_id, connection_id, type,
                         ir.symbols.intern("kernel"),
                         ir.symbols.intern("MatMul"), type,
                         SymbolId::invalid());
}

HostApiEventId append_api(NativeIr& ir,
                          SourceRefId source,
                          std::uint64_t row_id,
                          std::string name,
                          std::int64_t start_ns,
                          std::int64_t end_ns,
                          std::int64_t connection_id,
                          bool has_device_id = false,
                          std::uint32_t device_id = 0) {
  return ir.host_api_events.append(
      source, row_id, start_ns, end_ns, 77, connection_id,
      SymbolId::invalid(), ir.symbols.intern(std::move(name)), has_device_id,
      device_id);
}

ProductiveTimelineRunResult productive_fixture() {
  ProductiveTimelineRunResult run;
  DeviceTimelineResult device;
  device.device_id = 0;
  device.status = AnalysisStatus::kOk;
  device.span_start_ns = 0;
  device.span_end_ns = 400;
  device.intervals = {
      DeviceIntervalRow{0, 100, DeviceIntervalKind::kProductiveActive, {}},
      DeviceIntervalRow{100, 300,
                        DeviceIntervalKind::kVisibleProductiveIdle, {}},
      DeviceIntervalRow{300, 400, DeviceIntervalKind::kProductiveActive, {}}};
  run.devices.push_back(std::move(device));
  return run;
}

StreamStateRunResult streams_fixture() {
  StreamStateRunResult run;
  StreamStateTimeline timeline;
  timeline.device_id = 0;
  timeline.stream_id = 3;
  timeline.span_start_ns = 0;
  timeline.span_end_ns = 400;
  timeline.intervals.push_back(
      StreamStateInterval{0, 400, StreamState::kEmptyObserved, {}});
  StreamStateDeviceResult device;
  device.device_id = 0;
  device.status = AnalysisStatus::kOk;
  device.span_start_ns = 0;
  device.span_end_ns = 400;
  device.stream_universe_size = 1;
  device.observed_universe_scan_complete = true;
  device.timelines.push_back(std::move(timeline));
  run.devices.push_back(std::move(device));
  run.stream_universe_size = 1;
  run.observed_universe_scan_complete = true;
  return run;
}

ClockAlignmentRunResult identity_alignment(AlignmentStatus status) {
  ClockAlignmentRunResult run;
  ClockModel model;
  model.device_id = 0;
  model.scale = 1.0L;
  model.reference_host_ns = 0.0L;
  model.reference_device_ns = 0.0L;
  model.has_profiler_host_mapping = true;
  model.epsilon_ns = 10;
  model.alignment_status = status;
  run.models.push_back(model);
  return run;
}

HostApiRuleset ruleset() {
  return HostApiRuleset(
      {HostApiRule{"aclrtLaunchKernel*", HostApiFamily::kEnqueue, "", 1},
       HostApiRule{"aclrtSynchronize*", HostApiFamily::kHostSync, "", 2}},
      "test-host-rules-v1", "0123456789abcdef");
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  require(task_api_link_status_name(TaskApiLinkStatus::kUnique) == "unique" &&
              task_api_link_status_name(TaskApiLinkStatus::kOneToMany) ==
                  "one_to_many" &&
              task_api_link_status_name(TaskApiLinkStatus::kAmbiguous) ==
                  "ambiguous" &&
              task_api_link_status_name(TaskApiLinkStatus::kUnresolved) ==
                  "unresolved",
          "link status spellings are frozen");

  const HostApiRuleset default_rules =
      load_default_idle_evidence_host_api_ruleset();
  const std::optional<HostApiMatch> default_enqueue =
      default_rules.classify("aclrtLaunchKernelV2");
  const std::optional<HostApiMatch> default_sync =
      default_rules.classify("aclrtSynchronizeStreamWithTimeout");
  require(default_rules.version() == "idle-evidence-host-api-v1" &&
              default_rules.sha256().size() == 64 &&
              default_enqueue.has_value() &&
              default_enqueue->family == HostApiFamily::kEnqueue &&
              default_sync.has_value() &&
              default_sync->family == HostApiFamily::kHostSync &&
              !default_rules.classify("aclrtCreateStream").has_value(),
          "versioned default rules classify only allowlisted host APIs");

  NativeIr ir;
  const SourceRefId task_source =
      ir.source_refs.append("synthetic", "fixture.db", "TASK", 0);
  const SourceRefId api_source =
      ir.source_refs.append("synthetic", "fixture.db", "CANN_API", 0);
  const TaskId task42 = append_task(ir, task_source, 1, 0, 300, 400, 42);
  (void)append_task(ir, task_source, 2, 0, 300, 400, 43);
  (void)append_task(ir, task_source, 3, 0, 300, 400, 99);
  (void)append_task(ir, task_source, 4, 1, 300, 400, 99);

  const HostApiEventId enqueue = append_api(
      ir, api_source, 10, "aclrtLaunchKernelV2", 200, 250, 42);
  const HostApiEventId sync = append_api(
      ir, api_source, 11, "aclrtSynchronizeStream", 150, 250, -1, true, 0);
  const HostApiEventId short_sync = append_api(
      ir, api_source, 12, "aclrtSynchronizeEvent", 120, 130, -1, true, 0);
  const HostApiEventId short_delay = append_api(
      ir, api_source, 13, "aclrtLaunchKernel", 280, 295, 43);
  const HostApiEventId ambiguous = append_api(
      ir, api_source, 14, "aclrtLaunchKernel", 200, 220, 99);
  (void)sync;
  (void)short_sync;
  (void)short_delay;

  const ProductiveTimelineRunResult productive = productive_fixture();
  const HostCorrelationRunResult correlation = build_host_correlation(
      ir, productive, identity_alignment(AlignmentStatus::kSyntheticOnly),
      ruleset());
  require(correlation.host_api_rules_version == "test-host-rules-v1" &&
              correlation.host_api_rules_sha256 == "0123456789abcdef",
          "ruleset identity propagates into correlation output");

  bool saw_unique = false;
  bool saw_ambiguous = false;
  for (const TaskApiLinkRow& link : correlation.task_api_links) {
    if (link.host_api_event_id == enqueue) {
      saw_unique = link.link_status == TaskApiLinkStatus::kUnique &&
                   link.task_id == task42 && link.has_device_id &&
                   link.device_id == 0;
    }
    if (link.host_api_event_id == ambiguous) {
      saw_ambiguous = link.link_status == TaskApiLinkStatus::kAmbiguous;
    }
  }
  require(saw_unique, "unique connectionId link is preserved exactly");
  require(saw_ambiguous,
          "cross-device connectionId remains ambiguous and unpromoted");

  require(correlation.evidence_intervals.size() == 2,
          "one robust enqueue delay and one robust sync overlap are emitted");
  require(correlation.evidence_intervals[0].start_ns == 160 &&
              correlation.evidence_intervals[0].end_ns == 240 &&
              correlation.evidence_intervals[0].category ==
                  HostEvidenceCategory::kHostSyncApiPresent,
          "host sync uses the epsilon-shrunk robust interval");
  require(correlation.evidence_intervals[1].start_ns == 260 &&
              correlation.evidence_intervals[1].end_ns == 300 &&
              correlation.evidence_intervals[1].category ==
                  HostEvidenceCategory::kQueuedVisibleTaskDelay,
          "enqueue delay begins only after mapped API end plus epsilon");
  require(correlation.candidates.size() == 2,
          "short sync and non-robust delay remain diagnostic candidates");
  bool saw_possible = false;
  bool saw_non_robust = false;
  for (const HostIdleCandidateRow& candidate : correlation.candidates) {
    saw_possible = saw_possible ||
                   candidate.candidate_status ==
                       HostCandidateStatus::kPossibleOnly;
    saw_non_robust = saw_non_robust ||
                     candidate.candidate_status ==
                         HostCandidateStatus::kNonRobustDelay;
  }
  require(saw_possible && saw_non_robust,
          "candidate statuses distinguish overlap from delay failure");

  IdleExplanationOptions explanation_options;
  explanation_options.collection_status = CollectionStatus::kComplete;
  const IdleExplanationRunResult explanations = build_idle_explanations(
      productive, streams_fixture(), explanation_options, &correlation);
  const std::vector<IdleExplanationRow>& rows =
      explanations.devices.front().explanations;
  require(rows.size() == 4, "host windows split the gap exactly");
  require(rows[0].start_ns == 100 && rows[0].end_ns == 160 &&
              rows[0].category ==
                  IdleExplanationCategory::kNoObservedDeviceWork &&
              rows[1].start_ns == 160 && rows[1].end_ns == 240 &&
              rows[1].category ==
                  IdleExplanationCategory::kHostSyncApiPresent &&
              rows[1].evidence_level == IdleEvidenceLevel::kCorrelated &&
              rows[1].evidence_relation ==
                  IdleEvidenceRelation::kTemporalOverlap &&
              rows[2].start_ns == 240 && rows[2].end_ns == 260 &&
              rows[2].category ==
                  IdleExplanationCategory::kNoObservedDeviceWork &&
              rows[3].start_ns == 260 && rows[3].end_ns == 300 &&
              rows[3].category ==
                  IdleExplanationCategory::kQueuedVisibleTaskDelay &&
              rows[3].evidence_relation ==
                  IdleEvidenceRelation::kExactConnectionId,
          "E4 priority inserts correlated host evidence before absence");

  // The robust evidence intervals partition only the gap and conserve all
  // duration. Candidate-only evidence does not replace any official slice.
  std::int64_t total = 0;
  for (const IdleExplanationRow& row : rows) {
    total += row.end_ns - row.start_ns;
  }
  require(total == 200, "host-aware explanation partition conserves the gap");

  const HostCorrelationRunResult uncalibrated = build_host_correlation(
      ir, productive, identity_alignment(AlignmentStatus::kUncalibrated),
      ruleset());
  require(uncalibrated.evidence_intervals.empty() &&
              uncalibrated.candidates.empty() &&
              !uncalibrated.task_api_links.empty(),
          "uncalibrated traces retain links but compute no time coverage");

  ClockAlignmentRunResult missing_profiler_leg =
      identity_alignment(AlignmentStatus::kCalibrated);
  missing_profiler_leg.models.front().has_profiler_host_mapping = false;
  const HostCorrelationRunResult incomplete_clock = build_host_correlation(
      ir, productive, missing_profiler_leg, ruleset());
  require(incomplete_clock.evidence_intervals.empty() &&
              incomplete_clock.candidates.empty() &&
              !incomplete_clock.task_api_links.empty(),
          "calibrated marker/device fit without profiler-host leg cannot "
          "promote host evidence");

  return 0;
}
