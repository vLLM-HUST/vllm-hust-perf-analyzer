# TraceLoom Publication-Readiness Roadmap

Status: active

Date: 2026-08-05

Target decision: EuroSys 2027 go/no-go by 2026-08-31

## North Star

TraceLoom should support one precise paper thesis:

```text
TraceLoom recovers compact, auditable execution structures from ordinary
accelerator-profiler artifacts while preserving cost, uncertainty, and
raw-row provenance.
```

The analyzer owns profiler-visible structure. It does not label a structural
unit as decode, prefill, a request, or an optimization mechanism unless such a
label is directly present in the input evidence. Humans and downstream agents
may make those interpretations from the neutral structure.

Submission readiness now depends more on closing evaluation and artifact
loops than on adding broad analyzer features.

## Evidence Snapshot

As of this roadmap:

- capability-complete Ascend artifacts support exact ACLGraph occurrence,
  body-template, and replay-composition recovery;
- capability-incomplete and truncated inputs remain typed unrecognized
  regions rather than becoming plausible-looking units;
- fresh CANN 9 captures preserve a preregistered negative stability result
  while a post-hoc compatibility path recovers the evidence that is actually
  present;
- mapped gather gives a correctness-gated, known structural perturbation;
- fused expert MLP exposes a large task-count change, but its local graph-body
  speed verdict remains inconclusive;
- a real Nsight node-level CUDA Graph export recovers five exact visible-body
  units, two templates, and the oracle schedule `A/B/A/A/B`, with corruption
  tests that fail closed;
- the repository's sanitized 78,585,856-byte kickstart pair supplies 145,927
  normalized events and a current, CPU-only folding oracle;
- a newly audited production pair now emits an exact neutral top-level
  structural partition. Across all four retained rank profiles, every
  productive anchor has exactly one unit owner; open trace boundaries remain
  typed unrecognized rows.

The CUDA evidence details are in `notes/cuda-graph-exact-evidence.md`. The next
CUDA collection task is specified in `notes/cuda-real-model-graph-handoff.md`.

## Submission Gates

All P0 gates must be complete for submission. P1 items strengthen the paper
but must not displace a failing P0 gate.

| ID | Priority | Gate | Current state | Completion evidence |
| --- | --- | --- | --- | --- |
| R1 | P0 | Neutral interleaved structural units | implemented, real-profile reproduced, and checkout golden verified | analyzer output, goldens, wide composition table |
| R2 | P0 | Fair workflow comparison | implemented, checkout-verified, and CTest-gated | one fixed question set answered by top-k, timeline/SQL, repeat-only, and TraceLoom |
| R3 | P0 | Patch/cost-shift case | bounded current case integrated; positive follow-up preregistered | correctness, structural delta, cost localization, bounded mechanism wording |
| R4 | P0 | Raw-row provenance for every central result | packaged Ascend exact/communication/perturbation rows green; fresh TP2 and CUDA external | one command verifies every central row from immutable input |
| R5 | P0 | Offline reviewer artifact | two-tier CPU-only ledger implemented and CTest-gated | clean CPU-only checkout reproduces exact and folding contracts |
| R6 | P1 | Million-row analysis-cost point | five-run Release receipt complete | time, peak RSS, input/output size, compression reported |
| R7 | P1 | Real-model CUDA Graph external validity | real model and graph evidence are separate | correctness-gated single-GPU model graph, exact visible-body report |
| R8 | P1 | Paper figures and final case integration | implemented and TeX-verified | final TeX contains pipeline and counterintuitive cost-localization figures |

CUDA Graph plus tensor parallelism, hidden CUDA graph-definition recovery,
CUDA idle taxonomy, and cross-stream dependency inference are not submission
gates.

## R1: Neutral Interleaved Structural Units

Implement the smallest representation that makes productive graph-external
work visible without assigning workload semantics.

Required output shape:

```text
ordered structural unit
  kind: graph | non_graph_productive | unrecognized
  occurrence/template identity
  begin/end and duration
  task/event counts
  nested Loop Tree root or graph body reference
  exact provenance boundary
  evidence and recognition status
```

