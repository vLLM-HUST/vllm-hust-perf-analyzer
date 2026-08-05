#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "traceloom/analysis/graph_body_cost_summary.h"

namespace traceloom {

enum class GraphBodyPerformanceVerdict {
  kFaster,
  kSlower,
  kEquivalent,
  kInconclusive,
};

std::string_view graph_body_performance_verdict_name(
    GraphBodyPerformanceVerdict verdict);

struct GraphBodyProfileSample {
  std::string source_path;
  std::uint32_t device_id = 0;
  ReplayBodyTemplateId replay_body_template_id;
  std::uint32_t stream_count = 0;
  std::uint32_t compute_task_count = 0;
  std::uint32_t communication_task_count = 0;
  std::uint32_t data_move_task_count = 0;
  std::uint32_t replay_unit_launch_count = 0;
  std::vector<std::uint64_t> task_sum_ns;
  std::vector<std::uint64_t> busy_union_ns;
  std::vector<std::uint64_t> envelope_ns;
  std::vector<std::uint64_t> compute_ns;
  std::vector<std::uint64_t> communication_ns;
  std::vector<std::uint64_t> data_move_ns;
};

struct GraphBodyProfileSampleResult {
  bool supported = false;
  std::string reason_code;
  GraphBodyProfileSample sample;
};

// Extracts one chronologically ordered exact, period-one graph-body sample.
// Ambiguous templates and multi-launch ReplayUnits remain unsupported rather
// than being paired by guesswork.
GraphBodyProfileSampleResult extract_graph_body_profile_sample(
    const NativeIr& ir,
    std::string source_path = {});

struct GraphBodyMetricComparison {
  std::string metric;
  std::size_t baseline_count = 0;
  std::size_t candidate_count = 0;
  double baseline_median_ns = 0.0;
  double candidate_median_ns = 0.0;
  double improvement_pct = 0.0;
  double confidence_low_pct = 0.0;
  double confidence_high_pct = 0.0;
  GraphBodyPerformanceVerdict verdict =
      GraphBodyPerformanceVerdict::kInconclusive;
};

struct GraphBodyVariantSummary {
  std::size_t profile_count = 0;
  std::size_t rank_critical_sample_count = 0;
  std::uint32_t stream_count = 0;
  std::uint32_t compute_task_count = 0;
  std::uint32_t communication_task_count = 0;
  std::uint32_t data_move_task_count = 0;
  std::uint32_t replay_unit_launch_count = 0;
  std::vector<std::uint32_t> device_ids;
};

struct GraphBodyPerformanceComparisonConfig {
  bool same_workload_attested = false;
  std::size_t minimum_sample_count = 8;
  std::size_t bootstrap_iterations = 20000;
  double confidence_level = 0.95;
  double minimum_effect_pct = 1.0;
  std::uint64_t random_seed = 0x54524143454c4f4dULL;
};

struct GraphBodyPerformanceComparison {
  GraphBodyPerformanceVerdict verdict =
      GraphBodyPerformanceVerdict::kInconclusive;
  std::vector<std::string> reason_codes;
  std::string aggregation_policy = "ordinal_rank_critical_max";
  bool same_workload_attested = false;
  GraphBodyVariantSummary baseline;
  GraphBodyVariantSummary candidate;
  std::vector<GraphBodyMetricComparison> metrics;
};

// Compares independent baseline/candidate samples without requiring equal
// sample counts across variants. Within a multi-rank variant, equal-length
// per-rank sequences are reduced to the ordinal rank-critical maximum.
// Confidence intervals use a deterministic circular moving-block bootstrap to
// retain local temporal dependence. The envelope is primary; a confident
// contradictory task-sum or busy-union direction downgrades the overall
// result to inconclusive.
GraphBodyPerformanceComparison compare_graph_body_performance(
    const std::vector<GraphBodyProfileSample>& baseline,
    const std::vector<GraphBodyProfileSample>& candidate,
    const GraphBodyPerformanceComparisonConfig& config = {});

}  // namespace traceloom
