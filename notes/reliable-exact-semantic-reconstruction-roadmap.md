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
working. Capability-complete, body-confirmed `H + L* + T` regions drive
default replay-unit and Loop Tree projection. Exact body capability requires a
complete captured stream set plus classified identity for every observed body
task; a stable sequence on one visible stream is not enough to prove that the
graph has no other lanes. The legacy `capture_group_size` path remains only
where no completion-backed exact reconstruction is available.

The exact path currently recognizes both:

- a periodic decode suffix with every repetition revalidated against native
  replay bodies; and
- one decode-sized, independently body-confirmed one-shot leading composition
  (the observed prefill case).

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
   large profiles while the next analyzer fronts move to main-pipeline idle
   explanation and provider-neutral lowered-op semantics.
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
not invoke the legacy capture-cardinality projector: without an exact H/L/T
candidate, they retain launch/body evidence rather than manufacture coarse
ReplayUnits.

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
ReplayUnit path remains covered by capability-complete synthetic fixtures
until a fresh LLM trace supplies the missing real evidence.

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

- capability-complete real LLM H/L/T capture and promotion;
- main-pipeline idle explanation and Loop Tree aggregation;
- provider-neutral lowered-op and graph semantics;
- portable S1-S6, ambiguity, truncation, and cost-conservation
  golden fixtures;
- versioned JSON/SQLite/report contracts and large-profile performance gates.

## Promotion Gate

The exact path may replace legacy output only when every promoted replay unit
has a complete composition, exact launch membership, stable body template,
valid device interval, non-overlap with peer units, and conservation of child
event and wall-clock cost. Failure of any condition yields an unrecognized
result, not fallback reconstruction.
