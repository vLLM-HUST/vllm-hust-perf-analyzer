-- Fine-grained cost and raw-evidence locators for one concrete tree-node
-- occurrence. Replace the fallback target with a node handle from Loop Tree.
with target as (
  select db_idx, device_id, view_name, node_id, occurrence_idx
  from traceloom_v_node_replay_cost_member
  where coverage_kind = 'self'
  order by db_idx, device_id, node_id, occurrence_idx
  limit 1
)
select
  c.node_id,
  c.occurrence_idx,
  c.node_anchor_id as anchor_id,
  c.launch_id,
  c.member_id,
  c.slot_role,
  c.slot_order,
  c.stream_id,
  c.lane_ordinal,
  c.task_ordinal,
  c.identity,
  c.kind,
  c.duration_ns,
  c.scheduled_work_share_ppm,
  c.event_id,
  c.source_table,
  c.source_row_id
from traceloom_v_node_replay_cost_member c
join target t using (db_idx, device_id, view_name, node_id, occurrence_idx)
where c.coverage_kind = 'self'
order by c.node_member_order, c.lane_ordinal, c.task_ordinal, c.member_id;
