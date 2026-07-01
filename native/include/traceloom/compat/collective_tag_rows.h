#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/compat/sidecar_writer.h"

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

}  // namespace traceloom::compat
