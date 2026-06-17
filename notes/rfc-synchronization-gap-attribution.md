# RFC: TraceLoom Synchronization and Gap Attribution

Status: Draft for external review

Target: TraceLoom Ascend/CANN `msprof` analysis pipeline

Related issue: `notes/issues/synchronization-discovery.md`

## Summary

TraceLoom currently exposes prelude gaps and unattributed time around execution
anchors. This is useful because it shows where repeated model-execution
patterns contain hidden cost, but the current names and aggregates can make
unobserved time look like true hardware idle.

This RFC proposes a conservative synchronization and gap-attribution model. The
core idea is not to infer the complete internal CANN runtime state. Instead,
TraceLoom should reconstruct the strongest state that is supported by profiler
evidence, split gaps into auditable categories, and attach a confidence level to
each attribution.

The proposed first version is implementable using existing `msprof` SQLite
tables. It can attribute confirmed device-side wait tasks, communication tasks,
host-side synchronization overlap, and submitted-but-not-started task delay.
Precise stream/event dependency reconstruction remains out of reach without
additional instrumentation, because current `CANN_API` rows do not expose API
arguments such as stream handles and event handles.

## Background

TraceLoom reads Ascend/CANN `msprof` output, normalizes profiler rows into
events, compresses anchor sequences into repeated execution trees, and reports
node-level cost composition. Its current attribution model attaches auxiliary
and prelude activity to the following anchor.

Today, a gap before an anchor is summarized roughly as:

- activity in the prelude window, grouped by normalized task category;
- explicit wait task time when `TASK.taskType` looks like wait;
- communication task time;
- the remaining time, currently exposed as idle-like prelude gap.

This is already useful, but it is too coarse for synchronization diagnosis.
Large prelude or idle-like gaps may mean several different things:

- the target stream is waiting on an event, notify, collective, or queue
  dependency;
- the host is blocked in `aclrtSynchronize*` or related runtime calls;
- work was submitted but did not start immediately on device;
- the host did not submit work during the interval;
- the profiler did not expose the event needed to explain the gap;
- the device is genuinely idle.

Those cases have different engineering implications, so TraceLoom should stop
collapsing them into a single idle-style number.

## Current Observations

The current Ascend `msprof` schema contains enough information for a useful
partial solution.

In the compact TraceLoom kickstart profile, the relevant tables include:

- `TASK(startNs, endNs, deviceId, connectionId, globalTaskId, globalPid,
  taskType, contextId, streamId, taskId, modelId)`
- `CANN_API(startNs, endNs, type, globalTid, connectionId, name)`
- `STRING_IDS(id, value)`
- `COMMUNICATION_OP(...)`
- `COMMUNICATION_TASK_INFO(...)`
- `COMPUTE_TASK_INFO(...)`

Important profiler properties:

- `TASK` has device and stream identity.
- `TASK` has `connectionId`, which often links a device task back to a host
  `CANN_API` row.
- `CANN_API` has timing, thread identity, API type, API name, and
  `connectionId`.
- `CANN_API` does not expose stream handles, event handles, notify handles, or
  API arguments.

Empirical observations from the kickstart profile:

- `aclrtStreamWaitEvent` appears 44,321 times in `CANN_API`; 41,906 of those
  rows join to one or more `TASK` rows through `connectionId`, covering 104
  streams.
- `EVENT_WAIT` appears 3,548 times in `TASK`, totaling about 2.376 seconds of
  device-side wait task time.
- `NOTIFY_WAIT` appears 236 times in `TASK`, totaling about 96 ms.
- `CAPTURE_WAIT` appears 37,483 times in `TASK`. These rows are confirmed
  profiler-visible control tasks, but their synchronization meaning is opaque.
  They should be preserved as a separate capture/control category rather than
  hidden as transparent noise or merged into wait time.
- `aclrtSynchronizeStream` appears 501 times in `CANN_API`, but in this profile
  none of those rows join to a `TASK` row by `connectionId`.
