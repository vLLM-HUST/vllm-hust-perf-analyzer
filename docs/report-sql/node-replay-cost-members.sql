-- Fine-grained cost and raw-evidence locators for one concrete tree-node
-- occurrence. Replace the fallback target with a node handle from Loop Tree.
with target as (
  select distinct db_idx, device_id, view_name, node_id, occurrence_idx
  from traceloom_v_node_replay_cost_member
  where coverage_kind = 'self'
  order by db_idx, device_id, node_id, occurrence_idx
  limit 1
), launches as (
  select db_idx, device_id, view_name, node_id, occurrence_idx
       , node_anchor_id, node_member_order, launch_id
  from traceloom_v_node_replay_cost_member c
  join target t using (db_idx, device_id, view_name, node_id, occurrence_idx)
  where c.coverage_kind = 'self'
  group by db_idx, device_id, view_name, node_id, occurrence_idx,
           node_anchor_id, node_member_order, launch_id
)
select
  l.node_id,
  l.occurrence_idx,
  p.position_anchor_id as anchor_id,
  p.launch_id,
  p.position_order,
  p.member_id,
  p.observed_order,
  p.observed_relation_to_previous,
  p.stream_id,
  p.lane_ordinal,
  p.task_ordinal,
  p.identity,
  p.kind,
  p.policy_role,
  p.final_role,
  p.duration_ns,
  p.scheduled_work_share_ppm,
  p.event_id,
  p.source_table,
  p.source_row_id
from launches l
join traceloom_v_replay_position_realization_member p
  using (db_idx, device_id, launch_id)
order by l.node_member_order, p.observed_order, p.member_id;
