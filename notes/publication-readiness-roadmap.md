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
- the repository's kickstart example already supplies more than 1.18 million
  selected source rows and is a suitable no-new-capture scaling input;
- a newly audited production pair shows graph runs interleaved with complete,
  repeated, productive non-graph sequences. The current Loop Tree preserves
  much of the order but does not yet promote those sequences as neutral
  top-level structural units.

The CUDA evidence details are in `notes/cuda-graph-exact-evidence.md`. The next
CUDA collection task is specified in `notes/cuda-real-model-graph-handoff.md`.

## Submission Gates

All P0 gates must be complete for submission. P1 items strengthen the paper
but must not displace a failing P0 gate.

| ID | Priority | Gate | Current state | Completion evidence |
| --- | --- | --- | --- | --- |
| R1 | P0 | Neutral interleaved structural units | designed, not implemented | analyzer output, goldens, wide composition table |
| R2 | P0 | Fair workflow comparison | prose only | one fixed question set answered by top-k, timeline/SQL, repeat-only, and TraceLoom |
| R3 | P0 | Patch/cost-shift case | evidence discovered, paper loop open | correctness, structural delta, cost localization, bounded mechanism wording |
| R4 | P0 | Raw-row provenance for every central result | mixed checkout/external | one command verifies every central row from immutable input |
| R5 | P0 | Offline reviewer artifact | partial scripts and fixtures | clean CPU-only checkout reproduces key tables and one Loop Tree |
| R6 | P1 | Million-row analysis-cost point | input exists | time, peak RSS, input/output size, compression reported |
| R7 | P1 | Real-model CUDA Graph external validity | real model and graph evidence are separate | correctness-gated single-GPU model graph, exact visible-body report |
| R8 | P1 | Paper figures and final case integration | planned | final TeX contains pipeline and counterintuitive cost-localization figures |

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

The approximately 0.7 MiB Nsight node-level CUDA Graph SQLite is the first
candidate. A compact controlled Ascend oracle and a claim-preserving reduced
mapped-gather pair are also candidates.

**Tier B: curated medium examples.** A 10--50 MiB input may be checked in when
it is uniquely useful as a user-facing example and cannot be represented by a
smaller faithful artifact. Review the history cost explicitly. The existing
kickstart example is grandfathered and already covers the million-row scale
point; do not duplicate it.

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
  manifest.json
  verify.sh
  cuda_graph_visible_body/
    input.sqlite
    oracle.json
    expected.tsv
    provenance.sql
  ascend_graph_oracle/
    ...
  mapped_gather_delta/
    ...
```

Generated reports go to an ignored output directory. Only small canonical
expected results belong in Git. Before an artifact-import change, reconcile
the repository's raw-profile hygiene rule through a narrow reviewed exception
for this manifest-governed directory; do not create a blanket exception for
`*.db` or `*.sqlite`.

### Reviewer command

The target interface is one CPU-only command from a clean checkout:

```bash
./examples/paper_artifacts/verify.sh ./build/native/native/traceloom /tmp/traceloom-paper
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

1. Implement and golden-test R1.
2. Freeze the central claim set and choose the minimum Tier A artifact set.
3. Build the R4-R5 verifier and run it in a clean CPU-only environment.
4. Run R2 over the frozen inputs and materialize the comparison table.
5. Close R3 with the strongest wording supported by the available ablation.
6. Measure R6 on the already checked-in kickstart profile.
7. Integrate the CUDA return from R7 if it passes its evidence gate.
8. Produce R8 and update the paper only from verified tables.

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
