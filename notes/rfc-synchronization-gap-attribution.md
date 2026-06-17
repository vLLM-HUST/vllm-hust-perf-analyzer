# RFC: Explaining Visible Device Idle in TraceLoom

Status: Draft v2 for external review

Target: TraceLoom Ascend/CANN `msprof` analysis pipeline

Related issue: `notes/issues/synchronization-discovery.md`

## Summary

TraceLoom already compresses visible compute and communication work into a
structured execution timeline. The next step should be narrower and simpler:

```text
Find visible device idle intervals, then explain why they look idle.
```

This RFC revises the earlier gap-attribution design around one core model:

1. Build a global productive device timeline from visible compute,
   communication, and data-movement work.
2. Extract visible productive idle intervals from gaps in that global timeline.
3. Build per-stream observable state timelines from all visible stream tasks.
4. Explain each global idle interval by projecting it onto per-stream states
   and host/device evidence.
5. Aggregate idle explanations back to anchors, prelude windows, symbols, and
   loop-tree nodes.

Prelude attribution becomes a consumer of this model, not the foundation of the
model. This keeps the design focused: TraceLoom is not trying to reconstruct
the full CANN runtime scheduler. It is labeling visible productive idle using
observable evidence.

## Key Terms

`productive task`: A visible device task that represents useful compute,
communication, or data movement. Examples include AI Core kernels, AI Vector
kernels, HCCL/communication work, and memcpy/data movement.

`non-productive visible task`: A visible device task that occupies a stream but
does not represent useful model work. Examples include `EVENT_WAIT`,
`NOTIFY_WAIT`, `CAPTURE_WAIT`, record tasks, and runtime control tasks.

`global productive timeline`: The union of productive task intervals for a
device. This is the timeline TraceLoom uses to describe visible model progress.

`visible productive idle`: A gap in the global productive timeline. This does
not prove the hardware is idle. It means TraceLoom sees no productive
compute/communication/data-movement task covering that interval.

`observable stream state`: The state TraceLoom can infer from profiler-visible
tasks on a stream. It is not the real internal runtime queue state.

`idle explanation`: A conservative label attached to a visible productive idle
interval, derived from stream-state projection and host/device evidence.

## Current Observations

The current Ascend `msprof` schema is sufficient for a useful first version.

Relevant tables include:

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

Empirical observations from the compact TraceLoom kickstart profile:

- `aclrtStreamWaitEvent` appears 44,321 times in `CANN_API`; 41,906 of those
  rows join to one or more `TASK` rows through `connectionId`, covering 104
  streams.
- `EVENT_WAIT` appears 3,548 times in `TASK`, totaling about 2.376 seconds of
  device-side visible wait task time.
- `NOTIFY_WAIT` appears 236 times in `TASK`, totaling about 96 ms.
- `CAPTURE_WAIT` appears 37,483 times in `TASK`. These rows are confirmed
  profiler-visible control tasks, but their synchronization meaning is opaque.
  They should be preserved as a separate capture/control state rather than
  hidden as noise or merged into semantic wait time.
- `aclrtSynchronizeStream` appears 501 times in `CANN_API`, but in this profile
  none of those rows join to `TASK` rows by `connectionId`.
- `aclrtSynchronizeDeviceWithTimeout` appears 1,215 times; 840 rows join to
  `MODEL_MAINTAINCE` tasks, but long blocking calls may still have no direct
  target-stream evidence.

These observations imply a boundary:

- Some idle explanations are directly observable and can be reported as
  confirmed.
- Some evidence is host-visible but not stream-specific and should be reported
  as contextual presence.
- Exact stream/event dependency reconstruction is not possible from current
  tables alone.

## Problem

TraceLoom currently exposes prelude gaps and idle-like cost around anchors, but
the underlying model is too attached to anchor/prelude accounting. That makes
the design harder to reason about.

The real question is simpler:

```text
When the global productive device timeline has a gap, what observable stream
and host states explain that gap?
```

Without this model, reports risk implying that a gap is true hardware idle.
In reality, a visible productive idle interval may be explained by:

- device streams running visible wait tasks;
- capture/control tasks;
- host synchronization API presence;
- submitted work waiting before device task start;
- no observed productive work on any stream;
- profiler blind spots;
- genuinely idle hardware.

The analyzer should preserve these distinctions instead of collapsing them into
a scalar `idle_us`.

## Goals

The design should:

- make visible productive idle a first-class timeline object;
- model per-stream observable state independently from anchors and prelude
  windows;
- explain device-level idle by querying stream states and host/device evidence;
- distinguish facts from inferences with explicit confidence;
- keep unknown time visible;
- avoid claiming exact stream/event dependencies when the profiler does not
  expose them;
