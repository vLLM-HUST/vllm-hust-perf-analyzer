# Observable inference trace v1 (initial implementation)

TraceLoom observes execution. It does not request, reconstruct, or publish model
hidden chain-of-thought. SAGE is one producer; no SAGE implementation knowledge
belongs in the importer. Existing accelerator IR, grammar and costs keep their
meanings. Inference observations occupy a separate, versioned SQL surface.

## Wire format

UTF-8 NDJSON, one complete JSON object plus newline per observation. Append-only
local spool is the v1 transport; no server or TraceLoom availability is required
on the inference execution path. A file snapshot can be imported repeatedly into
one analysis.db. `--follow` periodically imports complete records and publishes
an atomic HTML snapshot. A partial last line remains pending, never guessed.
This is a neutral interchange format, not an OTLP endpoint.

Required envelope fields:

| Field | Contract |
| --- | --- |
| schema_version | Integer 1. Unknown major versions reject the import transaction. |
| event_id | Stable opaque identifier, 1–128 ASCII identifier characters. Unique within trace. |
| trace_id | 32 lowercase hex, nonzero; maps to OTel TraceId. |
| span_id | 16 lowercase hex, nonzero; maps to OTel SpanId. |
| producer_id | Opaque producer instance identifier; no hostname/user path needed. |
| clock_id | Monotonic clock domain identifier, renewed at process restart. |
| sequence | Nonnegative per-producer sequence; gaps can be observed, not equated to loss. |
| event_type | span_start, span_end, span_event, trace_end, or metrics. |
| wall_time_ns | Nonnegative signed-64-bit Unix nanoseconds, JSON integer. Display only. |
| monotonic_ns | Nonnegative signed-64-bit monotonic nanoseconds, JSON integer. |

Optional fields: parent_span_id (same trace), name (bounded identifier), kind
(pipeline, model, retrieval, tool, data, step), status (ok, error, cancelled),
attributes (whitelisted scalar metadata), decision_summary (explicitly authored
public summary), evidence_refs (array of opaque local IDs), links (array of span
IDs that this span depends on). span_start carries identity, kind and parent;
span_end carries terminal status; span_event carries observations; trace_end is
an explicit root-level completion receipt; metrics carries cumulative
attributes.dropped_events per producer. Retrying creates a new span ID and uses
attributes.attempt plus an explicit link to the previous attempt. Exceptions
carry a safe error_type, never exception text, stacks or repr(input).

Allowed attributes: operation, model, provider, attempt, input_bytes,
output_bytes, input_tokens, output_tokens, retrieved_count, dropped_events,
error_type. Counts are nonnegative integers. String values are bounded labels,
not arbitrary bodies, URLs, SQL, headers, paths, prompts or generated tokens.
Unknown attributes are discarded and counted; unknown envelope keys reject
rather than silently accepting a spelling error. A nested arbitrary content
object is never a supported extension point.

## Privacy and bounds

Raw prompts, responses, token streams, reasoning fields, authentication data and
raw retrieved documents are prohibited. Producer code must not serialize them.
Attributes use an allowlist and identifier validation. evidence_refs contain
opaque IDs, not resolvable URLs or file contents. decision_summary is optional,
producer-authored public text (not model-private reasoning), at most 256 bytes;
the importer omits it by default. Explicit `--include-summaries` is an opt-in
for pre-redacted summaries. Do not rely on a regex to make arbitrary prose safe.
No raw record snapshots or local input paths are copied to the resulting DB;
provenance consists of trace/event ID and source line number plus a digest of
the accepted normalized record. Diagnostics never echo rejected values.

Limits: 16 KiB per record, 64 MiB per import file, 100,000 retained events per
DB by default, 32 attributes, 16 evidence refs and 16 dependency links per
record. Producers should sample whole traces before enqueueing, use a bounded
nonblocking queue, drop on full/disconnected sink, and expose cumulative
`dropped_events` through both local metrics and best-effort trace metrics.
No exporter exception may change SAGE outputs, status, cancellation or retries.
Producer acceptance budget: disabled overhead <1% median wall time on a fixed
no-op pipeline benchmark; enabled p95 enqueue <=100 microseconds; bounded queue
<=4096 records; <=16 MiB spool segment and <=64 MiB spool total, oldest segment
rotation; default retention <=24 h. These are measurable targets, not achieved
results. Enforcement and benchmarking of the SAGE producer belong to SAGE.
TraceLoom is local artifact storage: operator deletes expired artifacts; v1
imports neither run a retention daemon nor promise unattended deletion.

