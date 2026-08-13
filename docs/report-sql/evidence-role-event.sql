-- Forward canonical query. Replace the parameter subquery with an event_id
-- selected from traceloom_event or traceloom_v_event_source_locator.
WITH parameter(event_id) AS (
  SELECT event_id
  FROM traceloom_evidence_role_decision
  ORDER BY db_idx, device_id, start_ns, event_id
  LIMIT 1
)
SELECT
  d.event_id,
  d.source_path,
  d.source_table,
  d.source_key,
  d.input_provider_scope AS provider,
  d.policy_id,
  d.rule_id,
  d.rule_class,
  d.final_role,
  d.effective_structural_participation AS structural_participation,
  d.support_state,
  d.reason_code,
  d.available_fields,
  d.required_fields,
  d.missing_required_fields,
  d.missing_capability_rule_ids,
  d.cost_treatment,
  d.context_treatment,
  d.provenance_treatment,
  d.duration_ns AS retained_duration_ns,
  p.placement_kind,
  p.placement_id,
  p.owner_id,
  s.node_id,
  s.occurrence_idx
FROM traceloom_v_evidence_role_decision d
JOIN parameter q ON q.event_id = d.event_id
LEFT JOIN traceloom_evidence_role_placement p
  ON p.decision_id = d.decision_id
LEFT JOIN traceloom_v_evidence_role_structure s
  ON s.decision_id = d.decision_id
 AND s.placement_kind = p.placement_kind
 AND s.placement_id = p.placement_id
ORDER BY p.placement_order, s.node_id, s.occurrence_idx;
