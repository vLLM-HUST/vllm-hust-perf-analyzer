#include "traceloom/analysis/clock_calibration.h"
#include "traceloom/testing/test_util.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

using traceloom::ClockCalibrationObservation;

std::vector<ClockCalibrationObservation> make_observations() {
  std::vector<ClockCalibrationObservation> observations;
  for (std::size_t index = 0; index < 11; ++index) {
    const std::int64_t source =
        100000 + static_cast<std::int64_t>(index) * 10000;
    std::int64_t target =
        source + static_cast<std::int64_t>(index) + 5000;
    if (index == 4) {
      target += 7;
    } else if (index == 9) {
      target -= 3;
    }
    observations.push_back({"marker-" + std::to_string(index), source, target,
                            4});
  }
  return observations;
}

traceloom::ClockCalibrationOptions synthetic_options() {
  traceloom::ClockCalibrationOptions options;
  options.source_clock_domain = "rank-1-device";
  options.target_clock_domain = "rank-0-device";
  options.marker_contract = "synthetic-marker-pairs-v1";
  options.synthetic_fixture = true;
  return options;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  require(clock_calibration_status_name(
              ClockCalibrationStatus::kCalibrated) == "calibrated" &&
              clock_calibration_status_name(
                  ClockCalibrationStatus::kCandidateOnly) ==
                  "candidate_only" &&
              clock_calibration_status_name(
                  ClockCalibrationStatus::kSyntheticOnly) ==
                  "synthetic_only" &&
              clock_calibration_status_name(ClockCalibrationStatus::kInvalid) ==
                  "invalid",
          "clock calibration status spellings are frozen");

  {
    const ClockCalibrationModel model =
        fit_affine_clock_model(make_observations(), synthetic_options());
    require(model.status == ClockCalibrationStatus::kSyntheticOnly &&
                model.input_observation_count == 11 &&
                model.fit_observation_count == 9 &&
                model.validation_observation_count == 2,
            "the recovered every-fifth holdout split is exact");
    require(model.fit_marker_ids.front() == "marker-0" &&
                model.fit_marker_ids.back() == "marker-10" &&
                model.validation_marker_ids[0] == "marker-4" &&
                model.validation_marker_ids[1] == "marker-9",
            "endpoints remain fitted and every fifth marker is held out");
    require(model.scale > 1.000099999L && model.scale < 1.000100001L,
            "Theil-Sen recovers the injected 100 ppm drift");
    require(model.absolute_residual_p50_ns >= 2.9L &&
                model.absolute_residual_p50_ns <= 3.1L &&
                model.absolute_residual_p95_ns >= 6.9L &&
                model.absolute_residual_p95_ns <= 7.1L &&
                model.epsilon_ns == 11,
            "holdout residual and marker uncertainty remain explicit");
    require(map_calibrated_clock_timestamp_ns(model, 250000) == 255015,
            "a synthetic fixture exercises calibrated timestamp mapping");
  }

  {
    ClockCalibrationOptions options = synthetic_options();
    options.synthetic_fixture = false;
    const ClockCalibrationModel candidate =
        fit_affine_clock_model(make_observations(), options);
    require(candidate.status == ClockCalibrationStatus::kCandidateOnly &&
                !map_calibrated_clock_timestamp_ns(candidate, 250000) &&
                map_clock_timestamp_ns_for_display(candidate, 250000) ==
                    255015,
            "candidate time is displayable only through the explicit display API");
    options.marker_contract_validated = true;
    const ClockCalibrationModel calibrated =
        fit_affine_clock_model(make_observations(), options);
    require(calibrated.status == ClockCalibrationStatus::kCalibrated &&
                map_calibrated_clock_timestamp_ns(calibrated, 250000) ==
                    255015,
            "validated marker identity admits timestamp mapping");
  }

  {
    std::vector<ClockCalibrationObservation> short_input = make_observations();
    short_input.resize(5);
    const ClockCalibrationModel model =
        fit_affine_clock_model(short_input, synthetic_options());
    require(model.status == ClockCalibrationStatus::kInvalid,
            "fewer than six markers fail closed");
  }

  {
    std::vector<ClockCalibrationObservation> duplicate = make_observations();
    duplicate.back().marker_id = duplicate.front().marker_id;
    const ClockCalibrationModel model =
        fit_affine_clock_model(duplicate, synthetic_options());
    require(model.status == ClockCalibrationStatus::kInvalid &&
                model.reason.find("duplicate") != std::string::npos,
            "duplicate marker identity fails closed");
  }

  {
    ClockCalibrationModel model;
    model.status = ClockCalibrationStatus::kCalibrated;
    model.reference_source_ns = 0;
    model.reference_target_ns = 0;
    model.scale = 0.5L;
    require(map_calibrated_clock_timestamp_ns(model, 1) == 0 &&
                map_calibrated_clock_timestamp_ns(model, 3) == 2 &&
                map_calibrated_clock_timestamp_ns(model, 5) == 2,
            "timestamp mapping uses explicit round-half-to-even");
  }

  return 0;
}
