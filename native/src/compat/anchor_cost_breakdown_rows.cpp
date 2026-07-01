#include "traceloom/compat/anchor_cost_breakdown_rows.h"

#include <utility>

namespace traceloom::compat {

namespace {

double ns_to_us(std::int64_t ns) {
  return static_cast<double>(ns) / 1000.0;
}

}  // namespace

std::vector<AnchorCostBreakdownSqlRow> build_anchor_cost_breakdown_sql_rows(
    const AnchorInternalCostBreakdown& breakdown) {
  std::vector<AnchorCostBreakdownSqlRow> rows;
  rows.reserve(breakdown.rows.size());
  for (const AnchorInternalCostBreakdownRow& source : breakdown.rows) {
    AnchorCostBreakdownSqlRow row;
    row.anchor_idx = source.anchor_idx;
    row.symbol = source.symbol;
    row.anchor_kind = report_anchor_kind_name(source.anchor_kind);
    row.total_us = ns_to_us(source.total_ns);
    row.self_us = ns_to_us(source.self_ns);
    row.aux_us = ns_to_us(source.aux_ns);
    row.graph_child_us = ns_to_us(source.graph_child_ns);
    row.residual_us = ns_to_us(source.residual_ns);
    row.raw_child_task_count = source.raw_child_task_count;
    row.top_ops = source.top_ops;
    row.diagnostic_flags = source.diagnostic_flags;
    rows.push_back(std::move(row));
  }
  return rows;
}

}  // namespace traceloom::compat
