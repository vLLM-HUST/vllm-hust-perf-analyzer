-- Unknown anchors in one recovered family/occurrence. Replace the parameter
-- subquery with a handle selected from traceloom_v_tree_node.
WITH parameter(node_id, occurrence_idx) AS (
  SELECT s.node_id, s.occurrence_idx
  FROM traceloom_v_evidence_role_structure s
  JOIN traceloom_evidence_role_decision d
    ON d.decision_id = s.decision_id
  WHERE d.final_role = 'unknown_anchor'
  ORDER BY s.node_id, s.occurrence_idx
  LIMIT 1
)
SELECT
  s.node_id,
  s.occurrence_idx,
  d.event_id,
  d.final_role,
  d.rule_id,
  d.support_state,
  d.reason_code,
  d.symbol,
  d.duration_ns AS retained_duration_ns,
  d.source_table,
  d.source_key
FROM parameter q
JOIN traceloom_v_evidence_role_structure s
  ON s.node_id = q.node_id AND s.occurrence_idx = q.occurrence_idx
JOIN traceloom_v_evidence_role_decision d
  ON d.decision_id = s.decision_id
WHERE d.final_role = 'unknown_anchor'
ORDER BY d.start_ns, d.event_id;
