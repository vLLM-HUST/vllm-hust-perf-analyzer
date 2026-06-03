select
  na.local_node_id as node,
  na.occurrence_idx,
  na.anchor_order,
  a.anchor_idx,
  a.symbol as anchor_symbol,
  a.label as anchor_label,
  e.source_table,
  e.source_key,
  e.stream_id,
  e.start_ns,
  e.end_ns,
  round(e.dur_us, 3) as dur_us,
  e.role,
  e.semantic_role
from traceloom_tree_node_anchor na
join traceloom_anchor a on a.anchor_id = na.anchor_id
join traceloom_event e on e.event_id = a.event_id
where na.local_node_id = 'N027'
order by na.occurrence_idx, na.anchor_order;
