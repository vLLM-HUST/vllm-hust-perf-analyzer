#include "sidecar_views.h"

#include "sqlite_support.h"

namespace traceloom::compat {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)

void materialize_structural_compatibility_indexes(SqliteDb& db) {
  db.exec(
      "CREATE UNIQUE INDEX IF NOT EXISTS idx_traceloom_runtime_call_id "
      "ON traceloom_runtime_call(runtime_call_id)");
  db.exec(
      "CREATE UNIQUE INDEX IF NOT EXISTS idx_traceloom_device_work_id "
      "ON traceloom_device_work(device_work_id)");
  db.exec(
      "CREATE UNIQUE INDEX IF NOT EXISTS idx_traceloom_runtime_relation_id "
      "ON traceloom_runtime_device_relation(relation_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_event_device_step "
      "ON traceloom_event(db_idx, device_id, step_idx)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_event_id "
      "ON traceloom_event(event_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_event_identity "
      "ON traceloom_event(event_id, db_idx, device_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_event_source_lookup "
      "ON traceloom_event_source(source_table, source_key)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_runtime_call_time "
      "ON traceloom_runtime_call(db_idx, provider, clock_domain, start_ns, "
      "end_ns)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_runtime_call_correlation "
      "ON traceloom_runtime_call(provider, correlation_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_device_work_event "
      "ON traceloom_device_work(event_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_device_work_graph "
      "ON traceloom_device_work(graph_launch_occurrence_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_runtime_relation_call "
      "ON traceloom_runtime_device_relation(runtime_call_id, support_state)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_runtime_relation_work "
      "ON traceloom_runtime_device_relation(device_work_id, support_state)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_anchor_runtime_anchor "
      "ON traceloom_anchor_runtime_relation(anchor_id, relation_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_anchor_runtime_call "
      "ON traceloom_anchor_runtime_relation(runtime_call_id, anchor_id)");
  db.exec(
      "CREATE UNIQUE INDEX IF NOT EXISTS idx_traceloom_anchor_host_interval_id "
      "ON traceloom_anchor_host_interval(interval_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_anchor_host_interval_left "
      "ON traceloom_anchor_host_interval(db_idx, device_id, left_anchor_id)");
  db.exec(
      "CREATE UNIQUE INDEX IF NOT EXISTS "
      "idx_traceloom_anchor_host_activity_interval "
      "ON traceloom_anchor_host_activity(interval_id, observed_order)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_anchor_host_activity_call "
      "ON traceloom_anchor_host_activity(runtime_call_id, interval_id)");
  db.exec(
      "CREATE UNIQUE INDEX IF NOT EXISTS idx_traceloom_anchor_host_api_summary "
      "ON traceloom_anchor_host_api_summary(interval_id, api_family)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_anchor_host_interval_right "
      "ON traceloom_anchor_host_interval(db_idx, device_id, right_anchor_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_anchor_device_idx "
      "ON traceloom_anchor(db_idx, device_id, anchor_idx)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_anchor_key "
      "ON traceloom_anchor(anchor_id, db_idx, device_id)");
  db.exec(
      "CREATE UNIQUE INDEX IF NOT EXISTS idx_traceloom_symbol_rule_identity "
      "ON traceloom_symbol_normalization_rule(policy_id, policy_version, "
      "rule_id)");
  db.exec(
      "CREATE UNIQUE INDEX IF NOT EXISTS "
      "idx_traceloom_anchor_symbol_normalization_anchor "
      "ON traceloom_anchor_symbol_normalization(anchor_id, db_idx, "
      "device_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_symbol_normalization_symbol "
      "ON traceloom_anchor_symbol_normalization(db_idx, device_id, "
      "structural_symbol, observed_symbol)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_symbol_normalization_rule "
      "ON traceloom_anchor_symbol_normalization(rule_id, outcome)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_symbol_normalization_source "
      "ON traceloom_anchor_symbol_normalization(source_path, source_table, "
      "source_key)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_aux_anchor "
      "ON traceloom_aux_link(anchor_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_cuda_graph_replay_exec "
      "ON traceloom_cuda_graph_replay(db_idx, device_id, graph_exec_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_cuda_graph_envelope_graph "
      "ON traceloom_cuda_graph_envelope(graph_event_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_cuda_graph_envelope_child "
      "ON traceloom_cuda_graph_envelope(child_event_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_aclgraph_region_status "
      "ON traceloom_aclgraph_reconstruction_region("
      "db_idx, device_id, status)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_graph_launch_node "
      "ON traceloom_graph_launch(db_idx, device_id, graph_event_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_graph_launch_anchor "
      "ON traceloom_graph_launch(anchor_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_graph_launch_identity "
      "ON traceloom_graph_launch(launch_id, db_idx, device_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_graph_launch_anchor_identity "
      "ON traceloom_graph_launch(anchor_id, db_idx, device_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_graph_body_member_launch "
      "ON traceloom_graph_body_member(launch_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS "
      "idx_traceloom_graph_body_member_launch_identity "
      "ON traceloom_graph_body_member(launch_id, db_idx, device_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_graph_body_member_event "
      "ON traceloom_graph_body_member(event_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS "
      "idx_traceloom_graph_body_member_event_identity "
      "ON traceloom_graph_body_member(event_id, db_idx, device_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_graph_body_member_node "
      "ON traceloom_graph_body_member(graph_node_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_replay_cost_member_event "
      "ON traceloom_replay_cost_member(event_id, db_idx, device_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_replay_cost_member_launch "
      "ON traceloom_replay_cost_member(launch_id, db_idx, device_id, "
      "lane_ordinal, task_ordinal)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_replay_cost_member_observed "
      "ON traceloom_replay_cost_member(launch_id, db_idx, device_id, "
      "start_ns, end_ns, lane_ordinal, task_ordinal, member_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_replay_cost_aggregate_hotspot "
      "ON traceloom_replay_cost_aggregate(db_idx, device_id, "
      "duration_median_ns DESC)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_replay_cost_contributor "
      "ON traceloom_replay_cost_aggregate_member(aggregate_id, "
      "contributor_order)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_node_anchor_node "
      "ON traceloom_viz_node_anchor(node_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_viz_node_id "
      "ON traceloom_viz_node(node_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_node_anchor_occurrence "
      "ON traceloom_viz_node_anchor(node_id, db_idx, device_id, view_name, "
      "occurrence_idx)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_node_anchor_anchor "
      "ON traceloom_viz_node_anchor(anchor_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_semantic_node_tree_order "
      "ON traceloom_semantic_node(tree_id, preorder_idx)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_semantic_node_parent "
      "ON traceloom_semantic_node(parent_node_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_semantic_edge_tree "
      "ON traceloom_semantic_edge(tree_id, edge_order)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_collective_key "
      "ON traceloom_collective_global_link(candidate_collective_key)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_collective_pair "
      "ON traceloom_collective_global_link(pair_id, occurrence_idx, op_type, "
      "idx_in_occurrence)");
}

void materialize_global_collective_indexes(SqliteDb& db) {
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_global_collective_status "
      "ON traceloom_global_collective_summary(validation_status)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_global_collective_member_key "
      "ON traceloom_global_collective_member(candidate_collective_key)");
}

#endif

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
#endif

}  // namespace traceloom::compat
