with target_node as (
  select coalesce(
    (
      select 'N027'
      where exists (
        select 1
        from traceloom_tree_node_occurrence
        where local_node_id = 'N027'
      )
    ),
    (
      select local_node_id
      from traceloom_tree_node_occurrence
      order by db_idx, device_id, view_name, local_node_id
      limit 1
    )
  ) as local_node_id
)
select
  local_node_id as node,
  occurrence_idx,
  repeat_context,
  anchor_start_idx,
  anchor_end_idx,
  anchor_count,
  start_ns,
  end_ns,
  round(total_us, 3) as total_us,
  round(compute_us, 3) as compute_us,
  round(comm_us, 3) as comm_us,
  round(idle_us, 3) as idle_us,
  round(aux_us, 3) as aux_us
from traceloom_tree_node_occurrence
where local_node_id = (select local_node_id from target_node)
order by occurrence_idx;
