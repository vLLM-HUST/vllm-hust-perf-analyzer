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
