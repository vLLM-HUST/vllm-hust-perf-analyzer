with parent as (
  select node_id
  from traceloom_viz_node
  where repeat_count is not null
  order by total_us desc
  limit 1
)
select
  child.local_node_id,
  child.level,
  child.kind,
  child.node_type,
  child.repeat_count,
  child.occurrence_count,
  child.anchor_count,
  round(child.total_us, 3) as total_us,
  round(child.avg_total_us, 3) as avg_total_us
from traceloom_v_node_children child
join parent on parent.node_id = child.parent_node_id
order by child.edge_order;
