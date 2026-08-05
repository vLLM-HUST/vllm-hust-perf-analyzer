#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

struct CollectiveTagMemberInput {
  std::string db_name;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::vector<EventSqlRow> events;
  std::vector<AnchorSqlRow> anchors;
  LoopTreeSqlRows loop_tree;
  NodeAnchorCoverageSqlRows node_anchor_coverage;
};

struct CollectiveTagOptions {
  std::string run_name = "traceloom_run";
  // Zero means infer from the distinct member ids in the input.
  std::uint32_t expected_world_size = 0;
};

struct CollectiveTagSqlRows {
  std::vector<CollectiveGlobalLinkSqlRow> local_links;
  GlobalCollectiveSqlRows global_rows;
};

CollectiveTagSqlRows build_collective_tag_sql_rows(
    const std::vector<CollectiveTagMemberInput>& members,
    const CollectiveTagOptions& options = {});

// Builds workload-agnostic correspondence candidates for collective members
// that are directly observed inside exact graph-launch bodies.  Matching uses
// stable body-template identity, per-device occurrence order, normalized
// collective type, and member order.  It does not infer a parallelism mode or
// a cross-device causal/total order.
CollectiveTagSqlRows build_graph_body_collective_tag_sql_rows(
    const NativeIr& ir,
    const std::string& db_name,
    std::uint32_t db_idx = 0,
    const CollectiveTagOptions& options = {});

}  // namespace traceloom::compat
