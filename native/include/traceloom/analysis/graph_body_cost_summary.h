#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "traceloom/ir/native_ir.h"

namespace traceloom {

enum class GraphBodyCostScope {
  kAllObservedBodies,
  kExactReplayUnits,
};

std::string_view graph_body_cost_scope_name(GraphBodyCostScope scope);

struct GraphBodyOccurrenceCostRow {
  GraphLaunchBodyId graph_launch_body_id;
  GraphLaunchOccurrenceId graph_launch_occurrence_id;
  ReplayBodyTemplateId replay_body_template_id;
  bool exact_replay_unit = false;
  std::uint64_t member_count = 0;
  std::uint64_t compute_ns = 0;
  std::uint64_t communication_ns = 0;
  std::uint64_t data_move_ns = 0;
  std::uint64_t task_sum_ns = 0;
  std::uint64_t busy_union_ns = 0;
  std::uint64_t envelope_ns = 0;
};

struct GraphBodyCostDistributionRow {
  ReplayBodyTemplateId replay_body_template_id;
  GraphBodyCostScope scope = GraphBodyCostScope::kAllObservedBodies;
  std::uint64_t occurrence_count = 0;
  std::uint64_t task_sum_p25_ns = 0;
  std::uint64_t task_sum_median_ns = 0;
  std::uint64_t task_sum_p75_ns = 0;
  std::uint64_t busy_union_median_ns = 0;
  std::uint64_t envelope_median_ns = 0;
  std::uint64_t compute_median_ns = 0;
  std::uint64_t communication_median_ns = 0;
  std::uint64_t data_move_median_ns = 0;
};

struct GraphBodyCostSummary {
  std::vector<GraphBodyOccurrenceCostRow> occurrences;
  std::vector<GraphBodyCostDistributionRow> distributions;
};

// Builds overlap-aware per-occurrence cost evidence from exact graph-body
// membership. `task_sum_ns` preserves scheduled work, `busy_union_ns` removes
// cross-stream double counting, and `envelope_ns` retains the observed wall
// span. Exact-replay distributions may have unequal sample counts across runs.
// Fail-closed: body members whose body id or task/event references are
// invalid or out of range are skipped (never dereferenced, never thrown), so
// malformed IR yields zero-cost/partial rows instead of an exception; valid
// IR produces byte-identical results to strict membership.
GraphBodyCostSummary build_graph_body_cost_summary(const NativeIr& ir);

}  // namespace traceloom
