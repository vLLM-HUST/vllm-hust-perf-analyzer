# Reliable Exact Semantic Reconstruction Roadmap

Status: active

Date: 2026-08-04

TraceLoom's target is not zero unknowns. It is an auditable reconstruction in
which every reported semantic object has exact evidence, every inference names
its policy, cost is never double counted, and unsupported structure remains an
explicit unrecognized result instead of becoming a plausible-looking guess.

## Current Boundary

The input/provenance, anchor/grammar, overlap-safe cost, ACLGraph launch
identity, capture/replay body templates, and exact composition evidence are
working. Capability-complete, body-confirmed structural compositions drive
default replay-unit and Loop Tree projection; `H + L* + T` is optional shape
enrichment rather than an exactness gate. Exact body capability requires a
complete captured stream set plus classified identity for every observed body
task; a stable sequence on one visible stream is not enough to prove that the
graph has no other lanes. The legacy `capture_group_size` path remains only
where no completion-backed exact reconstruction is available.

The exact path currently recognizes both:

- a periodic suffix of one or more graph slots with every repetition
  revalidated against native replay bodies; and
- one decode-sized, independently body-confirmed one-shot leading composition
  (the observed prefill case).

The CUDA/Nsight path now supplies the same evidence discipline through a
different provider contract. Node-level exports directly correlate each
`cudaGraphLaunch` runtime row with graph-node kernel/memcpy activities. A raw
node set plus normalized visible stream-lane body repeated at least twice may
be promoted as an exact `CUDAGraph` unit. Singleton, ambiguous, missing-body,
and unsupported-activity cases remain typed unknown. This is exact recovery of
the **profiler-visible body**, not a claim to reconstruct hidden graph-definition
nodes. The real `A/B/A/A/B` fixture recovers five exact occurrences and two
templates; details and negative tests are recorded in
`notes/cuda-graph-exact-evidence.md`.

Every promoted unit links to its composition region and ordered launch/slot
membership. Leading context not covered by the one-shot rule, incomplete tails,
missing bodies, and body mismatches remain typed unrecognized regions.

## 2026-08-04 Promotion Evidence

- A checked-in synthetic Ascend profile proves separate one-shot prefill and
  periodic decode templates, exact H/L/T membership, protected unit bounds,
  and an unrecognized final prefix.
- A changed-body regression proves repeated graph identity cannot override a
  contradictory replay body.
- All eight bounded real profiles still analyze successfully. Six retain their
  prior unit/region outcomes after the capability gate.
- The two older kickstart profiles contain strong 25-launch H/L/T periodic
  evidence but no `CaptureStreamInfo` database. Their previous `9` and `7`
  promoted units were therefore stronger claims than the artifact supports:
  they are now typed unknowns rather than single-stream bodies presented as
  complete graphs.
- The retained capability-complete TP2 LLM showcase supplies the missing real
  decode evidence. Each rank has 74 captured graph instances with two model
  streams each and 1,110 completed launches. CANN leaves `NOTIFY_RECORD.modelId`
  unavailable in this package, but the record stream is a direct, unique member
  of exactly one `CaptureStreamInfo` model group. The explicit
  `record_model_stream` association recovers all launches without ordinal or
  timestamp inference.
- Both ranks independently recover the same exact 37-launch
  `H + L×35 + T` composition repeated 30 times. All 30 regions per rank become
  exact ReplayUnits with 1,110 ordered launch members; no region is unknown.
  The sidecars likewise contain 30 `exact_replay_composition` rows and 30
  recognized ledger rows per rank.
- Exact sidecar rows continue to identify `exact_replay_composition`, source
  region, and launch membership when promotion is supported. Capability-gated
  unknowns create no replay or timeline event.

## Active Front

1. Keep exact ACLGraph reconstruction contract- and performance-stable on
   large profiles while the next analyzer fronts move to provider-neutral
   lowered-op semantics and paper evaluation.
2. Capture a current-runtime prefill graph only if the runtime actually graphs
   prefill and a concrete consumer needs that additional real promotion case.
   Exact periodic decode promotion is now established on real TP2 LLM data;
   the one-shot prefill rule remains synthetic because this retained workload
   executes prefill outside the 30 decode graph replays.

The Loop Tree now carries the concrete summary that was previously missing:
total/recognized/unrecognized reconstruction regions, exact/legacy ReplayUnit
counts, and a typed status histogram. Unknown regions remain absent from the
tree itself, but are no longer invisible beside it; a kickstart report shows
`0 exact / 11 unrecognized`, while each capability-complete TP2 rank shows
`30 exact / 0 unrecognized`.

