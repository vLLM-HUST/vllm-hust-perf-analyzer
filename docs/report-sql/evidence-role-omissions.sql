-- Positively omitted observations owned by one between-anchor region. Replace
-- parameter subquery with a following anchor selected from
-- traceloom_anchor_aux_slot.
WITH parameter(anchor_id) AS (
  SELECT owner_id
  FROM traceloom_evidence_role_placement
  WHERE placement_kind = 'auxiliary_link'
  ORDER BY owner_id
  LIMIT 1
)
SELECT
  p.owner_id AS following_anchor_id,
  d.event_id,
  d.final_role,
  d.rule_id,
  d.support_state,
  d.reason_code,
  d.duration_ns AS retained_duration_ns,
  d.source_table,
  d.source_key
FROM parameter q
JOIN traceloom_v_evidence_role_placement p
  ON p.owner_id = q.anchor_id
 AND p.placement_kind = 'auxiliary_link'
JOIN traceloom_v_evidence_role_decision d
  ON d.decision_id = p.decision_id
WHERE d.final_role IN ('auxiliary', 'transparent')
ORDER BY d.start_ns, d.event_id;
