select
  anchor_idx,
  symbol,
  anchor_kind,
  total_us,
  self_us,
  aux_us,
  graph_child_us,
  residual_us,
  raw_child_task_count,
  top_ops,
  diagnostic_flags
from traceloom_anchor_cost_breakdown
order by anchor_idx;
