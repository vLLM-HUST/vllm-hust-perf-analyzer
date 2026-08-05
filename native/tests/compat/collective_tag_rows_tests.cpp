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

  // Exact graph bodies keep collectives inside the protected ReplayUnit.  The
  // correspondence surface links their raw member events without promoting
  // them to top-level anchors or attaching workload/parallelism semantics.
  traceloom::NativeIr graph_ir;
  const traceloom::SourceRefId graph_source =
      graph_ir.source_refs.append("fixture", "graph", "KERNEL", 0);
  const traceloom::SymbolId nccl = graph_ir.symbols.intern(
      "ncclDevKernel_AllReduce_Sum_f32_RING_LL");
  const traceloom::SymbolId collective_task =
      graph_ir.symbols.intern("CUDA_COLLECTIVE_KERNEL");
  const traceloom::ReplayBodyTemplateId body_template =
      graph_ir.replay_body_templates.append(
          graph_source, 0x1234, nccl, 0, 1, 1,
          traceloom::ReplayBodyTopologyPolicy::kSingleModelStream);
  for (std::uint32_t occurrence = 0; occurrence < 2; ++occurrence) {
    for (std::uint32_t device_id = 0; device_id < 2; ++device_id) {
      const std::int64_t start_ns =
          10000 + static_cast<std::int64_t>(occurrence) * 1000 +
          static_cast<std::int64_t>(device_id) * 100;
      const traceloom::TraceEventId event = graph_ir.trace_events.append(
          graph_source, 100 + occurrence * 2 + device_id, device_id, 7,
          start_ns, start_ns + 500, nccl);
      const traceloom::TaskId task = graph_ir.tasks.append(
          graph_source, event, 100 + occurrence * 2 + device_id,
          100 + occurrence * 2 + device_id, -1, collective_task, nccl, nccl,
          traceloom::SymbolId::invalid(), nccl);
      const traceloom::GraphLaunchOccurrenceId launch =
          graph_ir.graph_launch_occurrences.append(
              graph_source, graph_source, device_id,
              200 + occurrence * 2 + device_id,
              300 + occurrence * 2 + device_id, -1, -1,
              traceloom::StreamId::invalid(), traceloom::StreamId::invalid(),
              traceloom::CapturedGraphInstanceId::invalid(),
              traceloom::TaskId::invalid(), traceloom::TaskId::invalid(),
              traceloom::TaskId::invalid(), start_ns, start_ns + 500, -1,
              traceloom::GraphLaunchMatchPolicy::kCudaRuntimeCorrelation,
              traceloom::GraphLaunchInstanceAssociationPolicy::
                  kCudaGraphNodeSet);
      const traceloom::GraphLaunchBodyId body =
          graph_ir.graph_launch_bodies.append(launch, body_template, task,
                                              task, 0, 1, 1);
      graph_ir.graph_launch_body_members.append(
          body, task, 0, 0,
          traceloom::GraphLaunchBodyMemberRow::Kind::kCommunication);
    }
  }

  rows = traceloom::compat::build_graph_body_collective_tag_sql_rows(
      graph_ir, "graph.db", 4, CollectiveTagOptions{"graph run", 2});
  require(rows.local_links.size() == 4);
  require(rows.global_rows.summaries.size() == 2);
  require(rows.local_links[0].pair_id == "GB_H0000000000001234");
  require(rows.local_links[0].candidate_collective_key.find(
              "graph_run:GB_H0000000000001234:occ_000001:allReduce:") == 0);
  require(rows.local_links[0].anchor_id.empty());
  require(!rows.local_links[0].event_id.empty());
  require(rows.local_links[0].source_table == "KERNEL");
  for (const auto& summary : rows.global_rows.summaries) {
    require(summary.member_count == 2);
    require(summary.validation_status == "complete");
    require(summary.missing_members.empty());
  }

  const traceloom::TraceEventId extra_event = graph_ir.trace_events.append(
      graph_source, 999, 0, 7, 13000, 13500, nccl);
  const traceloom::TaskId extra_task = graph_ir.tasks.append(
      graph_source, extra_event, 999, 999, -1, collective_task, nccl, nccl,
      traceloom::SymbolId::invalid(), nccl);
  const traceloom::GraphLaunchOccurrenceId extra_launch =
      graph_ir.graph_launch_occurrences.append(
          graph_source, graph_source, 0, 999, 999, -1, -1,
          traceloom::StreamId::invalid(), traceloom::StreamId::invalid(),
          traceloom::CapturedGraphInstanceId::invalid(),
          traceloom::TaskId::invalid(), traceloom::TaskId::invalid(),
          traceloom::TaskId::invalid(), 13000, 13500, -1,
          traceloom::GraphLaunchMatchPolicy::kCudaRuntimeCorrelation,
          traceloom::GraphLaunchInstanceAssociationPolicy::kCudaGraphNodeSet);
  const traceloom::GraphLaunchBodyId extra_body =
      graph_ir.graph_launch_bodies.append(extra_launch, body_template,
                                          extra_task, extra_task, 0, 1, 1);
  graph_ir.graph_launch_body_members.append(
      extra_body, extra_task, 0, 0,
      traceloom::GraphLaunchBodyMemberRow::Kind::kCommunication);
  rows = traceloom::compat::build_graph_body_collective_tag_sql_rows(
      graph_ir, "graph.db", 4, CollectiveTagOptions{"graph run", 2});
  require(rows.global_rows.summaries.size() == 5);
  for (const auto& summary : rows.global_rows.summaries) {
    require(summary.member_count == 1);
    require(summary.validation_status == "singleton");
    require(summary.pair_id.find("_D") != std::string::npos);
  }

  traceloom::NativeIr single_device_graph_ir;
  const traceloom::SourceRefId single_source =
      single_device_graph_ir.source_refs.append(
          "fixture", "single-graph", "KERNEL", 0);
  const traceloom::SymbolId single_nccl =
      single_device_graph_ir.symbols.intern("ncclKernel_AllReduce");
  const traceloom::ReplayBodyTemplateId single_template =
      single_device_graph_ir.replay_body_templates.append(
          single_source, 0x42, single_nccl, 0, 1, 1,
          traceloom::ReplayBodyTopologyPolicy::kSingleModelStream);
  const traceloom::TraceEventId single_event =
      single_device_graph_ir.trace_events.append(
          single_source, 1, 0, 7, 100, 200, single_nccl);
  const traceloom::TaskId single_task = single_device_graph_ir.tasks.append(
      single_source, single_event, 1, 1, -1, collective_task, single_nccl,
      single_nccl, traceloom::SymbolId::invalid(), single_nccl);
  const traceloom::GraphLaunchOccurrenceId single_launch =
      single_device_graph_ir.graph_launch_occurrences.append(
          single_source, single_source, 0, 1, 1, -1, -1,
          traceloom::StreamId::invalid(), traceloom::StreamId::invalid(),
          traceloom::CapturedGraphInstanceId::invalid(),
          traceloom::TaskId::invalid(), traceloom::TaskId::invalid(),
          traceloom::TaskId::invalid(), 100, 200, -1,
          traceloom::GraphLaunchMatchPolicy::kCudaRuntimeCorrelation,
          traceloom::GraphLaunchInstanceAssociationPolicy::kCudaGraphNodeSet);
  const traceloom::GraphLaunchBodyId single_body =
      single_device_graph_ir.graph_launch_bodies.append(
          single_launch, single_template, single_task, single_task, 0, 1, 1);
  single_device_graph_ir.graph_launch_body_members.append(
      single_body, single_task, 0, 0,
      traceloom::GraphLaunchBodyMemberRow::Kind::kCommunication);
  rows = traceloom::compat::build_graph_body_collective_tag_sql_rows(
      single_device_graph_ir, "single.db", 0,
      CollectiveTagOptions{"single run", 0});
  require(rows.local_links.empty());
  require(rows.global_rows.summaries.empty());

  return 0;
}
