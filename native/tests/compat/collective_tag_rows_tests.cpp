#include "traceloom/compat/collective_tag_rows.h"
#include "traceloom/testing/test_util.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

traceloom::compat::EventSqlRow make_event(std::uint32_t db_idx,
                                           std::uint32_t device_id,
                                           std::uint32_t idx,
                                           const std::string& role,
                                           const std::string& family,
                                           const std::string& label,
                                           std::int64_t start_ns) {
  traceloom::compat::EventSqlRow row;
  row.event_id = "event-" + std::to_string(db_idx) + "-" + std::to_string(idx);
  row.db_idx = db_idx;
  row.device_id = device_id;
  row.source_table = "TASK";
  row.source_key = "idx=" + std::to_string(idx);
  row.start_ns = start_ns;
  row.end_ns = start_ns + 1000;
  row.dur_us = 1.0;
  row.role = role;
  row.family = family;
  row.label = label;
  row.symbol = label;
  return row;
}

traceloom::compat::AnchorSqlRow make_anchor(
    std::uint32_t db_idx,
    std::uint32_t device_id,
    std::uint32_t idx,
    const traceloom::compat::EventSqlRow& event,
    const std::string& role) {
  traceloom::compat::AnchorSqlRow row;
  row.anchor_id =
      "anchor-" + std::to_string(db_idx) + "-" + std::to_string(idx);
  row.db_idx = db_idx;
  row.device_id = device_id;
  row.anchor_idx = idx + 1;
  row.event_id = event.event_id;
  row.role = role;
  row.family = event.family;
  row.label = event.label;
  row.symbol = event.symbol;
  row.start_ns = event.start_ns;
  row.end_ns = event.end_ns;
  row.dur_us = event.dur_us;
  return row;
}

traceloom::compat::VizNodeSqlRow make_repeat_node(std::uint32_t db_idx,
                                                   std::uint32_t device_id,
                                                   const std::string& node_id,
                                                   const std::string& local_id,
                                                   std::uint32_t anchor_count,
                                                   std::uint32_t first_anchor,
                                                   std::uint32_t level) {
  traceloom::compat::VizNodeSqlRow row;
  row.node_id = node_id;
  row.db_idx = db_idx;
  row.device_id = device_id;
  row.local_node_id = local_id;
  row.node_type = "Repeat";
  row.kind = "repeat";
  row.repeat_count = 1;
  row.occurrence_count = 1;
  row.anchor_count = anchor_count;
  row.anchors_per_occurrence = static_cast<double>(anchor_count);
  row.first_anchor_idx = first_anchor;
  row.last_anchor_idx = first_anchor + anchor_count - 1;
  row.level = level;
  row.path = local_id;
  return row;
}

traceloom::compat::VizNodeAnchorSqlRow make_coverage(
    std::uint32_t db_idx,
    std::uint32_t device_id,
    const std::string& node_id,
    const std::string& anchor_id,
    std::uint32_t order) {
  traceloom::compat::VizNodeAnchorSqlRow row;
  row.node_id = node_id;
  row.anchor_id = anchor_id;
  row.db_idx = db_idx;
  row.device_id = device_id;
  row.occurrence_idx = 0;
  row.anchor_order = order;
  return row;
}

traceloom::compat::CollectiveTagMemberInput make_member(
    const std::string& db_name,
    std::uint32_t db_idx,
    const std::string& node_id,
    const std::string& local_id,
    int repeats) {
  traceloom::compat::CollectiveTagMemberInput member;
  member.db_name = db_name;
  member.db_idx = db_idx;
  member.device_id = 0;

  const int token_count = repeats * 2;
  for (int idx = 0; idx < token_count; ++idx) {
    const bool collective = idx % 2 == 1;
    member.events.push_back(make_event(
        db_idx, 0, static_cast<std::uint32_t>(idx),
        collective ? "comm" : "compute", collective ? "hccl" : "compute",
        collective ? "HcclAllReduce" : "MatMul",
        1000 + static_cast<std::int64_t>(idx) * 1000 +
            static_cast<std::int64_t>(db_idx) * 100));
    member.anchors.push_back(make_anchor(
        db_idx, 0, static_cast<std::uint32_t>(idx), member.events.back(),
        collective ? "comm" : "compute"));
  }

  member.loop_tree.nodes.push_back(make_repeat_node(
      db_idx, 0, node_id, local_id, static_cast<std::uint32_t>(token_count), 1,
      1));
  for (int idx = 0; idx < token_count; ++idx) {
    member.node_anchor_coverage.node_anchors.push_back(make_coverage(
        db_idx, 0, node_id, member.anchors[static_cast<std::size_t>(idx)].anchor_id,
        static_cast<std::uint32_t>(idx)));
  }
  return member;
}

}  // namespace

