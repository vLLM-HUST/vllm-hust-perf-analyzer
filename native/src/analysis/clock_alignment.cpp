#include "traceloom/analysis/clock_alignment.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace traceloom {
namespace {

struct Observation {
  const ClockMarkerRow* marker = nullptr;
  long double host_ns = 0.0L;
  long double record_host_ns = 0.0L;
  long double profiler_host_ns = 0.0L;
  long double device_ns = 0.0L;
  long double half_bracket_ns = 0.0L;
  long double half_record_bracket_ns = 0.0L;
};

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

long double host_midpoint(const ClockMarkerRow& marker) {
  // Accepted timestamps are non-negative and ordered, making this exact
  // contract form overflow-safe: the difference cannot overflow and the
  // result cannot exceed host_after_ns.
  return static_cast<long double>(
      marker.host_before_ns +
      (marker.host_after_ns - marker.host_before_ns) / 2);
}

long double profiler_host_midpoint(const ClockMarkerRow& marker) {
  return static_cast<long double>(
      marker.profiler_host_start_ns +
      (marker.profiler_host_end_ns - marker.profiler_host_start_ns) / 2);
}

long double record_host_midpoint(const ClockMarkerRow& marker) {
  return static_cast<long double>(
      marker.host_before_ns +
      (marker.record_after_ns - marker.host_before_ns) / 2);
}

bool observation_less(const Observation& lhs, const Observation& rhs) {
  if (lhs.host_ns != rhs.host_ns) {
    return lhs.host_ns < rhs.host_ns;
  }
  if (lhs.device_ns != rhs.device_ns) {
    return lhs.device_ns < rhs.device_ns;
  }
  return lhs.marker->id < rhs.marker->id;
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
  } else if (magnitude == 0.5L) {
    const long double parity = std::fmod(std::fabs(integer), 2.0L);
    if (parity == 1.0L) {
      rounded += std::copysign(1.0L, fraction);
    }
  }
  if (rounded <
          static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
      rounded >
          static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(rounded);
}

ClockModel fit_device(std::uint32_t device_id,
                      const std::vector<const ClockMarkerRow*>& markers,
                      const std::set<std::uint32_t>& duplicate_marker_symbols,
                      bool synthetic_fixture) {
  ClockModel model;
  model.device_id = device_id;
  model.input_marker_count = markers.size();
  model.input_markers.reserve(markers.size());
  for (const ClockMarkerRow* marker : markers) {
    model.input_markers.push_back(marker->id);
  }
  if (markers.empty()) {
    model.alignment_status = AlignmentStatus::kUncalibrated;
    model.reason = "no clock markers were supplied for the device";
    return model;
  }

  std::set<std::uint32_t> seen_marker_symbols;
  std::vector<Observation> observations;
  observations.reserve(markers.size());
  bool duplicate_marker_id = false;
  bool missing_marker_id = false;
  for (const ClockMarkerRow* marker : markers) {
    const bool missing = !marker->marker_symbol_id.valid();
    const bool duplicate =
        !missing &&
        (duplicate_marker_symbols.count(marker->marker_symbol_id.value()) != 0 ||
         !seen_marker_symbols.insert(marker->marker_symbol_id.value()).second);
    missing_marker_id = missing_marker_id || missing;
    duplicate_marker_id = duplicate_marker_id || duplicate;
    const bool invalid_profiler_interval =
        marker->has_profiler_host_interval &&
        (marker->profiler_host_start_ns < 0 ||
         marker->profiler_host_end_ns < marker->profiler_host_start_ns);
    const bool missing_real_profiler_interval =
        !synthetic_fixture && !marker->has_profiler_host_interval;
    const bool invalid_record_bracket =
        marker->has_record_host_bracket &&
        (marker->record_after_ns < marker->host_before_ns ||
         marker->record_after_ns > marker->host_after_ns);
    const bool missing_real_record_bracket =
        !synthetic_fixture && !marker->has_record_host_bracket;
    if (marker->return_status != 0 || marker->host_before_ns < 0 ||
        marker->device_timestamp_ns < 0 ||
        marker->host_after_ns < marker->host_before_ns ||
        invalid_profiler_interval || missing_real_profiler_interval ||
        invalid_record_bracket || missing_real_record_bracket || missing ||
        duplicate) {
      model.rejected_markers.push_back(marker->id);
      continue;
    }
    const std::uint64_t width = static_cast<std::uint64_t>(
        marker->host_after_ns - marker->host_before_ns);
    const long double marker_host = host_midpoint(*marker);
    const long double record_host = marker->has_record_host_bracket
                                        ? record_host_midpoint(*marker)
                                        : marker_host;
    const std::uint64_t record_width = marker->has_record_host_bracket
                                           ? static_cast<std::uint64_t>(
                                                 marker->record_after_ns -
                                                 marker->host_before_ns)
                                           : 0;
    observations.push_back(Observation{
        marker, marker_host, record_host,
        marker->has_profiler_host_interval
            ? profiler_host_midpoint(*marker)
            : record_host,
        static_cast<long double>(marker->device_timestamp_ns),
        static_cast<long double>(width) / 2.0L,
        static_cast<long double>(record_width) / 2.0L});
    if (marker->resolution_method ==
        ClockMarkerResolutionMethod::kDirectOverlap) {
      ++model.direct_overlap_marker_count;
    } else if (marker->resolution_method ==
               ClockMarkerResolutionMethod::kOrdinalAffineFallback) {
      ++model.ordinal_affine_fallback_marker_count;
    }
  }
  model.inlier_marker_count = observations.size();
  model.rejected_marker_count = model.rejected_markers.size();
  if (missing_marker_id) {
    model.alignment_status = AlignmentStatus::kInvalid;
    model.reason = "empty marker_id violates the calibration contract";
    return model;
  }
  if (duplicate_marker_id) {
    model.alignment_status = AlignmentStatus::kInvalid;
    model.reason = "duplicate marker_id violates the calibration contract";
    return model;
  }
  if (observations.size() < 6) {
    model.alignment_status = AlignmentStatus::kInvalid;
    model.reason = "calibration requires at least six valid markers";
    return model;
  }
  std::sort(observations.begin(), observations.end(), observation_less);

  std::vector<const Observation*> fit;
  std::vector<const Observation*> validation;
  for (std::size_t index = 0; index < observations.size(); ++index) {
    const bool every_fifth = ((index + 1u) % 5u) == 0u;
    const bool endpoint = index == 0 || index + 1u == observations.size();
    if (every_fifth && !endpoint) {
      validation.push_back(&observations[index]);
      model.validation_markers.push_back(observations[index].marker->id);
    } else {
      fit.push_back(&observations[index]);
      model.fit_markers.push_back(observations[index].marker->id);
    }
  }
  model.fit_marker_count = fit.size();
  model.validation_marker_count = validation.size();
  if (validation.empty()) {
    model.alignment_status = AlignmentStatus::kInvalid;
    model.reason = "calibration holdout set is empty";
    return model;
  }

  std::vector<long double> slopes;
  slopes.reserve(fit.size() * (fit.size() - 1u) / 2u);
  for (std::size_t first = 0; first < fit.size(); ++first) {
    for (std::size_t second = first + 1; second < fit.size(); ++second) {
      const long double host_delta =
          fit[second]->host_ns - fit[first]->host_ns;
      if (host_delta == 0.0L) {
        continue;
      }
      slopes.push_back((fit[second]->device_ns - fit[first]->device_ns) /
                       host_delta);
    }
  }
  if (slopes.empty()) {
    model.alignment_status = AlignmentStatus::kInvalid;
    model.reason = "fit markers do not contain two distinct host times";
    return model;
  }
  model.scale = median(std::move(slopes));
  if (!std::isfinite(model.scale) || model.scale <= 0.0L) {
    model.alignment_status = AlignmentStatus::kInvalid;
    model.reason = "Theil-Sen fit produced a non-positive clock scale";
    return model;
  }

  std::vector<long double> fit_host_times;
  fit_host_times.reserve(fit.size());
  for (const Observation* item : fit) {
    fit_host_times.push_back(item->host_ns);
  }
  model.reference_host_ns = median(std::move(fit_host_times));
  std::vector<long double> reference_device_candidates;
  reference_device_candidates.reserve(fit.size());
  for (const Observation* item : fit) {
    reference_device_candidates.push_back(
        item->device_ns -
        model.scale * (item->host_ns - model.reference_host_ns));
  }
  model.reference_device_ns =
      median(std::move(reference_device_candidates));
  model.offset_ns = model.reference_device_ns - model.reference_host_ns;
  model.drift_ppm = (model.scale - 1.0L) * 1000000.0L;
  model.marker_to_device_scale = model.scale;
  model.marker_to_device_offset_ns = model.offset_ns;
  model.reference_marker_host_ns = model.reference_host_ns;
  model.marker_reference_device_ns = model.reference_device_ns;
  model.marker_to_device_drift_ppm = model.drift_ppm;

  std::vector<long double> validation_residuals;
  validation_residuals.reserve(validation.size());
  for (const Observation* item : validation) {
    const long double mapped =
        model.reference_device_ns +
        model.scale * (item->host_ns - model.reference_host_ns);
    validation_residuals.push_back(std::fabs(item->device_ns - mapped));
  }
  model.absolute_residual_p50_ns =
      nearest_rank_percentile(validation_residuals, 0.50L);
  model.absolute_residual_p95_ns =
      nearest_rank_percentile(validation_residuals, 0.95L);
  model.absolute_residual_max_ns =
      *std::max_element(validation_residuals.begin(),
                        validation_residuals.end());

  // Fit the first leg from timestamps around the same physical API:
  // profiler CANN_API aclrtRecordEvent midpoint -> caller CLOCK_REALTIME
  // record-call bracket midpoint. The outer record->synchronize bracket is
  // reserved for the marker->device leg and must not leak into this fit.
  std::vector<long double> host_clock_slopes;
  host_clock_slopes.reserve(fit.size() * (fit.size() - 1u) / 2u);
  for (std::size_t first = 0; first < fit.size(); ++first) {
    for (std::size_t second = first + 1; second < fit.size(); ++second) {
      const long double profiler_delta =
          fit[second]->profiler_host_ns - fit[first]->profiler_host_ns;
      if (profiler_delta == 0.0L) {
        continue;
      }
      host_clock_slopes.push_back(
          (fit[second]->record_host_ns - fit[first]->record_host_ns) /
          profiler_delta);
    }
  }
  if (host_clock_slopes.empty()) {
    model.alignment_status = AlignmentStatus::kInvalid;
    model.reason =
        "fit markers do not contain two distinct profiler host times";
    return model;
  }
  model.profiler_to_marker_scale = median(std::move(host_clock_slopes));
  if (!std::isfinite(model.profiler_to_marker_scale) ||
      model.profiler_to_marker_scale <= 0.0L) {
    model.alignment_status = AlignmentStatus::kInvalid;
    model.reason =
        "profiler-host Theil-Sen fit produced a non-positive clock scale";
    return model;
  }
  std::vector<long double> fit_profiler_times;
  fit_profiler_times.reserve(fit.size());
  for (const Observation* item : fit) {
    fit_profiler_times.push_back(item->profiler_host_ns);
  }
  model.reference_profiler_host_ns = median(std::move(fit_profiler_times));
  std::vector<long double> marker_reference_candidates;
  marker_reference_candidates.reserve(fit.size());
  for (const Observation* item : fit) {
    marker_reference_candidates.push_back(
        item->record_host_ns -
        model.profiler_to_marker_scale *
            (item->profiler_host_ns - model.reference_profiler_host_ns));
  }
  model.profiler_reference_marker_ns =
      median(std::move(marker_reference_candidates));
  model.profiler_to_marker_offset_ns =
      model.profiler_reference_marker_ns - model.reference_profiler_host_ns;
  model.profiler_to_marker_drift_ppm =
      (model.profiler_to_marker_scale - 1.0L) * 1000000.0L;

  std::vector<long double> host_clock_validation_residuals;
  host_clock_validation_residuals.reserve(validation.size());
  for (const Observation* item : validation) {
    const long double mapped_marker_host =
        model.profiler_reference_marker_ns +
        model.profiler_to_marker_scale *
            (item->profiler_host_ns - model.reference_profiler_host_ns);
    host_clock_validation_residuals.push_back(
        std::fabs(item->record_host_ns - mapped_marker_host));
  }
  model.host_clock_absolute_residual_p50_ns =
      nearest_rank_percentile(host_clock_validation_residuals, 0.50L);
  model.host_clock_absolute_residual_p95_ns =
      nearest_rank_percentile(host_clock_validation_residuals, 0.95L);
  model.host_clock_absolute_residual_max_ns =
      *std::max_element(host_clock_validation_residuals.begin(),
                        host_clock_validation_residuals.end());
  model.host_clock_uncertainty_p95_ns =
      std::fabs(model.marker_to_device_scale) *
      model.host_clock_absolute_residual_p95_ns;

  std::vector<long double> profiler_caller_bracket_uncertainties;
  profiler_caller_bracket_uncertainties.reserve(observations.size());
  for (const Observation& item : observations) {
    profiler_caller_bracket_uncertainties.push_back(
        std::fabs(model.marker_to_device_scale) *
        item.half_record_bracket_ns);
  }
  model.profiler_to_caller_bracket_uncertainty_p95_ns =
      nearest_rank_percentile(
          std::move(profiler_caller_bracket_uncertainties), 0.95L);

  // Publish the composed profiler-host -> device model consumed by E4. Keep the
  // component parameters above so reviewers can audit both coordinate changes.
  model.scale =
      model.marker_to_device_scale * model.profiler_to_marker_scale;
  model.reference_host_ns = model.reference_profiler_host_ns;
  const long double marker_at_profiler_reference =
      model.profiler_reference_marker_ns;
  model.reference_device_ns =
      model.marker_reference_device_ns +
      model.marker_to_device_scale *
          (marker_at_profiler_reference - model.reference_marker_host_ns);
  model.offset_ns = model.reference_device_ns - model.reference_host_ns;
  model.intercept_ns =
      model.reference_device_ns - model.scale * model.reference_host_ns;
  model.drift_ppm = (model.scale - 1.0L) * 1000000.0L;
  model.has_profiler_host_mapping = true;

  std::vector<long double> composed_validation_residuals;
  composed_validation_residuals.reserve(validation.size());
  for (const Observation* item : validation) {
    const long double mapped_device =
        model.reference_device_ns +
        model.scale * (item->profiler_host_ns - model.reference_host_ns);
    composed_validation_residuals.push_back(
        std::fabs(item->device_ns - mapped_device));
  }
  model.composed_absolute_residual_p50_ns =
      nearest_rank_percentile(composed_validation_residuals, 0.50L);
  model.composed_absolute_residual_p95_ns =
      nearest_rank_percentile(composed_validation_residuals, 0.95L);
  model.composed_absolute_residual_max_ns =
      *std::max_element(composed_validation_residuals.begin(),
                        composed_validation_residuals.end());

  std::vector<long double> bracket_uncertainties;
  bracket_uncertainties.reserve(observations.size());
  for (const Observation& item : observations) {
    bracket_uncertainties.push_back(
        std::fabs(model.marker_to_device_scale) * item.half_bracket_ns);
  }
  model.bracket_uncertainty_p95_ns =
      nearest_rank_percentile(std::move(bracket_uncertainties), 0.95L);
  const long double epsilon = model.absolute_residual_p95_ns +
                              model.bracket_uncertainty_p95_ns +
                              model.host_clock_uncertainty_p95_ns +
                              model.profiler_to_caller_bracket_uncertainty_p95_ns;
  if (!std::isfinite(epsilon) || epsilon < 0.0L ||
      epsilon > static_cast<long double>(
                    std::numeric_limits<std::uint64_t>::max())) {
    model.alignment_status = AlignmentStatus::kInvalid;
    model.reason = "calibration uncertainty is outside the supported range";
    return model;
  }
  model.epsilon_ns = static_cast<std::uint64_t>(std::ceil(epsilon));
  model.alignment_status = synthetic_fixture
                               ? AlignmentStatus::kSyntheticOnly
                               : AlignmentStatus::kCalibrated;
  model.reason = synthetic_fixture
                     ? "controlled synthetic composed clock calibration"
                     : "validated profiler-host, caller-host, and device "
                       "holdout calibration";
  return model;
}

}  // namespace

