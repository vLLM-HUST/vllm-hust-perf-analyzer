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

Ascend reports also contain a `Visible Productive Idle Evidence` section. It
is the device-level E1→E4 explanation partition: profiler-visible wait,
capture/control, runtime-control, and explicit unattributed residual. The
default real-profile `collection_status` is `unknown`, so TraceLoom does not
turn empty observed streams into an absence claim without external capture
completeness evidence. These values describe gaps in visible productive work;
they are not proof of hardware idleness or causality.

This section is intentionally separate from the tree's compatibility
`idle_us` cost. The latter is the residual in an anchor's prelude cost packet;
the former is a device-global productive-gap partition and may include visible
wait/control tasks. They must not be substituted for one another.

The `Anchor-Prelude Attribution` subsection intersects E4 slices with the same
disjoint prelude windows used by Loop Tree cost packets, then aggregates those
intersections through existing node/anchor coverage. It reports the exact
device-only residual rather than forcing uncovered time onto a node. Hotspot
rows are hierarchical: a parent's duration includes its descendants, so
parent and child rows are not additive. Nanosecond fields are authoritative;
microsecond fields are readable rounded summaries.

## Explicit Native Artifacts

Use `traceloom --help-advanced` for non-default outputs:

- `--out PATH`: native result JSON;
- `--grammar-debug-out PATH`: grammar-state diagnostics;
- `--compat-db-out PATH`: queryable compatibility SQLite sidecar;
- `--loop-tree-out PATH`: explicit Loop Tree output path;
- `--loop-tree-aux`: include auxiliary attribution in the Loop Tree build.
- `--idle-evidence-rules PATH`: override the Ascend idle-evidence semantic
  ruleset; an invalid override fails rather than falling back silently.

Only one output may target stdout at a time, and explicit output paths require
a single input database.

The checked-in SQL audit surfaces are:

- `docs/report-sql/reconstruction-capability-matrix.sql` for exact, typed
  unknown, and legacy ACLGraph capability outcomes;
- `docs/report-sql/idle-evidence-summary.sql` for paper-ready E4 category
  totals and shares; and
- `docs/report-sql/idle-evidence-audit.sql` for exact partition, lineage, and
  anchor/root conservation checks.

These queries are executable goldens: the native SQL compatibility test checks
their columns and known-good outputs, and deliberately corrupts one idle
interval to prove the audit changes from `PASS` to `FAIL`.

The idle audit's `PASS` is a sidecar-integrity result, not a replacement for
`analysis_status` or `collection_status`. Consumers must inspect all three:
an internally conserved `invalid_input` run remains an auditable negative
result, not positive semantic evidence.

## Provenance Contract

Native events and sidecar rows retain source kind, source path, source table,
and source row identifiers. Reports are summaries; use these links to confirm
diagnoses against the raw profiler evidence.

## Compatibility Rule

The schema is still alpha. Public releases should version table, column, JSON,
and report-field contracts before downstream systems depend on every name.
