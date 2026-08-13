SELECT
  input_provider_scope AS provider,
  policy_id,
  policy_version,
  final_role,
  support_state,
  event_count,
  retained_duration_ns,
  identity_duration_ns,
  non_identity_duration_ns
FROM traceloom_v_evidence_role_cost_coverage
ORDER BY provider, policy_id, final_role, support_state;
