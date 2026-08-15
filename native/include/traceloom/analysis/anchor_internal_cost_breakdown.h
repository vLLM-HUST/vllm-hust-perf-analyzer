#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/analysis/structural_occurrence_graph.h"
#include "traceloom/core/diagnostics.h"
#include "traceloom/core/ids.h"

namespace traceloom {

enum class AnchorCostComponentKind {
  kSelf,
  kAux,
  kGraphChild,
  kResidual,
};

struct AnchorCostComponentLeaf {
  StructuralCostLeafId id;
  std::uint32_t token_ordinal = 0;
  AnchorCostComponentKind kind = AnchorCostComponentKind::kSelf;
  std::int64_t duration_ns = 0;
  std::uint32_t raw_child_task_count = 0;
  std::uint32_t source_ref_count = 0;
  std::string top_ops;
  std::string diagnostic_flags;
};

struct AnchorInternalCostBreakdownRow {
  StructuralNodeOccurrenceId anchor_occurrence_id;
  StructuralNodeDefId anchor_def_id;
  std::uint32_t anchor_idx = 0;
  std::string symbol;
  StructuralAnchorKind anchor_kind = StructuralAnchorKind::kUnknown;
  std::int64_t total_ns = 0;
  std::int64_t self_ns = 0;
  std::int64_t aux_ns = 0;
  std::int64_t graph_child_ns = 0;
  std::int64_t residual_ns = 0;
  std::uint32_t raw_child_task_count = 0;
  std::uint32_t source_ref_count = 0;
  std::string top_ops;
  std::string diagnostic_flags;
};

struct AnchorInternalCostBreakdown {
  std::vector<AnchorInternalCostBreakdownRow> rows;
  std::vector<Diagnostic> diagnostics;
};

AnchorInternalCostBreakdown build_anchor_internal_cost_breakdown(
    const StructuralOccurrenceGraph& tree,
    const std::vector<StructuralProjectionToken>& tokens,
    const std::vector<AnchorCostComponentLeaf>& component_leaves);

}  // namespace traceloom