- aggregate idle explanations back into the existing TraceLoom loop tree.

## Non-Goals

The first implementation should not:

- reconstruct the full internal CANN scheduler state;
- claim exact stream arguments for APIs that do not expose them;
- infer event-handle dependency graphs without event-handle evidence;
- require patched CANN or patched `msprof`;
- make prelude attribution the lowest-level state model.

## Proposed Model

### Layer 1: Global Productive Device Timeline

For each `(db_idx, device_id)`, TraceLoom builds one global productive timeline
by taking the interval union of visible productive tasks:

- compute kernels;
- collective or communication tasks;
- memcpy and data movement tasks that represent useful work.

The complement of this union within the analyzed device span is the set of
visible productive idle intervals.

This definition is deliberately narrow. If a stream is running `EVENT_WAIT`,
that interval is not productive model work. Therefore it can explain visible
productive idle rather than remove it from the idle set.

Recommended output concepts:

- `productive_active_interval`
- `visible_productive_idle_interval`
- `productive_active_us`
- `visible_productive_idle_us`

### Layer 2: Per-Stream Observable State Timelines

For each `(db_idx, device_id, stream_id)`, TraceLoom builds a stream-state
timeline from all visible `TASK` rows.

Suggested observable states:

- `running_compute`
- `running_comm`
- `running_data_move`
- `running_wait`
- `running_capture_control`
- `running_record`
- `running_runtime_control`
- `empty_observed`
- `unknown`

`empty_observed` means TraceLoom sees no task on that stream in the interval.
It does not mean the runtime queue is empty. The real state may include queued
work that the profiler does not expose yet.

`running_capture_control` should include `CAPTURE_WAIT` by default. This is a
confirmed visible state, but its synchronization meaning remains separate from
semantic wait until additional evidence justifies promotion.

### Layer 3: Idle Explanation

For each global visible productive idle interval, TraceLoom projects the
interval onto:

- per-stream observable states;
- host API intervals from `CANN_API`;
- confirmed host-to-device links through `connectionId`;
- communication metadata when available.

The result is an interval-level explanation with a category and confidence.

Recommended explanation categories:

- `blocked_by_visible_wait`: one or more streams have visible wait tasks such
  as `EVENT_WAIT` or `NOTIFY_WAIT`.
- `capture_control_present`: one or more streams have `CAPTURE_WAIT` or related
  capture/control tasks.
- `runtime_control_present`: one or more streams have visible runtime control
  tasks that are neither productive nor semantic wait.
- `host_sync_api_present`: host-side synchronization APIs temporally overlap
  the idle interval. This is presence, not causality.
- `queued_visible_task_delay`: a host API is linked to a later visible device
  task through `connectionId`, and the interval covers API-to-task-start delay.
- `no_observed_device_work`: all relevant streams are `empty_observed`, and no
  stronger host/device evidence explains the interval.
- `unattributed_visible_idle`: remaining time with insufficient evidence.

Recommended confidence levels:

- `confirmed`: direct evidence from `TASK`, `COMMUNICATION_OP`, or an exact
  `CANN_API.connectionId -> TASK.connectionId` join.
- `contextual`: temporal presence of relevant host APIs without exact
  stream/event arguments.
- `heuristic`: inferred from same-thread neighborhood or repeated-pattern
  context.
- `unknown`: unresolved after available evidence is applied.

The core rule is:

> TraceLoom explains visible productive idle with observable evidence. It does
> not claim true hardware idle, exact stream dependencies, or causality unless
> the evidence exposes those facts.

## Algorithm Sketch

### 1. Normalize Device Tasks

Load `TASK` rows and resolve task type and labels through `STRING_IDS`,
`COMPUTE_TASK_INFO`, `COMMUNICATION_TASK_INFO`, and communication metadata.

Classify each task into:

- `productive_compute`
- `productive_comm`
- `productive_data_move`
- `visible_wait`
- `capture_control`
- `record`
- `runtime_control`
- `unknown`

### 2. Build the Global Productive Timeline

For each device:

1. Select productive task intervals.
2. Compute their union.
3. Compute gaps between union intervals within the selected analysis span.
4. Store those gaps as visible productive idle intervals.

The analysis span should be configurable. Reasonable first choices:

- from first productive task start to last productive task end;
- or from first anchor start to last anchor end for anchor-scoped analysis.

### 3. Build Per-Stream State Timelines

For each stream:

