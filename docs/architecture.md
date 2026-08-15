# Architecture

TraceLoom is a native C++17 offline profiler-analysis pipeline:

```text
profiler SQLite
  -> source adapters
  -> native columnar IR
  -> semantic anchors and protected sequences
  -> repeated-pattern grammar
  -> overlap-safe cost attribution
  -> queryable database timeline materializer
```

## Source Adapters

Adapters under `native/src/adapters/` discover and normalize profiler rows.
The Ascend adapter reads CANN `msprof` SQLite data; the Hygon adapter reads
supported HIP profiler exports. Every normalized row retains source path,
table, and row provenance.

The Ascend implementation keeps provider boundaries explicit: SQLite/schema
foundation, evidence extraction, task/communication normalization, ACLGraph
reconstruction, replay reconstruction, split-profile ingestion, and the public
orchestrator are separate translation units. Monolithic and split schemas share
the reconstruction core without pretending that their evidence or fallback
contracts are identical.

## Native IR

Tables under `native/include/traceloom/ir/` hold strings, symbols, streams,
events, tasks, communication operations, graph templates, replay units,
anchors, tokens, and protected intervals. Typed IDs make cross-table links
explicit and validateable.

## Semantic Projection

The analysis layer chooses major compute, communication, and graph activity as
semantic anchors. Auxiliary control and preparation events remain available
for attribution without adding noise to the pattern sequence. This behavior is
governed by the versioned, unknown-first
[evidence-role projection contract](evidence-role-projection.md); structural
compression does not delete normalized observations or source provenance.

## Pattern Grammar

The sequence and pattern layers discover repeated fragments and commit macros.
Canonical analysis lowers the resulting grammar into a structural occurrence
graph: definitions, realized occurrences, ordered edges, and exact token
coverage. Protected intervals prevent grammar transformations from crossing
semantic boundaries.

## Cost Model

The structural projection layer aggregates exact per-occurrence packets.
Wall-clock totals use the union of overlapping stream intervals rather than
summing streams twice. Repeat averages divide by both node occurrence count
and loop-body repeat count, making loop nodes comparable with their children.

## Materialization

The default materializer writes a self-contained queryable database timeline.
It embeds raw evidence and exposes hierarchy, occurrence, cost, replay, issue,
and provenance relations in one SQL-addressable timeline. The materialized
coordinates are closed under user-composed analytical projections: one scope
can select one or all occurrences, remain folded or expand, enter supported
host context, and change compatible measure lens. The database publishes this
UX through `traceloom_projection_recipe`; Markdown and native JSON are explicit
presentations rather than alternate models. Typed selector discovery lives in
`traceloom_projection_parameter`; returned reusable coordinates and ready next
queries live in `traceloom_projection_coordinate` and
`traceloom_v_projection_continuation`. The compatibility sidecar remains
available for legacy workflows.

## Compatibility Boundary

The augmented database is the analytical product; no in-memory report object
is a second source of truth. Provider-neutral structural semantics live under
`analysis/structural_occurrence_*`, and SQL-row projection lives under
`compat/structural_projection_*`. The former `ReportTree` headers and row
builders remain as thin source-compatibility wrappers for embedding clients and
the optional Markdown renderer.

Persisted compatibility names such as `native_report_tree`,
`native-report-tree`, and the existing tree/semantic relations are deliberately
unchanged. Renaming an internal owner is not authority to break stored queries;
those names can move only through an explicit, versioned schema migration.

## Maintenance Boundaries

No hand-written translation unit in the structural/Ascend debt surface may
cross 1,000 lines again. In this surface, the remaining 800-line yellow-zone
units each own one append-order-sensitive family: structural grammar lowering,
structural SQL-row projection, replay reconstruction, split-profile ingestion,
or the declarative augmented projection catalog. Validation, token
construction, provider evidence, task normalization, graph reconstruction,
raw-database packaging, and orchestration have already been split away. New
responsibilities must form a new unit rather than making one of these family
owners grow.

## CLI

The installed public interface is deliberately singular:

```bash
traceloom <profile.db-or-profile-dir>
```

TraceLoom never launches workloads or `msprof`; it only analyzes existing
artifacts.

## Design Principles

- Preserve raw evidence and provenance.
- Compress repetition instead of printing every event.
- Materialize reusable coordinates rather than one fixed report.
- Keep cost arithmetic auditable and overlap-safe.
- Treat source/operator attribution as evidence, not certainty.
- Keep large raw traces and generated reports out of source control.
