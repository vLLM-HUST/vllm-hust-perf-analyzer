-- Bind :run_id. Costs are supported event-level work, NOT complete step latency.
-- Exclude graph-launch envelopes: adding them to exact body events would mix
-- observation resolutions and falsely fill unobserved device-idle gaps.
WITH work AS (
 SELECT DISTINCT run_id,step_id,device_id,device_work_id,start_ns,end_ns,dur_us
 FROM traceloom_v_context_device_work WHERE run_id=:run_id AND event_id IS NOT NULL
), previous AS (
 SELECT *,MAX(end_ns) OVER(PARTITION BY run_id,step_id,device_id
              ORDER BY start_ns,end_ns,device_work_id
              ROWS BETWEEN UNBOUNDED PRECEDING AND 1 PRECEDING) AS prior_end
 FROM work
), devices AS (
 SELECT run_id,step_id,device_id,COUNT(*) AS linked_device_work,
 SUM(dur_us) AS summed_device_work_us,
 SUM(MAX(0,end_ns-MAX(start_ns,COALESCE(prior_end,start_ns))))/1000.0 AS device_busy_union_us,
 (MAX(end_ns)-MIN(start_ns))/1000.0 AS device_envelope_us
 FROM previous GROUP BY run_id,step_id,device_id
)
SELECT s.run_id,s.step_id,s.ordinal,s.total_scheduled_tokens,s.scheduled_requests,
 s.recorded_executions,s.supported_markers,s.capture_state,
 d.device_id,COALESCE(d.linked_device_work,0) AS linked_device_work,
 d.summed_device_work_us,d.device_busy_union_us,d.device_envelope_us
FROM traceloom_v_scheduler_step_context s LEFT JOIN devices d USING(run_id,step_id)
WHERE s.run_id=:run_id ORDER BY s.scheduler_id,s.ordinal,d.device_id;