The top-level sequence must preserve observations such as:

```text
G0 x1 -> N0 x1 -> G0 x3 -> N0 x1 -> G0 x1 -> N0 x1 -> ...
```

It must not rewrite `N0` as `prefill`, `mixed prefill`, or `request`.

Acceptance criteria:

- all covered productive events have exactly one owning structural unit;
- graph children are not double counted as neighboring non-graph work;
- incomplete boundaries become typed unrecognized units;
- repeated non-graph sequences use the existing structural grammar rather
  than a second ad hoc matcher;
- a wide table exposes order, kind, repetition, duration, task count,
  template, and status;
- source-row drill-down is available for every row;
- stock/fused goldens preserve the observed structural difference.

The detailed discovery and provisional table are recorded in
`notes/interleaved-structural-units-milestone.md`.

## R2: Fair Workflow Comparison

Use a fixed set of diagnostic questions, not subjective analyst-time claims.
Run each workflow over the same immutable inputs:

1. profiler/top-k aggregate;
2. raw timeline plus direct SQLite inspection;
3. repeat-only sequence compression;
4. TraceLoom's evidence-linked structure.

Questions should include:

- How many exact replay occurrences exist?
- What is the ordered composition of graph and non-graph productive work?
- Did an optimization change graph-body cost, graph-external cost, or both?
- Which raw rows support a reported unit?
- What unsupported or incomplete evidence remains?

Report whether the answer is directly materialized, derivable only through a
case-specific query, or unavailable. Do not claim that SQL or timelines are
incapable; the claim is that TraceLoom makes the reusable analysis object
explicit and auditable.

This comparison is now materialized in
`notes/workflow-comparison-study.md` and verified from the frozen Tier A pair
by `examples/paper_artifacts/tools/verify_workflow_comparison.py`. The receipt
checks top-k and raw candidate queries, a repeat-only capability ablation, the
complete typed unit sequence, source-link integrity, and the observed opposite
cost directions inside versus between exact graph units.

## R3: Patch And Cost-Shift Case

The primary case is the fused expert-MLP production pair:

- the macro result is positive;
- the exact graph replacement window does not show the expected speedup;
- complete productive units between graph runs do shorten and lose hundreds
  of tasks;
- the trace therefore localizes the benefit away from the originally assumed
  graph-internal path.

The paper may say that this observation is consistent with fusion and
launch/amortization benefits. A strict claim that fusion, rather than
cutthrough or pipeline behavior, is the unique cause requires a production
ablation that changes only that dimension.

The mapped-gather experiment remains the simpler known-perturbation case. It
should establish structural sensitivity, while the fused case demonstrates
that recovered structure can revise an initially plausible mechanism story.

The paper now carries that bounded case and its verified neutral `X/G/U`
figure. A strengthened fused operator is reserved as a separate prospective
positive microscopic gate: two matched captures per variant, correctness and
macro guards, a preregistered structural position, complete provenance, and a
`faster` verdict in both matched pairs. It may validate TraceLoom's positive
comparator path, but it must not overwrite the current inconclusive/corrective
case or become a submission blocker.

## R4-R5: Repository-Bundled Reviewer Artifact

Small and medium, reviewer-safe artifacts should live directly in the
TraceLoom repository when their value exceeds their Git-history cost. The
artifact is part of the paper argument, not an optional download recipe.

### Three artifact tiers

**Tier A: checkout fixtures.** Prefer this for canonical inputs needed by CI
or the paper's first-run path.

- target at most 10 MiB per newly added file and 30 MiB total per new bundle;
- preserve the profiler SQLite schema and all rows needed for the claim;
- include a manifest, immutable SHA-256, source/redistribution statement,
  oracle, expected summary, and verifier;
- do not require accelerator hardware or vendor runtime libraries.

The 3.55 MiB Ascend interleaved pair is now checked in with source manifests,
full-versus-reduced stable-field oracles, provenance checks, and an exact graph
verifier. The approximately 0.7 MiB Nsight node-level CUDA Graph SQLite remains
a candidate for the same tier.

