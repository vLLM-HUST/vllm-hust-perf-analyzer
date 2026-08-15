#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/analysis/anchor_internal_cost_breakdown.h"
#include "traceloom/compat/schema.h"

namespace traceloom {
struct NativeIr;
}

namespace traceloom::compat {

struct AuxAttributionSqlRows;

struct AnchorCostBreakdownSqlRow {
  std::uint32_t anchor_idx = 0;
  std::string symbol;
  std::string anchor_kind;
  double total_us = 0.0;
  double self_us = 0.0;
  double aux_us = 0.0;
  double graph_child_us = 0.0;
  double residual_us = 0.0;
  std::uint32_t raw_child_task_count = 0;
  std::string top_ops;
  std::string diagnostic_flags;
};

std::vector<AnchorCostBreakdownSqlRow> build_anchor_cost_breakdown_sql_rows(
    const AnchorInternalCostBreakdown& breakdown);

std::vector<AnchorCostBreakdownSqlRow>
build_native_anchor_cost_breakdown_sql_rows(
    const NativeIr& ir,
    const AuxAttributionSqlRows& aux_rows);

std::vector<AnchorCostBreakdownSqlRow>
build_native_anchor_cost_breakdown_sql_rows(const NativeIr& ir,
                                            std::uint32_t db_idx = 0);

const CompatTableSchema& anchor_cost_breakdown_sql_row_schema();

}  // namespace traceloom::compat
