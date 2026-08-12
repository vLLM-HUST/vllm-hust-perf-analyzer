.headers on
.mode box
.nullvalue NULL

.print ''
.print '1 / Structure: find a repeated inference region'
SELECT
  local_node_id AS node,
  label,
  path,
  occurrence_count AS occurrences,
  round(avg_total_us / 1000.0, 3) AS average_ms,
  round(total_us / 1000.0, 3) AS total_ms
FROM traceloom_v_tree_node
WHERE view_name = 'native_report_tree'
  AND repeat_count IN (24, 29)
ORDER BY depth, display_order;

.print ''
.print '2 / Horizontal: read the ordered cost structure inside Rep x24'
WITH target AS (
  SELECT node_id
  FROM traceloom_v_tree_node
  WHERE view_name = 'native_report_tree'
    AND repeat_count = 24
    AND occurrence_count > 1
  ORDER BY occurrence_count DESC, display_order
  LIMIT 1
)
SELECT
  local_node_id AS node,
  label,
  occurrence_count AS occurrences,
  round(avg_total_us, 3) AS average_us,
  round(total_us / 1000.0, 3) AS total_ms
FROM traceloom_v_tree_node
WHERE parent_node_id = (SELECT node_id FROM target)
ORDER BY display_order;

.print ''
.print '3 / Vertical: compare the same Rep x24 structure across occurrences'
WITH target AS (
  SELECT local_node_id
  FROM traceloom_v_tree_node
  WHERE view_name = 'native_report_tree'
    AND repeat_count = 24
    AND occurrence_count > 1
  ORDER BY occurrence_count DESC, display_order
  LIMIT 1
)
SELECT
  occurrence_idx AS occurrence,
  repeat_context,
  anchor_count,
  round(total_us / 1000.0, 3) AS total_ms,
  round(compute_us / 1000.0, 3) AS compute_ms,
  round(aux_us / 1000.0, 3) AS auxiliary_ms
FROM traceloom_tree_node_occurrence
WHERE local_node_id = (SELECT local_node_id FROM target)
ORDER BY total_us DESC
LIMIT 10;

.print ''
.print '4 / Evidence: follow one structural occurrence to profiler-visible rows'
WITH target AS (
  SELECT local_node_id
  FROM traceloom_v_tree_node
  WHERE view_name = 'native_report_tree'
    AND repeat_count = 24
    AND occurrence_count > 1
  ORDER BY occurrence_count DESC, display_order
  LIMIT 1
), selected_event AS (
  SELECT
    na.local_node_id,
    na.occurrence_idx,
    na.anchor_order,
    a.symbol,
    e.event_id,
    e.stream_id,
    e.dur_us
  FROM traceloom_tree_node_anchor na
  JOIN traceloom_anchor a USING (anchor_id)
  JOIN traceloom_event e USING (event_id)
  WHERE na.local_node_id = (SELECT local_node_id FROM target)
    AND na.occurrence_idx = 1
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
.print '5 / Raw evidence: query that exact embedded profiler row'
WITH target AS (
  SELECT local_node_id
  FROM traceloom_v_tree_node
  WHERE view_name = 'native_report_tree'
    AND repeat_count = 24
    AND occurrence_count > 1
  ORDER BY occurrence_count DESC, display_order
  LIMIT 1
), selected_event AS (
  SELECT e.event_id
  FROM traceloom_tree_node_anchor na
  JOIN traceloom_anchor a USING (anchor_id)
  JOIN traceloom_event e USING (event_id)
  WHERE na.local_node_id = (SELECT local_node_id FROM target)
    AND na.occurrence_idx = 1
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
.print 'The path is now queryable in both directions:'
.print 'structure -> occurrence -> event -> embedded raw row'
.print 'structure -> all equivalent occurrences -> comparable cost distribution'
