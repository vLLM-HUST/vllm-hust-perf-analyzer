-- Drill one structure-conditioned device bubble into the public host API
-- windows observed between its adjacent anchors' correlated runtime endpoints.
-- Replace the ORDER BY target with a literal bubble_id when desired. Provider
-- internal calls remain available from traceloom_v_structure_bubble_runtime_call
-- by removing the api_layer predicate below.

WITH target AS (
  SELECT bubble_id
  FROM traceloom_v_structure_bubble_occurrence
  ORDER BY bubble_us DESC, bubble_id
  LIMIT 1
)
SELECT bubble.bubble_id,
       bubble.structural_position_id,
       bubble.right_occurrence_idx,
       bubble.left_anchor_symbol,
       bubble.right_anchor_symbol,
       round(bubble.bubble_us, 3) AS bubble_us,
       bubble.host_observation_status,
       bubble.host_start_ns,
       bubble.host_end_ns,
       round(bubble.host_interval_us, 3) AS host_interval_us,
       call.observed_order,
       call.api_name,
       call.api_family,
       call.runtime_start_ns,
       call.runtime_end_ns,
       round(call.runtime_dur_us, 3) AS runtime_dur_us,
       round(call.observed_overlap_us, 3) AS observed_overlap_us,
       call.interval_relation,
       call.thread_id,
       call.runtime_source_table,
       call.runtime_source_key,
       bubble.api_association_semantics
FROM traceloom_v_structure_bubble_occurrence AS bubble
LEFT JOIN traceloom_v_structure_bubble_runtime_call AS call
  ON call.bubble_id = bubble.bubble_id
 AND call.api_layer = 'public'
WHERE bubble.bubble_id = (SELECT bubble_id FROM target)
ORDER BY call.observed_order, call.runtime_start_ns, call.runtime_end_ns;
