# TraceLoom Paper Claim Evidence Audit

Status: active

Date: 2026-08-04

Target framing:

> TraceLoom recovers observation-backed, auditable execution structures from
> accelerator profiler artifacts.

This ledger separates what the current analyzer and retained artifacts prove
from older report observations and planned platform validation. It is the
paper-facing claim gate: a claim may be promoted only when its conditions,
query or report surface, artifact, and negative boundary are all named.

## Outcome Vocabulary

- **REPRODUCED**: observed under the current analyzer and faithful artifact
  contract.
- **NOT_REPRODUCED**: a faithful current rerun contradicts the scoped claim.
- **INSUFFICIENT_EVIDENCE**: an old report or partial artifact exists, but the
  current contract cannot support the claim.
- **GATED**: implementation or external-validity evidence is deliberately
  required before the paper may make the claim.

## Current Claim Ledger

| ID | Scoped claim | Current outcome | Evidence boundary | Paper action |
| --- | --- | --- | --- | --- |
| C1 | Every promoted ACLGraph ReplayUnit has typed composition-region, launch-membership, body-template, interval, and raw-row evidence; unsupported regions remain typed unknown rather than fabricated units. | **REPRODUCED** | Native contract/unit tests, schema-capability goldens, and real sidecars. | Safe as the central exact-reconstruction claim. |
| C2 | Each retained TP2 LLM rank reconstructs 30 exact `H + L×35 + T` regions covering 1,110 completed launches, with zero unknown or legacy units. | **REPRODUCED** | Current full split-directory rerun: both ranks report available body/completion capability, 30 recognized regions, 30 exact units, and 1,110 observed/expected launch members. | Safe when the split-profile capability requirement is stated. |
| C3 | Older kickstart profiles support exact ACLGraph promotion. | **NOT_REPRODUCED** | Their missing `CaptureStreamInfo` makes body capability unavailable; current output is 10 missing-body-capability regions plus one missing-completion region and zero ReplayUnits on the larger rank. | Retain only as a conservative capability-gate/negative example. |
| C4 | The device-only visible-gap analysis is an exact, non-overlapping, source-linked partition of profiler-visible productive gaps. | **REPRODUCED** | Checked SQL audit and real sidecars pass arithmetic, partition, extent, lineage, and anchor/root conservation. Collection status remains `unknown`; one TP2 split-layout result is explicitly `invalid_input` because E3 found damaged unknown point events. | Safe only with “visible productive gap,” not hardware idle or causality; a positive semantic claim additionally requires `analysis_status=ok`. |
| C5 | Current exact TP2 Loop Trees reproduce the older report's single `GraphReplayUnit x30 at ≈35 ms` and single `AllReduce x280` nodes. | **NOT_REPRODUCED** as written; diagnosis **REPRODUCED** | Current monolithic reports split the 30 exact units into two context-specific `x15` runs and retain eight distinct pre-graph AllReduce positions, each repeated 35 times. Their 280 combined occurrences preserve the 9.864× cross-rank skew. | State the more precise current structure; do not quote the obsolete merged node shape. |
| C6 | Prefix hot/cold, output-tail, mixed-interference, overload, and eager/graph case-study counts remain valid under the current exact capability gate. | **INSUFFICIENT_EVIDENCE** | Existing counts come from older Loop Trees; most artifacts have not been rerun through the current capability matrix and audit surface. | Keep as historical observations or rerun; do not present them as current exact-reconstruction evaluation yet. |
| C7 | TraceLoom implements a general runtime relation graph with `contains`, `repeat_of`, `prelude_of`, `neighbor_of`, `covers`, `derived_from`, and `changed_with`. | **NOT_REPRODUCED** | Durable tables cover tree containment/coverage, provenance, prelude attribution, and graph envelopes; there is no uniform relation-graph contract and no implemented cross-run `changed_with`. | Frame these as concrete typed tables/relations, not one completed general graph. |
| C8 | CUDA/Nsight node traces support the same observation-backed exact visible-replay-structure contract as the Ascend path. | **REPRODUCED** for controlled transfer and real-model external validity | Two independent correctness-gated Qwen3.5-0.8B Nsight 2024.4 node exports each recover five exact occurrences, one 9,881-member template, 49,405 source-linked members, and zero reconstruction unknowns. The checkout graph-level companion retains five replay boundaries but zero exact bodies; controlled missing-child/unsupported-activity mutations also fail closed. | Use as bounded two-provider evidence. Say “profiler-visible body,” not hidden CUDA Graph definition, all Nsight versions, CUDA idle, or universal platform independence. CUDA Graph TP remains unsupported by the shared report path. |
| C9 | TraceLoom folds a medium ordinary-profiler input into compact, nested, auditable execution structure. | **REPRODUCED** | The sanitized 78,585,856-byte kickstart pair deterministically maps 145,927 normalized events to 44,733 anchors and 990 rendered nodes; both devices recover `x29 -> x74` and `x29 -> x24`. A CPU-only verifier checks hashes, privacy, counts, ratios, and nesting. | Safe as structural-compression evidence. Do not turn the current wall time into a performance claim without the isolated R6 protocol. |
| C10 | TraceLoom directly materializes a reusable typed execution partition that top-k and repeat-only views do not, while raw SQL can derive it with case-specific analysis. | **REPRODUCED** | The fixed stock/fused workflow receipt checks four views over identical immutable inputs. It recovers four exact graph units, `X1/G1/U1/G2/U2/G3/U3/G4/X2`, explicit open boundaries, and zero orphaned source links; the repeat-only ablation retains 33/35 repeat nodes but promotes zero exact units. | Safe as the fair-workflow claim. Do not claim unique information, analyst-time speedup, full-workload stability, or causality from the retained slice. |
| C11 | TraceLoom can analyze the repository's million-row medium profiles and materialize fully auditable outputs at practical offline cost. | **REPRODUCED** as a descriptive single-host point | Five fresh Release processes per input on Kunpeng-920: 1.26M/1.23M raw rows, 14.898/10.908 s median wall time, and 525.6/419.5 MiB median peak RSS with two threads. The scope includes Loop Tree, JSON, and 258/206 MiB provenance sidecars; warm OS cache was allowed and first runs are retained. | Safe as feasibility context only. Do not make it a central performance claim, cross-tool comparison, or hardware-independent rate. |
| C12 | TraceLoom's deterministic parallel front end accelerates ingestion and candidate mapping without changing recovered structure. | **REPRODUCED** for stage scaling; whole-tool scaling **NOT REPRODUCED** | Five fresh Release processes per 1/2/4/8-thread point on both medium profiles. At eight threads, TASK loading is 2.728--2.881x and candidate mapping is 1.360--1.571x faster; all Loop Trees are byte-identical. End-to-end speedup is only 1.018--1.032x because global stages dominate. | Safe as a practical deterministic-partitioning result. Do not claim linear or substantial end-to-end speedup, parallel grammar induction, or platform-independent scaling. |
| C13 | Owned partitions alone provide strong pattern-discovery scaling on million-token protected sequences. | **NOT REPRODUCED** at the preregistered 4.0x/8-thread threshold; deterministic execution **REPRODUCED** | The clean 90-process campaign reaches 2.348x scan speedup at four million tokens with identical hashes. Eight million occurrence rows then collapse to 200 summaries through a 5.87 s serial global sort, limiting scan+reduce to 1.152x. | Retain as the optimization baseline. Do not use it as a strong-scaling claim; evaluate partition-local reduction under a new preregistration. |
| C14 | Partition-local reduction preserves candidate semantics while making large protected-sequence pattern discovery scale. | **REPRODUCED** under the follow-up preregistration | All 90 optimized samples match baseline counts plus diagnostic/summary hashes. At four million tokens (7,999,949 occurrences), map/reduce reaches 7.752x at 8 threads and 24.665x at 32; one-thread time improves 2.443x and maximum median RSS falls 66.1%. | Safe as deterministic pattern-stage strong scaling on one generated workload/host. Do not convert it into full-analyzer, grammar, or cross-hardware scaling. |