- `aclrtSynchronizeDeviceWithTimeout` appears 1,215 times; 840 rows join to
  `MODEL_MAINTAINCE` tasks, but long blocking calls may still have no direct
  target stream evidence.

These observations imply a clear boundary:

- Some synchronization evidence is directly observable and should be reported
  as confirmed.
- Some synchronization evidence is host-visible but not stream-specific and
  should be reported as probable or contextual.
- Full stream/event dependency reconstruction is not possible from current
  tables alone.

## Problem

The current reporting surface has three issues.

First, unresolved gaps are too easy to overinterpret. A value called
`idle_us` may be read as true hardware idle, even when the interval overlaps
host synchronization, device wait tasks, or pending queue dependencies.

Second, TraceLoom does not currently maintain a first-class stream/device state
model. It has normalized events and anchor prelude windows, but it does not
persist the interval-level state that explains why a window was classified as
compute, communication, wait, host-blocked, queued, or unknown.

Third, CANN API records often lack the arguments needed for exact attribution.
For example, a `aclrtSynchronizeStream` row proves the host thread blocked in a
stream synchronization call, but current `CANN_API` rows do not expose which
stream was passed to that call. Without additional instrumentation, binding it
to a specific stream is heuristic.

## Goals

The design should:

- preserve all existing raw evidence and source links;
- distinguish confirmed facts from probable inferences;
- split prelude gaps into more meaningful categories;
- keep unresolved time visible as unresolved;
- avoid claiming precise stream/event dependencies when the profiler does not
  expose them;
- be useful without requiring patched CANN or patched `msprof`;
- provide a clean upgrade path for richer instrumentation later.

## Non-Goals

The first implementation should not:

- reverse engineer all internal CANN scheduler state;
- claim exact stream arguments for API calls that do not expose them;
- infer event-handle dependency graphs without event-handle evidence;
- depend on large private traces;
- require users to patch the Ascend software stack before getting useful
  reports.

## Proposed Design

Introduce an evidence-layered gap-attribution pipeline.

The pipeline should produce explicit interval slices for each anchor prelude
window. Each slice has:

- a time interval;
- a category;
- a confidence level;
- source links to raw profiler rows when available;
- an explanation string suitable for reports.

Recommended categories:

- `active_compute_us`: visible compute task time in the window.
- `active_comm_us`: visible communication or data-movement task time.
- `visible_wait_task_us`: visible device-side wait task time, such as
  `EVENT_WAIT` or `NOTIFY_WAIT`.
- `host_sync_api_overlap_us`: temporal presence of host-side synchronization
  APIs, such as `aclrtSynchronizeStream`,
  `aclrtSynchronizeDeviceWithTimeout`, `aclrtSynchronizeEvent`, or runtime
  equivalents. This means the host API overlaps the window; it does not prove
  that the host API caused the device-side gap.
- `queued_visible_task_delay_us`: time between host API submission and device
  task start, when `CANN_API.connectionId` joins to `TASK.connectionId`.
- `host_submit_activity_us`: host launch, memcpy, record, or wait API activity
  that may explain runtime overhead but is not itself device execution.
- `no_observed_submit_gap_us`: gap interval where TraceLoom observes no host
  submission or synchronization activity near the selected device timeline.
  This is an observation about profiler evidence, not proof that no host
  activity occurred.
- `unattributed_gap_us`: remaining unclassified time.

Recommended confidence levels:

- `confirmed`: direct evidence from `TASK`, `COMMUNICATION_OP`, or an exact
  `CANN_API.connectionId -> TASK.connectionId` join.
- `probable`: temporal overlap with a relevant host API, but without exact
  stream/event arguments.
- `heuristic`: inferred from same-thread neighborhood or repeated-pattern
  context.
- `unknown`: retained as unresolved time.

The key semantic rule is:

> TraceLoom may report that a gap overlaps host synchronization, or that a
> visible task is a wait task. TraceLoom must not report the precise waited-on
> stream or event unless the evidence exposes that relationship.

