#include "traceloom/cost/cost_summary_lite.h"

#include <algorithm>
#include <stdexcept>

namespace traceloom {

namespace {

std::uint32_t kind_rank(AnchorKind kind) {
  return static_cast<std::uint32_t>(kind);
}

}  // namespace

CostSummaryLite summarize_anchor_costs(const AnchorTable& anchors) {
  CostSummaryLite summary;

  for (const AnchorRow& anchor : anchors.rows()) {
    if (anchor.end_ns < anchor.start_ns) {
      throw std::invalid_argument("anchor end_ns is before start_ns");
    }

    const std::int64_t duration = anchor.end_ns - anchor.start_ns;
    summary.anchor_count += 1;
    summary.total_duration_ns += duration;

    auto found = std::find_if(summary.by_kind.begin(), summary.by_kind.end(),
                              [&](const CostByAnchorKind& row) {
                                return row.kind == anchor.kind;
                              });
    if (found == summary.by_kind.end()) {
      summary.by_kind.push_back(CostByAnchorKind{anchor.kind, 1, duration});
    } else {
      found->anchor_count += 1;
      found->duration_ns += duration;
    }
  }

  std::sort(summary.by_kind.begin(), summary.by_kind.end(),
            [](const CostByAnchorKind& lhs, const CostByAnchorKind& rhs) {
              return kind_rank(lhs.kind) < kind_rank(rhs.kind);
            });
  return summary;
}

}  // namespace traceloom
