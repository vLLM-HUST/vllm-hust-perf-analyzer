# Architecture

TraceLoom is an offline analysis pipeline for Ascend distributed inference
profiles. Its core design goal is to preserve raw profiler evidence while
adding enough structure for developers to reason about repeated execution
patterns, communication stalls, and likely operator-level causes.

The high-level flow is:

```text
msprof output
  -> parser layer
  -> event model
  -> pattern mining layer
  -> attribution layer
  -> report layer
  -> CLI / SQL consumers
```

## Parser Layer

The parser layer discovers profiler databases and validates the input layout.
The current production input is Ascend/CANN `msprof` SQLite output:

```text
<run_dir>/msprof_raw/PROF_*/msprof_*.db
<raw_dir>/PROF_*/msprof_*.db
```

The parser keeps the original profiler database as evidence and creates a
TraceLoom sidecar database for normalized events, anchors, loop structure, and
report views.

Current modules:

- `traceloom.io.discover`: profile DB discovery and raw layout inventory.
- `traceloom.msprof_reader`: Ascend `msprof` row loading helpers.

## Event Model

The event model maps low-level profiler rows into a stable analysis vocabulary:

- normalized events with timing, stream, device, and source metadata;
- semantic anchors for major compute, communication, and collective activity;
- symbols that make repeated kernel sequences comparable;
- source links back to the original profiler rows;
- auxiliary/prelude slots for events that prepare the following anchor.

This layer is what lets TraceLoom talk about runtime behavior without losing
the ability to audit each conclusion against raw profiler data.

## Pattern Mining Layer

Distributed inference traces often repeat the same decode-time structures many
times. The pattern mining layer compresses anchor streams into macro and repeat
trees so that developers can inspect the dominant shape of the execution rather
than every individual kernel launch.

Current modules:

- `traceloom.loop_tree`: grammar and tree utilities.
- `traceloom.compute_prelude_timeline`: current canonical pipeline, including
  anchor extraction, repeat discovery, and report generation.

Target modules after refactoring:

- `grammar.py`: macro discovery and repeat promotion.
- `tree.py`: structured loop tree construction.
- `patterns.py`: similarity matching and repeated-pattern scoring.

## Attribution Layer

The attribution layer answers two questions:

1. Which events are the semantic anchors of execution?
2. Which nearby events are auxiliary costs that prepare, synchronize, or move
   data for those anchors?

TraceLoom currently uses anchor roles, kernel families, collective detection,
and following-anchor prelude attribution. This produces conservative evidence
for:

- anchor kernels such as matmul, attention, normalization, and collectives;
- auxiliary kernels and host/device activity around the anchor sequence;
- communication or synchronization fragments near collective anchors;
- node-level cost composition for repeated execution patterns.

Attribution is diagnostic evidence. It should be interpreted with kernel names,
profiler metadata, and workload knowledge rather than treated as automatic
source-line proof.

## Report Layer

The report layer writes both human-readable and queryable artifacts:

- `summary.md`: selected devices and highest-cost loop structures.
- `tree-map.md`: compact node-cost map for humans.
- `queries/*.sql`: starter SQL reports for drill-down.
- `dbNN.traceloom_augmented.db`: sidecar SQLite database containing
  `traceloom_*` tables and views.
- Optional full debug exports: anchor steps, symbols, loop costs, tree JSON,
  node metrics, and auxiliary slots.

The database-backed workflow is intentional: reports are useful summaries, but
the augmented DB is the durable surface for deeper inspection and reproducible
queries.

## CLI Layer

The public CLI exposes three workflows:

- `traceloom analyze <profile_dir>`: analyze an existing `msprof` profile.
- `traceloom report <augmented.db> --sql query.sql`: run SQL reports against a
  TraceLoom augmented database.
- `traceloom create config` and `traceloom run`: optional convenience path for
  invoking `msprof` from an editable config.

The analyzer remains offline even when the optional runner is used.

## Current Module Map

- `traceloom.cli`: command-line entry points.
- `traceloom.compute_prelude_timeline`: transitional end-to-end Ascend/CANN
  analysis pipeline.
- `traceloom.augmented_db`: sidecar SQLite schema and writer.
- `traceloom.report`: SQL report runner and Markdown/CSV/JSON/TSV exporters.
- `traceloom.io.discover`: profile DB discovery.
- `traceloom.loop_tree`: grammar and tree utilities.
- `traceloom.score_view`: optional static HTML debug view.

## Design Principles

- Preserve raw evidence: never make the report the only source of truth.
- Compress repetition: expose dominant runtime patterns instead of every event.
- Keep attribution auditable: link diagnosis back to anchors, events, and SQL.
- Avoid overclaiming: report likely bottlenecks and causes, not automatic
  optimization decisions.
- Keep large traces out of source control: commit docs, examples, fixtures, and
  reproducibility recipes instead.
