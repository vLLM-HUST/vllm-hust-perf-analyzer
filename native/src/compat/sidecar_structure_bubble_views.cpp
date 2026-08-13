#include "sidecar_views.h"

#include "sqlite_support.h"

namespace traceloom::compat {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)

void materialize_structure_bubble_views(SqliteDb& db) {
  // A bubble occurrence is the overlap-safe uncovered device cost assigned
  // before a node-owned anchor, conditioned on that node's recovered
  // structural placement. The associated host interval is selected through
  // adjacent anchors' supported runtime endpoints, never by host/device
  // timestamp overlap. These surfaces expose observations and distributions
  // only; they do not assign a bubble cause.
  // Materialize the compact occurrence and distribution surfaces once. The
  // host-activity relation can contain millions of links; leaving this as a
  // stack of aggregate views makes an otherwise simple agent query rescan the
  // full relation. Detailed calls remain a selective drill-down view.
  db.exec(
      "CREATE TABLE traceloom_structure_bubble_occurrence AS "
      "SELECT "
      "'bubble-' || right_anchor.anchor_id AS bubble_id, "
      "right_na.db_idx, right_na.device_id, right_na.view_name, "
      "right_node.node_id AS right_node_id, "
      "right_node.local_node_id AS right_local_node_id, "
      "right_node.path AS right_node_path, "
      "right_node.kind AS right_node_kind, "
      "right_node.symbol AS right_node_symbol, "
      "right_na.occurrence_idx AS right_occurrence_idx, "
      "right_na.repeat_context, "
      "right_node.node_id AS structural_position_id, "
      "left_anchor.anchor_id AS left_anchor_id, "
      "left_anchor.anchor_idx AS left_anchor_idx, "
      "left_anchor.symbol AS left_anchor_symbol, "
      "left_anchor.role AS left_anchor_role, "
      "left_anchor.start_ns AS left_anchor_start_ns, "
      "left_anchor.end_ns AS left_anchor_end_ns, "
      "right_anchor.anchor_id AS right_anchor_id, "
      "right_anchor.anchor_idx AS right_anchor_idx, "
      "right_anchor.symbol AS right_anchor_symbol, "
      "right_anchor.role AS right_anchor_role, "
      "right_anchor.start_ns AS right_anchor_start_ns, "
      "right_anchor.end_ns AS right_anchor_end_ns, "
      "ROUND(MAX(0, right_anchor.start_ns - left_anchor.end_ns) / 1000.0, "
      "3) AS adjacent_anchor_gap_us, "
      "ROUND(right_na.idle_us, 3) AS bubble_us, "
      "ROUND(right_na.compute_us, 3) AS transition_compute_us, "
      "ROUND(right_na.comm_us, 3) AS transition_comm_us, "
      "ROUND(right_na.total_us, 3) AS transition_total_us, "
      "right_na.aux_events AS transition_aux_events, "
      "ROUND(right_na.aux_us, 3) AS transition_aux_us, "
      "ROUND(CASE WHEN right_na.total_us <= 0.0 THEN 0.0 ELSE "
      "right_na.idle_us / right_na.total_us END, 6) AS "
      "bubble_fraction_of_transition, "
      "host.interval_id AS host_interval_id, host.provider, "
      "host.clock_domain AS host_clock_domain, host.scope_policy, "
      "host.support_state AS host_observation_status, "
      "host.host_start_ns, host.host_end_ns, "
      "ROUND(CASE WHEN host.support_state = 'supported_ordered' THEN "
      "(host.host_end_ns - host.host_start_ns) / 1000.0 ELSE NULL END, 3) "
      "AS host_interval_us, "
      "'upstream_between_correlated_anchor_endpoints' AS "
      "api_association_semantics "
      "FROM traceloom_viz_node_anchor right_na "
      "JOIN traceloom_viz_node right_node ON right_node.node_id = "
      "right_na.node_id AND right_node.db_idx = right_na.db_idx AND "
      "right_node.device_id = right_na.device_id AND right_node.view_name = "
      "right_na.view_name "
      "JOIN traceloom_anchor right_anchor ON right_anchor.anchor_id = "
      "right_na.anchor_id AND right_anchor.db_idx = right_na.db_idx AND "
      "right_anchor.device_id = right_na.device_id "
      "JOIN traceloom_anchor_host_interval host ON host.right_anchor_id = "
      "right_anchor.anchor_id AND host.db_idx = right_na.db_idx AND "
      "host.device_id = right_na.device_id "
      "JOIN traceloom_anchor left_anchor ON left_anchor.anchor_id = "
      "host.left_anchor_id AND left_anchor.db_idx = host.db_idx AND "
      "left_anchor.device_id = host.device_id "
      "WHERE right_na.coverage_kind = 'self' AND right_na.idle_us > 0.0");

  db.exec(
      "CREATE UNIQUE INDEX idx_traceloom_structure_bubble_id ON "
      "traceloom_structure_bubble_occurrence(bubble_id)");
  db.exec(
      "CREATE INDEX idx_traceloom_structure_bubble_transition ON "
      "traceloom_structure_bubble_occurrence(db_idx, device_id, view_name, "
      "structural_position_id, bubble_us DESC)");
  db.exec(
      "CREATE INDEX idx_traceloom_structure_bubble_host_interval ON "
      "traceloom_structure_bubble_occurrence(host_interval_id)");
  db.exec(
      "CREATE VIEW traceloom_v_structure_bubble_occurrence AS SELECT * FROM "
      "traceloom_structure_bubble_occurrence");

  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_structure_bubble_runtime_call AS "
      "SELECT bubble.*, activity.observed_runtime_call_id AS "
      "runtime_call_id, activity.api_name, activity.api_type, "
      "activity.observed_start_ns AS runtime_start_ns, "
      "activity.observed_end_ns AS runtime_end_ns, "
      "activity.observed_dur_us AS runtime_dur_us, "
      "activity.observed_overlap_us, activity.interval_relation, "
      "activity.observed_process_id AS process_id, "
      "activity.observed_thread_id AS thread_id, "
      "activity.observed_source_table AS runtime_source_table, "
      "activity.observed_source_key AS runtime_source_key, "
      "activity.observed_order, "
      "CASE WHEN LOWER(COALESCE(activity.api_name, '')) GLOB 'acl*' OR "
      "LOWER(COALESCE(activity.api_name, '')) GLOB 'cuda*' OR "
      "LOWER(COALESCE(activity.api_name, '')) GLOB 'hip*' THEN 'public' "
      "ELSE 'provider_internal_or_unknown' END AS api_layer, "
      "CASE "
      "WHEN LOWER(COALESCE(activity.api_name, '')) LIKE '%wait%' THEN 'wait' "
      "WHEN LOWER(COALESCE(activity.api_name, '')) LIKE '%synchronize%' "
      "THEN 'synchronize' "
      "WHEN LOWER(COALESCE(activity.api_name, '')) LIKE '%query%' THEN "
      "'query' "
      "WHEN LOWER(COALESCE(activity.api_name, '')) LIKE '%eventrecord%' OR "
      "LOWER(COALESCE(activity.api_name, '')) LIKE '%recordevent%' THEN "
      "'event_record' "
      "WHEN (LOWER(COALESCE(activity.api_name, '')) LIKE '%eventcreate%' OR "
      "LOWER(COALESCE(activity.api_name, '')) LIKE '%createevent%' OR "
      "LOWER(COALESCE(activity.api_name, '')) LIKE '%eventdestroy%' OR "
      "LOWER(COALESCE(activity.api_name, '')) LIKE '%destroyevent%') THEN "
      "'event_lifecycle' "
      "WHEN LOWER(COALESCE(activity.api_name, '')) LIKE '%graphlaunch%' OR "
      "LOWER(COALESCE(activity.api_name, '')) LIKE "
      "'%aclmdlriexecuteasync%' THEN 'graph_launch' "
      "WHEN LOWER(COALESCE(activity.api_name, '')) LIKE '%launch%' THEN "
      "'launch' "
      "WHEN LOWER(COALESCE(activity.api_name, '')) LIKE '%memcpy%' OR "
      "LOWER(COALESCE(activity.api_name, '')) LIKE '%memset%' OR "
      "LOWER(COALESCE(activity.api_name, '')) LIKE '%inplacecopy%' THEN "
      "'memory' "
      "WHEN LOWER(COALESCE(activity.api_name, '')) LIKE '%capture%' OR "
      "LOWER(COALESCE(activity.api_name, '')) LIKE '%graph%' THEN "
      "'graph_control' ELSE 'other' END AS api_family "
      "FROM traceloom_structure_bubble_occurrence bubble "
      "JOIN traceloom_v_anchor_host_activity activity ON "
      "activity.interval_id = bubble.host_interval_id "
      "WHERE bubble.host_observation_status = 'supported_ordered'");

  db.exec(
      "CREATE TABLE traceloom_structure_bubble_api_occurrence AS "
      "SELECT bubble.bubble_id, summary.api_family, summary.call_count, "
      "summary.distinct_api_name_count, "
      "ROUND(summary.scheduled_call_us, 3) AS scheduled_call_us, "
      "ROUND(summary.scheduled_overlap_us, 3) AS scheduled_overlap_us "
      "FROM traceloom_structure_bubble_occurrence bubble "
      "JOIN traceloom_anchor_host_api_summary summary ON "
      "summary.interval_id = bubble.host_interval_id "
      "WHERE bubble.host_observation_status = 'supported_ordered'");

  db.exec(
      "CREATE UNIQUE INDEX idx_traceloom_structure_bubble_api_occurrence ON "
      "traceloom_structure_bubble_api_occurrence(bubble_id, api_family)");
  db.exec(
      "CREATE VIEW traceloom_v_structure_bubble_api_occurrence AS SELECT * "
      "FROM traceloom_structure_bubble_api_occurrence");

  db.exec(
      "CREATE TABLE traceloom_structure_bubble_api_stats AS "
      "WITH bubble_population AS ("
      "SELECT db_idx, device_id, view_name, structural_position_id, "
      "right_node_id, "
      "right_local_node_id, right_node_symbol, "
      "COUNT(*) AS bubble_occurrence_count, "
      "SUM(CASE WHEN host_observation_status = 'supported_ordered' THEN "
      "1 ELSE 0 END) AS host_observable_occurrence_count, "
      "ROUND(SUM(bubble_us), 3) AS total_bubble_us, "
      "ROUND(AVG(bubble_us), 3) AS avg_bubble_us, "
      "ROUND(MIN(bubble_us), 3) AS min_bubble_us, "
      "ROUND(MAX(bubble_us), 3) AS max_bubble_us "
      "FROM traceloom_structure_bubble_occurrence "
      "GROUP BY db_idx, device_id, view_name, structural_position_id, "
      "right_node_id, "
      "right_local_node_id, right_node_symbol), "
      "api_population AS ("
      "SELECT bubble.db_idx, bubble.device_id, bubble.view_name, "
      "bubble.structural_position_id, api.api_family, "
      "COUNT(*) AS presence_count, SUM(api.call_count) AS total_call_count, "
      "SUM(api.distinct_api_name_count) AS summed_distinct_api_names, "
      "ROUND(SUM(api.scheduled_call_us), 3) AS total_scheduled_call_us, "
      "ROUND(SUM(api.scheduled_overlap_us), 3) AS "
      "total_scheduled_overlap_us "
      "FROM traceloom_structure_bubble_api_occurrence api "
      "JOIN traceloom_structure_bubble_occurrence bubble ON "
      "bubble.bubble_id = api.bubble_id "
      "GROUP BY bubble.db_idx, bubble.device_id, bubble.view_name, "
      "bubble.structural_position_id, api.api_family) "
      "SELECT bubbles.*, api.api_family, api.presence_count, "
      "ROUND(api.presence_count * 1.0 / bubbles.bubble_occurrence_count, 6) "
      "AS presence_fraction_of_all_bubbles, "
      "ROUND(api.presence_count * 1.0 / "
      "bubbles.host_observable_occurrence_count, 6) AS "
      "presence_fraction_of_observable_bubbles, "
      "ROUND(bubbles.host_observable_occurrence_count * 1.0 / "
      "bubbles.bubble_occurrence_count, 6) AS host_observation_coverage, "
      "api.total_call_count, "
      "ROUND(api.total_call_count * 1.0 / bubbles.bubble_occurrence_count, "
      "3) AS avg_calls_per_bubble, "
      "api.summed_distinct_api_names, "
      "ROUND(api.total_call_count * 1.0 / "
      "bubbles.host_observable_occurrence_count, 3) AS "
      "avg_calls_per_observable_bubble, "
      "api.total_scheduled_call_us, "
      "ROUND(api.total_scheduled_call_us / bubbles.bubble_occurrence_count, "
      "3) AS avg_scheduled_call_us_per_bubble, "
      "api.total_scheduled_overlap_us, "
      "ROUND(api.total_scheduled_overlap_us / "
      "bubbles.bubble_occurrence_count, 3) AS "
      "avg_scheduled_overlap_us_per_bubble, "
      "'call durations may overlap; counts and scheduled durations are "
      "observations, not device-cause attribution' AS interpretation_note "
      "FROM bubble_population bubbles JOIN api_population api ON "
      "api.db_idx = bubbles.db_idx AND api.device_id = bubbles.device_id "
      "AND api.view_name = bubbles.view_name AND "
      "api.structural_position_id = bubbles.structural_position_id");

  db.exec(
      "CREATE UNIQUE INDEX idx_traceloom_structure_bubble_api_stats ON "
      "traceloom_structure_bubble_api_stats(db_idx, device_id, view_name, "
      "structural_position_id, api_family)");
  db.exec(
      "CREATE INDEX idx_traceloom_structure_bubble_api_hotspot ON "
      "traceloom_structure_bubble_api_stats(total_bubble_us DESC, "
      "bubble_occurrence_count DESC)");
  db.exec(
      "CREATE VIEW traceloom_v_structure_bubble_api_stats AS SELECT * FROM "
      "traceloom_structure_bubble_api_stats");
}

#endif

}  // namespace traceloom::compat
