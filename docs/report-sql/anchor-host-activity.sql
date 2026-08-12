-- Profiler-visible host runtime behavior delimited by adjacent device anchors.
-- This does not assign an idle cause and does not describe unobserved CPU work.

SELECT left_anchor_id,
       right_anchor_id,
       support_state,
       scope_policy,
       host_start_ns,
       host_end_ns,
       observed_runtime_call_id,
       api_name,
       api_type,
       observed_start_ns,
       observed_end_ns,
       round(observed_dur_us, 3) AS runtime_us,
       observed_source_table,
       observed_source_key
FROM traceloom_v_anchor_host_activity
ORDER BY db_idx,
         device_id,
         left_anchor_id,
         observed_start_ns,
         observed_runtime_call_id
LIMIT 200;

-- Audit intervals that could not be queried safely:
-- SELECT * FROM traceloom_v_anchor_host_interval
-- WHERE support_state != 'supported_ordered';
