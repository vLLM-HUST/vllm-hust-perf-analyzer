#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace traceloom {

// A fitted affine relation is not automatically a calibration. The caller
// must separately establish marker identity across the two clock domains.
enum class ClockCalibrationStatus {
  kCalibrated,
  kCandidateOnly,
  kSyntheticOnly,
  kInvalid,
};

std::string_view clock_calibration_status_name(
    ClockCalibrationStatus status);

struct ClockCalibrationObservation {
  std::string marker_id;
  std::int64_t source_timestamp_ns = 0;
  std::int64_t target_timestamp_ns = 0;
  // Conservative uncertainty already expressed in the target clock domain.
  std::uint64_t target_uncertainty_ns = 0;
};

struct ClockCalibrationOptions {
  std::string source_clock_domain;
  std::string target_clock_domain;
  std::string marker_contract;
  // Set only after marker identity is established independently of timestamp
  // proximity. A structural candidate fit must leave this false.
  bool marker_contract_validated = false;
  bool synthetic_fixture = false;
  std::size_t maximum_observation_count = 4096;
};

struct ClockCalibrationModel {
  std::string source_clock_domain;
  std::string target_clock_domain;
  std::string marker_contract;
  std::string fit_method = "theil_sen_median";
  std::string fit_method_version = "theil_sen_affine_v1";
  long double scale = 1.0L;
  // Reference-coordinate delta and affine intercept are deliberately separate.
  long double offset_ns = 0.0L;
  long double intercept_ns = 0.0L;
  long double reference_source_ns = 0.0L;
  long double reference_target_ns = 0.0L;
  long double drift_ppm = 0.0L;
  std::uint64_t input_observation_count = 0;
  std::uint64_t fit_observation_count = 0;
  std::uint64_t validation_observation_count = 0;
  long double absolute_residual_p50_ns = 0.0L;
  long double absolute_residual_p95_ns = 0.0L;
  long double absolute_residual_max_ns = 0.0L;
  long double target_uncertainty_p95_ns = 0.0L;
  std::uint64_t epsilon_ns = 0;
  ClockCalibrationStatus status = ClockCalibrationStatus::kInvalid;
  std::string reason;
  std::vector<std::string> fit_marker_ids;
  std::vector<std::string> validation_marker_ids;
};

// Recovers the robust affine fitter and deterministic every-fifth holdout used
// by TraceLoom's earlier host/device calibration work. At least six unique,
// bounded observations are required; the first and last observations always
// remain in the fit set.
ClockCalibrationModel fit_affine_clock_model(
    const std::vector<ClockCalibrationObservation>& observations,
    const ClockCalibrationOptions& options);

// Candidate-only models intentionally cannot move production timestamps.
// Synthetic models remain mappable so fixtures can exercise exact rounding.
std::optional<std::int64_t> map_calibrated_clock_timestamp_ns(
    const ClockCalibrationModel& model,
    std::int64_t source_timestamp_ns);

}  // namespace traceloom
