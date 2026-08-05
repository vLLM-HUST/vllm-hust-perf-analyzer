#include "traceloom/analysis/graph_body_performance_comparison.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace traceloom {
namespace {

double median(std::vector<std::uint64_t> values) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2;
  if (values.size() % 2 != 0) {
    return static_cast<double>(values[middle]);
  }
  return (static_cast<double>(values[middle - 1]) +
          static_cast<double>(values[middle])) /
         2.0;
}

class DeterministicRandom {
 public:
  explicit DeterministicRandom(std::uint64_t state)
      : state_(state == 0 ? 1 : state) {}

  std::uint64_t next() {
    state_ ^= state_ >> 12;
    state_ ^= state_ << 25;
    state_ ^= state_ >> 27;
    return state_ * 2685821657736338717ULL;
  }

  std::size_t index(std::size_t size) {
    return static_cast<std::size_t>(next() % size);
  }

 private:
  std::uint64_t state_;
};

std::vector<std::uint64_t> block_resample(
    const std::vector<std::uint64_t>& values,
    DeterministicRandom& random) {
  std::vector<std::uint64_t> sampled;
  sampled.reserve(values.size());
  const std::size_t block_size = static_cast<std::size_t>(
      std::ceil(std::sqrt(static_cast<double>(values.size()))));
  while (sampled.size() < values.size()) {
    const std::size_t start = random.index(values.size());
    for (std::size_t offset = 0;
         offset < block_size && sampled.size() < values.size(); ++offset) {
      sampled.push_back(values[(start + offset) % values.size()]);
    }
  }
  return sampled;
}

double percentile(const std::vector<double>& sorted, double probability) {
  if (sorted.empty()) {
    return 0.0;
  }
  const double position = probability *
                          static_cast<double>(sorted.size() - 1);
  const std::size_t low = static_cast<std::size_t>(std::floor(position));
  const std::size_t high = static_cast<std::size_t>(std::ceil(position));
  const double fraction = position - static_cast<double>(low);
  return sorted[low] * (1.0 - fraction) + sorted[high] * fraction;
}

GraphBodyPerformanceVerdict metric_verdict(double low,
                                           double high,
                                           double minimum_effect_pct) {
  if (low > minimum_effect_pct) {
    return GraphBodyPerformanceVerdict::kFaster;
  }
  if (high < -minimum_effect_pct) {
    return GraphBodyPerformanceVerdict::kSlower;
  }
  if (low >= -minimum_effect_pct && high <= minimum_effect_pct) {
    return GraphBodyPerformanceVerdict::kEquivalent;
  }
  return GraphBodyPerformanceVerdict::kInconclusive;
}

GraphBodyMetricComparison compare_metric(
    std::string metric,
    const std::vector<std::uint64_t>& baseline,
    const std::vector<std::uint64_t>& candidate,
    const GraphBodyPerformanceComparisonConfig& config,
    std::uint64_t seed_offset) {
  GraphBodyMetricComparison result;
  result.metric = std::move(metric);
  result.baseline_count = baseline.size();
  result.candidate_count = candidate.size();
  result.baseline_median_ns = median(baseline);
  result.candidate_median_ns = median(candidate);
  if (baseline.empty() || candidate.empty()) {
    return result;
  }
  if (result.baseline_median_ns == 0.0 &&
      result.candidate_median_ns == 0.0 &&
      std::all_of(baseline.begin(), baseline.end(),
                  [](std::uint64_t value) { return value == 0; }) &&
      std::all_of(candidate.begin(), candidate.end(),
                  [](std::uint64_t value) { return value == 0; })) {
    result.verdict = GraphBodyPerformanceVerdict::kEquivalent;
    return result;
  }
  if (result.baseline_median_ns <= 0.0) {
    return result;
  }
  result.improvement_pct =
      100.0 * (result.baseline_median_ns - result.candidate_median_ns) /
      result.baseline_median_ns;
  DeterministicRandom random(config.random_seed ^ seed_offset);
  std::vector<double> improvements;
  improvements.reserve(config.bootstrap_iterations);
  for (std::size_t iteration = 0;
       iteration < config.bootstrap_iterations; ++iteration) {
    const double baseline_median = median(block_resample(baseline, random));
    const double candidate_median = median(block_resample(candidate, random));
    if (baseline_median > 0.0) {
      improvements.push_back(
          100.0 * (baseline_median - candidate_median) / baseline_median);
    }
  }
  std::sort(improvements.begin(), improvements.end());
  const double tail = (1.0 - config.confidence_level) / 2.0;
  result.confidence_low_pct = percentile(improvements, tail);
  result.confidence_high_pct = percentile(improvements, 1.0 - tail);
  result.verdict = metric_verdict(result.confidence_low_pct,
                                  result.confidence_high_pct,
                                  config.minimum_effect_pct);
  return result;
}

