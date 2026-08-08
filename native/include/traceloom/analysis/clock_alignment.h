#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "traceloom/ir/native_ir.h"

namespace traceloom {

enum class AlignmentStatus {
  kCalibrated,
  kSyntheticOnly,
  kUncalibrated,
  kInvalid,
};

std::string_view alignment_status_name(AlignmentStatus status);
bool alignment_supports_cross_clock_evidence(AlignmentStatus status);

struct ClockModel {
  std::uint32_t device_id = 0;
  std::string source_clock_domain = "profiler_host";
  std::string intermediate_clock_domain = "caller_clock_realtime";
  std::string target_clock_domain = "device";
  std::string mapping_kind = "composed_affine";
  // Final profiler-host -> device affine mapping consumed by host correlation.
  long double scale = 1.0L;
  long double offset_ns = 0.0L;
  long double reference_host_ns = 0.0L;
  long double reference_device_ns = 0.0L;
  long double drift_ppm = 0.0L;
  bool has_profiler_host_mapping = false;
  // Component 1: caller CLOCK_REALTIME marker midpoint -> device TASK ns.
  long double marker_to_device_scale = 1.0L;
  long double marker_to_device_offset_ns = 0.0L;
  long double reference_marker_host_ns = 0.0L;
  long double marker_reference_device_ns = 0.0L;
  long double marker_to_device_drift_ppm = 0.0L;
  // Component 2: profiler host ns -> caller CLOCK_REALTIME ns.
  long double profiler_to_marker_scale = 1.0L;
  long double profiler_to_marker_offset_ns = 0.0L;
  long double reference_profiler_host_ns = 0.0L;
  long double profiler_reference_marker_ns = 0.0L;
  long double profiler_to_marker_drift_ppm = 0.0L;
  std::string fit_method = "theil_sen_median";
  std::string fit_method_version = "theil_sen_composed_host_v2";
  std::uint64_t fit_random_seed = 0;
  std::uint64_t input_marker_count = 0;
  std::uint64_t inlier_marker_count = 0;
  std::uint64_t rejected_marker_count = 0;
  std::uint64_t fit_marker_count = 0;
  std::uint64_t validation_marker_count = 0;
  long double absolute_residual_p50_ns = 0.0L;
  long double absolute_residual_p95_ns = 0.0L;
  long double absolute_residual_max_ns = 0.0L;
  long double bracket_uncertainty_p95_ns = 0.0L;
  long double host_clock_absolute_residual_p50_ns = 0.0L;
  long double host_clock_absolute_residual_p95_ns = 0.0L;
  long double host_clock_absolute_residual_max_ns = 0.0L;
  long double host_clock_uncertainty_p95_ns = 0.0L;
  long double composed_absolute_residual_p50_ns = 0.0L;
  long double composed_absolute_residual_p95_ns = 0.0L;
  long double composed_absolute_residual_max_ns = 0.0L;
  std::uint64_t direct_overlap_marker_count = 0;
  std::uint64_t ordinal_affine_fallback_marker_count = 0;
  std::uint64_t epsilon_ns = 0;
  AlignmentStatus alignment_status = AlignmentStatus::kUncalibrated;
  std::string reason;
  std::vector<ClockMarkerId> input_markers;
  std::vector<ClockMarkerId> fit_markers;
  std::vector<ClockMarkerId> validation_markers;
  std::vector<ClockMarkerId> rejected_markers;
};

bool clock_model_supports_cross_clock_evidence(const ClockModel& model);

struct ClockAlignmentOptions {
  // Controlled synthetic fixtures may exercise the correlation mechanism,
  // but must never be reported as calibrated real-trace evidence.
  bool synthetic_fixture = false;
};

struct ClockAlignmentRunResult {
  std::vector<ClockModel> models;
  std::string model_version = "profiler_marker_device_composed_affine_v2";

  const ClockModel* find_device(std::uint32_t device_id) const;
};

ClockAlignmentRunResult fit_host_device_clock_models(
    const NativeIr& ir,
    const ClockAlignmentOptions& options = {});

// Applies the fitted profiler-host -> caller-realtime -> device composition,
// then uses explicit round-half-to-even. Mapping is unavailable when either
// component is absent, for uncalibrated/invalid models, and on overflow.
std::optional<std::int64_t> map_host_to_device_ns(
    const ClockModel& model,
    std::int64_t host_timestamp_ns);

}  // namespace traceloom