## Ordering, completion and compatibility

Event identity is (trace_id,event_id). Reimporting a normalized identical record
is a no-op; conflicting reuse rejects and rolls back the entire snapshot.
Also unique: (trace_id,producer_id,sequence). Event arrival order is irrelevant;
end-before-start and missing parents are retained as incomplete evidence.
Conflicting start/end identities, clock changes within a span, negative elapsed
time and parent/dependency cycles are rejected when both endpoints are known.
Monotonic elapsed time is available only for a matched start/end in the same
producer and clock domain. Wall clock jumps never affect measured duration.
Missing ends remain open; root cancellation does not fabricate child ends.
A trace_end receipt plus terminal known spans means finished *observed* trace;
it does not prove that dropped or unsampled spans never existed.

Parentage means scope, not data dependency. Explicit links alone form a
completion-before-start dependency DAG. Longest observed dependency path is
reported only for complete spans in a comparable clock domain; it is not a
claimed exact global critical path. Cross-clock links remain visible but timing
and global concurrency are unknown. Parent/child durations must not be summed
as independent work. Overlap/concurrency is display-clock-local.

Wire major version and inference SQL module version evolve separately. Version
1 creates namespaced tables transactionally; repeat imports reuse the module.
Unknown newer SQL versions reject without mutation. The schema is additive to
an existing analysis.db; no profiler tables are renamed or reinterpreted.
Future optional attributes require a documented allowlist update; unsupported
fields never become raw payload storage. OTel adapters map IDs and parentage,
span kinds/status and links; clock_id/monotonic timestamps are an explicit
extension because portable OTel span timestamps alone do not provide them.

## SQL and display surface

- traceloom_inference_meta: module version and content policy.
- traceloom_inference_event: sanitized immutable lifecycle observations.
- traceloom_v_inference_span: start/end, status, elapsed time and completeness.
- traceloom_v_inference_trace: observed completion and dropped-event receipts.
- Dependency/parent links retain distinct meanings; unresolved endpoints remain.
- Self-contained HTML shows live/finished state, timelines, nesting, overlaps,
  safe metadata, summaries (if enabled), evidence IDs and longest observed path.
- Perfetto complete/instant events project the same DB; open spans are instants,
  not invented closed slices. Each clock domain has a separate track group.

Example (one trace, two lifecycle observations; integers must not be converted
through IEEE-754 doubles by an adapter):

```json
{"schema_version":1,"event_id":"p1-0","trace_id":"11111111111111111111111111111111","span_id":"1111111111111111","producer_id":"p1","clock_id":"boot1","sequence":0,"event_type":"span_start","wall_time_ns":1788919000000000000,"monotonic_ns":1000000,"name":"inference","kind":"pipeline"}
{"schema_version":1,"event_id":"p1-1","trace_id":"11111111111111111111111111111111","span_id":"1111111111111111","producer_id":"p1","clock_id":"boot1","sequence":1,"event_type":"span_end","wall_time_ns":1788919000002000000,"monotonic_ns":3000000,"status":"ok","attributes":{"output_tokens":8}}
```

