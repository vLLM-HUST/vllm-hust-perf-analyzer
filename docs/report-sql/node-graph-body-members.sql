-- Exact graph members for one concrete tree-node occurrence.
-- Replace the fallback target with a report-local node_id/occurrence_idx when
-- following a node from traceloom_v_tree_node or a Markdown projection.
with target_occurrence as (
  select db_idx, device_id, view_name, node_id, occurrence_idx
  from traceloom_v_node_graph_body_member
  where coverage_kind = 'self'
  order by db_idx, device_id, node_id, occurrence_idx
  limit 1
)
select
  g.node_id,
  g.occurrence_idx,
  g.node_anchor_id as anchor_id,
  g.launch_correlation_id as correlation_id,
  g.lane_ordinal,
  g.task_ordinal,
  g.kind,
  g.event_id,
  g.member_symbol,
  g.graph_node_id,
  g.original_graph_node_id,
  g.source_table,
  g.source_row_id,
  round(g.dur_us, 3) as dur_us
from traceloom_v_node_graph_body_member g
join target_occurrence t
  on t.db_idx = g.db_idx
 and t.device_id = g.device_id
 and t.view_name = g.view_name
 and t.node_id = g.node_id
 and t.occurrence_idx = g.occurrence_idx
where g.coverage_kind = 'self'
order by g.node_member_order, g.lane_ordinal, g.task_ordinal, g.member_id;
