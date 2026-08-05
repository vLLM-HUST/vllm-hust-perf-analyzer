with membership as (
  select
    unit_id,
    db_idx,
    device_id,
    count(*) as membership_count
  from traceloom_structural_unit_anchor
  group by unit_id, db_idx, device_id
)
select
  u.unit_order,
  u.unit_id,
  u.kind,
  u.run_count,
  u.family_id,
  u.body_fingerprint,
  u.anchor_count,
  coalesce(m.membership_count, 0) as membership_count,
  u.shape_signature,
  u.span_us,
  u.total_us,
  u.compute_us,
  u.comm_us,
  u.idle_us,
  u.evidence_status,
  u.boundary_policy,
  u.expansion_nodes
from traceloom_structural_unit u
left join membership m
  on m.unit_id = u.unit_id
 and m.db_idx = u.db_idx
 and m.device_id = u.device_id
order by u.db_idx, u.device_id, u.unit_order;