OTel mapping reference: [Tracing API](https://opentelemetry.io/docs/specs/otel/trace/api/).
OTel links are general causal associations; adapters must not upgrade arbitrary
OTel links to this contract's completion-before-start dependencies without
additional evidence. V1 kind/status vocabulary is not a verbatim OTel enum.

## Producer integration and acceptance cases

A root pipeline span encloses a run. Packet dispatch and actor calls use `step`;
a queue wait is its own `step` named `queue.wait`, followed by a dependency link
from execution to that queue span (only when explicitly observed). A model HTTP
call uses `model`; a retrieval/tool extension uses `retrieval`/`tool`; source and
sink steps use `data`. Names describe operations, never framework-private class
names. Runtime context propagation carries trace/span IDs; a worker starts a
fresh span rather than mutating its parent span from another clock domain.

The fixture matrix includes concurrent dispatch, queue wait and execution,
model input/output counters, opaque evidence hashes, retrieval and tool spans,
retry with an error predecessor, root cancellation with a still-open child,
missing parent, delayed terminal event and exporter loss metrics. Browser and
Perfetto projections must keep every span ID/status reachable and must never
interpret scope nesting as an execution dependency.

Identity fields (parent, kind, name, links) may be repeated on lifecycle records;
when present they must agree with span_start. span_event may use a different
observation name. Model labels additionally allow single `/` separators (e.g.
`vendor/model`), but not absolute paths or `//`. Parent `null` normalizes to
absence. Dependencies and evidence arrays require unique IDs and normalize to sorted order.
Derived SQL surfaces also include `traceloom_v_inference_dependency` (explicit
endpoint timing state) and `traceloom_inference_span_metric` (depth, missing
parent and longest observed dependency prefix with a partial-evidence label).
Retained normalized payloads are additionally bounded to 64 MiB per database.


## Commands and SQL

```bash
traceloom import-inference events.ndjson --output analysis.db \
  --html-out inference.html --perfetto-out inference.json
traceloom import-inference events.ndjson --output analysis.db \
  --html-out inference.html --follow
traceloom export-inference analysis.db --html-out finished.html
```

Open the local HTML in a browser. Follow polls a local file every second and
atomically republishes projections; SIGINT/SIGTERM stops the watcher and removes
the refresh marker. Trace state still reflects producer evidence, not watcher
liveness. The producer can rotate segments: pass its spool directory to import/watch all
regular `.ndjson` files (nonrecursive, excluding symlinks), up to 1024 files and
64 MiB per scan. Each segment is its own atomic transaction; the projection
updates after the scan. Segment removal does not delete already imported rows.
A not-yet-created path is waited for in follow mode; specify `--output` when
starting before a spool directory exists. A malformed complete
record stops this observer without contacting or changing the producer.

```sql
SELECT * FROM traceloom_v_inference_trace;
SELECT trace_id,span_id,parent_span_id,name,kind,status,duration_ns,attributes
FROM traceloom_v_inference_span ORDER BY trace_id,start_ns;
SELECT * FROM traceloom_v_inference_dependency;
SELECT * FROM traceloom_inference_span_metric
WHERE observed_path_ns IS NOT NULL ORDER BY observed_path_ns DESC;
SELECT event_id,source_line,record_sha256,payload
FROM traceloom_inference_event WHERE event_type='span_event';
```

`payload` is a canonical allowlisted observation, never the source record.
The record digest covers that sanitized observation. Repeated identical imports
do not accumulate rows. `discarded_attributes` in the import receipt describes
this snapshot (including re-read records), not a durable event-loss counter.
Reported producer dropped-events counters are cumulative producer receipts;
they are not automatically attributable to the particular trace carrying the
receipt. They must not be summed across trace roots from the same producer.

Inference schema migration initializes an absent module at version 1 inside the
import transaction. Version 1 is reused; newer versions fail closed. Existing
profiler tables are preserved. `export-inference` projects this module only;
existing profiler `export-perfetto` retains its current semantics. There is no
implicit clock alignment between inference and accelerator timelines.

SQLite JSON1 support is required. New database/projection files are owner-only
on POSIX. The Python CLI regression driver is a development-test dependency
only; importing and viewing artifacts require no Python runtime.

## Validation and bounded performance acceptance

The native tests cover lifecycle reordering and missing endpoints, conflicting
IDs/sequence/parents, cycles, impossible same-clock dependencies, cross-clock
links, atomic rollback, unknown versions, exact integers above 2^53, wall-clock
jumps, UTF-8/JSON/type/size rejection, summary opt-in and HTML/JSON escaping.
The optional Python CLI test verifies parsed Perfetto output, file-tail and
rotated-directory watching, shutdown, and source/output collision protection.

Native import acceptance target: a synthetic 10,000-event file below 5 MiB
imports in <=3 seconds with <=128 MiB peak RSS on the development host. This is
a local regression budget, not a hardware-independent guarantee. Measure with
`/usr/bin/time` around a fresh `traceloom import-inference` invocation; do not
include an existing DB or claim that duplicate reimports measure fresh ingestion.
Producer enqueue/disabled-path budgets above require independent SAGE tests;
TraceLoom's offline import measurement cannot establish inference overhead.
