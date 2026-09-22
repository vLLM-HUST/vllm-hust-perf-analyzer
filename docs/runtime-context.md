# Import scheduler context into AugDB

TraceLoom can optionally consume scheduling context alongside a profiler input:

```bash
traceloom /path/to/profile.db --output analysis.db \
  --context /path/to/scheduler-producer.jsonl \
  --context /path/to/worker-producer.jsonl
```

Repeat `--context` for additional producers. File names emitted by the vLLM
adapter are UUIDs; the descriptive names above are placeholders. This option
belongs to augmented-database analysis, not `export-perfetto` or sidecar-only
mode. Without it, the existing analysis and database surfaces are unchanged.
Profiler inputs and context files are read-only. All context parsing, validation
and relation construction occur before atomic AugDB publication; an invalid
context does not replace an existing output. Output may not overwrite an input.

## Scheduler-only plugin versus execution-linked capture

The package now provides `traceloom_vllm_scheduler.TracingScheduler` and
`TracingAsyncScheduler` for vLLM's `--scheduler-cls` argument. This is the
patch-free collection route; see the adapter README for installation and async
mode selection. It records the final scheduler result without mutating or
extending `SchedulerOutput`. Session metadata explicitly identifies
`association_contract: scheduler_only`.

Scheduler-only JSONL is valid input to the same importer. Step/request/cache
queries work, including empty decisions; `recorded_executions=0` means no supplied
execution record, NOT no device work. No worker markers or device associations
are inferred from ordinals, timestamps, or the absence of execution records.
The identity-transport and profiler-association contract below applies only to
the separate execution-linked mode, which still requires its explicit patch.

## What v1 records

The opt-in vLLM adapter is under [`integrations/vllm`](../integrations/vllm/README.md).
It records the **final returned scheduling decision before executor dispatch**,
not a guessed step cut or GPU-completion snapshot:

- run identity, scheduler-producer identity, unique step identity and local ordinal;
- pseudonymous request IDs and per-request scheduled token counts;
- request payload kind (`new` or `cached`, not first-ever admission), observed
  prompt length and `computed_tokens_after_schedule` when available;
- finished, preempted and resumed request IDs where the corresponding scheduling
  output exposes them (missing information stays null, not an empty list);
- free blocks and total pool blocks **after schedule**; pool capacity can include
  reserved blocks, so the importer does not derive a utilization percentage;
- an allowlisted scheduler/cache configuration and installed vLLM package
  version once per scheduler producer;
- worker execution IDs, execution phase (`execute_model` / `sample_tokens`),
  rank when known, marker emission state, and whether the
  host call returned or raised. `returned` does not assert accelerator completion.

`num_computed_tokens` may have been advanced optimistically by the scheduler;
it is an observed post-schedule field, not a measurement of completed device
work. Prefix hit rate, allocated/freed deltas, per-block ownership and full
cache contents are **not** inferred from the two pool counters. No prompt,
output token values, KV payload, model path, credentials or arbitrary full
configuration is intentionally exported. Pseudonyms are stable only within one
scheduler producer, not cross-run or global request identities.

## Association contract: identity first, then a bounded host scope

1. A real dataclass field on `SchedulerOutput` carries `{run_id, step_id}` through
   serialization. Arbitrary dynamic Python attributes are not accepted.
2. The common worker-wrapper execution hook creates a unique execution ID and
   `torch.profiler.record_function("traceloom.execution.<execution_id>")` scope.
   One decision may lead to several independently identified worker invocations.
3. The importer matches that **exact marker name** in embedded Ascend
   `PYTORCH_API` evidence. It supports the `name/startNs/endNs/globalTid` layout,
   inline text names or same-source `STRING_IDS(id,value)` decoding, canonical
   decimal-text or integer nanosecond timestamps (without floating-point
   conversion), and an
   auditable source row ID. No nearest-time join establishes identity.
4. With one unambiguous marker, runtime calls can enter its host scope only
   through the same profiler source, clock domain, global thread ID, and full
   host-interval containment. This never compares the exporter's monotonic
   observation time with profiler clocks. Calls lying in multiple admitted
   execution scopes remain ambiguous rather than being assigned twice.
