-- Reverse canonical query. Replace the parameter CTE with one anchor,
-- auxiliary link, graph body member, replay unit, or protected interval ID.
WITH parameter(placement_kind, placement_id) AS (
  SELECT placement_kind, placement_id
  FROM traceloom_evidence_role_placement
  WHERE placement_kind <> 'normalized_event'
  ORDER BY placement_kind, placement_id
  LIMIT 1
)
SELECT
  p.placement_kind,
  p.placement_id,
  p.owner_id,
  p.event_id,
  p.final_role,
  p.policy_id,
  p.rule_id,
  p.decision_support_state AS support_state,
  p.decision_reason_code AS reason_code,
  p.duration_ns AS retained_duration_ns,
  p.source_table,
  p.source_key
FROM traceloom_v_evidence_role_placement p
JOIN parameter q
  ON q.placement_kind = p.placement_kind
 AND q.placement_id = p.placement_id
ORDER BY p.event_id, p.placement_order;
