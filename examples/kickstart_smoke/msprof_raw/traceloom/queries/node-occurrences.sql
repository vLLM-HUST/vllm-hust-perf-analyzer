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
where local_node_id = 'N027'
order by occurrence_idx;
