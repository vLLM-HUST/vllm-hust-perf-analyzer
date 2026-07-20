# Gap Signal Source-Level Case Studies

## Case 1: MODEL_MAINTAINCE -- Profiler Marker, Not Runtime Task

**Origin:** `ascend_aclgraph.py` GRAPH_TASK_KEYS = {"MODEL_EXECUTE", "MODEL_MAINTAINCE", ...}

**Evidence:** In the kickstart profile, ALL MODEL_MAINTAINCE tasks have 0us duration
(startNs = endNs). They appear on dedicated streams (406/702) with 100% gap rate.
They are profiler-level markers inserted at graph boundary points, not real device work.

**Recommendation:** Relabel from `runtime_control_present` to
`device_non_productive_interval.acl_graph_control_phase`, confidence=heuristic.


## Case 2: CAPTURE_WAIT -- Profiler-Internal Control, 3x Device Variation

**Origin:** In `ascend_aclgraph.py`: GRAPH_BODY_EXCLUDED_KEYS = {..., "CAPTURE_WAIT", ...}.
In `msprof_reader.py`: CAPTURE_WAIT rows are filtered from event output.

**Evidence:** Device 0: 37,483 CAPTURE_WAIT rows vs Device 1: 12,475 (3x difference).
Total time is minimal (~90ms / ~30ms). The count depends on device role during ACL
graph capture (primary device does more capture work).

**Recommendation:** Keep `capture_control_present` label, confidence=heuristic.
Note the 3x device variation as evidence this is profiler artifact, not runtime behavior.


## Case 3: EVENT_RECORD -> EVENT_WAIT -- Strongest Confirmed Signal

**Origin:** In `msprof_reader.py`:
- EVENT_RECORD is in COMM_TASK_TYPES -> classified as "comm"
- EVENT_WAIT matches WAIT keyword in _classify_task -> classified as "wait"

The `aclrtStreamWaitEvent` CANN API has a direct connectionId link to EVENT_WAIT TASK
rows: 6,984 API calls, 4,564 confirmed links. This is the ONLY fully traceable
host-to-device causality chain in the profiler data.

**Evidence:** Stream 415/707 dedicated to EVENT_RECORD->EVENT_WAIT pattern
(~1,000 occurrences each). Each gap has a corresponding aclrtStreamWaitEvent.

**Recommendation:** Label = `blocked_by_visible_wait.event_synchronization`,
confidence = confirmed. This is the strongest gap signal available.


## Summary Table

| Signal | Source | Nature | Label | Confidence |
|--------|--------|--------|-------|------------|
| MODEL_MAINTAINCE | GRAPH_TASK_KEYS | Profiler marker (0us tasks) | acl_graph_control_phase | heuristic |
| CAPTURE_WAIT | GRAPH_BODY_EXCLUDED | Profiler-internal, 3x variation | capture_control_present | heuristic |
| EVENT_RECORD->WAIT | COMM_TASK_TYPES | Cross-stream sync | event_synchronization_boundary | confirmed |
