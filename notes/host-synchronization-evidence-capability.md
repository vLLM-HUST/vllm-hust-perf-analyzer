# Host synchronization evidence capability audit

Status: 2026-08-12, evidence audit for #32 / #33.

TraceLoom should first expose the synchronization facts retained by a profiler,
then add composites only when the artifact contains the identity needed to
construct them. An observed wait is not automatically an explanation for a
device gap.

## Capability matrix

| Question | CUDA / retained Nsight SQLite | Ascend / retained msprof SQLite | Current contract |
| --- | --- | --- | --- |
| What kind of synchronization action is this? | `CUPTI_ACTIVITY_KIND_SYNCHRONIZATION.syncType`, decoded through `ENUM_CUPTI_SYNC_TYPE` | `TASK.taskType` + `STRING_IDS` exposes `EVENT_RECORD` / `EVENT_WAIT`; host API names expose synchronize calls | Supported factual action kind |
| Which runtime call is associated with the action? | CUPTI documents `correlationId` as the associated API. Nsight may reuse IDs; unique containment may disambiguate only within the correlation candidate set | `CANN_API.connectionId = TASK.connectionId`; relation is 1:N where observed | Exact direct ID when unique; CUDA reused-ID cases are deterministic, not exact |
| Which record does an event wait consume? | Export exposes `eventId`, but retained schemas do not expose CUPTI's newer `cudaEventSyncId`; event handles can be reused | Retained `CANN_API` / `TASK` schemas do not expose an event-handle identity that joins record to wait | Unsupported; do not pair by time |
| What work frontier does stream/device synchronize complete? | Context/stream scope can be observed, but the export does not enumerate a unique causal frontier | Host synchronize calls exist; stream identity/frontier is not retained on the host row | Region-only candidate for future work |
| Did synchronization cause a visible idle interval? | Neither correlation nor temporal overlap proves causality | Same | Unsupported; preserve separate observations |

## Retained-profile observations

CUDA TP2 node profile:

- 230 synchronization activity rows.
- 149 have one direct runtime candidate.
- 79 more have a unique interval-containing candidate after a reused
  `correlationId` selects multiple candidates.
- 2 retain multiple containing candidates and stay ambiguous.
- all rejected reused-ID candidates remain queryable.

Ascend kickstart profile:

- 5,333 direct one-to-one `aclrtRecordEvent -> EVENT_RECORD` relations.
- 3,548 direct one-to-one `aclrtStreamWaitEvent -> EVENT_WAIT` relations.
- some `connectionId` values attach one API to many capture/control tasks;
  cardinality must remain explicit rather than being flattened to 1:1.
- host `StreamSynchronize`, `DeviceSynchronize`, and `EventSynchronize` calls
  are observable even where no device task is directly related.

## First reliable slice

`traceloom_v_sync_runtime_call` exposes action-level runtime/device evidence:

- CUDA synchronization activity with decoded CUPTI kind;
- Ascend `EVENT_RECORD` and `EVENT_WAIT` tasks with their direct connection
  relation;
- exact, deterministic, ambiguous, rejected, and open outcomes without hiding
  multiplicity.

It deliberately does not yet implement `SyncRegion`, record-to-wait peer edges,
or idle attribution. Those remain #33 work and require stronger fixture or
provider evidence than the retained artifacts currently contain.
