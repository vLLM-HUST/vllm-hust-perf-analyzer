with gap_totals as (
  select
    run_id,
    sum(duration_ns) as visible_productive_idle_ns
  from traceloom_device_interval
  where interval_kind = 'visible_productive_idle'
  group by run_id
), category_totals as (
  select
    run_id,
    category,
    evidence_level,
    evidence_relation,
    count(*) as slice_count,
    sum(duration_ns) as duration_ns
  from traceloom_idle_explanation
  group by run_id, category, evidence_level, evidence_relation
)
select
  m.run_id,
  m.analysis_status,
  m.collection_status,
  c.category,
  c.evidence_level,
  c.evidence_relation,
  c.slice_count,
  c.duration_ns,
  round(c.duration_ns / 1000.0, 3) as duration_us,
  round(100.0 * c.duration_ns /
        nullif(g.visible_productive_idle_ns, 0), 6) as gap_share_pct
from category_totals c
join traceloom_run_metadata m using (run_id)
join gap_totals g using (run_id)
order by
  case c.category
    when 'blocked_by_visible_wait' then 1
    when 'capture_control_present' then 2
    when 'runtime_control_present' then 3
    when 'queued_visible_task_delay' then 4
    when 'host_sync_api_present' then 5
    when 'no_observed_device_work' then 6
    when 'unattributed_visible_idle' then 7
    else 8
  end,
  c.category;