int main() {
  using traceloom::compat::CollectiveTagMemberInput;
  using traceloom::compat::CollectiveTagOptions;
  using traceloom::compat::build_collective_tag_sql_rows;
  using traceloom::testing::require;

  CollectiveTagMemberInput expanded =
      make_member("db00.traceloom_augmented.db", 0, "node-expanded", "N010", 2);
  CollectiveTagMemberInput primitive =
      make_member("db01.traceloom_augmented.db", 1, "node-primitive", "N020", 1);

  CollectiveTagOptions options;
  options.run_name = "native run";
  options.expected_world_size = 2;
  auto rows = build_collective_tag_sql_rows({expanded, primitive}, options);

  require(rows.local_links.size() == 3);
  require(rows.global_rows.summaries.size() == 2);
  require(rows.local_links[0].pair_id == rows.local_links[1].pair_id);
  require(rows.local_links[0].candidate_collective_key.find(
              "native_run:LP_M002_01_") == 0);
  require(rows.local_links[0].candidate_collective_key.find(":allReduce:") !=
          std::string::npos);

  const auto complete_it = std::find_if(
      rows.global_rows.summaries.begin(), rows.global_rows.summaries.end(),
      [](const traceloom::compat::GlobalCollectiveSummarySqlRow& summary) {
        return summary.member_count == 2;
      });
  require(complete_it != rows.global_rows.summaries.end());
  require(complete_it->validation_status == "complete");
  require(complete_it->confidence == 0.85);
  require(complete_it->start_skew_us == 0.1);
  require(complete_it->missing_members.empty());

  const auto singleton_it = std::find_if(
      rows.global_rows.summaries.begin(), rows.global_rows.summaries.end(),
      [](const traceloom::compat::GlobalCollectiveSummarySqlRow& summary) {
        return summary.member_count == 1;
      });
  require(singleton_it != rows.global_rows.summaries.end());
  require(singleton_it->validation_status == "singleton");
  require(singleton_it->confidence == 0.35);

  CollectiveTagMemberInput partial0 =
      make_member("db10.traceloom_augmented.db", 10, "node-a", "N001", 1);
  CollectiveTagMemberInput partial1 =
      make_member("db11.traceloom_augmented.db", 11, "node-b", "N001", 1);
  rows = build_collective_tag_sql_rows({partial0, partial1},
                                       CollectiveTagOptions{"partial", 3});
  require(rows.global_rows.summaries.size() == 1);
  require(rows.global_rows.summaries[0].validation_status == "partial");
  require(rows.global_rows.summaries[0].confidence == 0.55);
  require(rows.global_rows.summaries[0].missing_members ==
          "unknown_member_1");

  CollectiveTagMemberInput owner;
  owner.db_name = "db20.traceloom_augmented.db";
  owner.db_idx = 20;
  owner.device_id = 0;
  owner.events.push_back(
      make_event(20, 0, 0, "comm", "hccl", "HcclAllReduce", 5000));
  owner.anchors.push_back(make_anchor(20, 0, 0, owner.events.back(), "comm"));
  owner.loop_tree.nodes.push_back(
      make_repeat_node(20, 0, "node-parent", "N100", 2, 1, 1));
  owner.loop_tree.nodes.push_back(
      make_repeat_node(20, 0, "node-child", "N101", 1, 1, 2));
  owner.node_anchor_coverage.node_anchors.push_back(
      make_coverage(20, 0, "node-parent", owner.anchors[0].anchor_id, 0));
  owner.node_anchor_coverage.node_anchors.push_back(
      make_coverage(20, 0, "node-child", owner.anchors[0].anchor_id, 0));
  rows = build_collective_tag_sql_rows({owner},
                                       CollectiveTagOptions{"owner", 1});
  require(rows.local_links.size() == 1);
  require(rows.local_links[0].local_node_id == "N101");
  require(rows.global_rows.summaries[0].validation_status == "complete");

  return 0;
}