## Pre-Registered Current TP2 Check

### Decision

Determine which TP2 paper statements survive the capability-complete exact
path, and whether the older communication-skew case remains a valid Loop Tree
diagnosis or must be downgraded to a historical legacy observation.

### Faithful Conditions

- analyzer commit: current `main` after the golden SQL contract;
- input: each retained TP2 `PROF_*` directory, loaded as
  `ascend_sqlite_split` so `CaptureStreamInfo`, `TaskInfo`, device controls,
  and communication identity are all available;
- outputs: one current Loop Tree and compatibility sidecar per rank;
- audits: `reconstruction-capability-matrix.sql` and
  `idle-evidence-audit.sql`.

### Expected Observation

- 30 recognized exact regions, 30 exact ReplayUnits, zero typed unknown and
  zero legacy units per rank;
- 1,110 ordered launch members per rank;
- equivalent `H + L×35 + T` shape on both ranks;
- if the old diagnostic claim survives, the current structural reports expose
  a comparable 280-occurrence AllReduce neighborhood with approximately 9.9×
  rank skew.

### Evidence Against

- any missing capability, unknown region, legacy replay, membership mismatch,
  or failed SQL invariant refutes the exact reconstruction statement;
- absence of a comparable AllReduce structural position, or a materially
  different ratio under the current exact projection, refutes the old
  Loop-Tree communication-skew wording even if raw communication imbalance is
  still present.

### Budget And Stop Condition

CPU-only offline analysis of the two retained ranks, at most one fresh
sidecar/report per rank and the checked SQL queries. No NPU capture or broad
parameter sweep is justified for this decision.

### Protocol Amendment After Split-Layout Observation

