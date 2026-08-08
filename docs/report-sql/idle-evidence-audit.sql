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
), evidence_with_run as (
  select
    l.*,
    coalesce(d.run_id, s.run_id, x.run_id, c.run_id, m.run_id) as run_id
  from traceloom_evidence_link l
  left join traceloom_device_interval d
    on l.owner_kind = 'device_interval' and d.interval_id = l.owner_id
  left join traceloom_stream_state s
    on l.owner_kind = 'stream_state' and s.state_id = l.owner_id
  left join traceloom_idle_explanation x
    on l.owner_kind = 'explanation' and x.idle_explanation_id = l.owner_id
  left join traceloom_idle_candidate c
    on l.owner_kind = 'candidate' and c.candidate_id = l.owner_id
  left join traceloom_clock_model m
    on l.owner_kind = 'clock_model' and m.clock_model_id = l.owner_id
), evidence_errors as (
  select
    l.run_id,
    sum(case
          when l.trace_event_id != '' and not exists (
            select 1 from traceloom_event ev
            where ev.event_id = l.trace_event_id
          ) then 1
          when l.trace_event_id = '' and not exists (
            select 1 from traceloom_host_api_event h
            where h.run_id = l.run_id
              and h.source_kind = l.source_kind
              and h.source_table = l.source_table
              and h.source_key = l.source_key
          ) and not exists (
            select 1 from traceloom_clock_marker k
            where k.run_id = l.run_id
              and k.source_kind = l.source_kind
              and k.source_table = l.source_table
              and k.source_key = l.source_key
          ) then 1
          else 0
        end) as source_errors,
    sum(case
          when l.relation = 'none' and
               (l.overlap_start_ns is not null or l.overlap_end_ns is not null)
            then 1
          when l.relation != 'none' and
               (l.overlap_start_ns is null or l.overlap_end_ns is null or
                l.overlap_end_ns <= l.overlap_start_ns or
                (l.relation = 'device_event_coverage' and not exists (
                  select 1 from traceloom_event ev
                  where ev.event_id = l.trace_event_id
                    and l.overlap_start_ns >= ev.start_ns
                    and l.overlap_end_ns <= ev.end_ns
                )) or
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
                )) or
                (l.owner_kind = 'candidate' and not exists (
                  select 1
                  from traceloom_idle_candidate c
                  join traceloom_device_interval g
                    on g.interval_id = c.gap_interval_id
                  where c.candidate_id = l.owner_id
                    and l.overlap_start_ns >= g.start_ns
                    and l.overlap_end_ns <= g.end_ns
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
          when l.owner_kind = 'candidate' and not exists (
            select 1 from traceloom_idle_candidate c
            where c.candidate_id = l.owner_id
          ) then 1
          when l.owner_kind = 'clock_model' and not exists (
            select 1 from traceloom_clock_model m
            where m.clock_model_id = l.owner_id
          ) then 1
          when l.owner_kind not in (
            'device_interval', 'stream_state', 'explanation', 'candidate',
            'clock_model'
          ) then 1
          else 0
        end) as owner_errors
  from evidence_with_run l
  group by l.run_id
), cross_clock_errors as (
  select
    e.run_id,
    sum(case
          when e.evidence_level != 'correlated'
            or e.alignment_status not in ('calibrated', 'synthetic_only')
            or e.contract_version != 'idle-evidence-contract-v4.4'
            or e.attribution_rule_version != 'host_device_projection_v2'
            or (e.category = 'host_sync_api_present' and
                e.evidence_relation != 'temporal_overlap')
            or (e.category = 'queued_visible_task_delay' and
                e.evidence_relation != 'exact_connection_id')
            then 1 else 0
        end) as explanation_contract_errors,
    sum(case
          when 1 != (
            select count(*)
            from traceloom_clock_model m
            where m.run_id = e.run_id
              and m.device_id = e.device_id
              and m.alignment_status = e.alignment_status
              and m.alignment_status in ('calibrated', 'synthetic_only')
              and m.has_profiler_host_mapping = 1
              and m.source_clock_domain = 'profiler_host'
              and m.intermediate_clock_domain = 'caller_clock_realtime'
              and m.target_clock_domain = 'device'
              and m.mapping_kind = 'composed_affine'
              and m.profiler_caller_observation_kind =
                    'record_api_midpoint_to_record_bracket_midpoint'
              and m.marker_device_observation_kind =
                    'record_sync_bracket_midpoint_to_task_start'
          ) then 1 else 0
        end) as fail_closed_errors,
    sum(case
          when not exists (
            select 1
            from traceloom_evidence_link l
            join traceloom_host_api_event h
              on h.run_id = e.run_id
             and h.source_kind = l.source_kind
             and h.source_table = l.source_table
             and h.source_key = l.source_key
            where l.owner_kind = 'explanation'
              and l.owner_id = e.idle_explanation_id
              and l.relation = e.evidence_relation
              and l.evidence_level = 'correlated'
              and h.clock_domain = 'profiler_host'
              and h.contract_version = e.contract_version
              and (
                (e.category = 'host_sync_api_present'
                  and h.api_family = 'host_sync')
                or
                (e.category = 'queued_visible_task_delay'
                  and h.api_family = 'enqueue')
              )
          ) then 1 else 0
        end) as host_source_errors,
    sum(case
          when e.category = 'queued_visible_task_delay' and 1 != (
            select count(*)
            from traceloom_evidence_link host_link
            join traceloom_host_api_event h
              on h.run_id = e.run_id
             and h.source_kind = host_link.source_kind
             and h.source_table = host_link.source_table
             and h.source_key = host_link.source_key
            join traceloom_task_api_link t
              on t.run_id = e.run_id
             and t.api_event_id = h.api_event_id
             and t.device_id = e.device_id
             and t.link_status = 'unique'
             and t.connection_id = h.connection_id
            join traceloom_evidence_link task_link
              on task_link.owner_kind = 'explanation'
             and task_link.owner_id = e.idle_explanation_id
             and task_link.trace_event_id = t.trace_event_id
             and task_link.relation = 'exact_connection_id'
             and task_link.evidence_level = 'correlated'
            where host_link.owner_kind = 'explanation'
              and host_link.owner_id = e.idle_explanation_id
              and host_link.relation = 'exact_connection_id'
              and host_link.evidence_level = 'correlated'
              and h.connection_id is not null
          ) then 1 else 0
        end) as queued_task_link_errors
  from traceloom_idle_explanation e
  where e.category in (
    'host_sync_api_present', 'queued_visible_task_delay'
  )
  group by e.run_id
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
  coalesce(c.explanation_contract_errors, 0) as
    host_explanation_contract_errors,
  coalesce(c.fail_closed_errors, 0) as cross_clock_fail_closed_errors,
  coalesce(c.host_source_errors, 0) as host_evidence_source_errors,
  coalesce(c.queued_task_link_errors, 0) as queued_task_link_errors,
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
      or coalesce(c.explanation_contract_errors, 0) != 0
      or coalesce(c.fail_closed_errors, 0) != 0
      or coalesce(c.host_source_errors, 0) != 0
      or coalesce(c.queued_task_link_errors, 0) != 0
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
left join evidence_errors v using (run_id)
left join cross_clock_errors c using (run_id)
left join anchor_totals a using (run_id)
left join root_totals r using (run_id)
order by m.db_idx, m.run_id;
