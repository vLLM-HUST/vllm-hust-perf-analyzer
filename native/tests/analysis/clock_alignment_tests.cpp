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
                   const std::string& marker_id = "") {
  const SymbolId id = ir.symbols.intern(
      marker_id.empty() ? "marker-" + std::to_string(index) : marker_id);
  ir.clock_markers.append(
      source, index + 1, id, host_mid_ns - 4, host_mid_ns + 5, device_ns,
      123, 456, device_id, true, 7, true,
      static_cast<std::int64_t>(1000 + index),
      ir.symbols.intern("clock_alignment_test"), return_status);
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
                model.epsilon_ns == 12,
            "holdout residual and conservative epsilon are reported");
    const std::optional<std::int64_t> mapped =
        map_host_to_device_ns(model, 250000);
    require(mapped.has_value() && *mapped == 255015,
            "host timestamp maps into the device ns domain");
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
