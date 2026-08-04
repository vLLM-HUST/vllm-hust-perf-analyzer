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
working. Body-confirmed `H + L* + T` regions now drive default replay-unit and
Loop Tree projection. The legacy `capture_group_size` path remains only for
devices which have no strong exact H/L/T candidate; once exact evidence claims
a device, ambiguity or contradiction stays unknown and never falls back to the
weaker heuristic.

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
- All eight bounded real profiles complete. The two kickstart profiles now
  yield `1 + 8` and `1 + 6` exact 25-launch units respectively: one prefill,
  then decode repetitions. Their 11- and 10-launch suffixes remain
  `unrecognized_incomplete_tail`.
- Default report root wall-clock totals are identical before and after exact
  cutover on all eight profiles. Kickstart exact membership is 225 and 175
  launches, with roles `H=9,L=207,T=9` and `H=7,L=161,T=7`.
- Exact sidecar evidence identifies `exact_replay_composition`, the source
  region, and launch-member count instead of claiming legacy overlap
  reconstruction.

## Active Front

1. Complete legacy/incomplete-schema and truncation golden coverage without
   weakening exact promotion gates.
2. Carry the same exact/unknown distinction into higher-level report summaries
   only where it improves a concrete diagnostic, without fabricating events.

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
all four unknown statuses share one typed schema with candidate policy,
launch-occurrence bounds, observed/expected launch counts, time bounds, and a
forward-compatible raw payload. The status and policy spellings are shared
with native JSON rather than duplicated across materializers.

Unknown regions deliberately do not become fake graph replay rows or timeline
events. `traceloom_cuda_graph_replay` therefore retains its strong join to a
real `traceloom_event`, while consumers can audit unsupported structure with
`status LIKE 'unrecognized_%'`. Sidecar metadata separately exposes total and
unrecognized region counts.

On the two kickstart profiles the new table reports exactly `9 recognized + 1
incomplete(11/25)` and `7 recognized + 1 incomplete(10/25)`. The replay table
still contains only the 9 and 7 exact units. A compatibility fixture exercises
all five region statuses and proves that unknown rows neither create replay
units nor survive an evidence-table replacement with an empty result.

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

## 2026-08-04 Hierarchical Projection And Cost Gate

Exact replay units are now seeded as atomic semantic grammar symbols keyed by
`GraphTemplateId`. Their full source-token spans replace the protected H/L/T
intervals in the outer grammar, so `kNoCross` remains strict while higher
grammar can discover a repeated body containing both eager operations and one
ReplayUnit. Report lowering expands the semantic symbol back into a visible
`ReplayUnit T<n>` node with H/L/T children and folds a uniform layer run into
an inner `Rep xN`.

On the two kickstart ranks this exposes the decode body as
`[MatMul, AllReduce, BroadcastTo, ReplayUnit T2] x8/x6`; both prefill units and
decode units retain `H + Rep x23(L) + T`. Root wall-clock totals remain exactly
`67,662,917 us` and `56,556,040 us`.

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
decode units while preserving its incomplete H/L suffix as unrecognized. All
eight real-profile candidate counts, exact-unit counts, and root wall-clock
totals remain unchanged.

## Following Fronts

- legacy/incomplete CANN capability coverage;
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
