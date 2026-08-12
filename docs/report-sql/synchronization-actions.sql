-- Profiler-visible synchronization actions with their runtime endpoints.
-- This is action-level evidence, not record/wait pairing or idle causality.

SELECT sync_action_id,
       provider,
       sync_kind,
       api_name,
       runtime_dur_us,
       device_start_ns,
       device_dur_us,
       match_policy,
       evidence_level,
       support_state,
       cardinality,
       runtime_source_table,
       runtime_source_key,
       device_source_table,
       device_source_key
FROM traceloom_v_sync_runtime_call
ORDER BY db_idx, device_start_ns, sync_action_id, runtime_start_ns
LIMIT 500;

-- Keep open, ambiguous, and rejected outcomes during an evidence audit:
-- SELECT sync_kind, support_state, cardinality, count(*) AS relation_rows
-- FROM traceloom_v_sync_runtime_call
-- GROUP BY sync_kind, support_state, cardinality
-- ORDER BY sync_kind, support_state, cardinality;
