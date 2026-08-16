#include "replay_position_views.h"

#include "sidecar_sqlite_utils.h"

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
namespace traceloom::compat::detail {

void materialize_replay_position_views(sqlite3* db) {
  const std::uint64_t required_table_count = sqlite_scalar_u64(
      db,
      "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name IN ("
      "'traceloom_replay_cost_member','traceloom_graph_launch',"
      "'traceloom_graph_body_member','traceloom_anchor',"
      "'traceloom_evidence_role_decision')",
      "failed to inspect replay Position view prerequisites");
  if (required_table_count != 5) {
    sqlite_exec(
        db,
        "DROP VIEW IF EXISTS "
        "traceloom_v_replay_position_realization_member",
        "failed to drop unavailable replay Position realization view");
    return;
  }
  sqlite_exec(
      db,
      R"SQL(
DROP VIEW IF EXISTS traceloom_v_replay_position_realization_member;
CREATE VIEW traceloom_v_replay_position_realization_member AS
WITH ordered_member AS (
  SELECT
    launch.anchor_id AS position_anchor_id,
    member.launch_id,
    member.cost_unit_id,
    member.db_idx,
    member.device_id,
    launch.replay_unit_id,
    launch.graph_template_id,
    launch.graph_launch_occurrence_id,
    member.composition_slot_id,
    member.slot_role,
    member.slot_order AS position_order,
    member.replay_body_template_id,
    member.body_id,
    anchor.symbol AS position_symbol,
    anchor.role AS position_role,
    anchor.label AS position_label,
    anchor.start_ns AS position_start_ns,
    anchor.end_ns AS position_end_ns,
    member.member_id,
    (SELECT COUNT(*)
       FROM traceloom_replay_cost_member AS prior
      WHERE prior.launch_id = member.launch_id
        AND prior.db_idx = member.db_idx
        AND prior.device_id = member.device_id
        AND (prior.start_ns, prior.end_ns, prior.lane_ordinal,
             prior.task_ordinal, prior.member_id) <
            (member.start_ns, member.end_ns, member.lane_ordinal,
             member.task_ordinal, member.member_id)) AS observed_order,
    (SELECT prior.member_id
       FROM traceloom_replay_cost_member AS prior
      WHERE prior.launch_id = member.launch_id
        AND prior.db_idx = member.db_idx
        AND prior.device_id = member.device_id
        AND (prior.start_ns, prior.end_ns, prior.lane_ordinal,
             prior.task_ordinal, prior.member_id) <
            (member.start_ns, member.end_ns, member.lane_ordinal,
             member.task_ordinal, member.member_id)
      ORDER BY prior.start_ns DESC, prior.end_ns DESC,
               prior.lane_ordinal DESC, prior.task_ordinal DESC,
               prior.member_id DESC
      LIMIT 1) AS previous_member_id,
    (SELECT prior.end_ns
       FROM traceloom_replay_cost_member AS prior
      WHERE prior.launch_id = member.launch_id
        AND prior.db_idx = member.db_idx
        AND prior.device_id = member.device_id
        AND (prior.start_ns, prior.end_ns, prior.lane_ordinal,
             prior.task_ordinal, prior.member_id) <
            (member.start_ns, member.end_ns, member.lane_ordinal,
             member.task_ordinal, member.member_id)
      ORDER BY prior.start_ns DESC, prior.end_ns DESC,
               prior.lane_ordinal DESC, prior.task_ordinal DESC,
               prior.member_id DESC
      LIMIT 1) AS previous_member_end_ns,
    member.stream_id,
    member.lane_ordinal,
    member.task_ordinal,
    member.kind,
    member.identity,
    role.decision_id AS role_decision_id,
    role.policy_role,
    role.final_role,
    role.policy_structural_participation,
    role.effective_structural_participation,
    role.support_state AS role_support_state,
    role.reason_code AS role_reason_code,
    role.rule_id AS role_rule_id,
    member.start_ns,
    member.end_ns,
    member.duration_ns,
    member.relative_start_ns,
    member.relative_end_ns,
    member.scheduled_work_share_ppm,
    member.scheduled_work_share_supported,
    member.scheduled_work_denominator_body_task_sum_ns,
    body.match_policy,
    body.association_policy,
    body.evidence_level,
    body.source_table,
    body.source_row_id,
    role.source_table AS role_source_table,
    role.source_key AS role_source_key,
    member.event_id,
    member.raw_task_id
  FROM traceloom_replay_cost_member AS member
  JOIN traceloom_graph_launch AS launch
    ON launch.launch_id = member.launch_id
   AND launch.db_idx = member.db_idx
   AND launch.device_id = member.device_id
  JOIN traceloom_graph_body_member AS body
    ON body.launch_id = member.launch_id
   AND body.member_id = member.member_id
   AND body.db_idx = member.db_idx
   AND body.device_id = member.device_id
  LEFT JOIN traceloom_anchor AS anchor
    ON anchor.anchor_id = launch.anchor_id
   AND anchor.db_idx = launch.db_idx
   AND anchor.device_id = launch.device_id
  LEFT JOIN traceloom_evidence_role_decision AS role
    ON role.event_id = member.event_id
   AND role.db_idx = member.db_idx
   AND role.device_id = member.device_id
)
SELECT
  ordered_member.*,
  'exact_graph_body_member' AS membership_relation,
  CASE
    WHEN position_start_ns IS NULL OR position_end_ns IS NULL
      THEN 'unavailable'
    WHEN start_ns >= position_start_ns AND end_ns <= position_end_ns
      THEN 'contained'
    WHEN start_ns < position_end_ns AND end_ns > position_start_ns
      THEN 'boundary_overlap'
    ELSE 'disjoint'
  END AS interval_relation,
  CASE
    WHEN previous_member_id IS NULL THEN 'first'
    WHEN start_ns < previous_member_end_ns THEN 'overlaps_previous'
    WHEN start_ns = previous_member_end_ns THEN 'meets_previous'
    ELSE 'after_previous'
  END AS observed_relation_to_previous,
  'timestamp_order_not_dependency' AS observation_semantics
FROM ordered_member;
)SQL",
      "failed to materialize replay Position realization view");
}

}  // namespace traceloom::compat::detail
#endif
