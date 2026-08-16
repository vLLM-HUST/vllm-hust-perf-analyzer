#include "replay_position_views.h"

#include "sidecar_sqlite_utils.h"

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
namespace traceloom::compat::detail {

void materialize_replay_position_views(sqlite3* db) {
  const std::uint64_t timeline_required_table_count = sqlite_scalar_u64(
      db,
      "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name IN ("
      "'traceloom_anchor','traceloom_event','traceloom_graph_launch',"
      "'traceloom_graph_body_member','traceloom_protected_interval')",
      "failed to inspect annotated timeline view prerequisites");
  if (timeline_required_table_count == 5) {
    sqlite_exec(
        db,
        R"SQL(
DROP VIEW IF EXISTS traceloom_v_flattened_execution_timeline;
DROP VIEW IF EXISTS traceloom_v_annotated_anchor_timeline;
DROP VIEW IF EXISTS traceloom_v_replay_region_annotation_status;

CREATE VIEW traceloom_v_replay_region_annotation_status AS
WITH device AS (
  SELECT DISTINCT db_idx, device_id
  FROM traceloom_anchor
),
ordered_region AS (
  SELECT
    interval.protected_interval_id,
    interval.db_idx,
    interval.device_id,
    interval.replay_unit_id,
    interval.support_state,
    interval.reason_code,
    interval.start_ns,
    interval.end_ns,
    first_anchor.anchor_idx AS first_anchor_idx,
    last_anchor.anchor_idx AS last_anchor_idx,
    first_anchor.start_ns AS first_anchor_start_ns,
    last_anchor.end_ns AS last_anchor_end_ns,
    LEAD(first_anchor.anchor_idx) OVER (
      PARTITION BY interval.db_idx, interval.device_id
      ORDER BY first_anchor.anchor_idx, interval.protected_interval_id
    ) AS next_first_anchor_idx
  FROM traceloom_protected_interval AS interval
  LEFT JOIN traceloom_anchor AS first_anchor
    ON first_anchor.anchor_id = interval.first_anchor_id
   AND first_anchor.db_idx = interval.db_idx
   AND first_anchor.device_id = interval.device_id
  LEFT JOIN traceloom_anchor AS last_anchor
    ON last_anchor.anchor_id = interval.last_anchor_id
   AND last_anchor.db_idx = interval.db_idx
   AND last_anchor.device_id = interval.device_id
  WHERE interval.kind = 'graph_replay_unit'
),
summary AS (
  SELECT
    db_idx,
    device_id,
    COUNT(*) AS replay_region_count,
    SUM(CASE WHEN support_state = 'supported' THEN 1 ELSE 0 END)
      AS supported_region_count,
    SUM(CASE WHEN support_state <> 'supported' THEN 1 ELSE 0 END)
      AS unsupported_region_count,
    SUM(CASE
          WHEN first_anchor_idx IS NULL OR last_anchor_idx IS NULL
            OR first_anchor_idx > last_anchor_idx
          THEN 1 ELSE 0
        END) AS invalid_bound_count,
    SUM(CASE
          WHEN first_anchor_start_ns IS NULL OR last_anchor_end_ns IS NULL
            OR start_ns <> first_anchor_start_ns
            OR end_ns <> last_anchor_end_ns
          THEN 1 ELSE 0
        END) AS boundary_time_mismatch_count,
    SUM(CASE
          WHEN next_first_anchor_idx IS NOT NULL
            AND last_anchor_idx >= next_first_anchor_idx
          THEN 1 ELSE 0
        END) AS overlap_count
  FROM ordered_region
  GROUP BY db_idx, device_id
)
SELECT
  device.db_idx,
  device.device_id,
  COALESCE(summary.replay_region_count, 0) AS replay_region_count,
  COALESCE(summary.supported_region_count, 0) AS supported_region_count,
  COALESCE(summary.unsupported_region_count, 0)
    AS unsupported_region_count,
  COALESCE(summary.invalid_bound_count, 0) AS invalid_bound_count,
  COALESCE(summary.boundary_time_mismatch_count, 0)
    AS boundary_time_mismatch_count,
  COALESCE(summary.overlap_count, 0) AS overlap_count,
  CASE
    WHEN summary.replay_region_count IS NULL THEN 'unavailable'
    WHEN summary.unsupported_region_count > 0 THEN 'unsupported'
    WHEN summary.invalid_bound_count > 0 THEN 'unsupported'
    WHEN summary.boundary_time_mismatch_count > 0 THEN 'unsupported'
    WHEN summary.overlap_count > 0 THEN 'unsupported'
    ELSE 'supported'
  END AS support_state,
  CASE
    WHEN summary.replay_region_count IS NULL THEN 'no_exact_replay_regions'
    WHEN summary.unsupported_region_count > 0
      THEN 'non_exact_protected_interval'
    WHEN summary.invalid_bound_count > 0 THEN 'invalid_anchor_bounds'
    WHEN summary.boundary_time_mismatch_count > 0
      THEN 'protected_interval_boundary_mismatch'
    WHEN summary.overlap_count > 0 THEN 'overlapping_replay_regions'
    ELSE 'ordered_disjoint_exact_replay_regions'
  END AS reason_code
FROM device
LEFT JOIN summary
  ON summary.db_idx = device.db_idx
 AND summary.device_id = device.device_id;

CREATE VIEW traceloom_v_annotated_anchor_timeline AS
WITH exact_region AS (
  SELECT
    interval.protected_interval_id,
    interval.db_idx,
    interval.device_id,
    interval.replay_unit_id,
    interval.start_ns AS replay_region_start_ns,
    interval.end_ns AS replay_region_end_ns,
    first_anchor.anchor_idx AS first_anchor_idx,
    last_anchor.anchor_idx AS last_anchor_idx
  FROM traceloom_protected_interval AS interval
  JOIN traceloom_anchor AS first_anchor
    ON first_anchor.anchor_id = interval.first_anchor_id
   AND first_anchor.db_idx = interval.db_idx
   AND first_anchor.device_id = interval.device_id
  JOIN traceloom_anchor AS last_anchor
    ON last_anchor.anchor_id = interval.last_anchor_id
   AND last_anchor.db_idx = interval.db_idx
   AND last_anchor.device_id = interval.device_id
  WHERE interval.kind = 'graph_replay_unit'
    AND interval.support_state = 'supported'
),
position_summary AS (
  SELECT
    anchor_id,
    db_idx,
    device_id,
    COUNT(*) AS position_match_count,
    CASE WHEN COUNT(*) = 1 THEN MIN(launch_id) END AS launch_id,
    CASE WHEN COUNT(*) = 1 THEN MIN(graph_provider) END AS graph_provider,
    CASE WHEN COUNT(*) = 1 THEN MIN(replay_unit_id) END AS replay_unit_id,
    CASE WHEN COUNT(*) = 1 THEN MIN(graph_template_id) END
      AS graph_template_id,
    CASE WHEN COUNT(*) = 1 THEN MIN(graph_launch_occurrence_id) END
      AS graph_launch_occurrence_id,
    CASE WHEN COUNT(*) = 1 THEN MIN(replay_body_template_id) END
      AS replay_body_template_id,
    CASE WHEN COUNT(*) = 1 THEN MIN(body_id) END AS body_id,
    CASE WHEN COUNT(*) = 1 THEN MIN(member_order) END AS member_order,
    CASE WHEN COUNT(*) = 1 THEN MIN(slot_order) END AS slot_order,
    CASE WHEN COUNT(*) = 1 THEN MIN(match_policy) END AS match_policy,
    CASE WHEN COUNT(*) = 1 THEN MIN(association_policy) END
      AS association_policy,
    CASE WHEN COUNT(*) = 1 THEN MIN(evidence_level) END AS evidence_level
  FROM traceloom_graph_launch
  WHERE anchor_id IS NOT NULL
  GROUP BY anchor_id, db_idx, device_id
)
SELECT
  anchor.anchor_id,
  anchor.db_idx,
  anchor.device_id,
  anchor.anchor_idx,
  anchor.event_id,
  anchor.step_idx,
  anchor.symbol,
  anchor.role,
  anchor.label,
  anchor.family,
  event.source_table,
  event.source_key,
  event.stream_id,
  event.category,
  event.semantic_role,
  event.task_type,
  anchor.start_ns,
  anchor.end_ns,
  anchor.dur_us,
  status.support_state AS replay_annotation_support_state,
  status.reason_code AS replay_annotation_reason_code,
  status.replay_region_count,
  CASE
    WHEN status.support_state = 'supported'
      AND region.protected_interval_id IS NOT NULL
      THEN 'inside_exact_replay'
    WHEN status.support_state = 'supported' THEN 'outside_exact_replay'
    WHEN status.support_state = 'unavailable' THEN 'unavailable'
    ELSE 'unsupported'
  END AS replay_region_state,
  CASE WHEN status.support_state = 'supported'
       THEN region.protected_interval_id END AS protected_interval_id,
  CASE WHEN status.support_state = 'supported'
       THEN region.replay_unit_id END AS replay_occurrence_id,
  CASE WHEN status.support_state = 'supported'
       THEN region.replay_region_start_ns END AS replay_region_start_ns,
  CASE WHEN status.support_state = 'supported'
       THEN region.replay_region_end_ns END AS replay_region_end_ns,
  CASE WHEN status.support_state = 'supported'
       THEN region.first_anchor_idx END AS replay_region_first_anchor_idx,
  CASE WHEN status.support_state = 'supported'
       THEN region.last_anchor_idx END AS replay_region_last_anchor_idx,
  CASE WHEN status.support_state = 'supported'
         AND region.protected_interval_id IS NOT NULL
       THEN anchor.anchor_idx - region.first_anchor_idx END
    AS replay_region_anchor_offset,
  COALESCE(position.position_match_count, 0) AS position_match_count,
  CASE
    WHEN position.position_match_count = 1 THEN 'exact_position'
    WHEN position.position_match_count > 1 THEN 'ambiguous_position_anchor'
    ELSE 'not_position'
  END AS position_support_state,
  position.launch_id AS replay_position_id,
  position.graph_provider,
  position.replay_unit_id AS provider_replay_unit_id,
  position.graph_template_id AS replay_pattern_id,
  position.graph_launch_occurrence_id,
  position.replay_body_template_id,
  position.body_id,
  position.member_order AS replay_position_member_order,
  position.slot_order AS replay_position_order,
  position.match_policy AS replay_position_match_policy,
  position.association_policy AS replay_position_association_policy,
  position.evidence_level AS replay_position_evidence_level,
  'one_mainline_anchor' AS row_grain,
  'region_highlight_not_event_ownership' AS replay_annotation_semantics,
  'anchor_order_not_dependency' AS observation_semantics
FROM traceloom_anchor AS anchor
JOIN traceloom_v_replay_region_annotation_status AS status
  ON status.db_idx = anchor.db_idx
 AND status.device_id = anchor.device_id
LEFT JOIN exact_region AS region
  ON status.support_state = 'supported'
 AND region.db_idx = anchor.db_idx
 AND region.device_id = anchor.device_id
 AND anchor.anchor_idx BETWEEN region.first_anchor_idx
                           AND region.last_anchor_idx
LEFT JOIN traceloom_event AS event
  ON event.event_id = anchor.event_id
 AND event.db_idx = anchor.db_idx
 AND event.device_id = anchor.device_id
LEFT JOIN position_summary AS position
  ON position.anchor_id = anchor.anchor_id
 AND position.db_idx = anchor.db_idx
 AND position.device_id = anchor.device_id;

CREATE VIEW traceloom_v_flattened_execution_timeline AS
WITH expandable_member AS (
  SELECT
    position.db_idx,
    position.device_id,
    position.anchor_idx,
    position.anchor_id AS position_anchor_id,
    position.replay_position_id,
    position.replay_occurrence_id,
    position.protected_interval_id,
    position.replay_region_start_ns,
    position.replay_region_end_ns,
    position.replay_region_first_anchor_idx,
    position.replay_region_last_anchor_idx,
    position.replay_region_anchor_offset,
    position.replay_pattern_id,
    position.provider_replay_unit_id,
    position.graph_launch_occurrence_id,
    position.replay_body_template_id,
    position.body_id,
    position.replay_position_member_order,
    position.replay_position_order,
    position.graph_provider,
    position.replay_position_match_policy,
    position.replay_position_association_policy,
    position.replay_position_evidence_level,
    member.member_id,
    member.event_id,
    member.kind AS member_kind,
    member.lane_ordinal,
    member.task_ordinal,
    member.raw_task_id,
    member.start_ns,
    member.end_ns,
    member.dur_us,
    event.symbol,
    event.role,
    event.label,
    event.family,
    event.source_table,
    event.source_key,
    event.stream_id,
    event.category,
    event.semantic_role,
    event.task_type,
    ROW_NUMBER() OVER (
      PARTITION BY member.launch_id, member.db_idx, member.device_id
      ORDER BY member.start_ns, member.end_ns, member.lane_ordinal,
               member.task_ordinal, member.member_id
    ) - 1 AS observed_position_member_order
  -- Preserve the selective Position -> launch-member -> event order. SQLite
  -- otherwise may start from the much larger event plane when this nested
  -- view is expanded, turning a bounded timeline read into a quadratic scan.
  FROM traceloom_v_annotated_anchor_timeline AS position
  CROSS JOIN traceloom_graph_body_member AS member
  CROSS JOIN traceloom_event AS event
  WHERE position.replay_annotation_support_state = 'supported'
    AND position.replay_region_state = 'inside_exact_replay'
    AND position.position_support_state = 'exact_position'
    AND member.launch_id = position.replay_position_id
    AND member.db_idx = position.db_idx
    AND member.device_id = position.device_id
    AND event.event_id = member.event_id
    AND event.db_idx = member.db_idx
    AND event.device_id = member.device_id
),
flat_item AS (
  SELECT
    'anchor:' || anchor.anchor_id AS timeline_item_id,
    anchor.db_idx,
    anchor.device_id,
    anchor.anchor_idx AS mainline_anchor_idx,
    anchor.anchor_id AS mainline_anchor_id,
    CASE WHEN anchor.position_support_state = 'exact_position'
         THEN anchor.anchor_id END AS position_anchor_id,
    'mainline_anchor' AS item_kind,
    anchor.event_id,
    anchor.symbol,
    anchor.role,
    anchor.label,
    anchor.family,
    anchor.source_table,
    anchor.source_key,
    anchor.stream_id,
    anchor.category,
    anchor.semantic_role,
    anchor.task_type,
    anchor.start_ns,
    anchor.end_ns,
    anchor.dur_us,
    NULL AS member_kind,
    NULL AS lane_ordinal,
    NULL AS task_ordinal,
    NULL AS raw_task_id,
    NULL AS observed_position_member_order,
    anchor.replay_annotation_support_state,
    anchor.replay_annotation_reason_code,
    anchor.replay_region_state,
    anchor.protected_interval_id,
    anchor.replay_occurrence_id,
    anchor.replay_region_start_ns,
    anchor.replay_region_end_ns,
    anchor.replay_region_first_anchor_idx,
    anchor.replay_region_last_anchor_idx,
    anchor.replay_region_anchor_offset,
    anchor.replay_pattern_id,
    anchor.provider_replay_unit_id,
    anchor.replay_position_id,
    anchor.graph_launch_occurrence_id,
    anchor.replay_body_template_id,
    anchor.body_id,
    anchor.replay_position_member_order,
    anchor.replay_position_order,
    anchor.graph_provider,
    anchor.replay_position_match_policy,
    anchor.replay_position_association_policy,
    anchor.replay_position_evidence_level,
    CASE
      WHEN anchor.position_support_state = 'exact_position'
        THEN 'unexpanded_position_anchor'
      ELSE 'retained_mainline_anchor'
    END AS flattening_action
  FROM traceloom_v_annotated_anchor_timeline AS anchor
  WHERE NOT (
    anchor.replay_annotation_support_state = 'supported'
    AND anchor.replay_region_state = 'inside_exact_replay'
    AND anchor.position_support_state = 'exact_position'
    AND EXISTS (
      SELECT 1
      FROM traceloom_graph_body_member AS member
      WHERE member.launch_id = anchor.replay_position_id
        AND member.db_idx = anchor.db_idx
        AND member.device_id = anchor.device_id
    )
  )

  UNION ALL

  SELECT
    'member:' || member.member_id AS timeline_item_id,
    member.db_idx,
    member.device_id,
    member.anchor_idx AS mainline_anchor_idx,
    member.position_anchor_id AS mainline_anchor_id,
    member.position_anchor_id,
    'position_member' AS item_kind,
    member.event_id,
    member.symbol,
    member.role,
    member.label,
    member.family,
    member.source_table,
    member.source_key,
    member.stream_id,
    member.category,
    member.semantic_role,
    member.task_type,
    member.start_ns,
    member.end_ns,
    member.dur_us,
    member.member_kind,
    member.lane_ordinal,
    member.task_ordinal,
    member.raw_task_id,
    member.observed_position_member_order,
    'supported' AS replay_annotation_support_state,
    'ordered_disjoint_exact_replay_regions'
      AS replay_annotation_reason_code,
    'inside_exact_replay' AS replay_region_state,
    member.protected_interval_id,
    member.replay_occurrence_id,
    member.replay_region_start_ns,
    member.replay_region_end_ns,
    member.replay_region_first_anchor_idx,
    member.replay_region_last_anchor_idx,
    member.replay_region_anchor_offset,
    member.replay_pattern_id,
    member.provider_replay_unit_id,
    member.replay_position_id,
    member.graph_launch_occurrence_id,
    member.replay_body_template_id,
    member.body_id,
    member.replay_position_member_order,
    member.replay_position_order,
    member.graph_provider,
    member.replay_position_match_policy,
    member.replay_position_association_policy,
    member.replay_position_evidence_level,
    'expanded_exact_position_member' AS flattening_action
  FROM expandable_member AS member
)
SELECT
  flat_item.*,
  ROW_NUMBER() OVER (
    PARTITION BY db_idx, device_id
    ORDER BY start_ns, end_ns, mainline_anchor_idx, item_kind,
             timeline_item_id
  ) - 1 AS timeline_order,
  'one_retained_anchor_or_exact_position_member' AS row_grain,
  'exact_position_expansion_else_mainline_anchor' AS flattening_semantics,
  'region_highlight_not_event_ownership' AS replay_annotation_semantics,
  'timestamp_order_not_dependency' AS observation_semantics,
  'flat_items_may_overlap_do_not_sum_without_cost_lens'
    AS duration_aggregation_semantics
FROM flat_item;
)SQL",
        "failed to materialize replay-annotated timeline views");
  } else {
    sqlite_exec(
        db,
        "DROP VIEW IF EXISTS traceloom_v_flattened_execution_timeline;"
        "DROP VIEW IF EXISTS traceloom_v_annotated_anchor_timeline;"
        "DROP VIEW IF EXISTS traceloom_v_replay_region_annotation_status;",
        "failed to drop unavailable replay-annotated timeline views");
  }

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
