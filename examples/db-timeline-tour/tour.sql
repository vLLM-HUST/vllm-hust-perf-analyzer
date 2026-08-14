.headers on
.mode box
.nullvalue NULL

.print ''
.print '0 / Projection model: one scope, composable analytical views'
SELECT
  projection_name,
  population_mode,
  resolution,
  observation_domain,
  measure_lens
FROM traceloom_projection_recipe
ORDER BY display_order;

.print ''
.print 'Typed selectors for the occurrence projection'
SELECT
  parameter_name,
  sqlite_type,
  is_nullable,
  coordinate_kind,
  selection_relation,
  selection_column
FROM traceloom_projection_parameter
WHERE projection_name = 'scope_occurrences'
ORDER BY parameter_order;

.print ''
.print 'Reusable coordinates returned by the member projection'
SELECT
  result_column,
  coordinate_kind,
  purpose
FROM traceloom_projection_coordinate
WHERE projection_name = 'scope_members'
ORDER BY coordinate_order;

.print ''
.print 'Compatible next queries after selecting an aligned position'
SELECT
  source_column,
  target_projection,
  target_parameter
FROM traceloom_v_projection_continuation
WHERE source_projection = 'position_population'
ORDER BY target_projection, source_column;

DROP TABLE IF EXISTS temp.traceloom_tour_scope;
CREATE TEMP TABLE traceloom_tour_scope AS
SELECT node_id, local_node_id, label
FROM traceloom_v_tree_node
WHERE view_name = 'native_report_tree'
  AND repeat_count = 24
  AND occurrence_count > 1
ORDER BY occurrence_count DESC, display_order
LIMIT 1;

.print ''
.print '1 / Scope: select one high-level repeated region once'
SELECT
  n.node_id,
  n.local_node_id AS node,
  n.label,
  n.path,
  n.occurrence_count AS occurrences,
  round(n.avg_total_us / 1000.0, 3) AS average_ms,
  round(n.total_us / 1000.0, 3) AS total_ms
FROM traceloom_v_tree_node n
JOIN traceloom_tour_scope s USING (node_id);

.print ''
.print '2 / Hierarchy: keep the scope folded and read its ordered children'
SELECT
  child.local_node_id AS node,
  child.label,
  child.node_type,
  child.repeat_count,
  child.occurrence_count AS occurrences,
  round(child.avg_total_us, 3) AS average_us,
  round(child.total_us / 1000.0, 3) AS total_ms
FROM traceloom_v_node_children child
JOIN traceloom_tour_scope s ON s.node_id = child.parent_node_id
ORDER BY child.edge_order;

.print ''
.print '3 / Population: compare all occurrences of the same selected scope'
SELECT
  o.occurrence_idx AS occurrence,
  o.repeat_context,
  o.anchor_count,
  round(o.total_us / 1000.0, 3) AS total_ms,
  round(o.compute_us / 1000.0, 3) AS compute_ms,
  round(o.aux_us / 1000.0, 3) AS auxiliary_ms
FROM traceloom_tree_node_occurrence o
JOIN traceloom_tour_scope s USING (node_id)
ORDER BY o.total_us DESC
LIMIT 10;

.print ''
.print '4 / Resolution: expand occurrence 1 to its ordered device members'
SELECT
  na.occurrence_idx AS occurrence,
  na.anchor_order,
  na.coverage_kind,
  a.symbol,
  e.event_id,
  e.stream_id,
  round(e.dur_us, 3) AS duration_us
FROM traceloom_tree_node_anchor na
JOIN traceloom_tour_scope s USING (node_id)
JOIN traceloom_anchor a USING (anchor_id)
LEFT JOIN traceloom_event e USING (event_id)
WHERE na.occurrence_idx = 1
ORDER BY na.anchor_order
LIMIT 20;

.print ''
.print '5 / Evidence: follow one member to its embedded profiler row'
WITH selected_event AS (
  SELECT
    na.local_node_id,
    na.occurrence_idx,
    na.anchor_order,
    a.symbol,
    e.event_id,
    e.stream_id,
    e.dur_us
  FROM traceloom_tree_node_anchor na
  JOIN traceloom_tour_scope t USING (node_id)
  JOIN traceloom_anchor a USING (anchor_id)
  JOIN traceloom_event e USING (event_id)
  WHERE na.occurrence_idx = 1
  ORDER BY e.dur_us DESC, na.anchor_order
  LIMIT 1
)
SELECT
  s.local_node_id AS node,
  s.occurrence_idx AS occurrence,
  s.anchor_order,
  s.symbol,
  s.event_id,
  s.stream_id,
  round(s.dur_us, 3) AS duration_us,
  l.source_table,
  l.source_key AS source_row,
  l.resolution_status
FROM selected_event s
JOIN traceloom_v_event_source_locator l USING (event_id);

.print ''
.print '6 / Cross-domain: retain every typed host window, then summarize calls'
SELECT
  i.coverage_kind,
  i.anchor_order,
  i.interval_id,
  i.support_state,
  c.api_name,
  count(a.runtime_call_id) AS observed_calls,
  coalesce(round(sum((min(c.end_ns, i.host_end_ns) -
                      max(c.start_ns, i.host_start_ns)) / 1000.0), 3), 0.0)
    AS scheduled_overlap_us
