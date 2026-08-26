#include "traceloom/analysis/clock_calibration.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace traceloom {
namespace {

long double median(std::vector<long double> values) {
  if (values.empty()) {
    return 0.0L;
  }
  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2;
  if ((values.size() & 1u) != 0u) {
    return values[middle];
  }
  return (values[middle - 1] + values[middle]) / 2.0L;
}

long double nearest_rank_percentile(std::vector<long double> values,
                                    long double probability) {
  if (values.empty()) {
    return 0.0L;
  }
  std::sort(values.begin(), values.end());
  const long double rank_value =
      std::ceil(probability * static_cast<long double>(values.size()));
  const std::size_t rank = static_cast<std::size_t>(
      std::max(1.0L, std::min(rank_value,
                              static_cast<long double>(values.size()))));
  return values[rank - 1];
}

std::optional<std::int64_t> round_half_to_even(long double value) {
  if (!std::isfinite(value)) {
    return std::nullopt;
  }
  long double integer = 0.0L;
  const long double fraction = std::modf(value, &integer);
  const long double magnitude = std::fabs(fraction);
  long double rounded = integer;
  if (magnitude > 0.5L) {
    rounded += std::copysign(1.0L, fraction);
  } else if (magnitude == 0.5L &&
             std::fmod(std::fabs(integer), 2.0L) == 1.0L) {
    rounded += std::copysign(1.0L, fraction);
  }
  if (rounded <
          static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
      rounded >
          static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(rounded);
}

bool observation_less(const ClockCalibrationObservation* lhs,
                      const ClockCalibrationObservation* rhs) {
  if (lhs->source_timestamp_ns != rhs->source_timestamp_ns) {
    return lhs->source_timestamp_ns < rhs->source_timestamp_ns;
  }
  if (lhs->target_timestamp_ns != rhs->target_timestamp_ns) {
    return lhs->target_timestamp_ns < rhs->target_timestamp_ns;
  }
  return lhs->marker_id < rhs->marker_id;
}

ClockCalibrationModel invalid_model(const ClockCalibrationOptions& options,
                                    std::size_t input_count,
                                    std::string reason) {
  ClockCalibrationModel model;
  model.source_clock_domain = options.source_clock_domain;
  model.target_clock_domain = options.target_clock_domain;
  model.marker_contract = options.marker_contract;
  model.input_observation_count = input_count;
  model.status = ClockCalibrationStatus::kInvalid;
  model.reason = std::move(reason);
  return model;
}

}  // namespace

std::string_view clock_calibration_status_name(
    ClockCalibrationStatus status) {
  switch (status) {
    case ClockCalibrationStatus::kCalibrated:
      return "calibrated";
    case ClockCalibrationStatus::kCandidateOnly:
      return "candidate_only";
    case ClockCalibrationStatus::kSyntheticOnly:
      return "synthetic_only";
    case ClockCalibrationStatus::kInvalid:
      return "invalid";
  }
  return "invalid";
}

ClockCalibrationModel fit_affine_clock_model(
    const std::vector<ClockCalibrationObservation>& observations,
    const ClockCalibrationOptions& options) {
  if (options.source_clock_domain.empty() ||
      options.target_clock_domain.empty()) {
    return invalid_model(options, observations.size(),
                         "source and target clock domains are required");
  }
  if (options.marker_contract.empty()) {
    return invalid_model(options, observations.size(),
                         "an explicit marker contract is required");
  }
  if (observations.size() > options.maximum_observation_count) {
    return invalid_model(options, observations.size(),
                         "observation count exceeds the bounded robust fit");
  }
  if (observations.size() < 6) {
    return invalid_model(options, observations.size(),
                         "calibration requires at least six observations");
  }

  std::set<std::string> marker_ids;
  std::vector<const ClockCalibrationObservation*> ordered;
  ordered.reserve(observations.size());
  for (const ClockCalibrationObservation& observation : observations) {
    if (observation.marker_id.empty()) {
      return invalid_model(options, observations.size(),
                           "empty marker_id violates the marker contract");
    }
    if (!marker_ids.insert(observation.marker_id).second) {
      return invalid_model(options, observations.size(),
                           "duplicate marker_id violates the marker contract");
    }
    ordered.push_back(&observation);
  }
  std::sort(ordered.begin(), ordered.end(), observation_less);

  ClockCalibrationModel model;
  model.source_clock_domain = options.source_clock_domain;
  model.target_clock_domain = options.target_clock_domain;
  model.marker_contract = options.marker_contract;
  model.input_observation_count = observations.size();

  std::vector<const ClockCalibrationObservation*> fit;
  std::vector<const ClockCalibrationObservation*> validation;
  for (std::size_t index = 0; index < ordered.size(); ++index) {
    const bool every_fifth = ((index + 1u) % 5u) == 0u;
    const bool endpoint = index == 0 || index + 1u == ordered.size();
    if (every_fifth && !endpoint) {
      validation.push_back(ordered[index]);
      model.validation_marker_ids.push_back(ordered[index]->marker_id);
    } else {
      fit.push_back(ordered[index]);
      model.fit_marker_ids.push_back(ordered[index]->marker_id);
    }
  }
  model.fit_observation_count = fit.size();
  model.validation_observation_count = validation.size();
  if (validation.empty()) {
    model.status = ClockCalibrationStatus::kInvalid;
    model.reason = "calibration holdout set is empty";
    return model;
  }

  std::vector<long double> slopes;
  slopes.reserve(fit.size() * (fit.size() - 1u) / 2u);
  for (std::size_t first = 0; first < fit.size(); ++first) {
    for (std::size_t second = first + 1; second < fit.size(); ++second) {
      const long double source_delta =
          static_cast<long double>(fit[second]->source_timestamp_ns) -
          static_cast<long double>(fit[first]->source_timestamp_ns);
      if (source_delta == 0.0L) {
        continue;
      }
      const long double target_delta =
          static_cast<long double>(fit[second]->target_timestamp_ns) -
          static_cast<long double>(fit[first]->target_timestamp_ns);
      slopes.push_back(target_delta / source_delta);
    }
  }
  if (slopes.empty()) {
    model.status = ClockCalibrationStatus::kInvalid;
    model.reason = "fit observations do not contain distinct source times";
    return model;
  }
  model.scale = median(std::move(slopes));
  if (!std::isfinite(model.scale) || model.scale <= 0.0L) {
    model.status = ClockCalibrationStatus::kInvalid;
    model.reason = "Theil-Sen fit produced a non-positive clock scale";
    return model;
  }

  std::vector<long double> source_times;
  source_times.reserve(fit.size());
  for (const ClockCalibrationObservation* item : fit) {
    source_times.push_back(
        static_cast<long double>(item->source_timestamp_ns));
  }
  model.reference_source_ns = median(std::move(source_times));

  std::vector<long double> target_reference_candidates;
  target_reference_candidates.reserve(fit.size());
  for (const ClockCalibrationObservation* item : fit) {
    target_reference_candidates.push_back(
        static_cast<long double>(item->target_timestamp_ns) -
        model.scale *
            (static_cast<long double>(item->source_timestamp_ns) -
             model.reference_source_ns));
  }
  model.reference_target_ns = median(std::move(target_reference_candidates));
  model.offset_ns = model.reference_target_ns - model.reference_source_ns;
  model.intercept_ns =
      model.reference_target_ns - model.scale * model.reference_source_ns;
  model.drift_ppm = (model.scale - 1.0L) * 1000000.0L;

  std::vector<long double> residuals;
  residuals.reserve(validation.size());
  for (const ClockCalibrationObservation* item : validation) {
    const long double mapped =
        model.reference_target_ns +
        model.scale *
            (static_cast<long double>(item->source_timestamp_ns) -
             model.reference_source_ns);
    residuals.push_back(
        std::fabs(static_cast<long double>(item->target_timestamp_ns) - mapped));
  }
  model.absolute_residual_p50_ns = nearest_rank_percentile(residuals, 0.50L);
  model.absolute_residual_p95_ns = nearest_rank_percentile(residuals, 0.95L);
  model.absolute_residual_max_ns =
      *std::max_element(residuals.begin(), residuals.end());

  std::vector<long double> uncertainties;
  uncertainties.reserve(observations.size());
  for (const ClockCalibrationObservation& item : observations) {
    uncertainties.push_back(
        static_cast<long double>(item.target_uncertainty_ns));
  }
  model.target_uncertainty_p95_ns =
      nearest_rank_percentile(std::move(uncertainties), 0.95L);
  const long double epsilon = model.absolute_residual_p95_ns +
                              model.target_uncertainty_p95_ns;
  if (!std::isfinite(epsilon) || epsilon < 0.0L ||
      epsilon > static_cast<long double>(
                    std::numeric_limits<std::uint64_t>::max())) {
    model.status = ClockCalibrationStatus::kInvalid;
    model.reason = "calibration uncertainty is outside the supported range";
    return model;
  }
  model.epsilon_ns = static_cast<std::uint64_t>(std::ceil(epsilon));

  if (options.synthetic_fixture) {
    model.status = ClockCalibrationStatus::kSyntheticOnly;
    model.reason = "controlled synthetic affine clock calibration";
  } else if (options.marker_contract_validated) {
    model.status = ClockCalibrationStatus::kCalibrated;
    model.reason = "validated marker identity with holdout calibration";
  } else {
    model.status = ClockCalibrationStatus::kCandidateOnly;
    model.reason =
        "affine relation fitted, but marker identity is not independently "
        "validated";
  }
  return model;
}

std::optional<std::int64_t> map_calibrated_clock_timestamp_ns(
    const ClockCalibrationModel& model,
    std::int64_t source_timestamp_ns) {
  if (model.status != ClockCalibrationStatus::kCalibrated &&
      model.status != ClockCalibrationStatus::kSyntheticOnly) {
    return std::nullopt;
  }
  const long double mapped =
      model.reference_target_ns +
      model.scale * (static_cast<long double>(source_timestamp_ns) -
                     model.reference_source_ns);
  return round_half_to_even(mapped);
}

std::optional<std::int64_t> map_clock_timestamp_ns_for_display(
    const ClockCalibrationModel& model,
    std::int64_t source_timestamp_ns) {
  if (model.status == ClockCalibrationStatus::kInvalid) {
    return std::nullopt;
  }
  const long double mapped =
      model.reference_target_ns +
      model.scale * (static_cast<long double>(source_timestamp_ns) -
                     model.reference_source_ns);
  return round_half_to_even(mapped);
}

}  // namespace traceloom