5. On the audited Ascend layout, `PYTORCH_API` queue records (type 50002,
   `Enqueue@` / `Dequeue@`) and `CONNECTION_IDS` explicitly bridge submission
   threads. An enqueue must be contained in the marked thread; one unambiguous
   shared queue correlation ID must identify its dequeue in the same process
   and profiler source. CANN calls must be contained in that dequeue's thread.
   The dequeue may occur after the marker ends. No queue-index or nearest-time
   matching is used. Source rows and correlation IDs are retained in
   `traceloom_context_queue_runtime`.
6. Existing `supported_exact` / `supported_deterministic` provider correlations
   connect those calls to device work and its retained event/raw-row identity.
   Device intervals are unchanged and may extend beyond the host marker.
   A device event with competing step identities is excluded from the supported
   work view and remains `ambiguous_step` in global device coverage.

7. A provider-supported graph launch retains its exact launch occurrence ID.
   When replay reconstruction supplies an exact body, that identity extends to
   `(launch_id, member_id, db_idx, device_id)` and the member's retained event.
   Capture-time operator calls and replay-time timestamps are **not** used as
   substitutes for this relation. Conflicts with direct event assignments remain
   ambiguous. Incomplete replay waves and best-effort overlap bodies do not gain
   exact membership just because their launch is inside a marker.

This first adapter deliberately requires same-source Ascend host evidence.
Other provider marker layouts, cross-file host clocks, work submitted by another
thread without explicit queue evidence, unwrapped later dispatch and graph
members without exact reconstructed launch membership are not silently covered. A missing or unsupported layout produces `marker_not_found`; an exporter
marker failure produces `marker_unavailable`; absent scheduling records and
multiple matching marker rows produce `missing_step` and `ambiguous_marker`.

**A marker association is not complete semantic step membership.** The coverage
view says `not_complete_step_membership`, reports linked runtime/device counts,
and retains capture state. These records do not create new grammar nodes, change
layer matches, certify step boundaries, or establish critical-path/causal edges.
A genuine empty decision is retained even when it has no execution record.

## Public queries

The normal surface and recipe catalogs expose the new relations. Start with:

```sql
SELECT * FROM traceloom_v_scheduler_step_context
WHERE run_id = :run_id ORDER BY scheduler_id, ordinal;

SELECT * FROM traceloom_v_context_coverage
WHERE run_id = :run_id AND step_id = :step_id;

SELECT * FROM traceloom_v_context_device_work
WHERE run_id = :run_id AND step_id = :step_id;

-- Reverse from an anomalous retained event to supplied scheduling context.
SELECT w.*, s.total_scheduled_tokens, s.scheduled_requests,
       s.free_blocks_after_schedule, s.pool_blocks_after_schedule
FROM traceloom_v_context_device_work w
JOIN traceloom_scheduler_step s USING (run_id, step_id)
WHERE w.event_id = :event_id;

-- Per-request shape fields are retained without reconstructing input text.
SELECT * FROM traceloom_scheduler_request
WHERE run_id = :run_id AND step_id = :step_id;

-- Audit exact imported lines and input completeness.
SELECT s.source_path, s.capture_state, r.line_no, r.raw_json
FROM traceloom_context_record r
JOIN traceloom_context_source s USING (source_id)
WHERE r.source_id = :source_id ORDER BY r.line_no;
```

Recipes `step_execution_context`, `step_device_work`, and
`event_scheduler_context` include selectors and result coordinates. The first
uses a left join so a recorded step without an execution does not disappear.
The other two expose only the supported device links, not zero-valued costs
for missing evidence. Context-source line IDs, marker profiler-source row IDs,
and ordinary runtime/device locators remain independently auditable.

## Wire contract and reliability

UTF-8 JSONL, one producer per file, newline-terminated records. Every line has
`schema: "traceloom.scheduler.v1"`, `run_id`, `producer_id`, and `type`.
The first record is `session` with object `metadata`. Interior records are
`step` or `execution`; the optional final `summary` contains `written_records`
and `dropped_records`. Identifiers are scoped explicitly; an execution refers
to its scheduling decision by `(run_id, step_id)`, not a timestamp or ordinal.

