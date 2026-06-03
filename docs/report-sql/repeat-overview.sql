select
  local_node_id,
  level,
  kind,
  node_type,
  repeat_count,
  occurrence_count,
  anchor_count,
  round(total_us, 3) as total_us,
  round(avg_total_us, 3) as avg_total_us,
  round(sql_anchor_us, 3) as anchor_us,
  round(sql_aux_us, 3) as aux_us
from traceloom_v_node_cost
where repeat_count is not null
order by total_us desc;
