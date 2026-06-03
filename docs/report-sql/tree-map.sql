select
  local_node_id as node,
  label,
  depth,
  occurrence_count as occ,
  round(avg_total_us, 3) as avg_total_us,
  round(avg_aux_us, 3) as avg_aux_us,
  round(total_us, 3) as total_us
from traceloom_v_tree_node
order by db_idx, device_id, view_name, display_order;