using MetricAccessor =
    const std::vector<std::uint64_t>& (*)(const GraphBodyProfileSample&);

const std::vector<std::uint64_t>& task_sum(
    const GraphBodyProfileSample& sample) {
  return sample.task_sum_ns;
}

const std::vector<std::uint64_t>& busy_union(
    const GraphBodyProfileSample& sample) {
  return sample.busy_union_ns;
}

const std::vector<std::uint64_t>& envelope(
    const GraphBodyProfileSample& sample) {
  return sample.envelope_ns;
}

const std::vector<std::uint64_t>& compute(
    const GraphBodyProfileSample& sample) {
  return sample.compute_ns;
}

const std::vector<std::uint64_t>& communication(
    const GraphBodyProfileSample& sample) {
  return sample.communication_ns;
}

const std::vector<std::uint64_t>& data_move(
    const GraphBodyProfileSample& sample) {
  return sample.data_move_ns;
}

std::vector<std::uint64_t> rank_critical_samples(
    const std::vector<GraphBodyProfileSample>& profiles,
    MetricAccessor accessor) {
  if (profiles.empty()) {
    return {};
  }
  const std::size_t count = accessor(profiles.front()).size();
  std::vector<std::uint64_t> result(count, 0);
  for (const GraphBodyProfileSample& profile : profiles) {
    const auto& values = accessor(profile);
    if (values.size() != count) {
      return {};
    }
    for (std::size_t index = 0; index < count; ++index) {
      result[index] = std::max(result[index], values[index]);
    }
  }
  return result;
}

bool homogeneous_variant(const std::vector<GraphBodyProfileSample>& profiles,
                         GraphBodyVariantSummary& summary,
                         std::vector<std::string>& reasons,
                         const std::string& prefix) {
  if (profiles.empty()) {
    reasons.push_back(prefix + "_missing_profiles");
    return false;
  }
  summary.profile_count = profiles.size();
  const GraphBodyProfileSample& first = profiles.front();
  summary.rank_critical_sample_count = first.envelope_ns.size();
  summary.stream_count = first.stream_count;
  summary.compute_task_count = first.compute_task_count;
  summary.communication_task_count = first.communication_task_count;
  summary.data_move_task_count = first.data_move_task_count;
  summary.replay_unit_launch_count = first.replay_unit_launch_count;
  std::set<std::uint32_t> devices;
  bool supported = true;
  for (const GraphBodyProfileSample& profile : profiles) {
    if (!devices.insert(profile.device_id).second) {
      reasons.push_back(prefix + "_duplicate_device_id");
      supported = false;
    }
    if (profile.envelope_ns.size() != summary.rank_critical_sample_count) {
      reasons.push_back(prefix + "_rank_sample_count_mismatch");
      supported = false;
    }
    const std::size_t count = profile.envelope_ns.size();
    if (profile.busy_union_ns.size() != count ||
        profile.task_sum_ns.size() != count ||
        profile.compute_ns.size() != count ||
        profile.communication_ns.size() != count ||
        profile.data_move_ns.size() != count) {
      reasons.push_back(prefix + "_profile_metric_count_mismatch");
      supported = false;
    }
    if (profile.stream_count != summary.stream_count ||
        profile.compute_task_count != summary.compute_task_count ||
        profile.communication_task_count !=
            summary.communication_task_count ||
        profile.data_move_task_count != summary.data_move_task_count ||
        profile.replay_unit_launch_count !=
            summary.replay_unit_launch_count) {
      reasons.push_back(prefix + "_rank_body_shape_mismatch");
      supported = false;
    }
  }
  summary.device_ids.assign(devices.begin(), devices.end());
  return supported;
}

const GraphBodyMetricComparison* find_metric(
    const GraphBodyPerformanceComparison& comparison,
    const std::string& metric) {
  const auto found = std::find_if(
      comparison.metrics.begin(), comparison.metrics.end(),
      [&](const GraphBodyMetricComparison& row) {
        return row.metric == metric;
      });
  return found == comparison.metrics.end() ? nullptr : &*found;
}

}  // namespace

std::string_view graph_body_performance_verdict_name(
    GraphBodyPerformanceVerdict verdict) {
  switch (verdict) {
    case GraphBodyPerformanceVerdict::kFaster:
      return "faster";
    case GraphBodyPerformanceVerdict::kSlower:
      return "slower";
    case GraphBodyPerformanceVerdict::kEquivalent:
      return "equivalent";
    case GraphBodyPerformanceVerdict::kInconclusive:
      return "inconclusive";
  }
  return "inconclusive";
}

