-- Bind :run_id. Captured event work, not complete step latency or request cost.
-- The public view deduplicates execution phases before sum/union/envelope and
-- retains decisions without supported device evidence as NULL costs.
SELECT run_id,step_id,ordinal,total_scheduled_tokens,scheduled_requests,
 recorded_executions,supported_markers,capture_state,device_id,linked_device_work,
 summed_device_work_us,device_busy_union_us,device_envelope_us
FROM traceloom_v_scheduler_step_device_cost
WHERE run_id=:run_id ORDER BY scheduler_id,ordinal,device_id;
