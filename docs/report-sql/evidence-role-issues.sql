SELECT
  i.issue_id,
  i.code,
  i.support_state,
  d.event_id,
  d.final_role,
  d.policy_id,
  d.rule_id,
  d.reason_code,
  d.missing_required_fields,
  d.missing_capability_rule_ids,
  i.related_ids,
  d.source_table,
  d.source_key
FROM traceloom_evidence_role_issue i
JOIN traceloom_evidence_role_decision d
  ON d.decision_id = i.decision_id
ORDER BY i.code, d.event_id, i.issue_id;
