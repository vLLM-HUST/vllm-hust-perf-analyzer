#include "sidecar_writer_test_support.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/testing/test_util.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace traceloom::testing::sidecar_writer_test {

void run_graph_projection_tests(
    const std::string& db_path,
    const std::vector<std::string>& expected_tables,
    const std::vector<traceloom::compat::CompatTableSchema>& table_schemas) {
  using traceloom::testing::require;

  traceloom::compat::GraphReplayEvidenceSqlRows graph_rows;
  traceloom::compat::GraphReplaySqlRow graph_replay;
  graph_replay.graph_event_id = "graph-1";
  graph_replay.graph_provider = "aclgraph";
  graph_replay.graph_kind = "acl_graph_replay";
  graph_replay.graph_event_idx = 10;
  graph_replay.event_id = "event-graph-1";
  graph_replay.step_idx = 10;
  graph_replay.stream_id = 7;
  graph_replay.graph_id = "graph-id-1";
  graph_replay.graph_exec_id = "graph-exec-1";
  graph_replay.start_ns = 1000;
  graph_replay.end_ns = 1500;
  graph_replay.dur_us = 0.5;
  graph_replay.enclosed_event_count = 1;
  graph_replay.enclosed_event_us = 0.3;
  graph_replay.enclosed_kernel_count = 1;
  graph_replay.enclosed_kernel_us = 0.3;
  graph_rows.graph_replays.push_back(graph_replay);
  traceloom::compat::GraphEnvelopeSqlRow graph_envelope;
  graph_envelope.envelope_id = "envelope-1";
  graph_envelope.graph_provider = graph_replay.graph_provider;
  graph_envelope.graph_kind = graph_replay.graph_kind;
  graph_envelope.envelope_idx = 1;
  graph_envelope.graph_event_id = graph_replay.graph_event_id;
  graph_envelope.child_event_id = "event-child-1";
  graph_envelope.graph_step_idx = graph_replay.step_idx;
  graph_envelope.child_step_idx = 11;
  graph_envelope.relation = "encloses";
  graph_envelope.stream_relation = "same_stream";
  graph_envelope.graph_id = graph_replay.graph_id;
  graph_envelope.graph_exec_id = graph_replay.graph_exec_id;
  graph_envelope.graph_start_ns = graph_replay.start_ns;
  graph_envelope.graph_end_ns = graph_replay.end_ns;
  graph_envelope.child_start_ns = 1100;
  graph_envelope.child_end_ns = 1400;
  graph_envelope.start_offset_us = 0.1;
  graph_envelope.end_offset_us = 0.1;
  graph_envelope.child_dur_us = 0.3;
  graph_rows.graph_envelopes.push_back(graph_envelope);
  traceloom::compat::GraphReconstructionRegionSqlRow region;
  region.region_id = "aclgraph-reconstruction-region-1";
  region.device_id = 2;
  region.candidate_id = "aclgraph-composition-candidate-1";
  region.region_order = 1;
  region.status = "unrecognized_incomplete_tail";
  region.boundary_policy = "exact_periodic_suffix";
  region.order_policy = "host_submission_order";
  region.identity_policy = "graph_connection";
  region.shape_policy = "head_repeated_layer_tail";
  region.first_launch_occurrence_id = 20;
  region.last_launch_occurrence_id = 21;
  region.observed_launch_count = 2;
  region.expected_launch_count = 25;
  region.start_ns = 2000;
  region.end_ns = 2500;
  region.dur_us = 0.5;
  graph_rows.reconstruction_regions.push_back(region);
  traceloom::compat::replace_graph_replay_evidence_rows(db_path, graph_rows);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_cuda_graph_replay") == 1);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_cuda_graph_envelope") == 1);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM "
              "traceloom_aclgraph_reconstruction_region") == 1);
  require(run_scalar_text(
              db_path,
              "SELECT status FROM "
              "traceloom_aclgraph_reconstruction_region") ==
          "unrecognized_incomplete_tail");
  traceloom::compat::replace_graph_replay_evidence_rows(
      db_path, traceloom::compat::GraphReplayEvidenceSqlRows{});
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_cuda_graph_replay") == 0);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_cuda_graph_envelope") == 0);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM "
              "traceloom_aclgraph_reconstruction_region") == 0);

  traceloom::compat::GraphLaunchSqlRow exact_launch;
  exact_launch.launch_id = "graph-launch-0";
  exact_launch.db_idx = 1;
  exact_launch.device_id = 0;
  exact_launch.graph_provider = "cuda";
  exact_launch.graph_event_id = "event-0";
  exact_launch.anchor_id = "anchor-0";
  exact_launch.replay_unit_id = 0;
  exact_launch.graph_template_id = 0;
  exact_launch.graph_launch_occurrence_id = 0;
  exact_launch.replay_body_template_id = 0;
  exact_launch.body_id = 0;
  exact_launch.member_order = 0;
  exact_launch.correlation_id = "101";
  exact_launch.match_policy = "cuda_runtime_correlation";
  exact_launch.association_policy = "cuda_graph_node_set";
  exact_launch.start_ns = 1000;
  exact_launch.end_ns = 2000;
  exact_launch.dur_us = 1.0;
  traceloom::compat::GraphBodyMemberSqlRow exact_member;
  exact_member.member_id = "graph-body-member-0";
  exact_member.launch_id = exact_launch.launch_id;
  exact_member.db_idx = exact_launch.db_idx;
  exact_member.device_id = exact_launch.device_id;
  exact_member.graph_provider = exact_launch.graph_provider;
  exact_member.graph_event_id = exact_launch.graph_event_id;
  exact_member.replay_unit_id = exact_launch.replay_unit_id;
  exact_member.graph_template_id = exact_launch.graph_template_id;
  exact_member.graph_launch_occurrence_id =
      exact_launch.graph_launch_occurrence_id;
  exact_member.body_id = exact_launch.body_id;
  exact_member.replay_body_template_id =
      exact_launch.replay_body_template_id;
  exact_member.member_order = exact_launch.member_order;
  exact_member.lane_ordinal = 0;
  exact_member.task_ordinal = 0;
  exact_member.kind = "compute";
  exact_member.event_id = "event-1";
  exact_member.task_id = 0;
  exact_member.source_table = "CUPTI_ACTIVITY_KIND_KERNEL";
  exact_member.source_row_id = 101;
  exact_member.raw_task_id = 101;
  exact_member.start_ns = 1010;
  exact_member.end_ns = 1020;
  exact_member.dur_us = 0.01;
  exact_member.correlation_id = exact_launch.correlation_id;
  exact_member.graph_node_id = 8589934592LL;
  exact_member.original_graph_node_id = 5000;
  exact_member.match_policy = exact_launch.match_policy;
  exact_member.association_policy = exact_launch.association_policy;
  traceloom::compat::ExactGraphSqlRows exact_rows;
  exact_rows.launches.push_back(exact_launch);
  exact_rows.members.push_back(exact_member);
  traceloom::compat::replace_exact_graph_rows(db_path, exact_rows);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_graph_launch") == 1);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_graph_body_member") == 1);
  require(run_scalar_text(db_path,
                          "SELECT correlation_id FROM traceloom_graph_launch") ==
          "101");
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM traceloom_graph_body_member "
              "WHERE graph_node_id = 8589934592") == 1);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM traceloom_graph_body_member "
              "WHERE original_graph_node_id = 5000") == 1);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM sqlite_master "
              "WHERE type = 'view' AND name = "
              "'traceloom_v_node_graph_body_member'") == 1);
  // Empty anchor_id / correlation_id materialize as SQL NULL (the schema
  // declares both nullable and the docs promise NULL for absent values).
  traceloom::compat::GraphLaunchSqlRow unanchored_launch = exact_launch;
  unanchored_launch.launch_id = "graph-launch-1";
  unanchored_launch.anchor_id.clear();
  unanchored_launch.correlation_id.clear();
  exact_rows.launches.push_back(unanchored_launch);
  traceloom::compat::replace_exact_graph_rows(db_path, exact_rows);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_graph_launch") == 2);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM traceloom_graph_launch "
              "WHERE launch_id = 'graph-launch-1' AND anchor_id IS NULL "
              "AND correlation_id IS NULL") == 1);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM traceloom_graph_launch "
              "WHERE launch_id = 'graph-launch-0' AND anchor_id = 'anchor-0' "
              "AND correlation_id = '101'") == 1);
  traceloom::compat::replace_exact_graph_rows(
      db_path, traceloom::compat::ExactGraphSqlRows{});
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_graph_launch") == 0);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_graph_body_member") == 0);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM sqlite_master "
              "WHERE type = 'view' AND name = "
              "'traceloom_v_node_graph_body_member'") == 1);

  traceloom::compat::LoopTreeSqlRows loop_tree_rows;
  traceloom::compat::VizNodeSqlRow parent_node;
  parent_node.node_id = "node-parent";
  parent_node.local_node_id = "N001";
  parent_node.path = "root";
  parent_node.node_type = "loop";
  parent_node.kind = "loop";
  parent_node.label = "root";
  parent_node.occurrence_count = 1;
  parent_node.anchor_count = 1;
  parent_node.total_us = 1.0;
  parent_node.avg_total_us = 1.0;
  loop_tree_rows.nodes.push_back(parent_node);
  traceloom::compat::VizNodeSqlRow child_node = parent_node;
  child_node.node_id = "node-child";
  child_node.local_node_id = "N002";
  child_node.path = "root/child";
  child_node.label = "child";
  child_node.depth = 1;
  child_node.level = 1;
  loop_tree_rows.nodes.push_back(child_node);
  traceloom::compat::VizEdgeSqlRow edge;
  edge.parent_node_id = parent_node.node_id;
  edge.child_node_id = child_node.node_id;
  edge.edge_order = 0;
  edge.edge_kind = "child";
  loop_tree_rows.edges.push_back(edge);
  traceloom::compat::LoopNodeSqlRow loop_node;
  loop_node.node_id = parent_node.node_id;
  loop_node.loop_rank = 0;
  loop_node.repeat_label = "root";
  loop_node.occurrence_count = 1;
  loop_node.anchor_count = 1;
  loop_node.total_us = 1.0;
  loop_node.avg_total_us = 1.0;
  loop_tree_rows.loop_nodes.push_back(loop_node);
  traceloom::compat::replace_loop_tree_rows(db_path, loop_tree_rows);
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_viz_node") ==
          2);
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_viz_edge") ==
          1);
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_loop_node") ==
          1);
  traceloom::compat::replace_loop_tree_rows(
      db_path, traceloom::compat::LoopTreeSqlRows{});
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_viz_node") ==
          0);
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_viz_edge") ==
          0);
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_loop_node") ==
          0);

  traceloom::compat::NodeAnchorCoverageSqlRows coverage_rows;
  traceloom::compat::VizNodeAnchorSqlRow node_anchor;
  node_anchor.node_id = "node-child";
  node_anchor.anchor_id = "anchor-1";
  node_anchor.occurrence_idx = 0;
  node_anchor.anchor_order = 0;
  node_anchor.coverage_kind = "self";
  coverage_rows.node_anchors.push_back(node_anchor);
  traceloom::compat::AnchorPrimaryNodeSqlRow primary_node;
  primary_node.anchor_id = "anchor-1";
  primary_node.node_id = "node-child";
  primary_node.reason = "smallest_covering_node";
  coverage_rows.anchor_primary_nodes.push_back(primary_node);
  traceloom::compat::replace_node_anchor_coverage_rows(db_path,
                                                       coverage_rows);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_viz_node_anchor") == 1);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_anchor_primary_node") == 1);
  traceloom::compat::replace_node_anchor_coverage_rows(
      db_path, traceloom::compat::NodeAnchorCoverageSqlRows{});
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_viz_node_anchor") == 0);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_anchor_primary_node") == 0);

  traceloom::compat::SemanticTreeHeaderSqlRow semantic_tree;
  semantic_tree.tree_id = "semantic-tree-1";
  semantic_tree.view_name = "semantic_tree";
  semantic_tree.tree_kind = "semantic";
  semantic_tree.stem = "root";
  semantic_tree.root_node_id = "semantic-node-root";
  semantic_tree.schema_version = "v1";
  semantic_tree.semantic_projection = "compat-test";
  semantic_tree.macro_discovery = "fixture";
  semantic_tree.readable_macro_mode = "compact";
  semantic_tree.auxiliary_attribution = "visible";
  traceloom::compat::replace_semantic_tree_catalog_rows(db_path,
                                                        {semantic_tree});
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_semantic_tree") == 1);

  traceloom::compat::SemanticGraphSqlRows semantic_graph_rows;
  traceloom::compat::SemanticNodeSqlRow semantic_root;
  semantic_root.node_id = semantic_tree.root_node_id;
  semantic_root.tree_id = semantic_tree.tree_id;
  semantic_root.view_name = semantic_tree.view_name;
  semantic_root.tree_kind = semantic_tree.tree_kind;
  semantic_root.local_node_id = "N001";
  semantic_root.path = "root";
  semantic_root.node_type = "loop";
  semantic_root.semantic_kind = "repeat";
  semantic_root.label = "root";
  semantic_root.occurrence_count = 1;
  semantic_root.anchor_count = 1;
  semantic_graph_rows.nodes.push_back(semantic_root);
  traceloom::compat::SemanticNodeSqlRow semantic_child = semantic_root;
  semantic_child.node_id = "semantic-node-child";
  semantic_child.local_node_id = "N002";
  semantic_child.parent_node_id = semantic_root.node_id;
  semantic_child.parent_local_node_id = semantic_root.local_node_id;
  semantic_child.preorder_idx = 1;
  semantic_child.sibling_order = 0;
  semantic_child.path = "root/child";
  semantic_child.depth = 1;
  semantic_child.display_depth = 1;
  semantic_child.node_type = "op";
  semantic_child.semantic_kind = "compute";
  semantic_child.label = "MatMul";
  semantic_graph_rows.nodes.push_back(semantic_child);
  traceloom::compat::SemanticEdgeSqlRow semantic_edge;
  semantic_edge.parent_node_id = semantic_root.node_id;
  semantic_edge.child_node_id = semantic_child.node_id;
  semantic_edge.tree_id = semantic_tree.tree_id;
  semantic_edge.view_name = semantic_tree.view_name;
  semantic_edge.tree_kind = semantic_tree.tree_kind;
  semantic_edge.edge_order = 0;
  semantic_edge.edge_kind = "child";
  semantic_graph_rows.edges.push_back(semantic_edge);
  traceloom::compat::replace_semantic_graph_rows(db_path,
                                                 semantic_graph_rows);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_semantic_node") == 2);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_semantic_edge") == 1);
  traceloom::compat::replace_semantic_graph_rows(
      db_path, traceloom::compat::SemanticGraphSqlRows{});
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_semantic_node") == 0);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_semantic_edge") == 0);
  traceloom::compat::replace_semantic_tree_catalog_rows(db_path, {});
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_semantic_tree") == 0);

  std::vector<traceloom::compat::CollectiveGlobalLinkSqlRow> local_links(2);
  local_links[0].candidate_collective_key = "collective-1";
  local_links[0].db_name = "db00.traceloom_augmented.db";
  local_links[0].db_idx = 0;
  local_links[0].device_id = 0;
  local_links[0].member_id = "rank0";
  local_links[0].pair_id = "pair-1";
  local_links[0].local_node_id = "N027";
  local_links[0].occurrence_idx = 0;
  local_links[0].idx_in_occurrence = 0;
  local_links[0].op_type = "allReduce";
  local_links[0].anchor_id = "anchor-1";
  local_links[0].event_id = "event-anchor-1";
  local_links[0].source_table = "COMMUNICATION_OP";
  local_links[0].source_key = "comm-1";
  local_links[0].connection_id = "conn-1";
  local_links[0].op_id = "op-1";
  local_links[0].start_ns = 1000;
  local_links[0].end_ns = 2000;
  local_links[0].dur_us = 1.0;
  local_links[0].validation_status = "complete";
  local_links[0].confidence = 1.0;
  local_links[1] = local_links[0];
  local_links[1].candidate_collective_key = "collective-2";
  local_links[1].member_id = "rank1";
  local_links[1].anchor_id = "anchor-2";
  traceloom::compat::replace_collective_global_link_rows(db_path, local_links);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM traceloom_collective_global_link") == 2);
  require(run_scalar_text(
              db_path,
              "SELECT validation_status FROM traceloom_collective_global_link "
              "WHERE candidate_collective_key = 'collective-1'") ==
          "complete");
  traceloom::compat::replace_collective_global_link_rows(
      db_path, {local_links.front()});
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM traceloom_collective_global_link") == 1);

  const std::string global_rows_db_path = temp_db_path();
  traceloom::compat::GlobalCollectiveSqlRows global_rows;
  traceloom::compat::GlobalCollectiveSummarySqlRow summary;
  summary.candidate_collective_key = "collective-1";
  summary.pair_id = "pair-1";
  summary.occurrence_idx = 0;
  summary.op_type = "allReduce";
  summary.idx_in_occurrence = 0;
  summary.member_count = 2;
  summary.expected_world_size = 2;
  summary.start_skew_us = 0.25;
  summary.duration_skew_us = 0.5;
  summary.connection_ids = "conn-1";
  summary.op_ids = "op-1";
  summary.members = "rank0,rank1";
  summary.validation_status = "complete";
  summary.confidence = 1.0;
  global_rows.summaries.push_back(summary);
  traceloom::compat::GlobalCollectiveMemberSqlRow member;
  member.candidate_collective_key = summary.candidate_collective_key;
  member.db_name = "db00.traceloom_augmented.db";
  member.db_idx = 0;
  member.device_id = 0;
  member.member_id = "rank0";
  member.pair_id = summary.pair_id;
  member.local_node_id = "N027";
  member.occurrence_idx = summary.occurrence_idx;
  member.idx_in_occurrence = summary.idx_in_occurrence;
  member.op_type = summary.op_type;
  member.anchor_id = "anchor-1";
  member.event_id = "event-anchor-1";
  member.source_table = "COMMUNICATION_OP";
  member.source_key = "comm-1";
  member.connection_id = "conn-1";
  member.op_id = "op-1";
  member.start_ns = 1000;
  member.end_ns = 2000;
  member.dur_us = 1.0;
  member.validation_status = summary.validation_status;
  member.confidence = summary.confidence;
  global_rows.members.push_back(member);
  traceloom::compat::replace_global_collective_rows(global_rows_db_path,
                                                    global_rows);
  require(run_scalar_int(global_rows_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_global_collective_summary") == 1);
  require(run_scalar_int(global_rows_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_global_collective_member") == 1);
  require(run_scalar_text(global_rows_db_path,
                          "SELECT validation_status FROM "
                          "traceloom_global_collective_summary") ==
          "complete");
  global_rows.members.clear();
  traceloom::compat::replace_global_collective_rows(global_rows_db_path,
                                                    global_rows);
  require(run_scalar_int(global_rows_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_global_collective_member") == 0);
  traceloom::compat::replace_global_collective_member_rows(global_rows_db_path,
                                                           {member});
  require(run_scalar_int(global_rows_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_global_collective_member") == 1);
  require(run_scalar_int(global_rows_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_global_collective_summary") == 1);
  summary.validation_status = "partial";
  traceloom::compat::replace_global_collective_summary_rows(
      global_rows_db_path, {summary});
  require(run_scalar_text(global_rows_db_path,
                          "SELECT validation_status FROM "
                          "traceloom_global_collective_summary") ==
          "partial");
  require(run_scalar_int(global_rows_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_global_collective_member") == 1);
  traceloom::compat::replace_global_collective_member_rows(
      global_rows_db_path, {});
  traceloom::compat::replace_global_collective_summary_rows(
      global_rows_db_path, {});
  require(run_scalar_int(global_rows_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_global_collective_member") == 0);
  require(run_scalar_int(global_rows_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_global_collective_summary") == 0);
  std::remove(global_rows_db_path.c_str());

  bool rejected_bad_schema = false;
  try {
    traceloom::compat::materialize_compatibility_schema(
        db_path,
        {traceloom::compat::CompatTableSchema{
            "bad-table", table_schemas.front().columns}});
  } catch (const std::invalid_argument&) {
    rejected_bad_schema = true;
  }
  require(rejected_bad_schema);

  traceloom::compat::materialize_structural_compatibility_views(db_path);
  std::vector<std::string> analyzed_tables = expected_tables;
  analyzed_tables.push_back("traceloom_structure_bubble_api_occurrence");
  analyzed_tables.push_back("traceloom_structure_bubble_api_stats");
  analyzed_tables.push_back("traceloom_structure_bubble_occurrence");
  analyzed_tables.push_back("traceloom_structure_bubble_position");
  analyzed_tables.push_back("sqlite_stat1");
  std::sort(analyzed_tables.begin(), analyzed_tables.end());
  require(load_sqlite_master_names(db_path, "table") == analyzed_tables);
  require(load_sqlite_master_names(db_path, "view") ==
          std::vector<std::string>({
              "traceloom_tree_node_anchor",
              "traceloom_tree_node_occurrence",
              "traceloom_v_anchor_host_activity",
              "traceloom_v_anchor_host_interval",
              "traceloom_v_anchor_runtime_call",
              "traceloom_v_anchor_symbol_lineage",
              "traceloom_v_aux_runtime_call",
              "traceloom_v_cuda_graph_envelope",
              "traceloom_v_cuda_graph_replay",
              "traceloom_v_event_reconciliation",
              "traceloom_v_node_anchor_cost",
              "traceloom_v_node_aux_cost",
              "traceloom_v_node_children",
              "traceloom_v_node_cost",
              "traceloom_v_node_graph_body_member",
              "traceloom_v_node_host_activity",
              "traceloom_v_node_host_interval",
              "traceloom_v_node_replay_cost_member",
              "traceloom_v_node_runtime_call",
              "traceloom_v_runtime_device",
              "traceloom_v_semantic_tree_node",
              "traceloom_v_semantic_tree_readable",
              "traceloom_v_structure_bubble_api_occurrence",
              "traceloom_v_structure_bubble_api_stats",
              "traceloom_v_structure_bubble_host_context",
              "traceloom_v_structure_bubble_occurrence",
              "traceloom_v_structure_bubble_position",
              "traceloom_v_structure_bubble_runtime_call",
              "traceloom_v_symbol_normalization_placement",
              "traceloom_v_symbol_variant_cost",
              "traceloom_v_sync_runtime_call",
              "traceloom_v_tree_node",
          }));
  const std::vector<ColumnInfo> tree_view_columns =
      load_columns(db_path, "traceloom_v_tree_node");
  require(has_column(tree_view_columns, "active_us"));
  require(has_column(tree_view_columns, "avg_active_us"));
  require(!has_column(tree_view_columns, "avg_self_us"));
  require(load_sqlite_master_names(db_path, "index") ==
          std::vector<std::string>({
              "idx_traceloom_aclgraph_region_status",
              "idx_traceloom_anchor_device_idx",
              "idx_traceloom_anchor_host_activity_call",
              "idx_traceloom_anchor_host_activity_interval",
              "idx_traceloom_anchor_host_api_summary",
              "idx_traceloom_anchor_host_interval_id",
              "idx_traceloom_anchor_host_interval_left",
              "idx_traceloom_anchor_host_interval_right",
              "idx_traceloom_anchor_key",
              "idx_traceloom_anchor_runtime_anchor",
              "idx_traceloom_anchor_runtime_call",
              "idx_traceloom_anchor_symbol_normalization_anchor",
              "idx_traceloom_aux_anchor",
              "idx_traceloom_collective_key",
              "idx_traceloom_collective_pair",
              "idx_traceloom_cuda_graph_envelope_child",
              "idx_traceloom_cuda_graph_envelope_graph",
              "idx_traceloom_cuda_graph_replay_exec",
              "idx_traceloom_device_work_event",
              "idx_traceloom_device_work_graph",
              "idx_traceloom_device_work_id",
              "idx_traceloom_event_device_step",
              "idx_traceloom_event_id",
              "idx_traceloom_event_identity",
              "idx_traceloom_event_source_lookup",
              "idx_traceloom_graph_body_member_event",
              "idx_traceloom_graph_body_member_event_identity",
              "idx_traceloom_graph_body_member_launch",
              "idx_traceloom_graph_body_member_launch_identity",
              "idx_traceloom_graph_body_member_node",
              "idx_traceloom_graph_launch_anchor",
              "idx_traceloom_graph_launch_anchor_identity",
              "idx_traceloom_graph_launch_identity",
              "idx_traceloom_graph_launch_node",
              "idx_traceloom_node_anchor_anchor",
              "idx_traceloom_node_anchor_node",
              "idx_traceloom_node_anchor_occurrence",
              "idx_traceloom_replay_cost_aggregate_hotspot",
              "idx_traceloom_replay_cost_contributor",
              "idx_traceloom_replay_cost_member_event",
              "idx_traceloom_replay_cost_member_launch",
              "idx_traceloom_replay_cost_member_observed",
              "idx_traceloom_runtime_call_correlation",
              "idx_traceloom_runtime_call_id",
              "idx_traceloom_runtime_call_time",
              "idx_traceloom_runtime_relation_call",
              "idx_traceloom_runtime_relation_id",
              "idx_traceloom_runtime_relation_work",
              "idx_traceloom_semantic_edge_tree",
              "idx_traceloom_semantic_node_parent",
              "idx_traceloom_semantic_node_tree_order",
              "idx_traceloom_structure_bubble_api_hotspot",
              "idx_traceloom_structure_bubble_api_occurrence",
              "idx_traceloom_structure_bubble_api_stats",
              "idx_traceloom_structure_bubble_host_interval",
              "idx_traceloom_structure_bubble_id",
              "idx_traceloom_structure_bubble_position",
              "idx_traceloom_structure_bubble_position_hotspot",
              "idx_traceloom_structure_bubble_transition",
              "idx_traceloom_symbol_normalization_rule",
              "idx_traceloom_symbol_normalization_source",
              "idx_traceloom_symbol_normalization_symbol",
              "idx_traceloom_symbol_rule_identity",
              "idx_traceloom_viz_node_id",
          }));

  std::vector<traceloom::compat::AnchorCostBreakdownSqlRow> rows(2);
  rows[0].anchor_idx = 2;
  rows[0].symbol = "ACLL";
  rows[0].anchor_kind = "graph_l";
  rows[0].total_us = 123.456;
  rows[0].self_us = 1.0;
  rows[0].aux_us = 2.0;
  rows[0].graph_child_us = 120.0;
  rows[0].residual_us = 0.456;
  rows[0].raw_child_task_count = 20;
  rows[0].top_ops = "MatMul:16";
  rows[0].diagnostic_flags = "partial_overlap";
  rows[1].anchor_idx = 3;
  rows[1].symbol = "Kernel";
  rows[1].anchor_kind = "exec";
  rows[1].total_us = 7.0;
  rows[1].self_us = 7.0;

  traceloom::compat::replace_anchor_cost_breakdown_rows(db_path, rows);
  const std::vector<StoredAnchorCostRow> stored = load_anchor_cost_rows(db_path);
  require(stored.size() == 2);
  require(stored[0].anchor_idx == 2);
  require(stored[0].symbol == "ACLL");
  require(stored[0].anchor_kind == "graph_l");
  require(stored[0].total_us == 123.456);
  require(stored[0].self_us == 1.0);
  require(stored[0].aux_us == 2.0);
  require(stored[0].graph_child_us == 120.0);
  require(stored[0].residual_us == 0.456);
  require(stored[0].raw_child_task_count == 20);
  require(stored[0].top_ops == "MatMul:16");
  require(stored[0].diagnostic_flags == "partial_overlap");
  require(stored[1].anchor_idx == 3);
  require(stored[1].symbol == "Kernel");
  require(stored[1].anchor_kind == "exec");
  require(stored[1].total_us == 7.0);

  traceloom::compat::replace_anchor_cost_breakdown_rows(
      db_path, {rows.front()});
  require(load_anchor_cost_rows(db_path).size() == 1);

}

}  // namespace traceloom::testing::sidecar_writer_test
