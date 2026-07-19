# Output Schema

## Default Report

For one database, TraceLoom writes:

```text
PROF_.../traceloom/loop_tree_v2.md
```

For a profiler directory containing multiple device databases:

```text
msprof_output/traceloom/device0_loop_tree_v2.md
msprof_output/traceloom/device1_loop_tree_v2.md
```

The report contains the compressed execution tree, occurrence and repeat
counts, total wall-clock cost, per-occurrence/per-iteration averages, compute,
communication, idle, active, auxiliary, and self-cost columns.

## Explicit Native Artifacts

Use `traceloom --help-advanced` for non-default outputs:

- `--out PATH`: native result JSON;
- `--grammar-debug-out PATH`: grammar-state diagnostics;
- `--compat-db-out PATH`: queryable compatibility SQLite sidecar;
- `--loop-tree-out PATH`: explicit Loop Tree output path;
- `--loop-tree-aux`: include auxiliary attribution in the Loop Tree build.

Only one output may target stdout at a time, and explicit output paths require
a single input database.

## Provenance Contract

Native events and sidecar rows retain source kind, source path, source table,
and source row identifiers. Reports are summaries; use these links to confirm
diagnoses against the raw profiler evidence.

## Compatibility Rule

The schema is still alpha. Public releases should version table, column, JSON,
and report-field contracts before downstream systems depend on every name.
