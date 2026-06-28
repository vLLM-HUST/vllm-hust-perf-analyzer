# Issue: ACLGraph Reconstruction Is Too Slow On Large msprof Packages

Status: Open

Priority: High for research throughput

## Context

While collecting modern TraceLoom reports for the Ascend follow-up experiments,
small graph/runtime profiles completed quickly enough for interactive research
use:

- `ascend_eager_offline_msprof_app`: completed, no ACLGraph replay.
- `ascend_graph_offline_msprof_app`: completed, 3 ACLGraph replay events.
- `exp_001_overload_profiler`: completed, 469 ACLGraph replay events.
- `exp_002_overload_profiler`: completed, 401 ACLGraph replay events.

The larger D1/D2 profiler packages did not complete within a practical
collection window. `exp010_prefix_cache_hot_profiler` spent more than six
minutes after creating the augmented DB and had not emitted `summary.md` or
`report_dev*.md`.

The interrupted stack consistently pointed at:

```text
analyze_aclgraph_for_device
  -> _infer_model_execute_wave_size
  -> _model_execute_controls_for_segment
  -> _normalize_key
```

This suggests a repeated scan over semantic tasks for each activity segment,
with repeated string normalization in the inner loop.

## Why This Matters

The immediate research workflow needs a modern readable TraceLoom report for
each profiler experiment. If large packages take many minutes before producing
any report file, analysis becomes hard to iterate and missing reports can block
paper evidence collection.

This is not a broad refactor request. The first goal is simple:

```text
Make modern report generation predictable on multi-GB msprof artifacts.
```

## Likely Hotspots

1. `semantic_tasks` are scanned repeatedly for each graph activity segment.
2. task labels are normalized repeatedly instead of once at load time.
3. interval overlap queries are implemented as Python list scans.
4. ACLGraph analysis runs even for workloads where graph reconstruction may not
   be needed for the requested report.
5. report generation is all-or-nothing: users get no readable report until
   expensive graph reconstruction finishes.

Observed scale from archived follow-up profiles:

| profile | TASK rows | capture streams | semantic tasks | MODEL_EXECUTE | mapped timed tasks | activity segments | naive segment x semantic |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| P0 graph | 13,990 | 58 | 1,823 | 569 | 5,398 | 3 | 5,469 |
| exp_001 overload | 415,516 | 319 | 37,095 | 11,565 | 152,166 | 469 | 17,397,555 |
| exp_002 overload | 325,889 | 319 | 32,297 | 10,111 | 130,651 | 401 | 12,951,097 |
| exp010 hot profiler | 2,098,879 | 87 | 276,399 | 92,075 | 1,052,745 | 326 | 90,106,074 |

The P0 case is tiny enough that the naive algorithm looks fine. The large D1
case pushes the same pattern into tens of millions of repeated overlap checks,
and each check currently performs string normalization on the hot path.

There are also secondary quadratic-ish scans:

- `_summarize_model_streams()` scans all `task_rows` once per mapped stream.
  For exp010 hot, this is roughly `87 x 2.1M` row checks before any graph
  typing.
- `_build_replay_rows()` scans all semantic tasks again for each replay segment
  to build control counters.
- `_build_envelope_rows()` checks every visible step against every replay
  interval.

These are algorithmic issues inside the current single-process design. They
should be fixed before introducing a heavier execution framework.

## Flat Timeline Constraint

TraceLoom cannot simply analyze each stream independently and stop there. The
primary product is a flattened, globally ordered device timeline:

```text
normal event, normal event, graph atom, normal event, repeated graph atom, ...
```

ACLGraph evidence is also cross-stream by nature:

- replayed kernels live on graph model streams;
- `MODEL_EXECUTE` / `NOTIFY_WAIT` controls live on control streams;
- visible main events may overlap graph replay intervals on other streams;
- loop-tree mining consumes the final global sequence, not per-stream
  fragments.

So the right decomposition is not "per-stream analysis as the final answer".
The right shape is:

```text
map per-stream/per-table evidence
  -> reduce into global time intervals and normalized event rows
  -> emit one flat, stable, globally ordered timeline
```

Per-stream partitioning is still useful as an implementation detail, because it
lets us sort and summarize locally. But every partition result must be reduced
through a global time merge before tree compression or report generation.

