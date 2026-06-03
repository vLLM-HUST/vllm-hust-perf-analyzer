select
  a.anchor_idx,
  a.symbol,
  a.role,
  count(al.aux_event_id) as aux_events,
  round(sum(e.dur_us), 3) as aux_us,
  min(al.aux_step_idx) as first_aux_step_idx,
  max(al.aux_step_idx) as last_aux_step_idx
from traceloom_anchor a
join traceloom_aux_link al on al.anchor_id = a.anchor_id
join traceloom_event e on e.event_id = al.aux_event_id
group by a.anchor_id
order by aux_events desc, aux_us desc;
