with interval_totals as (
  select
    run_id,
    count(*) as device_interval_count,
    sum(case when interval_kind = 'productive_active'
             then duration_ns else 0 end) as productive_active_ns,
    sum(case when interval_kind = 'visible_productive_idle'
             then duration_ns else 0 end) as visible_productive_idle_ns,
    sum(case when end_ns <= start_ns or duration_ns != end_ns - start_ns
             then 1 else 0 end) as invalid_interval_count
  from traceloom_device_interval
  group by run_id
), explanation_totals as (
  select
    run_id,
    count(*) as explanation_slice_count,
    sum(duration_ns) as explanation_ns,
    sum(case when end_ns <= start_ns or duration_ns != end_ns - start_ns
             then 1 else 0 end) as invalid_explanation_count
  from traceloom_idle_explanation
  group by run_id
), gap_link_errors as (
  select
    e.run_id,
    count(*) as count
  from traceloom_idle_explanation e
  left join traceloom_device_interval g
    on g.interval_id = e.gap_interval_id
   and g.run_id = e.run_id
  where g.interval_id is null
     or g.interval_kind != 'visible_productive_idle'
     or e.start_ns < g.start_ns
     or e.end_ns > g.end_ns
  group by e.run_id
), gap_partition_errors as (
  select
    g.run_id,
    sum(case
          when x.slice_count is null
            or x.first_start_ns != g.start_ns
            or x.last_end_ns != g.end_ns
            or x.duration_ns != g.duration_ns
            then 1 else 0
        end) as count
  from traceloom_device_interval g
  left join (
    select
      run_id,
      gap_interval_id,
      count(*) as slice_count,
      min(start_ns) as first_start_ns,
      max(end_ns) as last_end_ns,
      sum(duration_ns) as duration_ns
    from traceloom_idle_explanation
    group by run_id, gap_interval_id
  ) x on x.run_id = g.run_id and x.gap_interval_id = g.interval_id
  where g.interval_kind = 'visible_productive_idle'
  group by g.run_id
), explanation_overlap_errors as (
  select
    run_id,
    sum(case when previous_end_ns is not null and start_ns < previous_end_ns
             then 1 else 0 end) as count
  from (
    select
      e.*,
      lag(end_ns) over (
        partition by run_id, gap_interval_id
        order by explanation_order
      ) as previous_end_ns
    from traceloom_idle_explanation e
  )
  group by run_id
), stream_partition_errors as (
  select
    run_id,
    sum(case
          when end_ns <= start_ns or duration_ns != end_ns - start_ns
            or (previous_end_ns is not null and start_ns != previous_end_ns)
            then 1 else 0
        end) as count
  from (
    select
      s.*,
      lag(end_ns) over (
        partition by run_id, device_id, stream_id
        order by state_order
      ) as previous_end_ns
    from traceloom_stream_state s
  )
  group by run_id
), evidence_errors as (
  select
    sum(case when not exists (
          select 1 from traceloom_event ev
          where ev.event_id = l.trace_event_id
        ) then 1 else 0 end) as source_errors,
    sum(case
          when l.relation = 'none' and
               (l.overlap_start_ns is not null or l.overlap_end_ns is not null)
            then 1
          when l.relation != 'none' and
               (l.overlap_start_ns is null or l.overlap_end_ns is null or
                l.overlap_end_ns <= l.overlap_start_ns or
                not exists (
                  select 1 from traceloom_event ev
                  where ev.event_id = l.trace_event_id
                    and l.overlap_start_ns >= ev.start_ns
                    and l.overlap_end_ns <= ev.end_ns
                ) or
                (l.owner_kind = 'device_interval' and not exists (
                  select 1 from traceloom_device_interval d
                  where d.interval_id = l.owner_id
                    and l.overlap_start_ns >= d.start_ns
                    and l.overlap_end_ns <= d.end_ns
                )) or
                (l.owner_kind = 'stream_state' and not exists (
                  select 1 from traceloom_stream_state s
                  where s.state_id = l.owner_id
                    and l.overlap_start_ns >= s.start_ns
                    and l.overlap_end_ns <= s.end_ns
                )) or
                (l.owner_kind = 'explanation' and not exists (
                  select 1 from traceloom_idle_explanation x
                  where x.idle_explanation_id = l.owner_id
                    and l.overlap_start_ns >= x.start_ns
                    and l.overlap_end_ns <= x.end_ns
                )))
            then 1
          else 0
        end) as extent_errors,
    sum(case
          when l.owner_kind = 'device_interval' and not exists (
            select 1 from traceloom_device_interval d
            where d.interval_id = l.owner_id
          ) then 1
          when l.owner_kind = 'stream_state' and not exists (
            select 1 from traceloom_stream_state s
            where s.state_id = l.owner_id
          ) then 1
          when l.owner_kind = 'explanation' and not exists (
            select 1 from traceloom_idle_explanation x
            where x.idle_explanation_id = l.owner_id
          ) then 1
          when l.owner_kind not in (
            'device_interval', 'stream_state', 'explanation'
          ) then 1
          else 0
        end) as owner_errors
  from traceloom_evidence_link l
), anchor_totals as (
  select
    run_id,
    sum(duration_ns) as anchor_attributed_ns,
    sum(case when a.anchor_id is null then 1 else 0 end) as orphan_rows
  from traceloom_anchor_idle_explanation i
  left join traceloom_anchor a
    on a.anchor_id = i.anchor_id
   and a.db_idx = i.db_idx
   and a.device_id = i.device_id
  group by run_id
), root_totals as (
  select
    i.run_id,
    sum(i.duration_ns) as root_attributed_ns,
    sum(case when n.node_id is null then 1 else 0 end) as orphan_rows
  from traceloom_node_idle_explanation i
  left join traceloom_viz_node n
    on n.node_id = i.node_id
   and n.db_idx = i.db_idx
   and n.device_id = i.device_id
   and n.view_name = i.view_name
  where n.depth = 0 or n.node_id is null
  group by i.run_id
)
select
  m.run_id,
  m.analysis_status,
  m.collection_status,
  coalesce(i.device_interval_count, 0) as device_interval_count,
  coalesce(i.productive_active_ns, 0) as productive_active_ns,
  coalesce(i.visible_productive_idle_ns, 0) as visible_productive_idle_ns,
  coalesce(e.explanation_slice_count, 0) as explanation_slice_count,
  coalesce(e.explanation_ns, 0) as explanation_ns,
  coalesce(e.explanation_ns, 0) -
    coalesce(i.visible_productive_idle_ns, 0) as partition_delta_ns,
  coalesce(g.count, 0) as gap_link_errors,
  coalesce(gp.count, 0) as gap_partition_errors,
  coalesce(eo.count, 0) as explanation_overlap_errors,
  coalesce(sp.count, 0) as stream_partition_errors,
  coalesce(v.source_errors, 0) as evidence_source_errors,
  coalesce(v.extent_errors, 0) as evidence_extent_errors,
  coalesce(v.owner_errors, 0) as evidence_owner_errors,
  coalesce(a.orphan_rows, 0) as anchor_orphan_errors,
  coalesce(r.orphan_rows, 0) as node_orphan_errors,
  coalesce(a.anchor_attributed_ns, 0) as anchor_attributed_ns,
  coalesce(r.root_attributed_ns, 0) as root_attributed_ns,
  coalesce(i.visible_productive_idle_ns, 0) -
    coalesce(a.anchor_attributed_ns, 0) as device_only_residual_ns,
  case
    when coalesce(i.invalid_interval_count, 0) != 0
      or coalesce(e.invalid_explanation_count, 0) != 0
      or coalesce(e.explanation_ns, 0) !=
         coalesce(i.visible_productive_idle_ns, 0)
      or coalesce(g.count, 0) != 0
      or coalesce(gp.count, 0) != 0
      or coalesce(eo.count, 0) != 0
      or coalesce(sp.count, 0) != 0
      or coalesce(v.source_errors, 0) != 0
      or coalesce(v.extent_errors, 0) != 0
      or coalesce(v.owner_errors, 0) != 0
      or coalesce(a.orphan_rows, 0) != 0
      or coalesce(r.orphan_rows, 0) != 0
      or coalesce(a.anchor_attributed_ns, 0) >
         coalesce(i.visible_productive_idle_ns, 0)
      or coalesce(r.root_attributed_ns, 0) !=
         coalesce(a.anchor_attributed_ns, 0)
      then 'FAIL'
    else 'PASS'
  end as audit_status
from traceloom_run_metadata m
left join interval_totals i using (run_id)
left join explanation_totals e using (run_id)
left join gap_link_errors g using (run_id)
left join gap_partition_errors gp using (run_id)
left join explanation_overlap_errors eo using (run_id)
left join stream_partition_errors sp using (run_id)
left join evidence_errors v on 1 = 1
left join anchor_totals a using (run_id)
left join root_totals r using (run_id)
order by m.db_idx, m.run_id;