The main-pipeline idle front has reached E4 device-only explanation. E2
productive gaps are now projected through E3 per-stream state partitions into
an exact, non-overlapping explanation partition with frozen priority, evidence
level/relation, alignment status, and source lineage. Unknown collection
completeness conservatively disables absence claims; a complete synthetic
attestation enables `no_observed_device_work` only when every observed stream
is empty and the scan is complete. Unknown and ambiguous task coverage stays
typed `unattributed_visible_idle` with diagnostic lineage. The checked-in
host-wait counterexample now runs through E4 and proves that a host wait over a
fully productive device span creates no idle explanation.

Both real kickstart ranks run E1→E4 successfully. E4 takes 640 ms and 421 ms
in the recorded one-run audit and preserves the entire visible-gap duration;
the larger input improved from 18,009 ms after replacing full-timeline rescans
with indexed interval lookup. The device-only explanation semantics are no
longer the open front.

The production `traceloom` command now runs that E1→E4 path for Ascend Loop
Tree reports and renders a device-scoped `Visible Productive Idle Evidence`
section. The section reports analysis/collection/rule status, total gap time,
directly explained coverage, and every category including unattributed
residual. It remains separate from the compatibility tree's historical
prelude `idle_us`, preventing two different measurements from being silently
conflated. Ascend-specific rules are deliberately gated off for CUDA and Hygon
reports until those providers have validated taxonomies.

Exact anchor/node attribution is now implemented for the conservative
anchor-prelude view. E4 slices are intersected with the same disjoint prelude
windows used by Loop Tree cost packets; intersections aggregate through the
existing node/anchor coverage rows, while uncovered explanation time remains
an explicit device-only residual. The production report exposes the coverage
ratio and top hierarchical hotspots and warns that parent/child rows are not
additive. On the larger kickstart rank, 65,097,292,403 of 65,097,292,443 gap
nanoseconds map to anchor preludes; the remaining 40 ns is retained rather than
rounded into a node.

Raw idle evidence is now materialized in compatibility sidecars. One
deterministic `traceloom_run_metadata` row records the frozen analysis,
collection, ruleset, and attribution versions; its `run_id` is the lowercase
SHA-256 of the generated RFC-8785-canonical metadata JSON without `run_id`.
The sidecar also carries exact-nanosecond device intervals, per-stream state
partitions and universe completeness, E4 explanation slices linked to their
owning productive gaps, precise source-row evidence links, and anchor/node
aggregates. Host API rules are explicitly `not_loaded`, so this materializer
does not imply host correlation or collection completeness. Regenerating a
sidecar without a provider-validated idle pipeline clears these tables instead
of retaining stale Ascend conclusions.

The larger real kickstart rank materializes 30,628 productive intervals and
30,627 gaps. Explanation duration sums to the exact 65,097,292,443 ns gap
total; anchor aggregation remains 65,097,292,403 ns, the root-node aggregate
matches that value, all explanation-to-gap links resolve, and all 143,355
materialized source links resolve to compatibility timeline events. The next
idle-evidence front is therefore optional host/device correlation (only after
real clock calibration and host allowlist import), not more device-only
heuristics. The raw tables now have checked-in capability, summary, and audit
queries suitable for regression and paper evaluation.

Cross-stream reconstruction is intentionally **not** an active front. The
stable TraceLoom boundary is a coarse, permutation-invariant projection of
each captured stream into one readable lane sequence, followed by an unordered
lane set. TraceLoom does not need a cross-stream dependency graph to identify
replay templates or exact H/L/T compositions, and adding one would enlarge the
maintenance and compatibility surface without current evidence that it
improves those decisions. Revisit this only if an authoritative dependency
source and a concrete consumer require it; timestamps alone remain
insufficient.

## 2026-08-04 Typed Reconstruction Region Contract

The compatibility sidecar now publishes a complete ACLGraph reconstruction
ledger in `traceloom_aclgraph_reconstruction_region`. Recognized regions and
all six unknown statuses share one typed schema with candidate policy,
launch-occurrence bounds, observed/expected launch counts, time bounds, and a
forward-compatible raw payload. The status and policy spellings are shared
with native JSON rather than duplicated across materializers.

Unknown regions deliberately do not become fake graph replay rows or timeline
events. `traceloom_cuda_graph_replay` therefore retains its strong join to a
real `traceloom_event`, while consumers can audit unsupported structure with
`status LIKE 'unrecognized_%'`. Sidecar metadata separately exposes total and
unrecognized region counts.

On the two kickstart profiles the table now reports `10 missing body
capability + 1 missing completion` and `8 missing body capability + 1 missing
completion`. Their replay tables contain zero rows. A compatibility fixture
exercises all seven region statuses and proves that unknown rows neither create
replay units nor survive an evidence-table replacement with an empty result.

## 2026-08-04 Split CANN Semantic Parity