## Original-Algorithm Optimization Plan

### 1. Build Stream Buckets Once

Current pattern:

```text
for each mapped stream:
    scan all task rows
```

Replace with one pass:

```text
rows_by_stream[stream_id].append(task)
semantic_by_key[task_key].append(task)
mapped_model_tasks.append(task)
```

This preserves all raw events and avoids multiplying `TASK` rows by capture
stream count.

### 2. Segment Graph Activity By Global Time Merge

The current `_segment_tasks()` globally sorts all mapped model-stream tasks.
That preserves semantics, but it materializes and sorts the full list.

A scalable equivalent is:

```text
map:
  each model stream emits sorted timed task intervals

reduce:
  k-way merge all model-stream intervals by start time
  build union-like activity segments using the gap threshold
```

This is still a global timeline algorithm. It is not per-stream finalization.
It just avoids unnecessary repeated scans and makes streaming/chunking possible.

### 3. Replace Segment x Semantic Scans With A Sweep Join

Current pattern:

```text
for segment in segments:
    for semantic_task in semantic_tasks:
        if overlaps(segment, semantic_task):
            ...
```

Replacement:

```text
segments = sorted by start_ns
model_execs = sorted semantic_by_key["MODEL_EXECUTE"] by start_ns

walk both lists once:
  expire controls whose end < segment.start
  add controls whose start <= segment.end
  current active controls are the overlap candidates
```

This produces the same cross-stream overlap relation because it uses global
time intervals. Complexity becomes roughly:

```text
O(num_segments + num_model_execute + overlaps)
```

instead of:

```text
O(num_segments * num_semantic_tasks)
```

The same indexed/sweep result should be reused by:

- wave-size inference;
- segment splitting;
- replay row control counters.

### 4. Split Segments Using Precomputed Control Lists

`_split_one_segment_by_model_execute_wave()` should receive the segment's
already-computed `MODEL_EXECUTE` rows instead of calling
`_model_execute_controls_for_segment()` again.

This also makes the semantics clearer:

```text
MODEL_EXECUTE is the boundary/control signal for slicing a graph activity
interval into smaller replay waves.
```

### 5. Aggregate Graph Body While Assigning Tasks To Segments

After segments are known, each mapped model-stream task should be assigned to a
segment once, using the same global ordered walk. During that assignment,
compute:

- top op counters;
- task type counters;
- body signature counters;
- body noise counters;
- kernel count and kernel time;
- stream duration counters.

Then `_build_replay_rows()` no longer needs to iterate full child lists for
every downstream purpose. It can consume compact `GraphSegmentAccumulator`
objects.

The flat timeline is not lost: each accumulator still has `start_ns`, `end_ns`,
child evidence references/counters, and emits one graph atom event into the
global sequence.

### 6. Build Envelope Rows With Interval Join

Current pattern:

```text
for replay in replay_rows:
    for visible_step in visible_step_rows:
        if overlaps(...):
            emit envelope
```

Replacement:

```text
sort replay intervals
sort visible steps
sweep by time
```

This matters for large traces even when graph count is moderate, because
visible steps are the same rows the report later uses for tree construction.

### 7. Keep Final Projection As A Stable Global Merge

After graph atoms are produced, final timeline projection should still be:

```text
normal main events + graph atom events
  -> remove fully covered events when graph-as-atom view is requested
  -> stable sort by (start_ns, end_ns, stream_id, symbol/source)
```

This is the correctness boundary. All map/reduce optimizations are internal
ways to produce the same event set and graph links faster.

## Immediate Low-Risk Patch Candidate

The first patch does not need DuckDB or a new IR. It can stay in Python and
change only `ascend_aclgraph.py`:

1. Add `task_key` to semantic rows.
2. Build `semantic_by_key`.
3. Precompute `model_execs_by_segment` with a sorted interval overlap helper.
4. Reuse those lists for wave-size inference and splitting.
5. Build all-control overlaps once and pass them into `_build_replay_rows()`.
6. Group `task_rows` by stream once for `_summarize_model_streams()`.

Expected benefit: remove the largest repeated scans while preserving current
outputs and the global flat timeline.

## Proposed Fixes

### P0: Reframe Large DB Analysis As A Map/Reduce Pipeline

