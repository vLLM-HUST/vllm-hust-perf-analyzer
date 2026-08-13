-- Drill one structure-conditioned device bubble into the public host API
-- windows observed between its adjacent anchors' correlated runtime endpoints.
-- Replace the ORDER BY target with a literal bubble_id when desired. Provider
-- internal calls remain available from traceloom_v_structure_bubble_runtime_call
-- by removing the api_layer predicate below.

WITH target AS MATERIALIZED (
  SELECT bubble_id
  FROM traceloom_structure_bubble_occurrence
  ORDER BY bubble_us DESC, bubble_id
  LIMIT 1
),
bubble AS MATERIALIZED (
  SELECT occurrence.*
  FROM traceloom_structure_bubble_occurrence AS occurrence
  JOIN target USING (bubble_id)
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
       activity.observed_order,
       call.api_name,
       CASE
         WHEN lower(coalesce(call.api_name, '')) LIKE '%wait%' THEN 'wait'
         WHEN lower(coalesce(call.api_name, '')) LIKE '%synchronize%'
           THEN 'synchronize'
         WHEN lower(coalesce(call.api_name, '')) LIKE '%query%' THEN 'query'
         WHEN lower(coalesce(call.api_name, '')) LIKE '%eventrecord%'
           OR lower(coalesce(call.api_name, '')) LIKE '%recordevent%'
           THEN 'event_record'
         WHEN lower(coalesce(call.api_name, '')) LIKE '%eventcreate%'
           OR lower(coalesce(call.api_name, '')) LIKE '%createevent%'
           OR lower(coalesce(call.api_name, '')) LIKE '%eventdestroy%'
           OR lower(coalesce(call.api_name, '')) LIKE '%destroyevent%'
           THEN 'event_lifecycle'
         WHEN lower(coalesce(call.api_name, '')) LIKE '%graphlaunch%'
           OR lower(coalesce(call.api_name, '')) LIKE '%aclmdlriexecuteasync%'
           THEN 'graph_launch'
         WHEN lower(coalesce(call.api_name, '')) LIKE '%launch%' THEN 'launch'
         WHEN lower(coalesce(call.api_name, '')) LIKE '%memcpy%'
           OR lower(coalesce(call.api_name, '')) LIKE '%memset%'
           OR lower(coalesce(call.api_name, '')) LIKE '%inplacecopy%'
           THEN 'memory'
         WHEN lower(coalesce(call.api_name, '')) LIKE '%capture%'
           OR lower(coalesce(call.api_name, '')) LIKE '%graph%'
           THEN 'graph_control'
         ELSE 'other'
       END AS api_family,
       call.start_ns AS runtime_start_ns,
       call.end_ns AS runtime_end_ns,
       round(call.dur_us, 3) AS runtime_dur_us,
       round((min(call.end_ns, bubble.host_end_ns)
              - max(call.start_ns, bubble.host_start_ns)) / 1000.0, 3)
         AS observed_overlap_us,
       CASE
         WHEN call.start_ns >= bubble.host_start_ns
          AND call.end_ns <= bubble.host_end_ns THEN 'contained'
         ELSE 'boundary_overlap'
       END AS interval_relation,
       call.thread_id,
       call.source_table AS runtime_source_table,
       call.source_key AS runtime_source_key,
       bubble.api_association_semantics
FROM bubble
LEFT JOIN traceloom_anchor_host_activity AS activity
  ON activity.interval_id = bubble.host_interval_id
LEFT JOIN traceloom_runtime_call AS call
  ON call.runtime_call_id = activity.runtime_call_id
 AND (lower(coalesce(call.api_name, '')) GLOB 'acl*'
   OR lower(coalesce(call.api_name, '')) GLOB 'cuda*'
   OR lower(coalesce(call.api_name, '')) GLOB 'hip*')
ORDER BY activity.observed_order, call.start_ns, call.end_ns;