The importer embeds original record text, source path, consumed-byte SHA-256,
line numbers and normalized query tables. It rejects unknown versions/types,
duplicate producer/step/execution identities, invalid required fields, negative
counters, duplicate requests, inconsistent token totals, incorrect summaries,
records after summary and torn final lines. Missing referenced steps are instead
retained as explicit incomplete evidence. Limits: 256 MiB per file, 1 MiB per
line and 4,096 scheduled requests per step.

The exporter uses a bounded asynchronous queue (256 records) and exclusive 0600
producer files; it never waits for queue capacity. Loss is counted in the final
summary. A closed file with losses is `dropped_records`; no summary is `unclosed`.
Recorder failure warns once and must not alter scheduler decisions or swallow
model exceptions. This is not a zero-overhead claim: producer setup, snapshotting,
pseudonymization and annotations cost time. Observe that cost before using these
captures for a performance verdict. Stop producers cleanly before importing.

## Validation boundary

CPU tests exercise the real exporter, a synthetic Ascend SQLite profile, the
native CLI, atomic publication and SQL reverse audit. Device rows, anchors and
anchor costs match the context-free baseline. Negative tests cover missing and
duplicate markers, different threads, overlapping scopes, capture loss,
malformed input and output/input collision.

A separate source-contract check applies the explicit patch to a temporary copy
of the pinned vLLM files and verifies the actual `SchedulerOutput` field survives
pickle and msgpack. Unrelated payload types are stubbed; no vLLM or torch runtime
is imported. This does **not** validate a live vLLM/Ascend profiler campaign,
exported marker coverage, async replay semantics, or capture overhead. Bind the
runtime source/image identity and validate a real captured marker before
promoting this bridge into experimental evidence.

## Coverage and visible timeline

`traceloom_v_context_device_coverage` retains **every** device-work row,
including profiler control events, with one of `supported_step`,
`ambiguous_step`, `outside_supported_execution_scope` or
`no_supported_provider_relation`. It is a global denominator, not a per-step
completeness certificate. Legacy execution records without a phase default to
`execute_model`; unsupported phase strings are rejected.

`--perfetto-out timeline.json.gz` adds three context processes when context
has been imported: host execution scopes, step device envelopes, and associated
device events. Overlapping intervals occupy separate display lanes.
An envelope is minimum linked start through maximum linked end: **not busy
time**, sum of device durations, or a CPU stack. Raw provider lanes remain
available. Empty/missing steps stay in SQL and are not drawn as zero-cost
device slices. Step and execution IDs in slice arguments support reverse audit.

For numeric analysis, deduplicate `device_work_id` before summing durations.
Compute interval-union time separately per device; overlap makes duration sum,
busy union and elapsed envelope different quantities. Do not sum across phases
or semantic occurrences without considering overlapping evidence.

## Bounded real acceptance

On 2026-09-15, Qwen3-0.6B TP1/eager, async scheduling with an in-process engine,
vLLM `0fc695fc` / vLLM-Ascend `f4a08bdd` / torch-npu 2.10.0.post2:
34 scheduler decisions and 66 execution markers were imported with no producer
loss. All markers matched; 14,144 / 14,178 device events acquired step identity,
including all 11,684 non-control anchors. The remaining 32 EVENT_RECORD rows
(0.30 us summed) were outside scopes; two profiler control rows had no native
runtime link. No nearest-step fallback was applied. Two zero-token decisions
each had a real linked EVENT_RECORD, rather than an invented zero cost.

All 32 concrete recovered `Rep x27` occurrences associated with distinct
nonempty steps through HPO terminal event IDs. This is structural comparison
with supplied scheduling context, not automatic semantic layer certification.
Events, anchors, costs, structural order and HPO Occurrences were unchanged
against context-free analysis of the identical capture.

The stock profiler warned that stopping in RECORD could yield incomplete parsed
data. Existing offline DB export and retained raw evidence allowed the above
coverage audit; this does not establish universal capture completeness or
instrumentation overhead. Graph replay, distributed ranks and other decoding
modes are outside this acceptance.