Large profiler DBs should not be analyzed as one monolithic Python pass. The
analysis naturally decomposes into map/reduce stages:

```text
map stream/task partitions
  -> emit normalized event fragments and graph-control intervals
  -> reduce by device, stream, graph segment, op type, and tree symbol
  -> materialize compact intermediate tables
  -> render reports from compact tables
```

The immediate target is not distributed execution. The first useful version can
run locally, but the data model should be partitionable:

- partition raw `TASK` rows by `(db_idx, device_id, stream_id)` and time range;
- map `COMPUTE_TASK_INFO` into normalized op labels once;
- map `AscendTask` semantic rows into keyed control intervals once;
- reduce model-stream rows into ACLGraph candidate segments;
- reduce per-segment children into graph signatures and top-op counters;
- reduce normalized op rows into loop-tree symbols and costs.

This would let TraceLoom cache and reuse intermediate products instead of
rescanning multi-GB DBs for every report.

### P0: Consider A Columnar/SQL-First Execution Layer

Before adopting a heavyweight distributed framework, evaluate local analytical
engines that fit TraceLoom's offline profiler workload:

- DuckDB for columnar scans, Parquet caches, interval joins, and group-by heavy
  reductions.
- Polars for lazy dataframe pipelines and partitioned CSV/Parquet processing.
- SQLite only for source compatibility and final augmented DB packaging, not
  necessarily for all intermediate analysis.

Candidate direction:

```text
msprof sqlite tables
  -> extracted columnar cache
  -> vectorized interval joins and reductions
  -> compact TraceLoom IR tables
  -> augmented sqlite DB + Markdown reports
```

Spark/Ray/Dask should remain optional later-stage backends. The first priority
is a single-node big-data style pipeline that is fast and deterministic on one
research workstation.

### P0: Precompute Semantic Task Keys

Add normalized task keys when semantic task rows are created:

```text
task_label_key = normalize(task_label)
```

Then avoid calling `_normalize_key()` inside nested loops.

### P0: Index Semantic Tasks By Key And Time

Build per-key sorted arrays:

```text
MODEL_EXECUTE -> [(start_ns, end_ns, row), ...]
NOTIFY_WAIT   -> [(start_ns, end_ns, row), ...]
NOTIFY_RECORD -> [(start_ns, end_ns, row), ...]
```

Use `bisect` to retrieve candidates overlapping a segment instead of scanning
all semantic tasks.

### P0: Emit Base Report Before Optional Deep Graph Work

The readable tree report should be able to emit a base device timeline even if
deep ACLGraph reconstruction is slow or disabled. A graph pass can then enrich
the report when it completes.

Minimal CLI shape:

```text
--graph-analysis {auto,off,fast,deep}
```

Suggested semantics:

- `auto`: current behavior once optimized.
- `off`: skip graph reconstruction; still produce normal report.
- `fast`: detect graph intervals and counters, but skip expensive signatures.
- `deep`: compute full graph signatures and envelope details.

### P1: Cache Replay Segmentation Products

Materialize intermediate ACLGraph analysis rows in the augmented DB or a sidecar
cache keyed by source DB path, DB mtime/size, device id, and TraceLoom version.
Repeated report formatting should not re-run raw graph reconstruction.

### P1: Add Timing Diagnostics

Print or write phase timings:

```text
load_tasks_s
load_compute_info_s
semantic_task_build_s
activity_segmentation_s
wave_inference_s
replay_row_build_s
envelope_build_s
tree_build_s
report_render_s
```

This will make future slowdowns obvious.

## Acceptance Criteria

For archived follow-up artifacts:

- `exp010_prefix_cache_hot_profiler` produces `summary.md` and `report_dev*.md`
  within a small, predictable time budget.
- `exp010_prefix_cache_cold_profiler` produces the same.
- `exp012_mixed_interference_profiler` produces the same.
- Users can choose a fast report path when deep graph signatures are not the
  research question.

## Current Workaround

For the follow-up result sync, modern reports were collected for the smaller
P0/P2/D2 profiler packages. Large D1/D2 packages are recorded in
`experiments/ascend-followup/results/macro_metrics.csv` with status
`blocked_by_traceloom_aclgraph_analysis_runtime`.