## Algorithm Sketch

### 1. Load Raw Evidence

Build three normalized evidence collections:

1. Device task events from `TASK`.
   Include device id, stream id, task type, connection id, global task id,
   start/end timestamps, and resolved labels.

2. Host API events from `CANN_API`.
   Include API name, API type, global thread id, connection id, start/end
   timestamps, and coarse API family.

3. Communication events from `COMMUNICATION_OP` and
   `COMMUNICATION_TASK_INFO`, when available.

### 2. Classify Events Conservatively

Classify device tasks into:

- `compute`
- `comm`
- `wait`
- `capture_control`
- `record`
- `control`
- `unknown`

Classify host APIs into:

- `submit_kernel`
- `submit_memcpy`
- `record_event`
- `stream_wait_event`
- `stream_synchronize`
- `device_synchronize`
- `event_synchronize`
- `query`
- `allocation`
- `other`

This classification should eventually share infrastructure with the
signal-classification ruleset proposed in `notes/issues/signal_classification_rule.md`.

`CAPTURE_WAIT` should map to `capture_control` by default. It is confirmed as a
profiler-visible task, but its interpretation should remain separate from
semantic synchronization waits until additional evidence justifies promotion.

### 3. Build Observable Stream State

For each `(db_idx, device_id, stream_id)`, construct a timeline of visible
stream states from `TASK`:

- `running_compute`
- `running_comm`
- `running_wait`
- `running_capture_control`
- `running_control`
- `empty_observed`

This is not the real runtime queue state. It is the profiler-visible task
state.

### 4. Build Host-to-Device Links

Use `connectionId` as the strongest bridge:

```text
CANN_API.connectionId == TASK.connectionId
```

For each successful join, persist:

- API timing and task timing;
- API name and task type;
- thread id;
- stream id from the task;
- delay from API end to task start;
- overlap between API interval and task interval.

This supports confirmed attribution for submitted work whose device task is
visible.

### 5. Slice Prelude Windows

For each anchor prelude window `[prelude_start_ns, anchor_start_ns)`, perform
interval slicing in priority order:

1. Visible device task intervals.
2. Confirmed host-to-device submission delay intervals.
3. Host synchronization API presence intervals.
4. Host submit/record/query activity overlaps.
5. Residual time.

The priority order avoids double-counting. For example, if a visible
`EVENT_WAIT` task occupies an interval, that interval should be counted as
`visible_wait_task_us` before it is considered host synchronization API
presence.

### 6. Aggregate to Anchors and Nodes

Attach gap slices to the following anchor, matching the existing prelude
attribution model.

Then aggregate slice durations through existing node-to-anchor coverage:

- anchor-level gap slice table;
- symbol-level summaries;
- node-level totals and averages;
- run-level summary.

## Proposed Output Schema

Add experimental tables to the augmented DB.

### `traceloom_host_api_event`

Normalized host API events.

Important columns:

- `api_event_id`
- `db_idx`
- `start_ns`
- `end_ns`
- `dur_us`
- `global_tid`
- `connection_id`
- `api_type`
- `api_name`
- `api_family`
- `source_table`
- `source_key`

### `traceloom_task_api_link`

Confirmed links from host API rows to device tasks.

Important columns:

- `api_event_id`
- `event_id`
- `db_idx`
- `device_id`
- `stream_id`
- `connection_id`
- `api_name`
- `task_type`
- `submit_to_start_us`
- `api_task_overlap_us`
- `confidence`

For first implementation, rows in this table should be `confirmed`, because
they require direct `connectionId` evidence.

### `traceloom_gap_slice`

Interval-level gap attribution attached to anchors.

Important columns:

- `gap_slice_id`
- `anchor_id`
- `db_idx`
- `device_id`
- `stream_id`
- `start_ns`
- `end_ns`
- `dur_us`
- `category`
- `confidence`
- `reason`
- `source_event_id`
- `source_api_event_id`
- `source_key`