GraphBodyProfileSampleResult extract_graph_body_profile_sample(
    const NativeIr& ir,
    std::string source_path) {
  GraphBodyProfileSampleResult result;
  const GraphBodyCostSummary costs = build_graph_body_cost_summary(ir);
  std::map<ReplayBodyTemplateId::value_type,
           std::vector<const GraphBodyOccurrenceCostRow*>>
      exact_by_template;
  for (const GraphBodyOccurrenceCostRow& row : costs.occurrences) {
    if (row.exact_replay_unit) {
      exact_by_template[row.replay_body_template_id.value()].push_back(&row);
    }
  }
  if (exact_by_template.empty()) {
    result.reason_code = "no_exact_graph_body_sample";
    return result;
  }
  if (exact_by_template.size() != 1) {
    result.reason_code = "ambiguous_exact_graph_body_templates";
    return result;
  }

  std::map<ReplayUnitId::value_type, std::uint32_t> launches_per_unit;
  std::map<GraphLaunchOccurrenceId::value_type, ReplayUnitId::value_type>
      unit_by_occurrence;
  bool duplicate_occurrence_owner = false;
  for (const ReplayUnitLaunchMemberRow& member :
       ir.replay_unit_launch_members.rows()) {
    ++launches_per_unit[member.replay_unit_id.value()];
    if (!unit_by_occurrence
             .emplace(member.graph_launch_occurrence_id.value(),
                      member.replay_unit_id.value())
             .second) {
      duplicate_occurrence_owner = true;
    }
  }
  if (duplicate_occurrence_owner) {
    result.reason_code = "graph_launch_has_multiple_replay_unit_owners";
    return result;
  }
  std::uint32_t unit_launch_count = 0;
  for (const GraphBodyOccurrenceCostRow* row :
       exact_by_template.begin()->second) {
    const auto owner = unit_by_occurrence.find(
        row->graph_launch_occurrence_id.value());
    if (owner == unit_by_occurrence.end()) {
      result.reason_code = "exact_body_missing_replay_unit_owner";
      return result;
    }
    const std::uint32_t count = launches_per_unit.at(owner->second);
    if (unit_launch_count == 0) {
      unit_launch_count = count;
    } else if (count != unit_launch_count) {
      result.reason_code = "variable_replay_unit_launch_count";
      return result;
    }
  }
  if (unit_launch_count != 1) {
    result.reason_code = "multi_launch_replay_unit_requires_selector";
    return result;
  }

  const ReplayBodyTemplateId template_id(exact_by_template.begin()->first);
  const ReplayBodyTemplateRow& body_template =
      ir.replay_body_templates.row(template_id);
  GraphBodyProfileSample sample;
  sample.source_path = std::move(source_path);
  sample.replay_body_template_id = template_id;
  sample.stream_count = body_template.stream_count;
  sample.compute_task_count = body_template.compute_task_count;
  sample.communication_task_count = body_template.communication_task_count;
  sample.data_move_task_count = body_template.data_move_task_count;
  sample.replay_unit_launch_count = unit_launch_count;
  std::vector<const GraphBodyOccurrenceCostRow*> rows =
      exact_by_template.begin()->second;
  std::stable_sort(
      rows.begin(), rows.end(), [&](const GraphBodyOccurrenceCostRow* lhs,
                                    const GraphBodyOccurrenceCostRow* rhs) {
        const GraphLaunchOccurrenceRow& lhs_launch =
            ir.graph_launch_occurrences.row(lhs->graph_launch_occurrence_id);
        const GraphLaunchOccurrenceRow& rhs_launch =
            ir.graph_launch_occurrences.row(rhs->graph_launch_occurrence_id);
        return std::tie(lhs_launch.start_ns, lhs_launch.id) <
               std::tie(rhs_launch.start_ns, rhs_launch.id);
      });
  bool has_device = false;
  for (const GraphBodyOccurrenceCostRow* row : rows) {
    const GraphLaunchOccurrenceRow& launch =
        ir.graph_launch_occurrences.row(row->graph_launch_occurrence_id);
    if (!has_device) {
      sample.device_id = launch.device_id;
      has_device = true;
    } else if (sample.device_id != launch.device_id) {
      result.reason_code = "exact_body_sample_spans_multiple_devices";
      return result;
    }
    sample.task_sum_ns.push_back(row->task_sum_ns);
    sample.busy_union_ns.push_back(row->busy_union_ns);
    sample.envelope_ns.push_back(row->envelope_ns);
    sample.compute_ns.push_back(row->compute_ns);
    sample.communication_ns.push_back(row->communication_ns);
    sample.data_move_ns.push_back(row->data_move_ns);
  }
  result.supported = true;
  result.sample = std::move(sample);
  return result;
}