The 39.15 MiB two-rank TP2 pair is also checked in as a Tier B exactness
artifact. It preserves all primary event rows while removing host identity and
PMU bulk. A CPU-only verifier checks 30 exact `H + L×35 + T` units, all 1,110
launch members, 1,110 host rows and 13,500 distinct supporting task rows per
rank, and the current unknown-first Loop Tree. The same command checks eight
pre-graph `AllReduce` positions, all 280 occurrences per rank, their distinct
raw `COMMUNICATION_OP` rows, and the frozen 1.038x replay versus 9.864x
communication contrast. Optional reference mode proves full/reduced exact
observation equality; split-layout parity remains external to avoid another
large duplicate input bundle.

The 7.24 MiB mapped-gather pair packages the controlled positive mechanism
case. Sanitized receipts preserve both correctness oracles and capture/build
identity. A shared positive-selection rule materializes
`Rep x35840 -> MEMCPY_ASYNC` versus `Rep x70 -> KvCacheBlockGather`, and the
verifier resolves every target task/API row directly from the bundled inputs.
The exact gather registration keeps the new operator out of the unknown audit;
the result remains bounded to local multiplicity rather than serving speed.

**Tier B: curated medium examples.** A 10--50 MiB input may be checked in when
it is uniquely useful as a user-facing example and cannot be represented by a
smaller faithful artifact. Review the history cost explicitly. The existing
kickstart pair is now sanitized and manifest-governed at 36.42--38.52 MiB per
database. Its checked verifier freezes 145,927 normalized events, 44,733
anchors, 990 rendered nodes, and the shared nested-repeat oracle; do not
duplicate it.

**Tier C: external full captures.** Large rank sets, full production captures,
`.nsys-rep` files, and redundant raw profiles remain release assets or
external bundles. The repository contains their hashes, manifests, compact
claim-preserving inputs where possible, and commands that fail explicitly if
the optional full bundle is absent.

### Reduction contract

A reduced SQLite is evidence only if:

1. its construction script is deterministic and checked in;
2. it retains table/column capability semantics, including relevant empty
   tables rather than silently deleting unavailable dimensions;
3. every retained row keeps its original table and row identity or an explicit
   mapping to it;
4. the full and reduced inputs produce identical claim fields;
5. the full input hash and comparison receipt are recorded;
6. no path, credential, prompt text, model weight, or unrelated user data is
   retained.

A hand-edited SQLite is not acceptable evidence.

### Proposed checkout layout

```text
examples/paper_artifacts/
  README.md
  verify.py
  ascend_interleaved/
    README.md
    expected.json
    stock/...
    fused/...
  tools/
    reduce_ascend_sqlite.py
    verify_ascend_interleaved.py
    verify_kickstart_folding.py
```

Generated reports go to an ignored output directory. Only small canonical
expected results belong in Git. Before an artifact-import change, reconcile
the repository's raw-profile hygiene rule through a narrow reviewed exception
for this manifest-governed directory; do not create a blanket exception for
`*.db` or `*.sqlite`.

### Reviewer command

The target interface is one CPU-only command from a clean checkout:

```bash
examples/paper_artifacts/verify.py \
  --traceloom build/native-tests/native/traceloom
```

It must verify input hashes, regenerate outputs, compare canonical summaries,
run provenance queries, and print a per-claim PASS/FAIL/SKIP ledger. Missing
optional Tier C inputs must be reported as unavailable, never counted as a
pass.

## R6: Million-Row Cost Point

Use `examples/kickstart_smoke` before collecting new data. Measure a release
build on the two checked-in databases and report:

- selected source rows and input bytes;
- wall time over at least five cold-process runs;
- median and range, without hiding first-run behavior;
- peak resident memory;
- report/sidecar bytes and structural node count;
- analyzer commit, compiler, CPU, and command.

The profile is capability-incomplete for exact graph-body recovery. That does
not invalidate it as a throughput and memory input, but the paper must keep
the scaling claim separate from semantic recovery quality.

