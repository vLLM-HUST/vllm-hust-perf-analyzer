# Fixed-Input Workflow Comparison

Status: reproduced and checkout-verified

Date: 2026-08-05

## Decision

Test the paper's claim that TraceLoom supplies a missing reusable analysis
object without claiming that raw timelines or SQL are incapable of answering
the same questions. The comparison asks a fixed question set of four workflows
over the same immutable stock/fused profile pair.

## Inputs And Workflows

The inputs are the two manifest-governed reduced profiles under
`examples/paper_artifacts/ascend_interleaved`. Their exactness, reduction
fidelity, hashes, and source-row provenance are independently checked by the
artifact ledger.

The workflows are:

1. **Profiler/top-k aggregate:** group raw device tasks by operation name and
   rank by summed duration.
2. **Raw timeline/SQL:** inspect the same profiler tables with case-specific
   joins and ordering queries.
3. **Repeat-only compression:** run the same structural grammar on an isolated
   copy of the main database while deliberately withholding the graph
   capability companion. This preserves device events and repeat discovery but
   prevents exact graph promotion.
4. **TraceLoom:** run with the complete ordinary-profiler evidence and emit
   typed units, evidence status, costs, and source links.

No analyst-time measurement is used. The result labels each answer as:

- **D — directly materialized:** present as a stable field or relation in that
  workflow's normal output;
- **Q — query-derived:** obtainable from the input, but only by constructing a
  question-specific timeline/SQL analysis; or
- **U — unavailable:** the workflow's output lacks the identity or evidence
  needed to answer the question without becoming a different workflow.

## Fixed Question Matrix

| Diagnostic question | Top-k aggregate | Raw timeline + SQL | Repeat-only compression | TraceLoom |
| --- | :---: | :---: | :---: | :---: |
| How many exact replay occurrences exist? | U | Q | U | D |
| What is the ordered graph/non-graph productive composition? | U | Q | U | D |
| Did cost shift inside graph bodies, outside them, or both? | U | Q | U | D |
| Which raw rows support each reported unit? | U | D | U | D |
| What evidence is unsupported or incomplete? | U | Q | U | D |

The `Q` entries are important: a sufficiently careful raw-SQL analysis can
reconstruct the case. TraceLoom's paper claim is not unique information. It is
that the typed, ordered, evidence-linked result becomes a reusable artifact
instead of remaining a one-off analyst program.

## Checked Observation

The conventional aggregate emphasizes a different story:

| profile | rank | operation | count | summed device duration |
| --- | ---: | --- | ---: | ---: |
| stock | 1 | `aclnnGroupedMatmulV5_GroupedMatmul_GroupedMatmul` | 480 | 55.664 ms |
| stock | 2 | `AivKernel` | 490 | 18.039 ms |
| stock | 3 | `aclnnMatmul_MatMulCommon_MatMulV2` | 725 | 11.356 ms |
| fused | 1 | `DispatchFFNCombineBF16` | 240 | 120.368 ms |
| fused | 2 | `AivKernel` | 490 | 72.369 ms |
| fused | 3 | `aclnnMatmul_MatMulCommon_MatMulV2` | 725 | 11.543 ms |

A direct raw query finds four `aclmdlRIExecuteAsync` candidates in each
profile. Candidate count alone does not prove exact graph bodies or identify
the work between them.

The repeat-only ablation still finds repeated sequence structure—33 root-level
repeat nodes for stock and 35 for fused—but promotes zero exact replay units.
Its four graph-related regions remain unrecognized. This is the discriminating
result: repetition is useful, but repetition alone cannot establish the typed
graph/non-graph partition.

With complete evidence, TraceLoom materializes four exact graph units, two open
boundary regions, and the same neutral unit order in both profiles:

```text
X1 -> G1 -> U1 -> G2 -> U2 -> G3 -> U3 -> G4 -> X2
```

Every checked source link resolves to an original raw row. In this bounded
slice, all three complete inter-graph units are shorter in the fused profile,
while all four exact graph units are longer:

| scope | stock total | fused total | delta |
| --- | ---: | ---: | ---: |
| exact graph units `G1..G4` | 99.183 ms | 107.331 ms | +8.148 ms (+8.21%) |
| complete inter-graph units `U1..U3` | 188.589 ms | 166.941 ms | -21.648 ms (-11.48%) |

This is structural localization, not a standalone performance or causality
claim. The reduced artifact preserves a representative slice rather than a
sampling protocol; it cannot establish run-to-run stability, end-to-end
throughput, or which optimization mechanism caused the shift.

## Reproduction

```bash
examples/paper_artifacts/tools/verify_workflow_comparison.py \
  --traceloom build/native-tests/native/traceloom
```

The verifier runs all four views from scratch in temporary directories,
compares the complete receipt to `expected.json`, verifies zero orphaned
source links, and asserts the direction of every individual graph and
inter-graph unit—not only the aggregate totals.

## Paper-Safe Conclusion

The supported statement is:

> On a fixed ordinary-profiler input, aggregates expose operator cost and raw
> SQL can derive the execution partition with bespoke analysis. Repeat-only
> compression preserves recurring sequences but cannot type exact replay
> boundaries. TraceLoom directly materializes the four exact graph occurrences,
> their ordered interleaving with three complete productive units, explicit
> open boundaries, unit costs, and raw-row provenance. That object localizes
> the observed cost reduction outside rather than inside the exact graph units
> in the retained slice.

Do not turn this receipt into an analyst-speedup number, a claim that SQL is
insufficient in principle, or proof that the same cost direction is stable in
the full workload.