GraphBodyPerformanceComparison compare_graph_body_performance(
    const std::vector<GraphBodyProfileSample>& baseline,
    const std::vector<GraphBodyProfileSample>& candidate,
    const GraphBodyPerformanceComparisonConfig& config) {
  if (config.bootstrap_iterations == 0) {
    throw std::invalid_argument("bootstrap_iterations must be positive");
  }
  if (config.minimum_sample_count == 0) {
    throw std::invalid_argument("minimum_sample_count must be positive");
  }
  if (!(config.confidence_level > 0.0 && config.confidence_level < 1.0)) {
    throw std::invalid_argument("confidence_level must be between zero and one");
  }
  if (config.minimum_effect_pct < 0.0) {
    throw std::invalid_argument("minimum_effect_pct must be nonnegative");
  }

  GraphBodyPerformanceComparison result;
  result.same_workload_attested = config.same_workload_attested;
  const bool baseline_homogeneous = homogeneous_variant(
      baseline, result.baseline, result.reason_codes, "baseline");
  const bool candidate_homogeneous = homogeneous_variant(
      candidate, result.candidate, result.reason_codes, "candidate");
  bool shape_supported = baseline_homogeneous && candidate_homogeneous;
  if (shape_supported && baseline.size() != candidate.size()) {
    result.reason_codes.push_back("rank_count_mismatch");
    shape_supported = false;
  }
  if (shape_supported &&
      result.baseline.device_ids != result.candidate.device_ids) {
    result.reason_codes.push_back("device_set_mismatch");
    shape_supported = false;
  }
  if (shape_supported &&
      (result.baseline.stream_count != result.candidate.stream_count ||
       result.baseline.replay_unit_launch_count !=
           result.candidate.replay_unit_launch_count)) {
    result.reason_codes.push_back("observable_unit_shape_mismatch");
    shape_supported = false;
  }
  if (shape_supported &&
      (result.baseline.rank_critical_sample_count <
           config.minimum_sample_count ||
       result.candidate.rank_critical_sample_count <
           config.minimum_sample_count)) {
    result.reason_codes.push_back("insufficient_exact_samples");
    shape_supported = false;
  }

  const struct MetricSpec {
    const char* name;
    MetricAccessor accessor;
    std::uint64_t seed;
  } metrics[] = {
      {"envelope_ns", envelope, 0x11},
      {"busy_union_ns", busy_union, 0x22},
      {"task_sum_ns", task_sum, 0x33},
      {"compute_ns", compute, 0x44},
      {"communication_ns", communication, 0x55},
      {"data_move_ns", data_move, 0x66},
  };
  if (baseline_homogeneous && candidate_homogeneous) {
    for (const MetricSpec& metric : metrics) {
      const std::vector<std::uint64_t> baseline_values =
          rank_critical_samples(baseline, metric.accessor);
      const std::vector<std::uint64_t> candidate_values =
          rank_critical_samples(candidate, metric.accessor);
      result.metrics.push_back(compare_metric(
          metric.name, baseline_values, candidate_values, config,
          metric.seed));
    }
  }

  if (!config.same_workload_attested) {
    result.reason_codes.push_back("workload_identity_not_attested");
    shape_supported = false;
  }
  if (!shape_supported) {
    return result;
  }
  const GraphBodyMetricComparison* envelope_metric =
      find_metric(result, "envelope_ns");
  const GraphBodyMetricComparison* busy_metric =
      find_metric(result, "busy_union_ns");
  const GraphBodyMetricComparison* task_metric =
      find_metric(result, "task_sum_ns");
  if (envelope_metric == nullptr || busy_metric == nullptr ||
      task_metric == nullptr) {
    result.reason_codes.push_back("missing_primary_cost_metric");
    return result;
  }
  result.verdict = envelope_metric->verdict;
  const auto contradicts = [&](GraphBodyPerformanceVerdict secondary) {
    return (result.verdict == GraphBodyPerformanceVerdict::kFaster &&
            secondary == GraphBodyPerformanceVerdict::kSlower) ||
           (result.verdict == GraphBodyPerformanceVerdict::kSlower &&
            secondary == GraphBodyPerformanceVerdict::kFaster);
  };
  if (contradicts(busy_metric->verdict) ||
      contradicts(task_metric->verdict)) {
    result.verdict = GraphBodyPerformanceVerdict::kInconclusive;
    result.reason_codes.push_back("secondary_cost_contradicts_envelope");
  } else if (result.verdict == GraphBodyPerformanceVerdict::kEquivalent &&
             (busy_metric->verdict !=
                  GraphBodyPerformanceVerdict::kEquivalent ||
              task_metric->verdict !=
                  GraphBodyPerformanceVerdict::kEquivalent)) {
    result.verdict = GraphBodyPerformanceVerdict::kInconclusive;
    result.reason_codes.push_back("secondary_cost_not_equivalent");
  } else if (result.verdict == GraphBodyPerformanceVerdict::kInconclusive) {
    result.reason_codes.push_back("envelope_confidence_crosses_decision_band");
  }
  return result;
}

}  // namespace traceloom
