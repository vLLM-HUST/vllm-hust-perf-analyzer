#include "sidecar_views.h"

#include "sqlite_support.h"

namespace traceloom::compat {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)

void drop_structural_compatibility_views(SqliteDb& db) {
  db.exec("DROP VIEW IF EXISTS traceloom_v_structure_bubble_host_context");
  db.exec("DROP VIEW IF EXISTS traceloom_v_structure_bubble_position");
  db.exec("DROP VIEW IF EXISTS traceloom_v_structure_bubble_api_stats");
  db.exec(
      "DROP VIEW IF EXISTS traceloom_v_structure_bubble_api_occurrence");
  db.exec("DROP VIEW IF EXISTS traceloom_v_structure_bubble_runtime_call");
  db.exec("DROP VIEW IF EXISTS traceloom_v_structure_bubble_occurrence");
  db.exec("DROP TABLE IF EXISTS traceloom_structure_bubble_api_stats");
  db.exec(
      "DROP TABLE IF EXISTS traceloom_structure_bubble_api_occurrence");
  db.exec("DROP TABLE IF EXISTS traceloom_structure_bubble_occurrence");
  db.exec("DROP TABLE IF EXISTS traceloom_structure_bubble_position");
  db.exec("DROP VIEW IF EXISTS traceloom_v_node_host_activity");
  db.exec("DROP VIEW IF EXISTS traceloom_v_node_host_interval");
  db.exec("DROP VIEW IF EXISTS traceloom_v_node_runtime_call");
  db.exec("DROP VIEW IF EXISTS traceloom_v_anchor_host_activity");
  db.exec("DROP VIEW IF EXISTS traceloom_v_anchor_host_interval");
  db.exec("DROP VIEW IF EXISTS traceloom_v_aux_runtime_call");
  db.exec("DROP VIEW IF EXISTS traceloom_v_anchor_runtime_call");
  db.exec("DROP VIEW IF EXISTS traceloom_v_sync_runtime_call");
  db.exec("DROP VIEW IF EXISTS traceloom_v_runtime_call_family");
  db.exec("DROP VIEW IF EXISTS traceloom_v_runtime_device");
  db.exec("DROP VIEW IF EXISTS traceloom_v_semantic_tree_readable");
  db.exec("DROP VIEW IF EXISTS traceloom_v_semantic_tree_node");
  db.exec("DROP VIEW IF EXISTS traceloom_v_tree_node");
  db.exec("DROP VIEW IF EXISTS traceloom_v_node_children");
  db.exec("DROP VIEW IF EXISTS traceloom_v_node_cost");
  db.exec("DROP VIEW IF EXISTS traceloom_v_node_aux_cost");
  db.exec("DROP VIEW IF EXISTS traceloom_v_node_anchor_cost");
  db.exec("DROP VIEW IF EXISTS traceloom_tree_node_occurrence");
  db.exec("DROP VIEW IF EXISTS traceloom_tree_node_anchor");
  db.exec("DROP VIEW IF EXISTS traceloom_v_cuda_graph_envelope");
  db.exec("DROP VIEW IF EXISTS traceloom_v_cuda_graph_replay");
  db.exec(
      "DROP VIEW IF EXISTS traceloom_v_replay_position_realization_member");
  db.exec("DROP VIEW IF EXISTS traceloom_v_node_replay_cost_member");
  db.exec("DROP VIEW IF EXISTS traceloom_v_node_graph_body_member");
}

