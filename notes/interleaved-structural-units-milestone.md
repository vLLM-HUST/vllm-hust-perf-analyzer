# Milestone: productive structure between graph replays

Date: 2026-08-05

Status: **T1/T3 baseline implemented and reproduced on four retained Ascend
profiles; unit-level grammar, shape evidence, and immutable artifact packaging
remain**.

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

## Highest-level artifact

The primary human- and agent-readable view should make the observed composition
obvious without assigning workload semantics. A wide table is preferred over a
deep prose rendering for the highest level:

| order | structural node | kind | run | family | body fingerprint | anchor count | shape signature | total_us | evidence status |
| ---: | --- | --- | ---: | --- | --- | ---: | --- | ---: | --- |
| 0 | `X1` | `unrecognized` | 1 | `XF1` | `H...` | observed | `unavailable` | observed | `unrecognized_open_prefix` |
| 1 | `G1` | `graph_unit` | 1 | `GF1` | `H...` | 1 | `unavailable` | observed | `exact` |
| 2 | `U1` | `structural_unit` | 1 | `UF1` | `H...` | 1186 | `unavailable` | observed | `complete` |

Names such as `U1`, `UF1`, and the `H...` fingerprint are structural
identities, not semantic roles. The table must link to expanded Loop Tree nodes
and evidence rows rather than replace them.

## TODO

### T1. Promote interstitial productive sequences

Implemented on 2026-08-05. The partition is exact over productive anchors;
open prefix/suffix regions are emitted as typed `unrecognized` rows rather
than exceptions or silently completed units.

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

Implemented baseline on 2026-08-05 in Markdown, native JSON, and the
compatibility sidecar. The sidecar contains normalized exact anchor membership;
Markdown abbreviates long expansion lists while retaining their full sidecar
form. Shape signatures remain explicitly `unavailable` pending direct shape
materialization.

- Make the ordered, wide table the highest-level report map.
- Retain the existing hierarchical Loop Tree as the expansion/drill-down view.
- Include stable node/family IDs, order, run count, fingerprint, cardinality,
  shape signature, cost columns, completeness, and provenance handles.
- Keep Markdown easy to scan and expose the same normalized rows in JSON and
  the compatibility SQLite sidecar for agents.

### T4. Preserve the interpretation boundary in tests and documentation

The synthetic golden now checks graph/non-graph interleaving, adjacent graph
folding, exact one-owner anchor membership, typed open boundaries, stable
families, expansion links, and absence of synthesized `decode`/`prefill`
labels. A repository-bundled stock/fused input golden is still pending T5's
artifact reduction.

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

## 2026-08-05 retained-profile receipt

The new partition was run against both ranks of the retained stock/fused pair.
All four sidecars passed exact membership conservation: the number of unit
memberships, distinct member anchors, `traceloom_anchor` rows, and the sum of
unit `anchor_count` were equal.

| variant | rank | graph rows | complete structural rows | complete anchors | large family | small family |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| stock | 0 | 15 | 14 | 5104 | 4 × 1186 anchors, avg 191405 us | 10 × 36 anchors, avg 2458 us |
| stock | 1 | 15 | 14 | 5104 | 4 × 1186 anchors, avg 185225 us | 10 × 36 anchors, avg 2188 us |
| fused | 0 | 18 | 17 | 4444 | 4 × 994 anchors, avg 161669 us | 13 × 36 anchors, avg 2212 us |
| fused | 1 | 18 | 17 | 4444 | 4 × 994 anchors, avg 166842 us | 13 × 36 anchors, avg 2361 us |

This is strong structural evidence that the externally aligned large
graph-external unit changes from 1186 to 994 productive anchors per occurrence
and shortens in both fused ranks. That cross-variant alignment is an analyst
step, not an emitted workload label. The unequal graph/unit occurrence counts
mean this table is not itself a paired end-to-end performance verdict.

Input identities (full captures remain external):

| input | bytes | SHA-256 |
| --- | ---: | --- |
| stock rank 0 | 54099968 | `e5bca17ff92c81d3a3b9ef3dc52dd2b358e6114e2ebcb221453c1d07369888f0` |
| stock rank 1 | 54112256 | `49a9715146c1335780ddfcf768304f2a55b9ea4e5756c9d76825ab30de12f6ca` |
| fused rank 0 | 40873984 | `bc982546ffc9ae3718421db6b212024c100ebc981862d8acf109767c162dd3c1` |
| fused rank 1 | 40660992 | `adb4548466c5000b20d5c449c79e703c029a5297c149abc36050b3c9ccd74669` |
