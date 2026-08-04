#include "traceloom/compat/idle_explanation_rows.h"
#include "traceloom/testing/test_util.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

using traceloom::testing::require;

namespace {

using namespace traceloom;
using namespace traceloom::compat;

ReportToken token(std::uint32_t ordinal,
                  std::uint32_t anchor,
                  std::int64_t start_ns,
                  std::int64_t end_ns) {
  ReportToken row;
  row.ordinal = ordinal;
  row.device_id = 0;
  row.anchor_id = AnchorId(anchor);
  row.start_ns = start_ns;
  row.end_ns = end_ns;
  return row;
}

IdleExplanationRow explanation(std::int64_t start_ns,
                               std::int64_t end_ns,
                               IdleExplanationCategory category,
                               IdleEvidenceLevel level) {
  IdleExplanationRow row;
  row.start_ns = start_ns;
  row.end_ns = end_ns;
  row.category = category;
  row.evidence_level = level;
  row.evidence_relation = level == IdleEvidenceLevel::kDirect
                              ? IdleEvidenceRelation::kDeviceEventCoverage
                              : IdleEvidenceRelation::kNone;
  return row;
}

VizNodeAnchorSqlRow coverage(const std::string& node_id,
                             const std::string& anchor_id) {
  VizNodeAnchorSqlRow row;
  row.node_id = node_id;
  row.anchor_id = anchor_id;
  row.device_id = 0;
  row.view_name = "native_report_tree";
  return row;
}

std::uint64_t node_duration(const IdleExplanationAttributionRows& rows,
                            const std::string& node_id,
                            const std::string& category) {
  for (const NodeIdleExplanationRow& row : rows.nodes) {
    if (row.node_id == node_id && row.category == category) {
      return row.duration_ns;
    }
  }
  return 0;
}

}  // namespace

int main() {
  using namespace traceloom;
  using namespace traceloom::compat;

  // Token 0 establishes the initial frontier. Token 1 owns prelude [20,40),
  // token 2 owns [50,70). The explanation inside token 0 is intentionally
  // not a prelude and must remain device-only rather than being guessed onto
  // an anchor.
  const std::vector<ReportToken> tokens = {
      token(0, 0, 10, 20), token(1, 1, 40, 50), token(2, 2, 70, 80)};

  IdleExplanationRunResult explanations;
  IdleExplanationDeviceResult device;
  device.device_id = 0;
  device.explanations = {
      explanation(12, 15,
                  IdleExplanationCategory::kUnattributedVisibleIdle,
                  IdleEvidenceLevel::kNone),
      explanation(20, 30, IdleExplanationCategory::kBlockedByVisibleWait,
                  IdleEvidenceLevel::kDirect),
      explanation(30, 40,
                  IdleExplanationCategory::kUnattributedVisibleIdle,
                  IdleEvidenceLevel::kNone),
      explanation(50, 60, IdleExplanationCategory::kCaptureControlPresent,
                  IdleEvidenceLevel::kDirect),
      explanation(60, 70,
                  IdleExplanationCategory::kUnattributedVisibleIdle,
                  IdleEvidenceLevel::kNone),
  };
  explanations.devices.push_back(std::move(device));

  NodeCoverageSqlRows coverage_rows;
  coverage_rows.node_anchors = {
      coverage("root", "anchor-0"), coverage("root", "anchor-1"),
      coverage("root", "anchor-2"), coverage("repeat", "anchor-1"),
      coverage("repeat", "anchor-2"), coverage("atom", "anchor-1"),
  };

  const IdleExplanationAttributionRows rows =
      build_idle_explanation_attribution_rows(tokens, explanations,
                                              coverage_rows);
  require(rows.visible_productive_idle_ns == 43,
          "device total includes every E4 explanation");
  require(rows.anchor_prelude_attributed_ns == 40,
          "prelude intersections conserve their exact duration");
  require(rows.device_only_unassigned_ns == 3,
          "non-prelude explanation remains explicitly device-only");
  require(rows.anchors.size() == 4,
          "two categories are attributed to each prelude owner");

  std::map<std::pair<std::string, std::string>, std::uint64_t> anchors;
  for (const AnchorIdleExplanationRow& row : rows.anchors) {
    anchors[{row.anchor_id, row.category}] = row.duration_ns;
    require(row.slice_count == 1, "each fixture intersection is one slice");
  }
  require(anchors[{"anchor-1", "blocked_by_visible_wait"}] == 10 &&
              anchors[{"anchor-1", "unattributed_visible_idle"}] == 10 &&
              anchors[{"anchor-2", "capture_control_present"}] == 10 &&
              anchors[{"anchor-2", "unattributed_visible_idle"}] == 10,
          "anchor/category attribution is exact");

  require(node_duration(rows, "root", "blocked_by_visible_wait") == 10 &&
              node_duration(rows, "root", "capture_control_present") == 10 &&
              node_duration(rows, "root", "unattributed_visible_idle") == 20,
          "root aggregates every attributed anchor exactly once");
  require(node_duration(rows, "repeat", "blocked_by_visible_wait") == 10 &&
              node_duration(rows, "repeat", "capture_control_present") == 10 &&
              node_duration(rows, "repeat", "unattributed_visible_idle") ==
                  20,
          "repeat node aggregates its covered anchors");
  require(node_duration(rows, "atom", "blocked_by_visible_wait") == 10 &&
              node_duration(rows, "atom", "unattributed_visible_idle") == 10 &&
              node_duration(rows, "atom", "capture_control_present") == 0,
          "atom node receives only its own anchor prelude");

  return 0;
}