void materialize_runtime_device_views(SqliteDb& db) {
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_runtime_device AS "
      "SELECT r.*, COALESCE(c.provider, w.provider) AS provider, "
      "c.clock_domain, c.source_table AS "
      "runtime_source_table, c.source_key AS runtime_source_key, c.start_ns "
      "AS runtime_start_ns, c.end_ns AS runtime_end_ns, c.dur_us AS "
      "runtime_dur_us, c.api_name, c.api_type, c.process_id, c.thread_id, "
      "c.global_tid, c.context_id AS runtime_context_id, w.device_id, "
      "w.work_kind, w.event_id, w.task_id, w.graph_launch_occurrence_id, "
      "w.source_table AS device_source_table, w.source_key AS "
      "device_source_key, w.start_ns AS device_start_ns, w.end_ns AS "
      "device_end_ns, w.dur_us AS device_dur_us, w.symbol AS device_symbol "
      "FROM traceloom_runtime_device_relation r "
      "LEFT JOIN traceloom_runtime_call c ON c.runtime_call_id = "
      "r.runtime_call_id "
      "LEFT JOIN traceloom_device_work w ON w.device_work_id = "
      "r.device_work_id");

  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_sync_runtime_call AS "
      "SELECT device_work_id AS sync_action_id, device_symbol AS sync_kind, "
      "* FROM traceloom_v_runtime_device WHERE "
      "(provider = 'cuda' AND device_source_table = "
      "'CUPTI_ACTIVITY_KIND_SYNCHRONIZATION') OR "
      "(provider = 'ascend' AND device_source_table IN ('TASK', "
      "'AscendTask') AND device_symbol IN ('EVENT_RECORD', 'EVENT_WAIT'))");

  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_runtime_call_family AS SELECT "
      "call.*, CASE WHEN LOWER(COALESCE(api_name, '')) GLOB 'acl*' OR "
      "LOWER(COALESCE(api_name, '')) GLOB 'cuda*' OR "
      "LOWER(COALESCE(api_name, '')) GLOB 'hip*' THEN 'public' ELSE "
      "'provider_internal_or_unknown' END AS api_layer, CASE WHEN "
      "LOWER(COALESCE(api_name, '')) LIKE '%wait%' THEN 'wait' WHEN "
      "LOWER(COALESCE(api_name, '')) LIKE '%synchronize%' THEN "
      "'synchronize' WHEN LOWER(COALESCE(api_name, '')) LIKE '%query%' "
      "THEN 'query' WHEN LOWER(COALESCE(api_name, '')) LIKE "
      "'%eventrecord%' OR LOWER(COALESCE(api_name, '')) LIKE "
      "'%recordevent%' THEN 'event_record' WHEN "
      "LOWER(COALESCE(api_name, '')) LIKE '%eventcreate%' OR "
      "LOWER(COALESCE(api_name, '')) LIKE '%createevent%' OR "
      "LOWER(COALESCE(api_name, '')) LIKE '%eventdestroy%' OR "
      "LOWER(COALESCE(api_name, '')) LIKE '%destroyevent%' THEN "
      "'event_lifecycle' WHEN LOWER(COALESCE(api_name, '')) LIKE "
      "'%graphlaunch%' OR LOWER(COALESCE(api_name, '')) LIKE "
      "'%aclmdlriexecuteasync%' THEN 'graph_launch' WHEN "
      "LOWER(COALESCE(api_name, '')) LIKE '%launch%' THEN 'launch' WHEN "
      "LOWER(COALESCE(api_name, '')) LIKE '%memcpy%' OR "
      "LOWER(COALESCE(api_name, '')) LIKE '%memset%' OR "
      "LOWER(COALESCE(api_name, '')) LIKE '%inplacecopy%' THEN 'memory' "
      "WHEN LOWER(COALESCE(api_name, '')) LIKE '%capture%' OR "
      "LOWER(COALESCE(api_name, '')) LIKE '%graph%' THEN 'graph_control' "
      "ELSE 'other' END AS api_family FROM traceloom_runtime_call call");

  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_anchor_runtime_call AS "
      "SELECT a.anchor_id, a.db_idx, a.device_id, a.anchor_idx, a.symbol AS "
      "anchor_symbol, a.role AS anchor_role, a.start_ns AS anchor_start_ns, "
      "a.end_ns AS anchor_end_ns, l.endpoint_kind, d.* "
      "FROM traceloom_anchor a "
      "JOIN traceloom_anchor_runtime_relation l ON l.anchor_id = "
      "a.anchor_id JOIN traceloom_v_runtime_device d ON d.relation_id = "
      "l.relation_id");

  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_node_runtime_call AS "
      "SELECT na.node_id, n.local_node_id, na.view_name, "
      "na.occurrence_idx, na.anchor_order, na.coverage_kind, "
      "na.repeat_context, a.anchor_idx, d.* "
      "FROM traceloom_viz_node_anchor na "
      "JOIN traceloom_viz_node n ON n.node_id = na.node_id "
      "AND n.db_idx = na.db_idx AND n.device_id = na.device_id "
      "AND n.view_name = na.view_name "
      "JOIN traceloom_anchor a ON a.anchor_id = na.anchor_id "
      "AND a.db_idx = na.db_idx AND a.device_id = na.device_id "
      "JOIN traceloom_anchor_runtime_relation l ON l.anchor_id = "
      "na.anchor_id JOIN traceloom_v_runtime_device d ON d.relation_id = "
      "l.relation_id");

  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_aux_runtime_call AS "
      "SELECT l.anchor_id, l.aux_event_id, l.aux_order, l.aux_kind, "
      "l.aux_dur_us, d.* FROM traceloom_aux_link l "
      "JOIN traceloom_device_work w ON w.event_id = l.aux_event_id "
      "JOIN traceloom_v_runtime_device d ON d.device_work_id = "
      "w.device_work_id");

  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_anchor_host_interval AS "
      "SELECT * FROM traceloom_anchor_host_interval");

  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_anchor_host_activity AS "
      "SELECT i.*, c.runtime_call_id AS observed_runtime_call_id, c.api_name, "
      "c.api_type, c.start_ns AS observed_start_ns, c.end_ns AS "
      "observed_end_ns, c.dur_us AS observed_dur_us, "
      "ROUND((MIN(c.end_ns, i.host_end_ns) - MAX(c.start_ns, "
      "i.host_start_ns)) / 1000.0, 3) AS observed_overlap_us, "
      "CASE WHEN c.start_ns >= i.host_start_ns AND c.end_ns <= "
      "i.host_end_ns THEN 'contained' ELSE 'boundary_overlap' END AS "
      "interval_relation, c.process_id AS "
      "observed_process_id, c.thread_id AS observed_thread_id, "
      "c.source_table AS observed_source_table, c.source_key AS "
      "observed_source_key, ROW_NUMBER() OVER (PARTITION BY i.interval_id "
      "ORDER BY c.start_ns, c.end_ns, c.runtime_call_id) - 1 AS "
      "observed_order FROM traceloom_v_anchor_host_interval i JOIN "
      "traceloom_runtime_call c ON i.support_state = 'supported_ordered' "
      "AND c.db_idx = i.db_idx AND c.provider = i.provider AND "
      "c.clock_domain = i.clock_domain AND c.start_ns < i.host_end_ns AND "
      "c.end_ns > i.host_start_ns AND (i.scope_policy <> 'same_process' OR "
      "c.process_id = i.process_id) AND (i.scope_policy <> 'same_thread' OR "
      "(c.process_id = i.process_id AND c.thread_id = i.thread_id))");

  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_node_host_interval AS "
      "SELECT na.node_id, n.local_node_id, na.view_name, "
      "na.occurrence_idx, na.anchor_order, na.coverage_kind, "
      "na.repeat_context, a.anchor_idx, next.anchor_idx AS "
      "right_anchor_idx, next.symbol AS right_anchor_symbol, next.role AS "
      "right_anchor_role, ROUND((h.host_end_ns - h.host_start_ns) / 1000.0, "
      "3) AS host_interval_us, "
      "'after_anchor_interval' AS placement_semantics, h.* "
      "FROM traceloom_viz_node_anchor na "
      "JOIN traceloom_viz_node n ON n.node_id = na.node_id "
      "AND n.db_idx = na.db_idx AND n.device_id = na.device_id "
      "AND n.view_name = na.view_name "
      "JOIN traceloom_anchor a ON a.anchor_id = na.anchor_id "
      "AND a.db_idx = na.db_idx AND a.device_id = na.device_id "
      "JOIN traceloom_v_anchor_host_interval h ON h.left_anchor_id = "
      "na.anchor_id AND h.db_idx = na.db_idx AND h.device_id = "
      "na.device_id JOIN traceloom_anchor next ON next.anchor_id = "
      "h.right_anchor_id AND next.db_idx = h.db_idx AND next.device_id = "
      "h.device_id");

  // Keep interval support states in the node projection even when no runtime
  // call was observed.  The activity surface is intentionally a narrower
  // inner projection over those intervals; callers that need a typed result
  // for every structural coordinate start from traceloom_v_node_host_interval
  // and left-join activity.
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_node_host_activity AS "
      "SELECT i.*, c.runtime_call_id AS observed_runtime_call_id, c.api_name, "
      "c.api_type, c.start_ns AS observed_start_ns, c.end_ns AS "
      "observed_end_ns, c.dur_us AS observed_dur_us, "
      "ROUND((MIN(c.end_ns, i.host_end_ns) - MAX(c.start_ns, "
      "i.host_start_ns)) / 1000.0, 3) AS observed_overlap_us, "
      "CASE WHEN c.start_ns >= i.host_start_ns AND c.end_ns <= "
      "i.host_end_ns THEN 'contained' ELSE 'boundary_overlap' END AS "
      "interval_relation, c.process_id AS observed_process_id, c.thread_id "
      "AS observed_thread_id, c.source_table AS observed_source_table, "
      "c.source_key AS observed_source_key, ROW_NUMBER() OVER (PARTITION BY "
      "i.node_id, i.occurrence_idx, i.interval_id ORDER BY c.start_ns, "
      "c.end_ns, c.runtime_call_id) - 1 AS observed_order FROM "
      "traceloom_v_node_host_interval i JOIN traceloom_runtime_call c ON "
      "i.support_state = 'supported_ordered' AND c.db_idx = i.db_idx AND "
      "c.provider = i.provider AND c.clock_domain = i.clock_domain AND "
      "c.start_ns < i.host_end_ns AND c.end_ns > i.host_start_ns AND "
      "(i.scope_policy <> 'same_process' OR c.process_id = i.process_id) "
      "AND (i.scope_policy <> 'same_thread' OR (c.process_id = i.process_id "
      "AND c.thread_id = i.thread_id))");
}

#endif

}  // namespace traceloom::compat