See [step-device-summary.sql](../integrations/vllm/step-device-summary.sql)
and [occurrence-step-context.sql](../integrations/vllm/occurrence-step-context.sql)
for directly executable queries with explicit selectors.

## Optional step-bounded candidate recovery

Use --rules-config configs/scheduler-step.yaml alongside the linked --context
inputs to forbid candidate occurrences crossing supplied scheduler steps.
See the partition contract in configs/README.md. Context binding now precedes
grammar recovery inside the temporary AugDB; query recipes are registered after
the ordinary catalog exists. No second profile capture or external database
join is required.

With no partition rule, the structural result is still unchanged by context
import. With the rule, structure is deliberately recovered differently;
timestamps, event IDs, anchors, ordering and measured costs are not rewritten.
Auxiliary attribution remains its existing separate lens, not newly certified
step ownership. Step identity is keyed by run/step rather than local ordinal.

## Scheduler steps and reconstructed replay

`traceloom_v_context_replay_launch` exposes supported step/launch associations;
`traceloom_v_context_replay_member` extends them to exact body members, retaining
lane/task order and the launch runtime endpoint. `traceloom_v_context_anchor`
provides the corresponding ordinary/replay terminal-anchor join for HPO queries.
These are evidence relations, not inferred cross-stream dependencies.

With `scheduler-step.yaml`, exact replay anchors take their partition from the
launch occurrence, not a synthetic graph event ID. Every protected replay unit
must have one known, closed/lossless step identity across its tokens. Missing or
conflicting identities reject constrained analysis atomically; omit the rule to
inspect incomplete evidence without claiming step-bounded structure. The rule
does not split atomic replay units or rewrite their internal reconstruction.
Definitions may be reused across steps, but candidate occurrences cannot cross
the supplied boundary. Marked-structure projection remains unsupported with this
partition rule.

```sql
SELECT s.ordinal, r.launch_id, r.replay_unit_id, r.runtime_call_id
FROM traceloom_v_context_replay_launch r
JOIN traceloom_scheduler_step s USING(run_id, step_id)
ORDER BY s.ordinal, r.start_ns;

SELECT m.*, b.kind, b.source_table, b.source_row_id
FROM traceloom_v_context_replay_member m
JOIN traceloom_graph_body_member b
USING(launch_id, member_id, db_idx, device_id)
WHERE m.run_id=:run_id AND m.step_id=:step_id;
```

Graph-launch envelopes and body events are different observation resolutions.
Do not add their durations as if they were disjoint work. For replay costs use
the dedicated replay cost surfaces; for step costs select one resolution and
compute per-device interval union. A summed device-work duration is an
observation sum, not accelerator busy time or step latency.

### Bounded graph acceptance (2026-09-16)

The same pinned Qwen3-0.6B runtime, TP1 on physical device 1, default
FULL_AND_PIECEWISE graph mode and capture sizes [1,2], produced 30 decode
replays. All 30 launches map to distinct supplied steps, and all 10,170 exact
operator members (339 per launch) have the same step identity as their launch.
The 369 recovered internal pattern occurrences and exact body/cost rows equal
the context-free reconstruction. No non-root HPO occurrence crosses steps;
34 exported step envelopes match linked event geometry. Known controls remain
raw evidence, not invented operator members. This does not qualify distributed
runs, universal capture completeness or recording overhead.

Keep the original rank capture layout. Parse the exact raw PROF container with
`msprof --parse=on --output=/absolute/path/to/PROF_container` if its host SQLite
mapping has not been generated. The loader admits one sibling container next
to `ASCEND_PROFILER_OUTPUT`; it never selects the first of several candidates.

## Start from a request or step kind

With imported context, `requests` discovers identities scoped by
`(run_id, scheduler_id, request_id)`. Bind the returned coordinates to
`request_steps` or `request_observations`; a returned `(run_id, step_id)` then
continues to `step_execution_context` and `step_device_work`. The event coordinate
continues to ordinary raw-row audit. Request steps express participation in shared
work, **not** exclusive device-cost ownership.