std::string_view alignment_status_name(AlignmentStatus status) {
  switch (status) {
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

bool alignment_supports_cross_clock_evidence(AlignmentStatus status) {
  return status == AlignmentStatus::kCalibrated ||
         status == AlignmentStatus::kSyntheticOnly;
}

bool clock_model_supports_cross_clock_evidence(const ClockModel& model) {
  return alignment_supports_cross_clock_evidence(model.alignment_status) &&
         model.has_profiler_host_mapping;
}

const ClockModel* ClockAlignmentRunResult::find_device(
    std::uint32_t device_id) const {
  const auto found = std::lower_bound(
      models.begin(), models.end(), device_id,
      [](const ClockModel& model, std::uint32_t value) {
        return model.device_id < value;
      });
  return found != models.end() && found->device_id == device_id ? &*found
                                                                : nullptr;
}

ClockAlignmentRunResult fit_host_device_clock_models(
    const NativeIr& ir,
    const ClockAlignmentOptions& options) {
  std::map<std::uint32_t, std::vector<const ClockMarkerRow*>> by_device;
  std::map<std::uint32_t, std::size_t> marker_symbol_counts;
  for (const TaskRow& task : ir.tasks.rows()) {
    if (task.trace_event_id.valid() &&
        task.trace_event_id.value() < ir.trace_events.size()) {
      by_device[ir.trace_events.row(task.trace_event_id).device_id];
    }
  }
  for (const ClockMarkerRow& marker : ir.clock_markers.rows()) {
    by_device[marker.device_id].push_back(&marker);
    if (marker.marker_symbol_id.valid()) {
      ++marker_symbol_counts[marker.marker_symbol_id.value()];
    }
  }
  std::set<std::uint32_t> duplicate_marker_symbols;
  for (const auto& count : marker_symbol_counts) {
    if (count.second > 1) {
      duplicate_marker_symbols.insert(count.first);
    }
  }
  ClockAlignmentRunResult run;
  run.models.reserve(by_device.size());
  for (const auto& item : by_device) {
    run.models.push_back(
        fit_device(item.first, item.second, duplicate_marker_symbols,
                   options.synthetic_fixture));
  }
  return run;
}

std::optional<std::int64_t> map_host_to_device_ns(
    const ClockModel& model,
    std::int64_t host_timestamp_ns) {
  if (!clock_model_supports_cross_clock_evidence(model)) {
    return std::nullopt;
  }
  const long double mapped =
      model.reference_device_ns +
      model.scale * (static_cast<long double>(host_timestamp_ns) -
                     model.reference_host_ns);
  return round_half_to_even(mapped);
}

}  // namespace traceloom
