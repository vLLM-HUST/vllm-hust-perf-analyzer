#include "traceloom/analysis/clock_alignment.h"
#include "traceloom/testing/test_util.h"

#include <cstdint>
#include <string>

namespace {

using namespace traceloom;
using traceloom::testing::require;

void append_marker(NativeIr& ir,
                   SourceRefId source,
                   std::uint32_t device_id,
                   std::size_t index,
                   std::int64_t host_mid_ns,
                   std::int64_t device_ns,
                   std::int64_t return_status = 0,
                   const std::string& marker_id = "",
                   std::int64_t profiler_host_mid_ns = -1) {
  const SymbolId id = ir.symbols.intern(
      marker_id.empty() ? "marker-" + std::to_string(index) : marker_id);
  ir.clock_markers.append(
      source, index + 1, id, host_mid_ns - 4, host_mid_ns + 5, device_ns,
      123, 456, device_id, true, 7, true,
      static_cast<std::int64_t>(1000 + index),
      ir.symbols.intern("clock_alignment_test"), return_status, true,
      (profiler_host_mid_ns < 0 ? host_mid_ns : profiler_host_mid_ns) - 2,
      (profiler_host_mid_ns < 0 ? host_mid_ns : profiler_host_mid_ns) + 3,
      ClockMarkerResolutionMethod::kDirectOverlap, false, 0.0L, true,
      host_mid_ns + 4);
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  require(alignment_status_name(AlignmentStatus::kCalibrated) ==
                  "calibrated" &&
              alignment_status_name(AlignmentStatus::kSyntheticOnly) ==
                  "synthetic_only" &&
              alignment_status_name(AlignmentStatus::kUncalibrated) ==
                  "uncalibrated" &&
              alignment_status_name(AlignmentStatus::kInvalid) == "invalid",
          "alignment status spellings are frozen");

  // Eleven ordered markers produce two holdout observations (5th and 10th),
  // while the first and last always remain in the Theil-Sen fit. Jitter is
  // injected only into holdout markers so its exact residual is auditable.
  {
    NativeIr ir;
    const SourceRefId source = ir.source_refs.append(
        "synthetic", "clock_alignment.tsv", "clock_marker", 0);
    constexpr std::int64_t kIntercept = 5000;
    for (std::size_t index = 0; index < 11; ++index) {
      const std::int64_t host =
          100000 + static_cast<std::int64_t>(index) * 10000;
      std::int64_t device =
          host + static_cast<std::int64_t>(index) + kIntercept;
      if (index == 4) {
        device += 7;
      } else if (index == 9) {
        device -= 3;
      }
      append_marker(ir, source, 3, index, host, device);
    }
    ClockAlignmentOptions options;
    options.synthetic_fixture = true;
    const ClockAlignmentRunResult run =
        fit_host_device_clock_models(ir, options);
    require(run.models.size() == 1, "one marker device produces one model");
    const ClockModel& model = run.models.front();
    require(model.device_id == 3 &&
                model.alignment_status == AlignmentStatus::kSyntheticOnly,
            "synthetic fixtures never claim real calibration");
    require(model.input_marker_count == 11 &&
                model.inlier_marker_count == 11 &&
                model.rejected_marker_count == 0 &&
                model.fit_marker_count == 9 &&
                model.validation_marker_count == 2,
            "frozen fit/holdout split counts are exact");
    require(model.fit_markers.front() == ClockMarkerId(0) &&
                model.fit_markers.back() == ClockMarkerId(10) &&
                model.validation_markers[0] == ClockMarkerId(4) &&
                model.validation_markers[1] == ClockMarkerId(9),
            "every fifth marker is held out and endpoints stay in fit");
    require(model.scale > 1.000099999L && model.scale < 1.000100001L,
            "Theil-Sen scale recovers the injected 100 ppm drift");
    require(model.absolute_residual_p50_ns >= 2.9L &&
                model.absolute_residual_p50_ns <= 3.1L &&
                model.absolute_residual_p95_ns >= 6.9L &&
                model.absolute_residual_p95_ns <= 7.1L &&
                model.absolute_residual_max_ns >= 6.9L &&
                model.host_clock_absolute_residual_p95_ns == 0.0L &&
                model.host_clock_uncertainty_p95_ns == 0.0L &&
                model.profiler_to_caller_bracket_uncertainty_p95_ns > 4.0L &&
                model.composed_absolute_residual_p50_ns >= 2.9L &&
                model.composed_absolute_residual_p95_ns >= 6.9L &&
                model.composed_absolute_residual_max_ns >= 6.9L &&
                model.epsilon_ns == 16 && model.has_profiler_host_mapping &&
                model.offset_ns ==
                    model.reference_device_ns - model.reference_host_ns &&
                model.intercept_ns ==
                    model.reference_device_ns -
                        model.scale * model.reference_host_ns,
            "both holdout residuals and conservative epsilon are reported");
    const std::optional<std::int64_t> mapped =
        map_host_to_device_ns(model, 250000);
    require(mapped.has_value() && *mapped == 255015,
            "host timestamp maps into the device ns domain");
  }

  // With the old shared outer midpoint, a fixed record->device delay produced
  // zero end-to-end residual while the coordinate mapping was still wrong.
  // Separate observation brackets expose the half-delay residual and keep the
  // resulting error inside epsilon; neither midpoint may train both legs.
  {
    NativeIr ir;
    const SourceRefId source = ir.source_refs.append(
        "synthetic", "shared_midpoint_counterexample.tsv", "clock_marker", 0);
    constexpr std::int64_t kDelayNs = 200;
    for (std::size_t index = 0; index < 11; ++index) {
      const std::int64_t timestamp =
          1000 + static_cast<std::int64_t>(index) * 1000;
      ir.clock_markers.append(
          source, index + 1,
          ir.symbols.intern("counterexample-" + std::to_string(index)),
          timestamp - 2, timestamp + kDelayNs + 2,
          timestamp + kDelayNs, 123, 456, 0, true, 7, true,
          static_cast<std::int64_t>(3000 + index),
          ir.symbols.intern("clock_alignment_test"), 0, true, timestamp - 1,
          timestamp + 1, ClockMarkerResolutionMethod::kDirectOverlap, false,
          0.0L, true, timestamp + 2);
    }
    ClockAlignmentOptions options;
    options.synthetic_fixture = true;
    const ClockModel& model =
        fit_host_device_clock_models(ir, options).models.front();
    const std::optional<std::int64_t> mapped =
        map_host_to_device_ns(model, 6000);
    require(mapped.has_value() && *mapped == 6100 &&
                model.composed_absolute_residual_p95_ns == 100.0L &&
                static_cast<std::uint64_t>(*mapped - 6000) <= model.epsilon_ns,
            "separate observation brackets bound shared-delay midpoint error");
  }

  // A non-identity profiler-host clock is fitted separately and composed with
  // caller CLOCK_REALTIME -> device. Its holdout error contributes to epsilon.
  {
    NativeIr ir;
    const SourceRefId source = ir.source_refs.append(
        "synthetic", "composed_clock.tsv", "clock_marker", 0);
    for (std::size_t index = 0; index < 11; ++index) {
      const std::int64_t profiler_host =
          1000 + static_cast<std::int64_t>(index) * 100;
      const std::int64_t true_marker_host = 2 * profiler_host + 100;
      std::int64_t observed_marker_host = true_marker_host;
      if (index == 4) {
        observed_marker_host += 5;
      } else if (index == 9) {
        observed_marker_host -= 3;
      }
      const std::int64_t device = 3 * true_marker_host + 500;
      append_marker(ir, source, 0, index, observed_marker_host, device, 0, "",
                    profiler_host);
    }
    ClockAlignmentOptions options;
    options.synthetic_fixture = true;
    const ClockModel& model =
        fit_host_device_clock_models(ir, options).models.front();
    require(model.alignment_status == AlignmentStatus::kSyntheticOnly &&
                model.profiler_to_marker_scale == 2.0L &&
                model.marker_to_device_scale == 3.0L && model.scale == 6.0L &&
                model.host_clock_absolute_residual_p50_ns == 3.0L &&
                model.host_clock_absolute_residual_p95_ns == 5.0L &&
                model.host_clock_absolute_residual_max_ns == 5.0L &&
                model.host_clock_uncertainty_p95_ns == 15.0L &&
                model.composed_absolute_residual_p50_ns == 0.0L &&
                model.composed_absolute_residual_p95_ns == 0.0L &&
                model.composed_absolute_residual_max_ns == 0.0L &&
                model.epsilon_ns >= 15,
            "profiler-host holdout uncertainty is explicit in composed model");
    require(map_host_to_device_ns(model, 2500) == 15800,
            "CANN_API time maps through both fitted clock domains");
  }

  // Fewer than six valid markers and duplicate marker ids are contract
  // violations. Neither case may be used for cross-clock evidence.
  {
    NativeIr ir;
    const SourceRefId source =
        ir.source_refs.append("synthetic", "short.tsv", "clock_marker", 0);
    for (std::size_t index = 0; index < 5; ++index) {
      append_marker(ir, source, 1, index, 1000 + index * 100, 2000 + index * 100);
    }
    const ClockModel& model = fit_host_device_clock_models(ir).models.front();
    require(model.alignment_status == AlignmentStatus::kInvalid &&
                !map_host_to_device_ns(model, 123).has_value(),
            "short calibration is invalid and unmappable");
  }
  {
    NativeIr ir;
    const SourceRefId source = ir.source_refs.append(
        "fixture", "missing-profiler-clock.tsv", "clock_marker", 0);
    for (std::size_t index = 0; index < 6; ++index) {
      const std::int64_t host = 1000 + static_cast<std::int64_t>(index) * 100;
      ir.clock_markers.append(
          source, index + 1,
          ir.symbols.intern("missing-profiler-" + std::to_string(index)),
          host - 2, host + 3, host + 1000, 123, 456, 0, true, 7, true,
          static_cast<std::int64_t>(2000 + index),
          ir.symbols.intern("clock_alignment_test"), 0);
    }
    const ClockModel& model = fit_host_device_clock_models(ir).models.front();
    require(model.alignment_status == AlignmentStatus::kInvalid &&
                model.inlier_marker_count == 0 &&
                !model.has_profiler_host_mapping &&
                !map_host_to_device_ns(model, 123).has_value(),
            "real markers without a profiler-host clock leg fail closed");
  }
  {
    NativeIr ir;
    const SourceRefId source = ir.source_refs.append(
        "fixture", "missing-record-bracket.tsv", "clock_marker", 0);
    for (std::size_t index = 0; index < 6; ++index) {
      const std::int64_t host =
          1000 + static_cast<std::int64_t>(index) * 100;
      ir.clock_markers.append(
          source, index + 1,
          ir.symbols.intern("missing-record-" + std::to_string(index)),
          host - 2, host + 3, host + 1000, 123, 456, 0, true, 7, true,
          static_cast<std::int64_t>(2000 + index),
          ir.symbols.intern("clock_alignment_test"), 0, true, host - 1,
          host + 1, ClockMarkerResolutionMethod::kDirectOverlap);
    }
    const ClockModel& model = fit_host_device_clock_models(ir).models.front();
    require(model.alignment_status == AlignmentStatus::kInvalid &&
                model.inlier_marker_count == 0 &&
                !model.has_profiler_host_mapping,
            "real markers without a caller record-call bracket fail closed");
  }
  {
    NativeIr ir;
    const SourceRefId source =
        ir.source_refs.append("synthetic", "duplicate.tsv", "clock_marker", 0);
    for (std::size_t index = 0; index < 6; ++index) {
      append_marker(ir, source, 1, index, 1000 + index * 100,
                    2000 + index * 100, 0,
                    index == 5 ? "marker-0" : "marker-" + std::to_string(index));
    }
    const ClockModel& model = fit_host_device_clock_models(ir).models.front();
    require(model.alignment_status == AlignmentStatus::kInvalid &&
                model.reason.find("duplicate") != std::string::npos,
            "duplicate marker ids invalidate calibration");
  }
  {
    NativeIr ir;
    const SourceRefId source = ir.source_refs.append(
        "synthetic", "cross-device-duplicate.tsv", "clock_marker", 0);
    for (std::uint32_t device = 0; device < 2; ++device) {
      for (std::size_t index = 0; index < 6; ++index) {
        append_marker(
            ir, source, device, device * 10 + index,
            1000 + static_cast<std::int64_t>(index) * 100,
            2000 + static_cast<std::int64_t>(index) * 100, 0,
            index == 0 ? "shared-marker"
                       : "device-" + std::to_string(device) + "-marker-" +
                             std::to_string(index));
      }
    }
    const ClockAlignmentRunResult run = fit_host_device_clock_models(ir);
    require(run.models.size() == 2 &&
                run.models[0].alignment_status == AlignmentStatus::kInvalid &&
                run.models[1].alignment_status == AlignmentStatus::kInvalid,
            "marker_id uniqueness is enforced across devices");
  }

  // Explicit round-half-to-even is independent of the process floating-point
  // rounding mode.
  {
    ClockModel model;
    model.alignment_status = AlignmentStatus::kCalibrated;
    model.has_profiler_host_mapping = true;
    model.reference_host_ns = 0;
    model.reference_device_ns = 0;
    model.scale = 0.5L;
    require(map_host_to_device_ns(model, 1) == 0 &&
                map_host_to_device_ns(model, 3) == 2 &&
                map_host_to_device_ns(model, 5) == 2,
            "mapped timestamps use round-half-to-even");
  }

  return 0;
}
