-- Reverse exact navigation: one normalized internal event back to every
-- concrete tree-node occurrence whose exact graph body contains it.
-- Replace the fallback target_event with an event_id selected from evidence.
with target_event as (
  select db_idx, device_id, event_id
  from traceloom_graph_body_member
  order by start_ns, member_id
  limit 1
)
select
  g.event_id,
  g.member_symbol,
  g.graph_node_id,
  g.original_graph_node_id,
  g.node_id,
  g.occurrence_idx,
  g.node_anchor_id as anchor_id,
  g.node_member_order,
  g.node_slot_order,
  g.launch_correlation_id as correlation_id,
  g.source_table,
  g.source_row_id
from traceloom_v_node_graph_body_member g
join target_event t
  on t.db_idx = g.db_idx
 and t.device_id = g.device_id
 and t.event_id = g.event_id
where g.coverage_kind = 'self'
order by g.db_idx, g.device_id, g.node_id, g.occurrence_idx;
