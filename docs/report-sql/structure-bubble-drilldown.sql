-- Drill into one structure-conditioned device bubble.
-- Replace the ORDER BY target with a literal bubble_id when desired.

WITH target AS (
  SELECT bubble_id
  FROM traceloom_v_structure_bubble_occurrence
  ORDER BY bubble_us DESC, bubble_id
  LIMIT 1
)
SELECT bubble_id,
       structural_position_id,
       right_occurrence_idx,
       right_node_symbol,
       bubble_us,
       transition_compute_us,
       transition_comm_us,
       transition_total_us,
       host_observation_status,
       host_interval_us,
       api_association_semantics
FROM traceloom_v_structure_bubble_occurrence
WHERE bubble_id = (SELECT bubble_id FROM target);
