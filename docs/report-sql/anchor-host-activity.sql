-- Profiler-visible host runtime behavior delimited by adjacent device anchors.
-- This does not assign an idle cause and does not describe unobserved CPU work.
-- Replace the selector below with `SELECT 'anchor-NNN' AS left_anchor_id` to
-- inspect an anchor reached through the tree/node traversal.

WITH target_anchor AS MATERIALIZED (
  SELECT left_anchor_id
  FROM traceloom_anchor_host_interval
  WHERE support_state = 'supported_ordered'
  ORDER BY db_idx, device_id, host_start_ns
  LIMIT 1
)
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
       round(observed_dur_us, 3) AS call_duration_us,
       observed_overlap_us AS interval_overlap_us,
       interval_relation,
       observed_source_table,
       observed_source_key
FROM traceloom_v_anchor_host_activity
WHERE left_anchor_id = (SELECT left_anchor_id FROM target_anchor)
ORDER BY db_idx,
         device_id,
         left_anchor_id,
         observed_start_ns,
         observed_runtime_call_id
LIMIT 200;

-- Audit intervals that could not be queried safely:
-- SELECT * FROM traceloom_v_anchor_host_interval
-- WHERE support_state != 'supported_ordered';
