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

DROP TABLE IF EXISTS temp.traceloom_tour_outlier;
CREATE TEMP TABLE traceloom_tour_outlier AS
SELECT o.node_id, o.occurrence_idx, o.anchor_count, o.total_us,
       o.compute_us, o.comm_us, o.idle_us, o.aux_us
FROM traceloom_tree_node_occurrence o
JOIN traceloom_tour_scope s USING (node_id)
ORDER BY o.total_us DESC, o.occurrence_idx
LIMIT 1;

DROP TABLE IF EXISTS temp.traceloom_tour_median;
CREATE TEMP TABLE traceloom_tour_median AS
WITH ranked AS (
  SELECT
    o.*,
    row_number() OVER (ORDER BY o.total_us, o.occurrence_idx) AS cost_rank,
    count(*) OVER () AS population_count
  FROM traceloom_tree_node_occurrence o
  JOIN traceloom_tour_scope s USING (node_id)
)
SELECT node_id, occurrence_idx, anchor_count, total_us, compute_us, comm_us,
       idle_us, aux_us
FROM ranked
WHERE cost_rank = (population_count + 1) / 2;

.print ''
.print '4 / Decision: select the slowest occurrence returned by the population'
SELECT
  'median-by-total' AS selection,
  occurrence_idx AS occurrence,
  anchor_count AS anchors,
  round(total_us / 1000.0, 3) AS total_ms,
  round(compute_us / 1000.0, 3) AS compute_ms,
  round(comm_us / 1000.0, 3) AS communication_ms,
  round(idle_us / 1000.0, 3) AS uncovered_ms
FROM traceloom_tour_median
UNION ALL
SELECT
  'selected-slowest', occurrence_idx, anchor_count,
  round(total_us / 1000.0, 3), round(compute_us / 1000.0, 3),
  round(comm_us / 1000.0, 3), round(idle_us / 1000.0, 3)
FROM traceloom_tour_outlier;

DROP TABLE IF EXISTS temp.traceloom_tour_outlier_position;
CREATE TEMP TABLE traceloom_tour_outlier_position AS
WITH ranked_positions AS (
  SELECT
    na.node_id,
    na.db_idx,
    na.device_id,
    na.occurrence_idx,
    na.anchor_order,
    na.coverage_kind,
    na.anchor_id,
    na.total_us,
    row_number() OVER (
      PARTITION BY na.node_id, na.anchor_order, na.coverage_kind
      ORDER BY na.total_us, na.occurrence_idx
    ) AS cost_rank,
    count(*) OVER (
      PARTITION BY na.node_id, na.anchor_order, na.coverage_kind
    ) AS population_count
  FROM traceloom_tree_node_anchor na
  JOIN traceloom_tour_scope s USING (node_id)
), position_medians AS (
  SELECT node_id, anchor_order, coverage_kind,
         total_us AS median_position_us
  FROM ranked_positions
  WHERE cost_rank = (population_count + 1) / 2
)
SELECT
  selected.node_id,
  selected.occurrence_idx,
  selected.anchor_order,
  selected.coverage_kind,
  selected.anchor_id,
  a.symbol,
  e.event_id,
  selected.total_us,
  median.median_position_us,
  selected.total_us - median.median_position_us AS excess_us
FROM ranked_positions selected
JOIN traceloom_tour_outlier outlier
  ON outlier.node_id = selected.node_id
 AND outlier.occurrence_idx = selected.occurrence_idx
JOIN position_medians median
  ON median.node_id = selected.node_id
 AND median.anchor_order = selected.anchor_order
 AND median.coverage_kind = selected.coverage_kind
JOIN traceloom_anchor a
  ON a.anchor_id = selected.anchor_id
 AND a.db_idx = selected.db_idx
 AND a.device_id = selected.device_id
LEFT JOIN traceloom_event e
  ON e.event_id = a.event_id
 AND e.db_idx = a.db_idx
 AND e.device_id = a.device_id;

.print ''
.print '5 / Resolution: align positions and rank the selected occurrence excess'
SELECT
  occurrence_idx AS occurrence,
  anchor_order,
  coverage_kind,
  symbol,
  event_id,
  round(total_us, 3) AS selected_us,
  round(median_position_us, 3) AS population_median_us,
  round(excess_us, 3) AS excess_us
