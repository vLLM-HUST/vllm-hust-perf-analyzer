-- Provider-neutral runtime call <-> device work relation.
-- Open/ambiguous rows are intentional analytical results.

SELECT relation_id,
       provider,
       api_name,
       runtime_start_ns,
       runtime_dur_us,
       work_kind,
       device_symbol,
       device_start_ns,
       device_dur_us,
       match_policy,
       evidence_level,
       support_state,
       cardinality
FROM traceloom_v_runtime_device
ORDER BY db_idx,
         provider,
         COALESCE(runtime_start_ns, device_start_ns),
         relation_id
LIMIT 200;

-- Reverse path for one normalized device event:
-- SELECT * FROM traceloom_v_runtime_device WHERE event_id = 'event-42';

-- Forward path for one runtime call:
-- SELECT * FROM traceloom_v_runtime_device
-- WHERE runtime_call_id = 'runtime-call-7';

-- Structural path in either direction:
-- SELECT node_id, occurrence_idx, anchor_order, runtime_call_id,
--        device_work_id, api_name, device_symbol, support_state
-- FROM traceloom_v_node_runtime_call
-- WHERE coverage_kind = 'self' AND node_id = 'node-42'
-- ORDER BY occurrence_idx, anchor_order, runtime_start_ns;
--
-- Replace the node predicate with runtime_call_id = 'runtime-call-7' to walk
-- from one call back to every recovered structural placement.
