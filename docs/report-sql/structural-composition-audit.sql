with unit_stats as (
  select
    count(*) as unit_count,
    sum(case when kind = 'graph_unit' then 1 else 0 end) as graph_unit_count,
    sum(case when kind = 'structural_unit' and evidence_status = 'complete'
             then 1 else 0 end) as complete_structural_unit_count,
    sum(case when kind = 'unrecognized' then 1 else 0 end) as unrecognized_unit_count,
    sum(anchor_count) as unit_anchor_sum
  from traceloom_structural_unit
  having count(*) > 0
), per_unit_membership as (
  select
    u.unit_id,
    u.db_idx,
    u.device_id,
    u.anchor_count,
    count(m.anchor_id) as membership_count
  from traceloom_structural_unit u
  left join traceloom_structural_unit_anchor m
    on m.unit_id = u.unit_id
   and m.db_idx = u.db_idx
   and m.device_id = u.device_id
  group by u.unit_id, u.db_idx, u.device_id, u.anchor_count
), membership_stats as (
  select
    count(*) as membership_count,
    count(distinct cast(db_idx as text) || ':' || cast(device_id as text) ||
                   ':' || anchor_id) as distinct_member_count
  from traceloom_structural_unit_anchor
), unit_membership_errors as (
  select count(*) as count
  from per_unit_membership
  where anchor_count != membership_count
), duplicate_memberships as (
  select count(*) as count
  from (
    select db_idx, device_id, anchor_id
    from traceloom_structural_unit_anchor
    group by db_idx, device_id, anchor_id
    having count(*) != 1
  )
), orphan_memberships as (
  select count(*) as count
  from traceloom_structural_unit_anchor m
  left join traceloom_anchor a
    on a.anchor_id = m.anchor_id
   and a.db_idx = m.db_idx
   and a.device_id = m.device_id
  where a.anchor_id is null
), ordered_units as (
  select
    u.*,
    row_number() over (
      partition by db_idx, device_id order by unit_order
    ) - 1 as expected_order,
    lag(token_end_ordinal) over (
      partition by db_idx, device_id order by unit_order
    ) as previous_token_end,
    lag(kind) over (
      partition by db_idx, device_id order by unit_order
    ) as previous_kind,
    lead(kind) over (
      partition by db_idx, device_id order by unit_order
    ) as next_kind
  from traceloom_structural_unit u
), order_partition_errors as (
  select count(*) as count
  from ordered_units
  where unit_order != expected_order
     or token_end_ordinal <= token_start_ordinal
     or (previous_token_end is null and token_start_ordinal != 0)
     or (previous_token_end is not null and
         token_start_ordinal != previous_token_end)
     or (kind = 'structural_unit' and
         (coalesce(previous_kind, '') != 'graph_unit' or
          coalesce(next_kind, '') != 'graph_unit'))
     or (evidence_status = 'unrecognized_open_prefix' and
         expected_order != 0)
     or (evidence_status = 'unrecognized_open_suffix' and
         next_kind is not null)
), identity_errors as (
  select
    (select count(*) from (
      select db_idx, device_id, unit_id
      from traceloom_structural_unit
      group by db_idx, device_id, unit_id
      having count(*) != 1
    )) +
    (select count(*) from (
      select db_idx, device_id, family_id
      from traceloom_structural_unit
      group by db_idx, device_id, family_id
      having count(distinct kind || ':' || body_fingerprint) != 1
    )) +
    (select count(*) from (
      select db_idx, device_id, kind, body_fingerprint
      from traceloom_structural_unit
      group by db_idx, device_id, kind, body_fingerprint
      having count(distinct family_id) != 1
    )) as count
), evidence_policy_errors as (
  select count(*) as count
  from traceloom_structural_unit
  where not (
    (kind = 'graph_unit' and evidence_status = 'exact' and
     boundary_policy = 'direct_exact_graph_unit') or
    (kind = 'structural_unit' and evidence_status = 'complete' and
     boundary_policy = 'bounded_by_adjacent_exact_graph_units') or
    (kind = 'unrecognized' and
     ((evidence_status = 'unrecognized_open_prefix' and
       boundary_policy = 'open_trace_prefix') or
      (evidence_status = 'unrecognized_open_suffix' and
       boundary_policy = 'open_trace_suffix')))
  )
), cost_errors as (
  select count(*) as count
  from traceloom_structural_unit
  where abs(total_us - (compute_us + comm_us + idle_us)) > 0.000001
), interval_errors as (
  select count(*) as count
  from traceloom_structural_unit
  where end_ns < start_ns
     or abs(span_us - cast(end_ns - start_ns as real) / 1000.0) > 0.000001
), expansion_errors as (
  select count(*) as count
  from traceloom_structural_unit
  where expansion_nodes is null or expansion_nodes = ''
), anchor_order_errors as (
  select count(*) as count
  from (
    select
      unit_id,
      db_idx,
      device_id,
      count(*) as row_count,
      count(distinct anchor_order) as distinct_order_count,
      min(anchor_order) as first_order,
      max(anchor_order) as last_order
    from traceloom_structural_unit_anchor
    group by unit_id, db_idx, device_id
    having first_order != 0
        or last_order != row_count - 1
        or distinct_order_count != row_count
  )
), anchor_stats as (
  select count(*) as anchor_count from traceloom_anchor
)
select
  u.unit_count,
  u.graph_unit_count,
  u.complete_structural_unit_count,
  u.unrecognized_unit_count,
  a.anchor_count,
  u.unit_anchor_sum,
  m.membership_count,
  m.distinct_member_count,
  m.membership_count - a.anchor_count as membership_count_delta,
  um.count as unit_membership_errors,
  d.count as duplicate_anchor_memberships,
  o.count as orphan_anchor_memberships,
  p.count as order_partition_errors,
  ao.count as anchor_order_errors,
  i.count as identity_errors,
  e.count as evidence_policy_errors,
  c.count as cost_errors,
  t.count as interval_errors,
  x.count as expansion_errors,
  case
    when u.unit_anchor_sum != a.anchor_count
      or m.membership_count != a.anchor_count
      or m.distinct_member_count != a.anchor_count
      or um.count != 0
      or d.count != 0
      or o.count != 0
      or p.count != 0
      or ao.count != 0
      or i.count != 0
      or e.count != 0
      or c.count != 0
      or t.count != 0
      or x.count != 0
      then 'FAIL'
    else 'PASS'
  end as audit_status
from unit_stats u
cross join anchor_stats a
cross join membership_stats m
cross join unit_membership_errors um
cross join duplicate_memberships d
cross join orphan_memberships o
cross join order_partition_errors p
cross join anchor_order_errors ao
cross join identity_errors i
cross join evidence_policy_errors e
cross join cost_errors c
cross join interval_errors t
cross join expansion_errors x;
