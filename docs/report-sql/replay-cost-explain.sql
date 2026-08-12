-- Explain one aggregate as its exact ordered contributors and profiler-row
-- locators. Replace the fallback target with an aggregate_id from hotspots.
with target as (
  select aggregate_id
  from traceloom_replay_cost_aggregate
  order by duration_median_ns desc, aggregate_id
  limit 1
)
select
  a.aggregate_id,
  a.aggregation_scope,
  a.identity as aggregate_identity,
  a.duration_p25_ns,
  a.duration_median_ns,
  a.duration_p75_ns,
  x.contributor_order,
  m.cost_unit_id,
  m.launch_id,
  m.member_id,
  m.slot_order,
  m.stream_id,
  m.task_ordinal,
  m.duration_ns,
  m.event_id,
  g.source_table,
  g.source_row_id
from target t
join traceloom_replay_cost_aggregate a using (aggregate_id)
join traceloom_replay_cost_aggregate_member x using (aggregate_id)
join traceloom_replay_cost_member m
  on m.member_id = x.member_id
 and m.db_idx = x.db_idx
 and m.device_id = x.device_id
join traceloom_graph_body_member g
  on g.member_id = m.member_id
 and g.db_idx = m.db_idx
 and g.device_id = m.device_id
order by x.contributor_order;
