# TraceLoom paper artifacts

This directory is the narrow exception to the repository's general rule
against committed profiler databases. Only deterministic, manifest-governed,
claim-preserving reductions may live here. A reduced database is accepted only
when its source hash, construction command, privacy audit, expected claim
fields, and full-versus-reduced equivalence check are recorded.

Generated TraceLoom reports and sidecars do not belong in Git. The target
reviewer workflow writes them beneath an explicit temporary output directory.

After building TraceLoom, run the complete CPU-only checkout ledger with:

```bash
examples/paper_artifacts/verify.py \
  --traceloom build/native-tests/native/traceloom
```

The repository deliberately uses complementary contracts rather than padding
one fixture:

| Artifact | Main DB size | Contract |
| --- | ---: | --- |
| `ascend_interleaved` | 1.69-1.86 MiB each | exact graph/interleaved-unit fidelity |
| `ascend_tp2_exact` | 15.60/23.55 MiB | exact TP2 composition, raw provenance, and communication localization |
| `ascend_tp2_fresh_negative` | 10.87-10.98 MiB each | preregistered six-rank negative, fail-closed current recovery |
| `ascend_mapped_gather` | 7.12 MiB/124 KiB | correctness-gated 512x target-operation perturbation |
| `../kickstart_smoke` | 36.42-38.52 MiB each | realistic ingestion and nested folding |

The medium pair is close to the requested 50 MB-per-profile review scale while
remaining natural data. Keeping the contracts separate prevents irrelevant
rows from making the exactness fixture look more representative without adding
evidence.

## Active exact-composition contract: Ascend TP2

The two-rank `ascend_tp2_exact` artifact turns the paper's strongest Ascend
exact-reconstruction result into a checkout-green claim. Each rank recovers 30
exact `H + L×35 + T` units covering 1,110 capture-instance-linked launches.
The verifier resolves all 1,110 host launch rows and 13,500 distinct task rows
per rank directly against the reduced source database and freezes the current
unknown-first Loop Tree.

The same verifier localizes the asymmetric rank cost to eight repeated
pre-graph `AllReduce` positions: replay envelopes differ by only 1.038x, while
the selected communication positions differ by 9.864x. All 280 selected
anchors per rank resolve to distinct bundled `COMMUNICATION_OP` source rows.

## Active negative contract: fresh Ascend TP2

The six-rank `ascend_tp2_fresh_negative` campaign makes the preregistered
stability failure reproducible from a checkout. Its hypothesis required every
rank to reproduce 30 exact units, 1,110 launch members, zero unknown regions,
and stable within-rank topology. None reproduced the cardinality. Current
analysis recovers only 27--28 exact period-one units, preserves one typed
missing-completion region in four profiles, and exposes a 9-task versus
411-task body mismatch. Passing the verifier means that this negative remains
faithful; it does not relabel the failed hypothesis as a success.

## Active perturbation contract: mapped gather

The `ascend_mapped_gather` pair packages the fixed 16 MiB transfer
microbenchmark and its correctness receipts. A shared artifact-scoped positive
selection rule exposes `Rep x35840 -> MEMCPY_ASYNC` and
`Rep x70 -> KvCacheBlockGather`, while direct source queries resolve all
35,840/70 target task rows and matching API rows. The 512x multiplicity change
is a local mechanism result, not a serving-speed claim.

The pair is a deterministic full-time-range reduction: it preserves every
primary event row, dependent semantic row, referenced string, table schema,
and source rowid while omitting only host identity and PMU bulk. An optional
reference mode verifies complete observation equality against the retained
full monolithic profiles. Split-layout parity remains a separately verified
external result rather than duplicating another approximately 100 MiB of input
into Git.

## Active reduction contract: Ascend interleaved structure

**Claim.** A small ordinary Ascend profiler SQLite can preserve exact graph
reconstruction together with a large and small productive sequence interleaved
between graph units. A stock/fused pair can preserve the representative rank-0
structural delta without retaining the full production capture.

**Reproduction layer.** Author-artifact packaging of an already reproduced
external-validity observation. This reduction does not create new performance
evidence.

**Expected observation.** Each reduced input retains four exact graph units and
the three complete intervening structural units from full-profile units
`G1..G4`/`U1..U3`. Corresponding complete units retain anchor membership,
fingerprint, and cost fields exactly. The stock/fused large unit remains
1186/994 anchors respectively.

**Evidence against the claim.** Any exact graph becomes typed unrecognized;
unit membership, fingerprint, or cost changes; the structural audit fails; a
required source-row link becomes orphaned; sensitive or unrelated strings
remain; or either database exceeds 10 MiB.

**Budget and stop rule.** Try at most three deterministic time-window/dependency
policies. Do not hand-edit a database. Stop and keep the inputs external if a
faithful pair cannot fit below 20 MiB combined without weakening capability
semantics or provenance.

`tools/reduce_ascend_sqlite.py` is the deterministic reducer. It copies the
original table schemas, preserves source `rowid` where SQLite exposes one,
keeps only time-overlapping primary evidence and its task/string dependencies,
retains relevant empty tables, and deliberately removes host identity and PMU
bulk that are not used by the scoped claim. When supplied, it also reconstructs
the small numeric `host/sqlite/stream_info.db` companion required for exact
capture-instance association.

The accepted pair and its reviewer workflow are in `ascend_interleaved/`.
`tools/verify_ascend_interleaved.py` analyzes both inputs from scratch, runs the
checked-in structural audit, checks exact graph capability, validates every
materialized source-row link, and compares stable unit fields plus ordered
logical membership hashes against the full-profile reference observations.
Generated reports and sidecars remain temporary.

## Active scale contract: Ascend nested folding

The sanitized medium pair under `../kickstart_smoke` retains 78,585,856 bytes
of real profiler input and 145,927 normalized events. Current TraceLoom reduces
those to 44,733 semantic anchors and 990 rendered tree nodes while recovering
both `Repeat x29 -> Repeat x74` and `Repeat x29 -> Repeat x24` on each device.
This is a structural-compression claim only; elapsed analyzer time is not part
of the contract.

`tools/verify_kickstart_folding.py` checks input hashes and privacy, regenerates
both reports, verifies exact event/anchor/node counts and nested repeats, and
requires at least 40 anchors and 100 normalized events per rendered node.

## Fixed workflow-comparison receipt

`workflow_comparison/` adds no new capture. It evaluates top-k aggregation,
direct raw SQL, repeat-only structural compression, and complete TraceLoom over
the same exact stock/fused pair. Its verifier freezes the directly observed
answers, including four exact units, the neutral interleaved order, zero
orphaned source links, and the opposite cost directions inside and between
graph units. The interpretation and explicit non-claims are recorded in
`../../notes/workflow-comparison-study.md`.

## Descriptive analysis cost

`kickstart_performance/` contains the five-process Release-build timing and
peak-RSS receipt for both medium profiles. It is a feasibility point, not a
primary paper claim or a cross-tool speed comparison. The measurement includes
the compact Loop Tree and the intentionally expansive provenance sidecar, and
retains the first run plus the full range rather than reporting only a warmed
best case.

The same directory contains a separate ordinary-report-path thread-scaling
receipt. On both medium profiles, eight workers give 2.728--2.881x TASK-row
loading speedup and 1.360--1.571x candidate-map speedup with byte-identical
Loop Trees across 1/2/4/8 threads. End-to-end speedup remains only
1.018--1.032x at eight threads because global evidence and materialization
stages dominate; the artifact freezes this limit rather than promoting it to a
linear-scaling claim.