FROM traceloom_tour_outlier_position
ORDER BY excess_us DESC, anchor_order
LIMIT 12;

.print ''
.print '6 / Evidence: follow the largest excess position to embedded profiler evidence'
WITH selected_event AS (
  SELECT *
  FROM traceloom_tour_outlier_position
  ORDER BY excess_us DESC, anchor_order
  LIMIT 1
)
SELECT
  s.node_id AS node,
  s.occurrence_idx AS occurrence,
  s.anchor_order,
  s.symbol,
  s.event_id,
  round(s.excess_us, 3) AS excess_us,
  l.source_table,
  l.source_key AS source_row,
  l.resolution_status
FROM selected_event s
JOIN traceloom_v_event_source_locator l USING (event_id);

.print ''
.print '7 / Cross-domain: retain the selected occurrence typed host windows'
SELECT
  i.coverage_kind,
  i.anchor_order,
  i.interval_id,
  i.support_state,
  a.api_name,
  count(a.observed_runtime_call_id) AS observed_calls,
  coalesce(round(sum(a.observed_overlap_us), 3), 0.0) AS scheduled_overlap_us
FROM traceloom_v_node_host_interval i
JOIN traceloom_tour_scope s USING (node_id)
LEFT JOIN traceloom_v_anchor_host_activity a USING (interval_id)
WHERE i.occurrence_idx = (
  SELECT occurrence_idx FROM traceloom_tour_outlier
)
GROUP BY i.coverage_kind, i.anchor_order, i.interval_id, i.support_state,
         a.api_name
ORDER BY scheduled_overlap_us DESC, i.anchor_order, a.api_name
LIMIT 10;

.print ''
.print '8 / Raw evidence: query the selected exact embedded profiler row'
WITH selected_event AS (
  SELECT event_id
  FROM traceloom_tour_outlier_position
  ORDER BY excess_us DESC, anchor_order
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
.print '9 / Branch: rank recurrent device-bubble positions without hiding support states'
DROP TABLE IF EXISTS temp.traceloom_tour_bubble_scope;
CREATE TEMP TABLE traceloom_tour_bubble_scope AS
SELECT p.structural_position_id
FROM traceloom_v_structure_bubble_position p
WHERE EXISTS (
  SELECT 1
  FROM traceloom_v_structure_bubble_occurrence b
  JOIN traceloom_v_anchor_host_activity h
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
.print '10 / Decision: select one bubble occurrence from that returned population'
DROP TABLE IF EXISTS temp.traceloom_tour_bubble_occurrence;
CREATE TEMP TABLE traceloom_tour_bubble_occurrence AS
SELECT b.structural_position_id, b.bubble_id, b.host_interval_id, b.bubble_us
FROM traceloom_v_structure_bubble_occurrence b
JOIN traceloom_tour_bubble_scope s USING (structural_position_id)
WHERE b.host_observation_status = 'supported_ordered'
  AND EXISTS (
    SELECT 1 FROM traceloom_v_anchor_host_activity h
    WHERE h.interval_id = b.host_interval_id
  )
ORDER BY b.bubble_us DESC, b.right_occurrence_idx
LIMIT 1;

SELECT * FROM traceloom_tour_bubble_occurrence;

.print ''
.print '11 / Context: expand its returned host_interval_id to literal calls'
SELECT
  i.interval_id,
  i.support_state,
  a.observed_runtime_call_id AS runtime_call_id,
  a.api_name,
  a.interval_relation,
  a.observed_overlap_us AS overlap_us
FROM traceloom_v_anchor_host_interval i
LEFT JOIN traceloom_v_anchor_host_activity a USING (interval_id)
WHERE i.interval_id = (
  SELECT host_interval_id FROM traceloom_tour_bubble_occurrence
)
ORDER BY a.observed_order
LIMIT 10;

.print ''
.print '12 / Audit: continue from runtime_call_id to embedded host evidence'
WITH selected_call AS (
  SELECT a.observed_runtime_call_id AS runtime_call_id
  FROM traceloom_v_anchor_host_activity a
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
.print 'scope -> all occurrences -> selected outlier -> aligned positions -> raw row'
.print 'scope -> ordered children -> hierarchical view'
.print 'scope -> selected occurrence -> typed host windows -> runtime API context'
.print 'bubble position -> occurrence -> host interval -> runtime call -> raw row'
