#include "sidecar_views.h"

#include "sqlite_support.h"

namespace traceloom::compat {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)

void materialize_cuda_graph_views(SqliteDb& db) {
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_cuda_graph_replay AS "
      "SELECT "
      "g.*, "
      "e.symbol, "
      "e.label, "
      "e.task_type, "
      "e.semantic_role, "
      "e.semantic_role_reason, "
      "a.anchor_idx "
      "FROM traceloom_cuda_graph_replay g "
      "JOIN traceloom_event e ON e.event_id = g.event_id "
      "LEFT JOIN traceloom_anchor a ON a.event_id = e.event_id");
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_cuda_graph_envelope AS "
      "SELECT "
      "ge.*, "
      "graph_anchor.anchor_idx AS graph_anchor_idx, "
      "graph.label AS graph_label, "
      "graph.stream_id AS graph_stream_id, "
      "child.label AS child_label, "
      "child.task_type AS child_task_type, "
      "child.source_table AS child_source_table, "
      "child.stream_id AS child_stream_id, "
      "child.symbol AS child_symbol, "
      "child.semantic_role AS child_semantic_role "
      "FROM traceloom_cuda_graph_envelope ge "
      "JOIN traceloom_event graph ON graph.event_id = ge.graph_event_id "
      "LEFT JOIN traceloom_anchor graph_anchor ON "
      "graph_anchor.event_id = graph.event_id "
      "JOIN traceloom_event child ON child.event_id = ge.child_event_id");
}

void materialize_exact_graph_views(SqliteDb& db) {
  // Canonical tree-occurrence view over the exact graph relations. Rows are
  // the exact ordered members of a graph launch whose promoted tree anchor
  // exists in traceloom_viz_node_anchor; exact launches without a tree anchor
  // stay in the base tables and do not masquerade as node-view rows. The
  // launch<->anchor, launch<->member, and member<->event joins are composite
  // (id + db_idx + device_id), never bare IDs. Filter by (node_id,
  // occurrence_idx) or node_event_id to walk a tree node occurrence to its
  // exact members/events; filter by event_id to walk a member event back to
  // the tree occurrences that contain it.
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_node_graph_body_member AS "
      "SELECT "
      "na.node_id AS node_id, "
      "na.view_name AS view_name, "
      "na.occurrence_idx AS occurrence_idx, "
      "(SELECT COUNT(*) FROM traceloom_viz_node_anchor na2 "
      "WHERE na2.node_id = na.node_id AND na2.db_idx = na.db_idx "
      "AND na2.device_id = na.device_id AND na2.view_name = na.view_name "
      "AND na2.occurrence_idx = na.occurrence_idx "
      "AND na2.anchor_order < na.anchor_order) AS idx_in_occurrence, "
      "na.anchor_order AS anchor_order, "
      "na.coverage_kind AS coverage_kind, "
      "na.repeat_context AS repeat_context, "
      "na.compute_us AS anchor_compute_us, "
      "na.comm_us AS anchor_comm_us, "
      "na.idle_us AS anchor_idle_us, "
      "na.total_us AS anchor_total_us, "
      "na.self_us AS anchor_self_us, "
      "na.aux_events AS anchor_aux_events, "
      "na.aux_us AS anchor_aux_us, "
      "l.launch_id AS node_launch_id, "
      "l.graph_event_id AS node_event_id, "
      "l.anchor_id AS node_anchor_id, "
      "l.replay_unit_id AS node_replay_unit_id, "
      "l.graph_template_id AS node_graph_template_id, "
      "l.graph_launch_occurrence_id AS node_graph_launch_occurrence_id, "
      "l.member_order AS node_member_order, "
      "l.slot_order AS node_slot_order, "
      "l.correlation_id AS launch_correlation_id, "
      "l.match_policy AS launch_match_policy, "
      "l.association_policy AS launch_association_policy, "
      "l.start_ns AS launch_start_ns, "
      "l.end_ns AS launch_end_ns, "
      "l.dur_us AS launch_dur_us, "
      "m.member_id, "
      "m.db_idx, "
      "m.device_id, "
      "m.graph_provider, "
      "m.replay_unit_id, "
      "m.graph_template_id, "
      "m.graph_launch_occurrence_id, "
      "m.body_id, "
      "m.replay_body_template_id, "
      "m.member_order, "
      "m.slot_order, "
      "m.lane_ordinal, "
      "m.task_ordinal, "
      "m.kind, "
      "m.event_id, "
      "m.task_id, "
      "m.source_table, "
      "m.source_row_id, "
      "m.raw_task_id, "
      "m.start_ns, "
      "m.end_ns, "
      "m.dur_us, "
      "m.correlation_id, "
      "m.graph_node_id, "
      "m.original_graph_node_id, "
      "m.match_policy, "
      "m.association_policy, "
      "m.evidence_level, "
      "e.symbol AS member_symbol, "
      "e.label AS member_label, "
      "e.task_type AS member_task_type, "
      "e.semantic_role AS member_semantic_role "
      "FROM traceloom_graph_launch l "
      "JOIN traceloom_viz_node_anchor na "
      "ON na.anchor_id = l.anchor_id AND na.db_idx = l.db_idx "
      "AND na.device_id = l.device_id "
      "JOIN traceloom_graph_body_member m "
      "ON m.launch_id = l.launch_id AND m.db_idx = l.db_idx "
      "AND m.device_id = l.device_id "
      "JOIN traceloom_event e "
      "ON e.event_id = m.event_id AND e.db_idx = m.db_idx "
      "AND e.device_id = m.device_id");
}

void materialize_replay_cost_views(SqliteDb& db) {
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_node_replay_cost_member AS "
      "SELECT g.node_id, g.occurrence_idx, g.view_name, g.coverage_kind, "
      "g.node_anchor_id, g.node_member_order, g.node_slot_order, c.*, "
      "g.source_table, g.source_row_id, g.graph_node_id, "
      "g.original_graph_node_id, g.evidence_level "
      "FROM traceloom_v_node_graph_body_member g "
      "JOIN traceloom_replay_cost_member c "
      "ON c.member_id = g.member_id AND c.db_idx = g.db_idx "
      "AND c.device_id = g.device_id "
      "WHERE g.coverage_kind = 'self'");
}

#endif

}  // namespace traceloom::compat