FROM traceloom_v_node_host_interval i
JOIN traceloom_tour_scope s USING (node_id)
LEFT JOIN traceloom_anchor_host_activity a USING (interval_id)
LEFT JOIN traceloom_runtime_call c USING (runtime_call_id)
WHERE i.occurrence_idx = 1
GROUP BY i.coverage_kind, i.anchor_order, i.interval_id, i.support_state,
         c.api_name
ORDER BY scheduled_overlap_us DESC, i.anchor_order, c.api_name
LIMIT 10;

.print ''
.print '7 / Raw evidence: query the selected exact embedded profiler row'
WITH selected_event AS (
  SELECT e.event_id
  FROM traceloom_tree_node_anchor na
  JOIN traceloom_tour_scope t USING (node_id)
  JOIN traceloom_anchor a USING (anchor_id)
  JOIN traceloom_event e USING (event_id)
  WHERE na.occurrence_idx = 1
  ORDER BY e.dur_us DESC, na.anchor_order
  LIMIT 1
), selected_source AS (
  SELECT source_key
  FROM traceloom_v_event_source_locator
  WHERE event_id = (SELECT event_id FROM selected_event)
    AND source_table = 'COMMUNICATION_OP'
)
SELECT
  rowid AS source_row,
  startNs,
  endNs,
  connectionId,
  count,
  opType,
  deviceId
FROM COMMUNICATION_OP
WHERE rowid = CAST((SELECT source_key FROM selected_source) AS INTEGER);

.print ''
.print '8 / Branch: rank recurrent device-bubble positions without hiding support states'
DROP TABLE IF EXISTS temp.traceloom_tour_bubble_scope;
CREATE TEMP TABLE traceloom_tour_bubble_scope AS
SELECT p.structural_position_id
FROM traceloom_v_structure_bubble_position p
WHERE EXISTS (
  SELECT 1
  FROM traceloom_v_structure_bubble_occurrence b
  JOIN traceloom_anchor_host_activity h
    ON h.interval_id = b.host_interval_id
  WHERE b.structural_position_id = p.structural_position_id
)
ORDER BY p.total_bubble_us DESC, p.bubble_occurrence_count DESC
LIMIT 1;

SELECT
  p.structural_position_id,
  p.right_node_path,
  p.bubble_occurrence_count AS bubbles,
  p.supported_host_occurrence_count AS supported_windows,
  p.missing_endpoint_occurrence_count AS missing_endpoints,
  p.nonmonotonic_occurrence_count AS nonmonotonic,
  round(p.host_observation_coverage, 3) AS host_coverage,
  round(p.total_bubble_us / 1000.0, 3) AS total_bubble_ms
FROM traceloom_v_structure_bubble_position p
JOIN traceloom_tour_bubble_scope s USING (structural_position_id);

.print ''
.print '9 / Decision: select one bubble occurrence from that returned population'
DROP TABLE IF EXISTS temp.traceloom_tour_bubble_occurrence;
CREATE TEMP TABLE traceloom_tour_bubble_occurrence AS
SELECT b.structural_position_id, b.bubble_id, b.host_interval_id, b.bubble_us
FROM traceloom_v_structure_bubble_occurrence b
JOIN traceloom_tour_bubble_scope s USING (structural_position_id)
WHERE b.host_observation_status = 'supported_ordered'
  AND EXISTS (
    SELECT 1 FROM traceloom_anchor_host_activity h
    WHERE h.interval_id = b.host_interval_id
  )
ORDER BY b.bubble_us DESC, b.right_occurrence_idx
LIMIT 1;

SELECT * FROM traceloom_tour_bubble_occurrence;

.print ''
.print '10 / Context: expand its returned host_interval_id to literal calls'
SELECT
  i.interval_id,
  i.support_state,
  c.runtime_call_id,
  c.api_name,
  CASE WHEN c.start_ns >= i.host_start_ns AND c.end_ns <= i.host_end_ns
       THEN 'contained' ELSE 'boundary_overlap' END AS interval_relation,
  round((min(c.end_ns, i.host_end_ns) -
         max(c.start_ns, i.host_start_ns)) / 1000.0, 3) AS overlap_us
FROM traceloom_v_anchor_host_interval i
LEFT JOIN traceloom_anchor_host_activity a USING (interval_id)
LEFT JOIN traceloom_runtime_call c USING (runtime_call_id)
WHERE i.interval_id = (
  SELECT host_interval_id FROM traceloom_tour_bubble_occurrence
)
ORDER BY a.observed_order
LIMIT 10;

.print ''
.print '11 / Audit: continue from runtime_call_id to embedded host evidence'
WITH selected_call AS (
  SELECT a.runtime_call_id
  FROM traceloom_anchor_host_activity a
  WHERE a.interval_id = (
    SELECT host_interval_id FROM traceloom_tour_bubble_occurrence
  )
  ORDER BY a.observed_order
  LIMIT 1
)
SELECT
  l.runtime_call_id,
  l.api_name,
  l.source_table,
  l.source_key AS source_row,
  l.resolution_status
FROM selected_call c
JOIN traceloom_v_runtime_call_source_locator l USING (runtime_call_id);

.print ''
.print 'One selected scope produced several projections:'
.print 'scope -> all occurrences -> comparable cost population'
.print 'scope -> occurrence 1 -> ordered members -> embedded raw row'
.print 'scope -> ordered children -> hierarchical view'
.print 'scope -> typed host windows -> runtime API context'
.print 'bubble position -> occurrence -> host interval -> runtime call -> raw row'
