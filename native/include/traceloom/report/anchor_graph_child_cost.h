#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/core/diagnostics.h"
#include "traceloom/core/ids.h"
#include "traceloom/report/anchor_internal_cost_breakdown.h"

namespace traceloom {

struct AnchorGraphChildWindow {
  std::uint32_t token_ordinal = 0;
  std::uint32_t stream_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::string diagnostic_flags;
};

struct AnchorGraphChildTask {
  std::uint32_t stream_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::int64_t duration_ns = 0;
  std::string op;
  SourceRefId source_ref_id;
};

struct AnchorGraphChildCostConfig {
  ReportCostLeafId first_leaf_id{0};
  std::uint32_t max_top_ops = 3;
  bool emit_zero_duration_windows = true;
  bool diagnostic_only_partial_overlap = true;
};

struct AnchorGraphChildCostResult {
  std::vector<AnchorCostComponentLeaf> component_leaves;
  std::vector<Diagnostic> diagnostics;
};

AnchorGraphChildCostResult build_anchor_graph_child_cost_components(
    const std::vector<AnchorGraphChildWindow>& windows,
    const std::vector<AnchorGraphChildTask>& tasks,
    const AnchorGraphChildCostConfig& config = AnchorGraphChildCostConfig{});

}  // namespace traceloom
