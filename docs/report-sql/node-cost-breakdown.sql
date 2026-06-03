select
  local_node_id as node,
  depth,
  loop_depth,
  kind,
  label,
  repeat_count,
  occurrence_count,
  round(total_us, 3) as total_us,
  round(avg_total_us, 3) as avg_total_us,
  round(avg_compute_us, 3) as avg_compute_us,
  round(avg_comm_us, 3) as avg_comm_us,
  round(avg_idle_us, 3) as avg_idle_us,
  round(avg_self_us, 3) as avg_self_us,
  round(avg_aux_us, 3) as avg_aux_us,
  round(100.0 * compute_us / nullif(total_us, 0), 2) as compute_pct,
  round(100.0 * comm_us / nullif(total_us, 0), 2) as comm_pct,
  round(100.0 * idle_us / nullif(total_us, 0), 2) as idle_pct,
  round(100.0 * self_us / nullif(total_us, 0), 2) as self_pct,
  round(100.0 * aux_us / nullif(total_us, 0), 2) as aux_pct
from traceloom_v_tree_node
where local_node_id = 'N027'
order by db_idx, device_id, view_name, display_order;
