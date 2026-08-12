-- Role-collapsed replay-internal hotspots. Median duration is emitted by the
-- native cost map; this query does not reconstruct or realign distributions.
select
  aggregate_id,
  graph_template_id,
  slot_role,
  replay_body_template_id,
  stream_id,
  task_ordinal,
  identity,
  kind,
  member_occurrence_count,
  replay_unit_count,
  duration_p25_ns,
  duration_median_ns,
  duration_p75_ns,
  scheduled_work_share_ppm
from traceloom_replay_cost_aggregate
where distribution_supported = 1
order by duration_median_ns desc, aggregate_id
limit 50;