```sql
SELECT * FROM traceloom_v_request_catalog;

SELECT * FROM traceloom_v_request_step
WHERE run_id=:run_id AND scheduler_id=:scheduler_id AND request_id=:request_id
ORDER BY ordinal;

SELECT * FROM traceloom_request_observation
WHERE run_id=:run_id AND scheduler_id=:scheduler_id AND request_id=:request_id
ORDER BY ordinal, observation_kind;
```

The request catalog includes notification-only requests without inventing their
missing participation. `finished_observed`, `preempted_observed` and
`resumed_observed` mean a scheduler output carried that notification, not that
its scheduling timestamp was the actual state transition. Absent notifications
are not negative lifecycle facts. A closed producer can still contain an open
or truncated request history. These surfaces do not establish admission, queue
latency, client TTFT/ITL or resource ownership.

`step_shape` describes request/token counts independently of semantics.
`scheduler_steps_by_kind` additionally selects `prefill`, `decode`, `mixed`,
`empty_decision`, or `unknown` according to **planned token ranges**:

- New context producers copy the worker-input token offset from
  `NewRequestData.num_computed_tokens` or the identity-aligned cached payload.
  They do not subtract from optimistic post-schedule counters.
- Prompt-relative offsets split scheduled tokens into prompt and generation
  work. A step is `mixed` if both occur, including within one request's range.
- Missing, malformed or ambiguous offsets withhold classification; one unknown
  participating request keeps the step `unknown`. Old context files remain
  valid and are not retroactively guessed from `new/cached` or token counts.
- This classifies scheduled work, not completed tokens, accepted speculative
  outputs, graph type, or request lifecycle state. The source basis is explicit.

```sql
SELECT * FROM traceloom_v_scheduler_step_shape
WHERE run_id=:run_id AND step_kind=:step_kind
ORDER BY scheduler_id, ordinal;
```

Use `integrations/vllm/step-device-summary.sql` for linked event-level cost lenses.
Keep individual step identity before grouping or joining request participation;
a shared step must not become several independent device-cost samples.

### Compare step-kind distributions, then reopen a sample

`scheduler_step_costs` selects `traceloom_v_scheduler_step_device_cost` by
`:run_id` and optional `:step_kind`. Its grain is **step × device**, before any
request join. It carries planned kind, observed token/request counts, producer
capture state and supported-marker count alongside distinct event work, duration
sum, overlap-safe busy union and envelope. Graph-launch envelopes are excluded.
Missing device links remain visible with NULL costs; empty decisions can still
have observed control work and must not be forced to zero or NULL. The union
includes supported wait/control event intervals, not just compute kernels:
despite its `device_busy_union_us` column name, it is not hardware utilization.

```sql
SELECT step_kind,scheduled_requests,total_scheduled_tokens,device_id,
       COUNT(*) AS decisions,COUNT(device_busy_union_us) AS observed_samples,
       MIN(device_busy_union_us) AS min_union_us,
       AVG(device_busy_union_us) AS avg_union_us,
       MAX(device_busy_union_us) AS max_union_us
FROM traceloom_v_scheduler_step_device_cost
WHERE run_id=:run_id
GROUP BY step_kind,scheduled_requests,total_scheduled_tokens,device_id;
```

The returned distributions describe captured supported work, **not complete step
latency**. Keep NULL-device rows separate, and retain each concrete `step_id` to
continue through `step_device_work` and `event_audit`. When selecting a request,
use `EXISTS` rather than expanding one step into one row per participating
request:

```sql
SELECT c.* FROM traceloom_v_scheduler_step_device_cost c
WHERE c.run_id=:run_id AND EXISTS (
 SELECT 1 FROM traceloom_v_request_step q
 WHERE q.run_id=c.run_id AND q.step_id=c.step_id
   AND q.scheduler_id=:scheduler_id AND q.request_id=:request_id
)
ORDER BY c.ordinal,c.device_id;
```

This is the work associated with its participating steps, not exclusive request
cost. Two requests can legitimately select the same step; their selected costs
must not be added as a service-cost allocation. The optional observer's
`request_coordinate` receipt can connect an in-process caller ID to the exact
producer-scoped pseudonym, without exporting the private salt or guessing by
submission time. Remote service clients need their own explicit identity bridge.