The split-profile adapter now feeds the same ACLGraph evidence pipeline as the
monolithic adapter: `ApiData` execute/capture/synchronization metadata,
`CaptureStreamInfo` model groups, `AscendTask` control and model tasks,
`TaskInfo` compute identity, and `HCCLTaskSingleDevice` communication identity.
Graph launches retain their actual per-device `AscendTask` source refs rather
than borrowing one profile-wide task source.

A portable paired fixture converts the same evidence into monolithic and split
layouts and requires identical launch identities, activity membership,
multi-stream body-template hashes, normalized HCCL topology, and exact H/L/T
promotion. Both layouts produce two composition candidates, four exact units,
and the same typed incomplete tail.

Five retained real split profiles independently match their monolithic body
template hashes. They cover the two-rank two-template experiment, concurrent
multi-context scheduling, segmented eager islands, and crossed compute/HCCL
lanes. The crossed split profile recovers the same two-stream template with
three compute and fourteen communication tasks. Split profiles deliberately do
not invoke the legacy capture-cardinality projector: without an exact
structural candidate, they retain launch/body evidence rather than manufacture
coarse ReplayUnits.

The retained TP2 ranks also exposed a report-granularity mismatch: split input
initially promoted all `3,655` HCCL device tasks as primary anchors because it
did not materialize the `398` observed collective-operation envelopes. The
adapter now joins `HCCLOP`/`HCCLOpSingleDevice` identity to the
`HCCLTaskSingleDevice`-identified task groups, uses the linked task envelope as
device geometry, and keeps every task as trace/auxiliary evidence. Both ranks
now match their monolithic event and anchor cardinality and their complete
structural report-node rows. The evidence and validation protocol are recorded
in `notes/split-fallback-communication-parity.md`.

A separate adapter-level missing-row golden removes both normalized body lanes
from one otherwise complete decode composition. The candidate and surrounding
units survive, the affected region becomes
`unrecognized_missing_body_evidence`, and only the other three complete units
are promoted. This proves the failure mode from raw schema rows through the
exact projector rather than only at an isolated IR contract.

A true trace-cutoff golden now removes the final `NOTIFY_RECORD` while leaving
the host launch, `MODEL_EXECUTE`, `NOTIFY_WAIT`, and body task rows intact. The
launch occurrence remains in the IR with unmatched completion evidence and is
published as `unrecognized_missing_completion_evidence` under the explicit
`incomplete_launch_evidence` boundary policy. It does not disappear at the
composition segment break, fabricate a graph identity, or disturb the four
earlier exact units.

## 2026-08-04 CANN Schema Capability Gate

Exact body reconstruction now distinguishes an observed empty dimension from
an unavailable profiler dimension. The minimum capability matrix is:

| Semantic dimension | Monolithic source | Split source | Exact behavior when unavailable |
| --- | --- | --- | --- |
| Device task timeline | `TASK` | `AscendTask` | Required input; incompatible schemas are rejected |
| Compute body identity | `COMPUTE_TASK_INFO` | `TaskInfo` | Candidate regions become `unrecognized_missing_body_capability` |
| Communication body identity | `COMMUNICATION_TASK_INFO` | `HCCLTaskSingleDevice` | An unclassified carrier makes the body unknown; fully classified tasks may prove an observed zero |
| Complete captured stream set | `CaptureStreamInfo` | `CaptureStreamInfo` | A visible single stream is not promoted as a proven complete body |
| Host submission order | `CANN_API` | `ApiData` | Optional; exact reconstruction may continue in `device_execution_order` |
| Per-launch completion identity | `NOTIFY_WAIT` + `NOTIFY_RECORD` | corresponding `AscendTask` controls | The launch becomes `unrecognized_missing_completion_evidence` |

Optional tables which exist with incompatible columns are treated as
unavailable capabilities rather than causing an incidental SQL exception.
When a completion-backed graph launch exists but any body capability is
missing, its device cannot fall through to the capture-cardinality projector.
Older capture-only inputs with no completion-backed exact launch retain their
explicitly legacy reconstruction path for compatibility.

Portable goldens cover an incompatible compute table, a missing communication
table with an unclassified `SDMA` task, a missing capture-stream database, an
incompatible split `TaskInfo`, and a profile with no host API table. The first
four retain all 14 launch
members as five typed unknown regions and promote no ReplayUnits; the last
still promotes four exact units using device execution order.

## 2026-08-04 Golden SQL Audit Contract

Three checked-in, executable SQL reports freeze the paper-facing evidence
surface. `reconstruction-capability-matrix.sql` reduces the typed region ledger
and replay rows into capability, body, completion, ordering, recognition, and
legacy outcomes. `idle-evidence-summary.sql` emits exact category totals and
visible-gap shares. `idle-evidence-audit.sql` checks interval arithmetic,
per-gap partition and non-overlap, stream adjacency, source/owner lineage,
evidence extents, anchor/node references, and root conservation before
returning `PASS` or `FAIL`.