Example categories:

- `active_compute`
- `active_comm`
- `visible_wait_task`
- `capture_control`
- `host_sync_api_overlap`
- `queued_visible_task_delay`
- `host_submit_activity`
- `no_observed_submit_gap`
- `unattributed_gap`

### Views

Add convenience views:

- `traceloom_v_anchor_gap_cost`
- `traceloom_v_node_gap_cost`
- `traceloom_v_sync_hotspot`

These should expose both total durations and confidence breakdowns.

## Reporting Changes

Update human-readable reports to avoid overclaiming.

Recommended terminology:

- Keep `prelude_gap_us` as the total pre-anchor interval.
- Deprecate or qualify `prelude_idle_us`.
- Add `unattributed_gap_us`.
- Add `visible_wait_task_us`.
- Add `capture_control_us`.
- Add `host_sync_api_overlap_us`.
- Add `queued_visible_task_delay_us`.

For compatibility, `idle_us` can remain in existing CSVs for one release, but
the generated summary should explain that it means unresolved or uncovered
prelude time, not proven hardware idle.

Example report language:

```text
This node has 8.4 ms/occurrence of prelude gap. Of that, 5.7 ms is confirmed
device wait task time, 1.1 ms overlaps host stream synchronization API calls,
and 1.6 ms remains unattributed. The overlap is temporal presence, not proof
that the host API caused the device-side gap. The stream/event dependency for
the host sync calls is not available in the current msprof schema.
```

## Expected Effects

The proposed design should improve TraceLoom in several ways.

First, it reduces false idle diagnoses. Users will see when a gap is backed by
explicit wait tasks or host synchronization calls.

Second, it makes synchronization cost structurally comparable. Because slices
attach to anchors and aggregate through the existing loop tree, users can ask
which repeated pattern contains the most wait-like behavior.

Third, it creates an auditable path from high-level reports back to raw rows.
Every confirmed slice should link to a `TASK`, `CANN_API`, or communication
row.

Fourth, it makes profiler limitations visible. Instead of hiding uncertainty,
TraceLoom can report that the current schema lacks stream/event arguments for
some host synchronization calls.

## Expected Benefits

For model-serving developers:

- clearer separation between compute, communication, wait, capture/control,
  host sync API presence, queue delay, and unknown time;
- better prioritization of synchronization bottlenecks in repeated decode
  loops;
- fewer misleading "device idle" conclusions;
- SQL drill-down paths for suspicious synchronization regions.

For TraceLoom maintainers:

- a clean state model that can absorb richer profiler signals later;
- a natural home for future signal-classification rules;
- a stable schema for tests and external reviewers;
- a way to compare traces collected with and without extra instrumentation.

For systems researchers:

- explicit evidence levels make claims easier to evaluate;
- repeated-pattern aggregation turns low-level synchronization noise into
  workload-level structure;
- the design distinguishes observability limitations from algorithmic
  limitations.

## Instrumentation Extension

The current profiler schema is insufficient for exact stream/event dependency
graphs. To make attribution precise, TraceLoom would benefit from an optional
instrumentation channel that records CANN API arguments.

Minimum useful fields:

- API name;
- start/end timestamp;
- host process and thread id;
- device id, if known;
- stream handle or stream id;
- event or notify handle;
- connection id, if available;
- return status;
- optional queue depth or enqueue sequence number.

High-priority APIs:

- `aclrtLaunchKernel*`
- `aclrtMemcpyAsync*`
- `aclrtRecordEvent`
- `aclrtStreamWaitEvent`
- `aclrtSynchronizeStream`
- `aclrtSynchronizeDevice*`
- `aclrtSynchronizeEvent`
- notify create/import/record/wait APIs, if exposed by the stack.

Potential implementation approaches:

- LD_PRELOAD or wrapper interception around ACL runtime APIs;
- vLLM/vLLM-Ascend instrumentation at known call sites;
- patched `msprof`;
- patched ACL/CANN runtime emitting an auxiliary trace table.

The first implementation should not depend on this instrumentation, but the
schema should be designed so instrumented dependency edges can be added later.

## Validation Plan

Use three classes of validation traces.

1. Synthetic microbenchmarks.
   Build small Ascend programs with known patterns:
   - stream wait event;
   - stream synchronize;
   - device synchronize;
   - async memcpy followed by wait;
   - multiple streams with record/wait dependencies.

2. Existing compact TraceLoom kickstart profile.
   Use it to ensure the new logic works on a real vLLM-Ascend trace and does
   not regress current loop-tree outputs.

3. Instrumented comparison traces.
   When wrapper or patched-runtime data is available, compare heuristic
   attribution against ground-truth stream/event arguments.

Success criteria:

- confirmed wait task time matches `TASK` evidence;
- confirmed API-task links match `connectionId` joins;
- no host sync call without stream arguments is reported as exact stream wait;
- total gap slices sum to the original prelude gap within rounding tolerance;
- repeated-node aggregation is stable and explainable.

## Implementation Plan

### Milestone 0: Naming and Documentation

- Rename or qualify unresolved idle-like fields in docs.
- Add report notes explaining that unresolved gap is not proven hardware idle.
- Keep existing fields for compatibility.

### Milestone 1: Host API Loader

- Add a reader for `CANN_API` and API-name resolution through `STRING_IDS`.
- Normalize API families.
- Persist optional host API events into the augmented DB.

### Milestone 2: Confirmed API-to-Task Links

- Join `CANN_API` and `TASK` by `connectionId`.
- Persist confirmed host-to-device links.
- Compute submission-to-start delay and API/task overlap.

### Milestone 3: Gap Slice Attribution

- Replace scalar-only prelude accounting with interval slices.
- Preserve current aggregate columns by deriving them from slices.
- Add anchor-level and node-level gap views.

### Milestone 4: Reports and SQL

- Add summary tables for top synchronization-heavy nodes.
- Add starter SQL queries:
  - top wait-task nodes;
  - top host-sync API presence nodes;
  - top capture-control nodes;
  - top unattributed-gap nodes;
  - API-to-task delay outliers.

### Milestone 5: Optional Instrumentation

- Define an auxiliary trace schema for API arguments.
- Support importing that schema into the augmented DB.
- Upgrade selected `probable` and `heuristic` slices to `confirmed` when
  stream/event handles are available.

## Risks and Tradeoffs

The biggest risk is over-attribution. Temporal overlap is not causality. The
confidence model is therefore part of the design, not a reporting nicety.

Another risk is schema churn. Gap attribution should be introduced under
experimental table names first, while preserving existing outputs.

There is also a cost-model risk. If slices are attached only to the following
anchor, some cross-anchor synchronization behavior may be assigned to a nearby
anchor rather than the semantic source of the dependency. This is consistent
with current prelude attribution but should be documented.

Finally, host API volume can be high. The implementation should keep SQL
indexes on timestamp, connection id, API family, device id, and stream id where
available.

## Open Questions

- How should TraceLoom handle nested ACL/runtime API rows that describe the
  same operation at different abstraction layers?
- Should host synchronization API presence be attached to all devices in the
  same DB, or only devices with nearby task evidence?
- What threshold should distinguish meaningful submission delay from normal
  launch latency?
- Should the first version expose `idle_us` as a compatibility alias for
  `unattributed_gap_us`, or remove it from new views entirely?

## Recommendation

Proceed with the evidence-layered design.

The problem is partially solvable with current `msprof` output and becomes much
more precise with optional instrumentation. The right first step is not to
claim exact CANN runtime state. The right first step is to make TraceLoom's
existing gap accounting more explicit, auditable, and conservative.

This should produce immediate user value while keeping a clean path toward
precise stream/event dependency reconstruction when richer CANN API argument
data is available.
