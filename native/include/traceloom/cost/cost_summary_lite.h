#pragma once

#include <cstdint>
#include <vector>

#include "traceloom/ir/anchor_table.h"

namespace traceloom {

struct CostByAnchorKind {
  AnchorKind kind = AnchorKind::kUnknown;
  std::uint64_t anchor_count = 0;
  std::int64_t duration_ns = 0;
};

struct CostSummaryLite {
  std::uint64_t anchor_count = 0;
  std::int64_t total_duration_ns = 0;
  std::vector<CostByAnchorKind> by_kind;
};

CostSummaryLite summarize_anchor_costs(const AnchorTable& anchors);

}  // namespace traceloom