The SQL compatibility golden fixes the column contracts and known-good values,
then corrupts one explanation duration and requires the audit to change from
`PASS` to `FAIL`. Supporting identity indexes keep lineage checks bounded on
large sidecars.

The matrix separates the two important real outcomes without interpretation:
the older kickstart artifact reports `capability_incomplete`, unavailable body
capability, incomplete completion evidence, 11 typed unknown regions, and zero
promoted units; a capability-complete TP2 split rank reports available body and
completion evidence, device-execution ordering, 30 recognized regions, 30
exact ReplayUnits, and zero unknown or legacy units. The TP2 idle sidecar also
passes every audit invariant over 40,055 device intervals, 37,160 explanation
slices, and 221,377 evidence links. Its 47,986,216,440 ns visible gap is
conserved exactly; 47,191,604,280 ns maps to anchor/root preludes and
794,612,160 ns remains explicit device-only residual.

That real split profile also contains zero-duration CANN anchor events. They
are retained as ordering observations that may own the immediately preceding
prelude but contribute no productive duration; only inverted intervals are
rejected. This prevents valid point events from aborting sidecar materialization
without weakening duration conservation.

The second TP2 rank establishes a separate negative-result boundary: E2 can
retain a valid productive projection while E3 rejects damaged unknown point
events and marks the combined E4 analysis `invalid_input`. Sidecar
materialization now records that conservative joined status instead of
requiring every stage status to be identical and aborting. SQL
`audit_status=PASS` remains table-integrity evidence only; it never promotes an
`invalid_input` analysis into a positive semantic result.

## 2026-08-04 Hierarchical Projection And Cost Gate

Exact replay units are now seeded as atomic semantic grammar symbols keyed by
`GraphTemplateId`. Their full source-token spans replace the protected H/L/T
intervals in the outer grammar, so `kNoCross` remains strict while higher
grammar can discover a repeated body containing both eager operations and one
ReplayUnit. Report lowering expands the semantic symbol back into a visible
`ReplayUnit T<n>` node with H/L/T children and folds a uniform layer run into
an inner `Rep xN`.

Before the schema capability gate, the two kickstart ranks exposed the decode
body as `[MatMul, AllReduce, BroadcastTo, ReplayUnit T2] x8/x6`. That result
remains useful structural evidence, but it is no longer emitted as exact
because those artifacts cannot prove the captured stream set. The hierarchical
ReplayUnit path is covered by capability-complete synthetic fixtures and by
the retained TP2 LLM ranks, which independently promote 30 exact units.

The report-tree validator now checks that every structural node's children
exactly tile its span, atoms own one token, every occurrence has one matching
coverage row, and the root covers the complete token sequence. Normalized
native report costs also enforce a wall-clock invariant: root additive cost
must equal the per-device timeline envelope. A semantic-unit fixture covers
overlapping graph compute and communication, proves flat and hierarchical
roots both report `50 us`, and verifies that a deliberately perturbed cost is
rejected.

## 2026-08-04 Multi-Stream Body Evidence

Replay body templates now consume every captured model stream owned by the
linked graph instance rather than only the stream carrying the completion
record. Each stream is canonicalized independently and the template identity
is a permutation-invariant lane multiset. The published policy is explicitly
`captured_stream_set_unordered`; it does not fabricate dependency edges or
serialize concurrent lanes by timestamp. Launches without captured-instance
evidence remain `single_model_stream`.

Communication topology is no longer dropped. Stable HCCL family/task-type
steps participate alongside compute operations, while volatile communication
name suffixes and variable control tasks remain outside primary identity.
Templates and occurrences publish separate compute, communication,
normalized-total, and stream counts.

The crossed two-lane real profile now yields one shared body template per two
launches with `2` streams, `3` compute tasks, and `14` communication tasks. A
generated multi-stream exact-HLT fixture still promotes one prefill and three
decode units while preserving its incomplete H/L suffix as unrecognized. Six
bounded real profiles retain their prior outcomes; the two older kickstart
profiles are deliberately downgraded because their absent capture-stream
metadata cannot establish a complete lane set.

## Following Fronts

- provider-neutral lowered-op and graph semantics, beginning with the CUDA
  workspace handoff while keeping the Ascend evidence contract authoritative;
- paper claim/evaluation tables built only from checked SQL and retained
  artifacts;
- portable ambiguity, truncation, lineage, and cost-conservation regression
  fixtures as new provider/schema variants appear;
- versioned JSON/SQLite/report contracts and large-profile performance gates;
- optional host/device correlation only after real clock calibration and a
  provider-validated host allowlist exist.

## Promotion Gate

The exact path may replace legacy output only when every promoted replay unit
has a complete composition, exact launch membership, stable body template,
valid device interval, non-overlap with peer units, and conservation of child
event and wall-clock cost. Failure of any condition yields an unrecognized
result, not fallback reconstruction.