The checked Release receipt is now in
`examples/paper_artifacts/kickstart_performance/kunpeng920-release.json`. Each
database contains more than 1.2 million raw SQLite rows; TraceLoom selects
86,701 and 59,226 normalized events. On a 192-logical-CPU Kunpeng-920 host with
two analysis threads, five fresh processes per input give median end-to-end
times of 14.898 s and 10.908 s and median peak RSS of 525.6 MiB and 419.5 MiB.
The timing includes parsing, analysis, Loop Tree and JSON rendering, and the
compatibility sidecar. The OS page cache was not cleared, and the full ordered
samples, including the first runs, are retained.

Treat this as a descriptive usability/feasibility point, not a central speed
claim or a cross-tool comparison. The 64--68 KiB Loop Trees are compact; the
206--258 MiB compatibility sidecars intentionally expand normalized evidence
and raw-row provenance and must not be presented as compressed storage.

## R7: CUDA External Validity

The required next evidence is one correctness-gated, single-GPU real-model
CUDA Graph trace. It is intentionally narrower than CUDA Graph plus TP. The
complete collection and return contract lives in
`notes/cuda-real-model-graph-handoff.md`.

Success upgrades the CUDA statement from a controlled real-API transfer
fixture to real-model external validity. Failure is still useful when it
identifies a profiler capability boundary, but it must not be normalized into
an exact unit.

## R8: Paper Integration And Visuals

Two figures carry the central argument:

1. **Recovery pipeline:** raw vendor rows to normalized evidence, protected
   structural units, Loop Tree, and raw-row drill-down.
2. **Counterintuitive optimization:** macro throughput improves; exact graph
   windows do not; interleaved productive non-graph units shorten and their
   task count collapses.

The second figure should align stock and fused unit sequences horizontally and
use neutral unit labels. Workload attribution belongs in the caption's
external interpretation, not in TraceLoom's emitted schema.

## Execution Order

1. ~~Implement and golden-test R1.~~ Complete.
2. ~~Freeze the minimum Ascend Tier A/Tier B artifact set.~~ Complete.
3. ~~Run the implemented R4-R5 ledger once in a fresh clone and retain the receipt.~~ Complete at `dd979a40fe52`; the expanded current ledger now checks 5/5 claims, including exact TP2 provenance, communication localization, and mapped-gather perturbation.
4. ~~Run R2 over the frozen inputs and materialize the comparison table.~~ Complete.
5. ~~Close R3 with the strongest wording supported by the available ablation.~~ Complete; retain the strengthened-operator capture as optional positive evidence.
6. ~~Measure R6 on the already checked-in kickstart profile.~~ Complete.
7. Integrate the CUDA return from R7 if it passes its evidence gate.
8. ~~Produce R8 and update the paper only from verified tables.~~ Complete for the pipeline and interleaved cost-localization figures.

R7 may proceed concurrently on a CUDA host. It must not block R1-R6.

## Stop Rules

- Do not add broad provider features unless a central evaluation row needs
  them.
- Do not make unknown counts disappear to obtain a cleaner figure.
- Do not use a reduction that changes the analyzed claim.
- Do not make TP CUDA Graph a deadline gate after the recorded hang.
- Do not promote mechanism language beyond the available ablation.
- If repository artifacts exceed the stated budget, keep one canonical
  checkout fixture and move the rest to a versioned release bundle.

## Definition Of Done

The paper is ready for the go/no-go decision when:

- every P0 row in the submission-gate table has a checked verifier output;
- the repository first-run path demonstrates both a recognized exact case and
  a typed unknown case;
- every number in the main evaluation table maps to an immutable input and a
  reproducible query;
- a reviewer can reproduce the core evidence offline without an accelerator;
- the workflow comparison demonstrates the missing analysis object rather
  than caricaturing existing tools;
- the flagship case shows that TraceLoom changes an engineering conclusion;
  and
- CUDA wording precisely matches the strongest completed evidence tier.
