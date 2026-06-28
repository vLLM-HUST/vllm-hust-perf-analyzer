select
  g.graph_provider,
  g.graph_kind,
  g.graph_event_idx,
  g.anchor_idx,
  g.graph_exec_id,
  g.graph_id,
  g.stream_id,
  round(g.dur_us, 3) as graph_us,
  g.enclosed_event_count,
  round(g.enclosed_event_us, 3) as enclosed_event_us,
  g.enclosed_kernel_count,
  round(g.enclosed_kernel_us, 3) as enclosed_kernel_us,
  min(ge.child_step_idx) as first_child_step_idx,
  max(ge.child_step_idx) as last_child_step_idx
from traceloom_v_cuda_graph_replay g
left join traceloom_v_cuda_graph_envelope ge
  on ge.graph_event_id = g.graph_event_id
group by g.graph_event_id
order by g.dur_us desc, g.graph_event_idx;