1. Sort visible tasks by time.
2. Convert task intervals into observable states.
3. Fill uncovered intervals with `empty_observed`.
4. Preserve overlap or ambiguity explicitly when profiler rows overlap in a
   way that cannot be linearly ordered.

This is the stream-state layer that later explains device-level idle.

### 4. Build Host Evidence

Normalize `CANN_API` rows into host API events:

- API name;
- API family;
- global thread id;
- connection id;
- start/end timestamps.

Join host API events to device tasks by `connectionId` when possible. These
links are the strongest evidence for submitted work and API-to-task delay.

Host synchronization APIs without stream arguments should remain contextual:

- `aclrtSynchronizeStream`
- `aclrtSynchronizeDeviceWithTimeout`
- `aclrtSynchronizeEvent`
- runtime equivalents such as `StreamSynchronize`, `DeviceSynchronize`, and
  `EventSynchronize`

### 5. Explain Idle Intervals

For each visible productive idle interval:

1. Query overlapping per-stream states.
2. Query overlapping host API evidence.
3. Query API-to-task delay links.
4. Slice the idle interval into explanation intervals using a deterministic
   priority order.

Suggested priority order:

1. `blocked_by_visible_wait`
2. `capture_control_present`
3. `runtime_control_present`
4. `queued_visible_task_delay`
5. `host_sync_api_present`
6. `no_observed_device_work`
7. `unattributed_visible_idle`

This order intentionally gives device-visible states priority over host API
presence. Temporal overlap with a host synchronization API is useful evidence,
but overlap is not causality.

### 6. Aggregate Explanations Upward

After idle intervals are explained, aggregate them into existing TraceLoom
surfaces:

- anchor prelude windows;
- anchor symbols;
- loop-tree nodes;
- device summary rows;
- SQL reports.

This keeps the state model reusable. Prelude attribution becomes one report
view over the idle explanation table.

## Proposed Output Schema

Add experimental tables under the `traceloom_*` namespace.

### `traceloom_device_interval`

Device-level productive active and visible productive idle intervals.

Important columns:

- `interval_id`
- `db_idx`
- `device_id`
- `start_ns`
- `end_ns`
- `dur_us`
- `interval_kind`: `productive_active` or `visible_productive_idle`
- `source_count`

### `traceloom_stream_state`

Per-stream observable state intervals.

Important columns:

- `state_id`
- `db_idx`
- `device_id`
- `stream_id`
- `start_ns`
- `end_ns`
- `dur_us`
- `state`
- `confidence`
- `source_event_id`
- `source_key`

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
- `confidence`

Rows in this table require direct `connectionId` evidence and should therefore
start as `confirmed`.

### `traceloom_idle_explanation`

Explanation slices for visible productive idle intervals.

Important columns:

- `idle_explanation_id`
- `interval_id`
- `db_idx`
- `device_id`
- `start_ns`
- `end_ns`
- `dur_us`
- `category`
- `confidence`
- `reason`
- `source_state_id`
- `source_api_event_id`
- `source_event_id`

Example categories:

- `blocked_by_visible_wait`
- `capture_control_present`
- `runtime_control_present`
- `queued_visible_task_delay`
- `host_sync_api_present`
- `no_observed_device_work`
- `unattributed_visible_idle`

### Views

Recommended views:

- `traceloom_v_device_idle_summary`
- `traceloom_v_anchor_idle_explanation`
- `traceloom_v_node_idle_explanation`
- `traceloom_v_idle_hotspot`

These views should expose total duration, average duration per occurrence, and
confidence breakdowns.

## Reporting Changes

Reports should make the new semantic boundary explicit.

Recommended terminology:

- Use `visible_productive_idle_us` for device-level gaps in productive work.
- Use `unattributed_visible_idle_us` for unexplained residual time.
- Avoid using `idle_us` without a qualifier.
- Keep old `idle_us` columns temporarily as compatibility aliases, but document
  them as unresolved visible productive idle rather than proven hardware idle.

Example report language:

```text
Node N017 has 8.4 ms/occurrence of visible productive idle. Of that, 5.7 ms is
confirmed visible wait-task time, 0.8 ms is capture/control presence, 1.1 ms
has host synchronization API presence, and 0.8 ms remains unattributed. Host
sync API presence is temporal context, not proof of causality.
```

## Expected Benefits

This design gives TraceLoom a clearer backbone.

For users:

- fewer misleading device-idle conclusions;
- a direct answer to "why does the device-level productive timeline have a
  gap?";
- SQL drill-down from loop nodes to stream states and host API evidence;
- clearer distinction between wait, capture/control, host API presence, queue
  delay, and unknown time.

For implementation:

