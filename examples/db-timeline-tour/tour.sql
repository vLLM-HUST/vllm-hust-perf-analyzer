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
  selection_relation,
  selection_column
FROM traceloom_projection_parameter
WHERE projection_name = 'scope_occurrences'
ORDER BY parameter_order;

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
.print '6 / Cross-domain: project occurrence 1 into supported host windows'
SELECT
  h.coverage_kind,
  h.anchor_order,
  h.api_name,
  count(*) AS observed_calls,
  round(sum(h.observed_overlap_us), 3) AS scheduled_overlap_us
FROM traceloom_v_node_host_activity h
JOIN traceloom_tour_scope s USING (node_id)
WHERE h.occurrence_idx = 1
GROUP BY h.coverage_kind, h.anchor_order, h.api_name
ORDER BY scheduled_overlap_us DESC, h.anchor_order, h.api_name
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
.print 'One selected scope produced several projections:'
.print 'scope -> all occurrences -> comparable cost population'
.print 'scope -> occurrence 1 -> ordered members -> embedded raw row'
.print 'scope -> ordered children -> hierarchical view'
.print 'scope -> supported host windows -> runtime API context'
