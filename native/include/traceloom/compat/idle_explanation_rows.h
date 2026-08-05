#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "traceloom/analysis/idle_explanation.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/report/report_tree.h"

namespace traceloom::compat {

// An exact intersection between an E4 explanation slice and one anchor's
// prelude window. Prelude windows use the same frontier rule as Loop Tree
// cost packets: [max end of earlier anchors, this anchor start).
struct AnchorIdleExplanationRow {
  std::string anchor_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::uint32_t anchor_idx = 0;
  std::string category;
  std::string evidence_level;
  std::uint64_t slice_count = 0;
  std::uint64_t duration_ns = 0;
};

// Hierarchical aggregation through traceloom_viz_node_anchor coverage. Rows
// for parents and children intentionally overlap structurally and are not
// additive across nodes; each individual node conserves its anchor-prelude
// inputs.
struct NodeIdleExplanationRow {
  std::string node_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string view_name;
  std::string category;
  std::string evidence_level;
  std::uint64_t slice_count = 0;
  std::uint64_t duration_ns = 0;
};

struct IdleExplanationAttributionRows {
  std::vector<AnchorIdleExplanationRow> anchors;
  std::vector<NodeIdleExplanationRow> nodes;
  std::uint64_t visible_productive_idle_ns = 0;
  std::uint64_t anchor_prelude_attributed_ns = 0;
  std::uint64_t device_only_unassigned_ns = 0;
};

IdleExplanationAttributionRows build_idle_explanation_attribution_rows(
    const std::vector<ReportToken>& tokens,
    const IdleExplanationRunResult& explanations,
    const NodeCoverageSqlRows& node_coverage,
    std::uint32_t db_idx = 0,
    std::optional<std::uint32_t> device_id = std::nullopt);

}  // namespace traceloom::compat
