# Architecture

TraceLoom is a native C++17 offline profiler-analysis pipeline:

```text
profiler SQLite
  -> source adapters
  -> native columnar IR
  -> semantic anchors and protected sequences
  -> repeated-pattern grammar
  -> overlap-safe cost attribution
  -> Loop Tree / JSON / SQLite materializers
```

## Source Adapters

Adapters under `native/src/adapters/` discover and normalize profiler rows.
The Ascend adapter reads CANN `msprof` SQLite data; the Hygon adapter reads
supported HIP profiler exports. Every normalized row retains source path,
table, and row provenance.

## Native IR

Tables under `native/include/traceloom/ir/` hold strings, symbols, streams,
events, tasks, communication operations, graph templates, replay units,
anchors, tokens, and protected intervals. Typed IDs make cross-table links
explicit and validateable.

## Semantic Projection

The analysis layer chooses major compute, communication, and graph activity as
semantic anchors. Auxiliary control and preparation events remain available
for attribution without adding noise to the pattern sequence.

## Pattern Grammar

The sequence and pattern layers discover repeated fragments, commit macros,
and promote stable repeated bodies into a compressed report tree. Protected
intervals prevent grammar transformations from crossing semantic boundaries.

## Cost Model

The report layer aggregates exact per-occurrence packets. Wall-clock totals use
the union of overlapping stream intervals rather than summing streams twice.
Repeat averages divide by both node occurrence count and loop-body repeat count,
making loop nodes comparable with their children.

## Materialization

The default materializer writes `loop_tree_v2.md`. Advanced flags can also
write native result JSON, grammar diagnostics, and a compatibility SQLite
sidecar with evidence views.

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
- Keep cost arithmetic auditable and overlap-safe.
- Treat source/operator attribution as evidence, not certainty.
- Keep large raw traces and generated reports out of source control.