The split rerun satisfied the pre-registered exact-capability criteria but
produced a much larger tree than the normal monolithic-preferred CLI path.
This exposed a layout-parity question that the original contract did not
separate. We therefore ran one additional current monolithic sidecar/report per
rank, without changing any capability or diagnosis criterion, to distinguish
evidence parity from renderer compactness. The results below label those two
contracts separately rather than selecting whichever output looks better.

## Current TP2 Result

Outcome for C2: **REPRODUCED**.

Both split-profile ranks independently report:

```text
capability_state:             capability_complete
body_capability:              available
completion_capability:        available
ordering_mode:                device_execution_order
recognized regions:           30
unrecognized regions:         0
exact ReplayUnits:             30
legacy ReplayUnits:            0
observed/expected launches:    1,110 / 1,110
launches per region:           37
```

The normal CLI prefers the monolithic DB when its `TASK` table is usable. On
that current paper path, rank 2 renders 157 structural nodes and rank 3 renders
155. Both reports fold the 30 exact ReplayUnits into two context-specific
`x15` runs rather than the obsolete single `x30` node.

Outcome for the exact legacy node wording in C5: **NOT_REPRODUCED**; outcome
for its communication diagnosis: **REPRODUCED with a more precise shape**.

The current monolithic Loop Trees retain eight distinct pre-graph AllReduce
positions, each repeated 35 times. Summing those position-preserving nodes
gives 280 comparable occurrences: 763,455.460 us on rank 2 and 7,530,699.500 us
on rank 3, a 9.864× skew. Exact replay-envelope averages are 27,241.419 us and
28,271.932 us respectively, only 1.038× apart. The supported paper statement
is therefore that graph replay envelopes are similar while the eight repeated
AllReduce positions carry a strong cross-rank imbalance—not that one merged
`AllReduce x280` node exists.

Direct split-sidecar totals provide a broader corroborating observation:

| rank/device | exact replay total | exact replay average | raw communication events | raw communication total | raw communication average |
| --- | ---: | ---: | ---: | ---: | ---: |
| rank 2 / device 2 | 817,242.580 us | 27,241.419 us | 3,655 | 731,881.600 us | 200.241 us |
| rank 3 / device 3 | 848,157.960 us | 28,271.932 us | 3,655 | 7,703,767.780 us | 2,107.734 us |

The raw communication total and average differ by 10.526×. This broader number
includes all communication events, so the position-preserving 9.864× Loop Tree
comparison is the preferred diagnostic evidence.

The split-directory fallback now reaches the same exact capability **and**
primary report structure. The previous 2,512/2,510-node outputs were caused by
promoting all `3,655` HCCL device tasks because split input lacked explicit
collective-operation events. TraceLoom now combines observed
`HCCLOP`/`HCCLOpSingleDevice` identity with linked task-derived device
geometry. Rank 2 now has 133,292 events, 3,119 anchors, and 157 report nodes;
rank 3 has 236,946 events, 3,115 anchors, and 155 report nodes. For each rank,
the monolithic and split outputs have identical structural report-node rows,
anchor histograms, and collective-duration multisets. All 3,655 communication
task events remain available as detail, with 3,646 nonzero rows linked as
auxiliary evidence and nine zero-duration rows retained only as raw events.

Both monolithic paper-path idle sidecars have `analysis_status=ok` and pass
every SQL invariant. The rank-3 **split** tables also pass every structural SQL
invariant, but correctly carry `analysis_status=invalid_input`: E3 found 209
zero-duration unknown task rows, so its observed stream scan is incomplete.
This is a useful distinction:
`audit_status=PASS` proves materialized-table integrity, not that the semantic
input was valid. The analyzer now materializes this auditable negative result
instead of aborting because E2 and E3 have different run statuses.

## Paper Corrections Already Required

The current draft's graph-replay design description is stale if it says that
capture-time host API sequences form the replay dictionary. Exact ACLGraph
promotion now uses completion-backed launch occurrences, complete
`CaptureStreamInfo` lane sets, per-lane compute/communication body identity,
and exact `H/L/T` composition. Host API order is optional; the exact path can
use `device_execution_order`.

Likewise, “gap” must not be defined as generic idle inferred from neighboring
events. The implemented evidence surface distinguishes overlap-safe historical
prelude cost from the E1→E4 partition of **visible productive gaps**, preserves
unattributed residual, and records collection completeness explicitly.

## Highest-Value Remaining Evidence

1. Generate paper tables only from checked SQL plus current retained outputs.
2. Rerun old case-study artifacts individually only when the result can change
   paper scope; capability-incomplete artifacts are still useful negative
   evidence and need not be forced into exact promotion.
3. Report the passed CUDA visible-body audit alongside its explicit boundary:
   node-level single-GPU graph evidence plus eager TP/NCCL evidence, with no
   successful CUDA-Graph TP trace and no CUDA idle-semantics claim.
