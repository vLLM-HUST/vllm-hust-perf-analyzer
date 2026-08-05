# Milestone: productive structure between graph replays

Date: 2026-08-05

Status: **evidence reproduced; reader-facing structural promotion is TODO**.

## Why this matters

A retained two-rank serving capture appeared contradictory: client throughput
improved while the exact graph-body replacement window did not. Inspecting only
graph envelopes left the improvement outside the comparison denominator.

The device timeline was not:

```text
graph replay -> idle gap -> graph replay
```

It contained productive, repeated device work:

```text
graph replay -> large repeated task sequence -> graph replay
```

The current Loop Tree preserves this order. In one rank it exposes graph-unit
runs of `1, 3, 1, 1, 8, 1`, separated by four full repeated structures whose
interior is compressed as `Repeat x47`. Raw task shapes for the four structures
include leading dimensions `471`, `472`, `473`, and `474`; their stock task
counts alternate between `2105` and `2106`, while the changed implementation
has `1769` or `1770` tasks. All rows remain source-linked.

Those observations allowed an external analyst to explain the workload. That
explanation is deliberately **not** part of TraceLoom's contract.

## Responsibility boundary

TraceLoom owns observation-backed execution structure:

- protected graph replay units explicitly supported by profiler evidence;
- productive task sequences between protected units;
- exact and repeated structural bodies;
- order, occurrence count, duration, task cardinality, raw shapes, and source
  provenance;
- typed incomplete or unknown results when a boundary cannot be defended.

TraceLoom does not own workload-semantic or causal interpretation. Core output
must not label an observed unit as `decode`, `prefill`, `layer`, `request`, or
an optimization's cause. Those labels belong to a human or an analysis agent
that combines the structural artifact with workload and runtime context.

Profiler-native facts remain allowed: for example, an ACL graph replay may be
named a graph unit, an HCCL row may be named communication, and concrete raw
operator identities and shapes may be retained.

## Desired highest-level artifact

The primary human- and agent-readable view should make the observed composition
obvious without assigning workload semantics. A wide table is preferred over a
deep prose rendering for the highest level:

| order | structural node | kind | run | body fingerprint | task count | observed shape signature | total_us | evidence status |
| ---: | --- | --- | ---: | --- | ---: | --- | ---: | --- |
| 0 | `G1` | `graph_unit` | 1 | `graph:T1/body:B1` | 1024 | `S-graph-1` | observed | `exact` |
| 1 | `U7` | `structural_unit` | 1 | `H7 (contains Repeat x47)` | 2106 | `S471` | observed | `complete` |
| 2 | `G1` | `graph_unit` | 3 | `graph:T1/body:B1` | 1024 each | `S-graph-1` | observed | `exact` |
| 3 | `U8` | `structural_unit` | 1 | `H8 (contains Repeat x47)` | 2105 | `S472` | observed | `complete` |

Names such as `U7`, `H7`, and `S471` are stable structural identities, not
semantic roles. The table must link to expanded Loop Tree nodes and evidence
rows rather than replace them.

## TODO

### T1. Promote interstitial productive sequences

- Partition the global productive sequence around protected graph units.
- Preserve every nonempty interstitial task sequence; never collapse it into a
  generic gap or graph prelude.
- Promote a sequence to `structural_unit` only when its observed boundary and
  membership are complete. Otherwise emit a typed incomplete unit.
- Keep raw operator and shape evidence without interpreting their model role.

### T2. Build a unit-level composition grammar

- Run exact/repeat compression over the ordered unit sequence after protected
  graph units and structural units exist.
- Preserve unequal runs such as `G x1 -> U -> G x3 -> U -> G x1`.
- Group structurally related units into a family without erasing per-instance
  shape, task-count, or duration differences.
- Do not require two family members to be byte-identical when the existing
  grammar can prove a shared body plus explicit residuals.

### T3. Add a wide composition table

- Make the ordered, wide table the highest-level report map.
- Retain the existing hierarchical Loop Tree as the expansion/drill-down view.
- Include stable node/family IDs, order, run count, fingerprint, cardinality,
  shape signature, cost columns, completeness, and provenance handles.
- Keep Markdown easy to scan and expose the same normalized rows in JSON and
  the compatibility SQLite sidecar for agents.

### T4. Preserve the interpretation boundary in tests and documentation

- Add a golden with protected graph units interleaved with productive repeated
  sequences.
- Assert that all productive anchors appear exactly once in the top-level
  structural partition.
- Assert that no workload-semantic label is synthesized.
- Add an agent example that interprets the artifact only after reading external
  workload evidence and that states its inference separately from TraceLoom's
  observations.

### T5. Package the retained case for the paper

- Produce an immutable compact receipt for both stock and changed captures.
- Verify ordered unit membership, the four repeated structures, task counts,
  shape signatures, costs, and raw provenance.
- Show the flat profiler view, the wide structural table, and the analyst's
  separately labeled interpretation.
- Claim structural recovery and diagnostic leverage, not automatic phase or
  causal attribution.

## Paper-level takeaway

This case is stronger than a graph-speedup anecdote. TraceLoom preserved
productive work that a graph-only denominator made easy to overlook, exposed
its repeated structure and placement, and gave an analyst enough auditable
evidence to resolve an apparently contradictory performance result. The tool
recovered the structure; the analyst supplied the meaning.