- prelude attribution becomes simpler because it consumes idle explanations;
- stream/device state modeling is reusable outside prelude windows;
- the schema has a clean place for future richer instrumentation;
- existing loop-tree aggregation remains compatible.

For research:

- the model separates timeline construction from observability semantics;
- confidence levels make claims easier to audit;
- repeated-pattern aggregation turns low-level idle explanations into
  workload-level structure.

## Instrumentation Extension

The current `CANN_API` table does not expose API arguments, so exact stream and
event dependency reconstruction is limited. Optional instrumentation can
upgrade contextual or heuristic explanations to confirmed explanations.

Minimum useful fields:

- API name;
- start/end timestamp;
- host process and thread id;
- device id, if known;
- stream handle or stream id;
- event or notify handle;
- connection id, if available;
- return status;
- optional enqueue sequence number or queue depth.

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

## Validation Plan

Use three validation classes.

1. Synthetic microbenchmarks:
   - stream wait event;
   - stream synchronize;
   - device synchronize;
   - async memcpy followed by wait;
   - multiple streams with record/wait dependencies.

2. Existing compact TraceLoom kickstart profile:
   - verify that visible productive idle intervals are stable;
   - verify that `EVENT_WAIT`, `NOTIFY_WAIT`, and `CAPTURE_WAIT` are separated;
   - verify that old prelude totals can be derived from new idle explanations.

3. Instrumented comparison traces:
   - compare contextual host sync API presence against ground-truth stream and
     event arguments when instrumentation is available.

Success criteria:

- visible productive idle intervals plus productive active intervals cover the
  selected device analysis span within rounding tolerance;
- visible wait and capture/control explanations match `TASK` evidence;
- host API presence is never reported as causality without dependency evidence;
- prelude and loop-node reports can be derived from the new tables;
- unexplained time remains explicitly visible.

## Implementation Plan

### Milestone 0: Naming and Compatibility

- Qualify existing idle-like fields in docs.
- Keep old columns as aliases where necessary.
- Introduce `visible_productive_idle` terminology.

### Milestone 1: Productive Timeline

- Classify productive compute/communication/data-movement tasks.
- Build per-device productive interval unions.
- Emit visible productive idle intervals.

### Milestone 2: Stream State Timeline

- Build per-stream observable state intervals.
- Preserve wait, capture/control, record, runtime-control, and unknown states.
- Add stream-state SQL tables and basic views.

### Milestone 3: Idle Explanation

- Project each visible productive idle interval onto stream states.
- Add host API presence and API-to-task delay evidence.
- Emit `traceloom_idle_explanation`.

### Milestone 4: Aggregation and Reports

- Derive anchor prelude idle explanations from device idle explanations.
- Aggregate idle categories to symbols and loop-tree nodes.
- Add top idle-hotspot SQL reports.

### Milestone 5: Optional Instrumentation

- Define an auxiliary API-argument trace schema.
- Import instrumented stream/event handle data.
- Upgrade contextual explanations to confirmed explanations when possible.

## Risks and Tradeoffs

`visible productive idle` may still be misread as true hardware idle. The
reports must repeatedly state that it is a gap in productive profiler-visible
work, not proof that hardware resources are unused.

Temporal host API presence is not causality. It should remain contextual unless
instrumentation or profiler evidence exposes a dependency edge.

The global productive timeline intentionally excludes visible wait and control
tasks. This is useful for explaining lack of productive progress, but it means
TraceLoom should also expose a separate "all visible task" coverage metric for
users who want raw profiler occupancy.

The stream-state layer may increase output volume. The first version should
index by device, stream, start/end time, state, and source id.

## Open Questions

- Should the global productive timeline include memcpy/data movement by
  default, or should users choose whether data movement counts as productive?
- What threshold should distinguish meaningful API-to-task delay from normal
  launch latency?
- How should overlapping tasks on the same stream be represented if the
  profiler produces ambiguous intervals?
- Should host sync API presence attach to all devices in the DB, only devices
  with nearby task evidence, or only devices with thread/context evidence?
- Should compatibility `idle_us` map to `visible_productive_idle_us` or to
  `unattributed_visible_idle_us` in new views?

## Recommendation

Proceed with the simplified idle-explanation design.

The previous evidence-layered RFC had the right safety principles, but it was
too centered on prelude gap slicing. The revised design gives TraceLoom a
clearer backbone:

```text
global productive timeline
  -> visible productive idle intervals
  -> per-stream observable state projection
  -> idle explanations
  -> anchor and loop-tree aggregation
```

This keeps the system honest about what is observable while giving users a much
clearer answer to the practical question: why does the device-level productive
timeline stop making progress here?
