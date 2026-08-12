-- Profiler-visible host runtime distribution after each node-owned anchor.
-- placement_semantics is deliberately "after_anchor_interval": these calls
-- are context following a structural position, not CPU cost owned by the node.
-- scheduled_overlap_us sums clipped per-call intervals. Runtime calls may
-- overlap each other, so this is not an overlap-safe host busy union.
-- The default target is the highest-cost repeated atom. Replace the selector
-- below with `SELECT 'node-NNN' AS node_id` to inspect a chosen tree node.

WITH target_node AS MATERIALIZED (
  SELECT node_id
  FROM traceloom_v_tree_node
  WHERE node_type = 'Atom' AND occurrence_count > 1
  ORDER BY total_us DESC
  LIMIT 1
)
SELECT node_id,
       local_node_id,
       occurrence_idx,
       anchor_order,
       anchor_idx,
       right_anchor_idx,
       right_anchor_symbol,
       right_anchor_role,
       host_interval_us,
       placement_semantics,
       left_anchor_id,
       right_anchor_id,
       api_name,
       count(*) AS observed_call_count,
       round(sum(observed_overlap_us), 3) AS scheduled_overlap_us,
       scope_policy,
       support_state
FROM traceloom_v_node_host_activity
WHERE coverage_kind = 'self'
  AND node_id = (SELECT node_id FROM target_node)
GROUP BY node_id,
         local_node_id,
         occurrence_idx,
         anchor_order,
         anchor_idx,
         right_anchor_idx,
         right_anchor_symbol,
         right_anchor_role,
         host_interval_us,
         placement_semantics,
         left_anchor_id,
         right_anchor_id,
         api_name,
         scope_policy,
         support_state
ORDER BY node_id,
         occurrence_idx,
         anchor_order,
         scheduled_overlap_us DESC,
         api_name
LIMIT 200;
