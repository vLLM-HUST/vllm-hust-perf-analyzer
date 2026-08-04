# ACLGraph Replay Activity, Unit, And Template Research

Status: Exact hierarchical H/L/T units and coarse unordered multi-stream lane
bodies promoted; cross-stream dependency topology intentionally out of scope

Date: 2026-08-04

## Decision This Research Must Support

TraceLoom needs to distinguish structurally different ACLGraph templates inside
one profile without erasing legitimate multi-unit replay structure or inventing
template differences from concurrent/interleaved profiler evidence.

The next native implementation must not assume any of these cardinalities:

```text
one host execute == one replay activity
one replay activity == one replay unit
one replay unit == one capture slot
one replay activity == one structural template
```

The earlier claim that every exact host execute should immediately materialize
one replay unit was too strong. Exact execute evidence may help identify launch
or activity lineage, but unit count and unit boundaries require separate proof.

## Refined Semantic Model

The first controlled follow-up shows that four identities are not enough. Keep
at least these identities separate:

```text
Capture slot / graph instance
  -> one captured NPUGraph/model identity and its capture-side definition

Graph launch occurrence
  -> one aclmdlRIExecuteAsync / MODEL_EXECUTE occurrence
  -> resolves to one captured graph instance when evidence permits

Replay unit occurrence
  -> one protected higher-level execution cycle
  -> contains 1..N graph launch occurrences
  -> examples include a complete piecewise-model H/L*/T launch cycle

Replay activity/window
  -> a coarse provider-visible burst or application envelope
  -> contains 1..N replay unit occurrences

Slot template
  -> structural identity of an individual captured/replayed graph slot

Replay composition template
  -> ordered or partially ordered references to slot templates
  -> example: [H, L*27, T] or [T1, T2, T1]
```

A slot template may be reused by many launch occurrences. A replay composition
template may be reused by many replay units, and a replay activity may contain
multiple replay-unit occurrences. Individual slot-template identity and
whole-unit composition identity must not overload the same `GraphTemplateId`.

## Evidence Already Established

1. The legacy ACLGraph schema explicitly permits one replay activity/window to
   contain multiple replay units and treats activity boundaries as soft parent
   metadata.
2. Replay-unit boundaries are protected structures for replay tiling and H/L/T
   attribution; H/L/T subslots are the report-facing flat anchors.
3. The current native Ascend adapter assigns one profile-global capture-derived
   signature to every materialized replay unit when capture evidence exists.
   That construction cannot discriminate unit templates inside the profile.
4. Existing native semantic fixtures prove multi-unit activity lineage, but do
   not prove same-profile discrimination among multiple structural templates.
5. Execute, activity, unit, and template counts observed in existing profiles
   differ. Count differences alone do not identify which mapping is wrong.

Primary prior contracts:

```text
../ascend-llm-realworkload-prof/drafts/refactor/40_graph_semantics/
  aclgraph_replay_unit_schema.md
  aclgraph_fixture_plan.md
```

## 2026-08-04 Controlled Two-Template Experiment

### Pre-registered shape

Two ranks on two Ascend 910B2 devices each captured two graph instances:

```text
A capture body: Muls, Adds, Relu
B capture body: Muls, Subs, Relu, Sigmoid
```

Each rank then executed six application-level activity envelopes with ten total
graph launches:

```text
[A]
[B]
[A, A]
[A, B]
[B, A]
[A, B]
```

All output checks passed on both ranks. Raw profiler databases and helper
analysis remain under the ignored local research directory:

```text
build/research/aclgraph-cardinality/
```

### Raw cardinality result

Each device reported exactly:

```text
capture slots:          2
host execute calls:    10
MODEL_EXECUTE:         10
NOTIFY_WAIT:           10
NOTIFY_RECORD:         10
graph instances:        2
replay body signatures: 2
```

The graph-instance occurrence sequence recovered independently from both
devices was exactly the application oracle:

```text
A, B, A, A, A, B, B, A, A, B
```

The two replay-side body signatures were stable and matched the capture-side
distinction:

```text
A: Add:1, Mul:1, Relu:1
B: Mul:1, Sub:1, Relu:1, Sigmoid:1
```

### Control handshake identity bridge

The trace exposes a stronger graph-instance bridge than the current native
adapter uses:

```text
CANN_API aclmdlRIExecuteAsync
  -- same connection id --> MODEL_EXECUTE
  -- same connection id --> NOTIFY_WAIT
  -- completion adjacency --> NOTIFY_RECORD
  -- stable identity ------> graph connection id, model id, model stream
```

`MODEL_EXECUTE`/`NOTIFY_WAIT` connection ids identify the launch occurrence and
normally change on every launch. `NOTIFY_RECORD.connectionId` remains stable for
the captured graph instance. Its `modelId` and model stream identify the body
stream. On the controlled trace, `NOTIFY_RECORD.endNs` preceded the paired
`NOTIFY_WAIT.endNs` by only 440--640 ns.

This bridge recovered all 57 launches across six current-schema databases:

| Evidence set | Launches | Graph instances | Strong bridge matches |
| --- | ---: | ---: | ---: |
| controlled two-template, two devices | 20 | 4 rank-local ids | 20 |
| earlier single-template oracle | 4 | 1 | 4 |
| concurrent two-lane graph profile | 15 | 2 | 15 |
| segmented four-graph profile | 16 | 4 | 16 |
| crossed two-graph profile | 2 | 2 | 2 |

Across those rows the largest absolute wait/record completion delta was 640 ns.
The host execute and device `MODEL_EXECUTE` connection-id multisets were equal
in every bounded profile.

### Capture-side model grouping

`CaptureStreamInfo` must be grouped by `(device_id, model_id)`, not treated as a
flat set of model streams. A captured graph can own multiple model streams. In
the crossed HCCL profile, two capture windows correspond to two model ids but
four `CaptureStreamInfo` rows (two streams per model).

In all bounded current-schema profiles, ordering capture windows alongside
model groups by their capture timestamps produced the expected capture/model
order. This is useful candidate evidence, but remains weaker than an explicit
provider object-id link and should publish its ordinal policy/confidence.

The current native adapter loses this evidence twice:

1. `load_aclgraph_capture_info` flattens every model stream into one per-device
   set, discarding model-group membership.
2. The native `TASK` reader does not select `TASK.modelId`, and `TaskRow` has no
   field for it.

Without model id, TraceLoom cannot reliably collect a multi-stream graph body or
join a `NOTIFY_RECORD` back to the corresponding capture-side model group.

### Real LLM trace transfer check

The two checked-in kickstart LLM databases use an older/incomplete schema, but
the same control relation remains visible:

```text
NOTIFY_WAIT / NOTIFY_RECORD pairs: 421
completion-adjacent pairs:         420
largest adjacent delta:            760 ns
order-only fallback:                 1 (239.74 us; wait already satisfied)
```

After an initial 25-launch graph set, both devices show an exact period-25
sequence of stable `NOTIFY_RECORD.connectionId` values. The retained suffixes
contain respectively:

```text
8 complete replay cycles + 11 trailing launches
6 complete replay cycles + 10 trailing launches
```

This is direct identity-sequence evidence for reconstructing piecewise replay
cycles. It is stronger than cutting the body at a fixed `Index`/`Attention`
midpoint and naturally preserves a truncated trailing cycle as incomplete.

### Current native result on the controlled trace

Current native TraceLoom inferred `capture_group_size=2`, emitted five replay
units, and assigned one template on each device. The five fixed pairs are:

```text
[A, B]  # incorrectly crosses the two singleton application activities
[A, A]
[A, B]
[B, A]
[A, B]
```

This proves two distinct failures without assuming that one launch is one
replay unit:

1. Fixed capture-count grouping cannot represent variable activity/unit
   cardinality and can cross a known application boundary.
2. The profile-global capture signature erases two independently observed slot
   templates even though both capture and replay bodies distinguish them.

It does **not** prove that every `MODEL_EXECUTE` is a replay unit. It proves that
every accepted handshake is a graph launch occurrence which must be preserved
before higher-level unit/activity reconstruction.

## Highest-Value Unknowns

1. Which raw evidence defines one replay activity in each observed CANN schema?
2. Why can one activity contain multiple units: capture batching, graph/model
   decomposition, H/L/T grouping, stream topology, profiler aggregation, or a
   combination?
3. Can the notify handshake be reconstructed with explicit ambiguity handling
   under arbitrary concurrency, missing rows, and already-satisfied waits?
4. Which evidence can prove unit count before attempting timestamp splits?
5. Which evidence can prove unit boundaries without using body similarity as
   both the splitter and the template classifier?
6. Is unit-template identity always singular, or must matching retain multiple
   candidates/partial coverage?
7. Which observed variation is legitimate template structure and which is
   boundary leakage from concurrent or incomplete traces?

## Competing Hypotheses

### H1: Execute-led activity identity

Exact host/device execute correlation identifies replay activities, while each
activity requires an independent 1..N unit split.

Evidence against H1 would be a source-instrumented logical replay that produces
multiple unrelated execute correlations which only form one valid activity.

### H2: Provider-wave activity identity

One replay activity is a provider/device execution wave that may aggregate
multiple host executes or model executions. Execute rows are members, not
activity boundaries.

Evidence against H2 would be stable one-to-one execute/activity containment
across controlled single-, multi-unit, and concurrent probes.

### H3: Capture-group-derived unit count

Capture group structure predicts replay unit count, but timestamp boundaries
still require replay-side proof.

Evidence against H3 would be controlled replays whose stable unit count differs
from capture-group-derived expectations.

These hypotheses are not mutually exclusive across profiler versions or graph
shapes. The adapter may need typed policies selected by available evidence.

## Evidence-Gated Experiment Matrix

Ground truth must be recorded at the workload call site before inspecting the
trace. The smallest useful matrix is:

| Case | Logical replay calls | Expected unit composition | Purpose |
| --- | ---: | --- | --- |
| S1 | 1 | `[T1]` | establish the minimal activity/execute/unit mapping |
| S2 | 1 | `[T1, T1]` | repeated unit template inside one replay |
| S3 | 1 | `[T1, T2]` | distinct unit templates inside one replay |
| S4 | 2+ | repeated S2/S3 | template reuse and replay composition stability |
| S5 | concurrent | known per-lane compositions | detect boundary leakage/interleaving |
| S6 | incomplete evidence | known composition | require ambiguity instead of silent merge |

For every case, retain a correlation ledger rather than only aggregate counts:

```text
logical replay id
host API source row and connection id
device MODEL_EXECUTE/control source rows
model id and stream identities
candidate activity envelope
expected and effective unit counts
boundary proof and confidence
unit body/dictionary signatures
template candidates, winning evidence, and losing candidates
```

## Pre-Registered Interpretation

- A raw count mismatch is a diagnostic, not proof of undercount or overcount.
- A global profile signature may describe capture context, but cannot serve as
  the primary identity of every replay unit template.
- Body similarity must not be the only evidence for both unit splitting and
  template classification; that would make the conclusion circular.
- When unit count or boundaries lack proof, preserve an unsplit/ambiguous
  activity with diagnostics rather than fabricate precise units.
- Primary unit-template fingerprints exclude timing, absolute stream ids,
  replay count, activity count, and profile-global capture count.
- Sequence/topology hashes remain diagnostics until controlled evidence shows
  that they represent stable structural identity.

## Implementation Gate

S1-S3 now establish launch-occurrence identity and prove that fixed capture-size
pairing can cross real activity boundaries. They do not yet establish a universal
activity or replay-unit boundary rule.

The next native slice should therefore materialize an evidence table for graph
launch occurrences and the notify handshake before changing replay-unit
grouping. It should preserve:

```text
launch connection id
graph connection id
model id / model stream
MODEL_EXECUTE, NOTIFY_WAIT, NOTIFY_RECORD source refs
wait/record completion delta
match policy and confidence
capture-slot candidate
```

After that, unit reconstruction can tile the launch-identity sequence against
capture groups/composition candidates. It must publish the chosen boundary
policy, confidence, incomplete suffixes, and losing alternatives. Same-profile
slot-template classification must not be hidden by one profile-global capture
signature.

## 2026-08-04 Exact Projection Result

The gated implementation has now crossed its bounded promotion gate for the
single-stream H/L/T evidence class.

### Recognition and failure semantics

The native IR now preserves exact region membership and revalidates the replay
body of every slot in every repetition. A graph-identity match with a changed
body becomes `unrecognized_body_mismatch`; missing body evidence, leading
context, and incomplete tails have separate statuses. None silently become a
partial unit.

Complete body-confirmed regions materialize `ReplayUnit` rows linked to their
source region and one ordered launch-member row per composition slot. Template
identity hashes exact replay bodies plus H/L/T roles rather than raw graph
connection ids, so equivalent structure can remain stable across ranks.

The exact projector claims a device as soon as a strong H/L/T candidate exists.
The legacy fixed-cardinality projector may still serve other devices in the
same database, but cannot reinterpret ambiguity or contradiction on the
claimed device.

### One-shot prefill

The kickstart leading 25 launches are not an incomplete or arbitrary prefix.
On both devices they independently form `H + L*23 + T`, have exactly the same
cadence as the confirmed decode period, and carry different H/L replay bodies
from decode. They are therefore represented by a separate
`exact_one_shot_leading_composition` candidate and template. This rule requires:

1. a body-confirmed periodic H/L/T suffix;
2. leading length exactly equal to the suffix period; and
3. an independently complete H/L/T body shape in that leading window.

It does not generalize arbitrary leading context into prefill.

### Bounded real-profile matrix

All eight research profiles complete after cutover. Only the two kickstart
profiles meet the exact H/L/T promotion gate:

```text
device 0: prefill 1 + decode 8, 225 exact launch members,
          tail 11/25 unrecognized
device 1: prefill 1 + decode 6, 175 exact launch members,
          tail 10/25 unrecognized
```

The other profiles retain their existing legacy or composition-evidence
behavior. Default Loop Tree root wall-clock totals are unchanged on all eight
profiles, providing a bounded conservation check across the cutover.

### Remaining grammar boundary

Each exact unit is protected as H/L/T children under `kNoCross`. In the real
kickstart sequence, three eager anchors (`MatMul`, `AllReduce`, `BroadcastTo`)
also occur between graph compositions. The current flat grammar therefore
shows each H/L/T unit separately and does not form an outer repeat spanning
those eager anchors. Relaxing `kNoCross` would permit semantically unsafe
partial mixing. The next correct step is a hierarchical unit node which is a
single parent symbol for outer grammar while retaining exact H/L/T children
and membership evidence.

## Native Implementation Checkpoint

The launch-occurrence evidence gate above is now implemented without changing
the existing replay-unit boundary heuristic.

The native IR now preserves `TASK.modelId` (including split `AscendTask`
profiles), normalizes the legacy unsigned invalid sentinel to `-1`, and stores
one `GraphLaunchOccurrenceRow` per device `MODEL_EXECUTE`. Each occurrence keeps:

```text
CANN_API source ref/row and launch connection id
MODEL_EXECUTE / NOTIFY_WAIT / NOTIFY_RECORD TaskId lineage
stable graph connection id
model id and normalized execute/model StreamId
launch interval and signed wait/record completion delta
explicit match policy
```

The adapter uses two deliberately distinct policies:

1. `notify_completion_adjacent`: unique same-device wait/record matching within
   10 us, minimized by absolute completion-time delta.
2. `notify_ordered_fallback`: order-preserving matching only when the remaining
   waits and records on a device have equal cardinality.

Missing or ambiguous evidence remains `unmatched`; the adapter does not invent
a graph id, model id, or model stream. Result JSON publishes total, strong,
fallback, and unmatched launch counts plus the complete occurrence ledger with
source rows, raw stream ids, identities, timing deltas, and match policy.

Validation against the controlled and transfer profiles matched the independent
raw-SQL audit exactly:

| Evidence | Occurrences | Strong | Ordered fallback | Unmatched |
| --- | ---: | ---: | ---: | ---: |
| controlled two-template device 0 | 10 | 10 | 0 | 0 |
| controlled two-template device 1 | 10 | 10 | 0 | 0 |
| earlier single-template oracle | 4 | 4 | 0 | 0 |
| concurrent multi-context | 15 | 15 | 0 | 0 |
| segmented four-graph | 16 | 16 | 0 | 0 |
| crossed two-graph | 2 | 2 | 0 | 0 |
| kickstart LLM device 0 | 237 | 236 | 0 | 1 |
| kickstart LLM device 1 | 186 | 184 | 1 | 1 |

The controlled sequence therefore exists natively as ten launch occurrences
with two stable graph/model identities instead of being irreversibly reduced
to five fixed pairs. The old replay-unit output is intentionally unchanged for
now: consuming this sequence is the next semantic step, and must not happen
until a unit boundary policy can abstain on the singleton/multi-launch activity
mixture rather than merely replacing one fixed-size guess with another.

## Capture Model Groups And Slot Templates

The native adapter now preserves three identities that the old flat stream set
conflated:

```text
GraphSlotTemplate
  -> canonical capture-body API sequence

CapturedGraphInstance
  -> one exact (device_id, model_id) CaptureStreamInfo group
  -> references zero or one GraphSlotTemplate

CapturedGraphStream
  -> every original/model stream member of that graph instance
```

Capture-window ordinal association is intentionally conditional. It is accepted
only when one device is represented, capture-window and model-group counts are
equal, every group has a timestamp, and timestamps are strictly increasing.
Otherwise the model group is retained with policy `model_group_only` and no
slot template is invented.

This distinction matters in both directions:

- The controlled two-template profile has two graph instances and two slot
  templates. Its native launch sequence is now exactly `T0,T1,T0,T0,T0,T1,
  T1,T0,T0,T1` on both devices.
- The crossed multi-stream profile has two graph instances and four member
  streams, but only one structural slot template because the two capture API
  sequences are identical. Graph-instance identity is therefore not falsely
  promoted into a template difference.

The bounded current-schema matrix produced:

| Evidence | Slot templates | Graph instances | Member streams | Launches linked |
| --- | ---: | ---: | ---: | ---: |
| controlled device 0 | 2 | 2 | 2 | 10/10 |
| controlled device 1 | 2 | 2 | 2 | 10/10 |
| single-template oracle | 1 | 1 | 1 | 4/4 |
| concurrent multi-context | 1 | 2 | 2 | 15/15 |
| segmented four-graph | 1 | 4 | 4 | 16/16 |
| crossed multi-stream | 1 | 2 | 4 | 2/2 |

The older kickstart profiles lack usable replay `modelId`/capture groups, so
they retain graph-connection identity without fabricated instance links.

## Exact Periodic-Suffix Composition Candidates

TraceLoom now mines a conservative replay-composition **candidate** from each
contiguous, identified per-device launch segment. This does not create replay
units or claim that the selected phase is an application boundary.

The policy is:

```text
identity = captured graph instance when every launch is linked
           otherwise stable graph connection id

order = complete sync-bounded host submission order when available
        plus device execution order when concurrency changes that order

accept only an exact periodic suffix with at least three full repeats
rank by maximum covered suffix, then repeat count, then minimum period
split evidence at every launch with missing graph identity
retain leading launches and incomplete trailing launches explicitly
```

The resulting behavior is deliberately asymmetric:

| Evidence | Candidate result |
| --- | --- |
| controlled singleton/multi-launch mixture | abstain |
| segmented four-graph trace | device order: abstain after a concurrent swap |
| crossed two-launch trace | abstain |
| single-template oracle | period 1, four full repeats |
| concurrent multi-context | device order: period 2, four full repeats, one trailing launch |
| segmented four-graph trace | host order: period 4, four full repeats |
| kickstart device 0 | 25 leading, period 25, eight full repeats, 11 trailing |
| kickstart device 1 | 25 leading, period 25, six full repeats, 10 trailing |

This resolves the immediate multi-unit hazard correctly: strong repeated
composition is retained as a candidate, while the controlled variable-cardinality
sequence is not forced into pairs or singleton units. The candidate records
whether its order came from device execution or host submission; neither order
is silently treated as universal under concurrency.

## Sync-Bounded Host Launch Activities

The next evidence slice preserves host submission batches without promoting
them into replay units. Within each CANN `globalTid`, TraceLoom groups consecutive
`aclmdlRIExecuteAsync` calls up to the next explicit blocking API:

```text
aclrtSynchronizeStreamWithTimeout
aclrtSynchronizeStream
aclrtSynchronizeDeviceWithTimeout
aclrtSynchronizeDevice
```

Each `GraphLaunchActivity` retains the first and last execute source rows, the
blocking boundary source row/API, host thread, interval, raw host execute count,
matched launch count, and ordered launch-occurrence members. A final unterminated
thread group is retained as `host_thread_tail` rather than assigned a fabricated
sync boundary. Host calls without a matched device occurrence remain visible in
the activity count difference.

The bounded matrix produced:

| Evidence | Host activities | Member launches | Host group sizes |
| --- | ---: | ---: | --- |
| controlled device 0 | 6 | 10 | `1,1,2,2,2,2` |
| controlled device 1 | 6 | 10 | `1,1,2,2,2,2` |
| single-template oracle | 4 | 4 | four singletons |
| concurrent multi-context | 15 | 15 | fifteen singletons |
| segmented four-graph | 4 | 16 | four groups of four |
| crossed multi-stream | 1 | 2 | one group of two |
| kickstart device 0 | 300 | 237 | 300 singletons; 63 unmatched host executes |
| kickstart device 1 | 300 | 186 | 300 singletons; 114 unmatched host executes |

The controlled group sizes exactly recover the application oracle, independently
of capture count. The segmented trace exposes a second important ordering fact:
device start-time order swaps two concurrently scheduled graph instances in one
wave, while host submission order is the same four-instance composition in all
four activities. TraceLoom now evaluates periodic candidates in both orderings
when they differ. This recovers period 4 repeated four times from the host order,
while preserving the multi-context period-2 candidate that exists only in device
execution order.

The kickstart result is the decisive limitation: every host execute is followed
by a blocking sync, yet the stable replay composition still spans 25 graph
launches. Therefore a sync-bounded host activity is useful independent lineage
and ordering evidence, but **not** a universal replay-unit boundary. The unit
materialization gate therefore cannot be opened from host batching alone.

## Replay Body Templates And H/L/T Confirmation

TraceLoom now derives a replay-side body template independently for every
identified launch that has a model stream. The initial promoted slice used
normalized compute rows; the later multi-stream extension also admits
communication rows with stable family/task semantics. Variable-count control
tasks such as `CAPTURE_WAIT`, graph-control `NOTIFY_RECORD`, and memory-value
operations remain excluded. Timing, task ids, connection ids, raw model
streams, and replay count are excluded from primary identity.

This gives two independent template views when the current schema provides
both:

```text
capture slot template = capture-side host API sequence
replay body template  = replay-side normalized per-lane semantic sequence
```

On both controlled devices, all ten launches produced exactly two replay body
templates (three and four compute ops), and their occurrence labels agreed
launch-for-launch with the two capture slot templates. The single-template,
multi-context, segmented, and crossed profiles each produced one replay body
template, so graph-instance identity was not falsely promoted into structural
template variation.

The older kickstart traces produced five replay body templates on both ranks:

| Phase/role | Compute ops | Stable normalized shape |
| --- | ---: | --- |
| prefill head | 22 | head sequence containing two `Slice` ops |
| prefill layer | 13 | layer sequence containing two `Slice` ops |
| shared tail | 6 | `MatMulV2, AddRmsNormBias, MatMulV2, SwiGlu, MatMulV2, AddRmsNormBias` |
| decode head | 20 | head sequence without the two `Slice` ops |
| decode layer | 11 | layer sequence without the two `Slice` ops |

The initial 25 launches are exactly:

```text
prefill-head, prefill-layer * 23, tail
```

Every complete period-25 suffix cycle on both ranks is exactly:

```text
decode-head, decode-layer * 23, tail
```

All 25 slot roles were stable across eight complete cycles on device 0 and six
complete cycles on device 1. The normalized compute sequence had one variant
per slot and rank; only duration and excluded control-task multiplicity varied.

Composition candidates now publish `shape_policy=head_repeated_layer_tail` only
when all slot bodies exist, the middle body template is exactly repeated, and
head, layer, and tail templates are pairwise distinct. Slots are explicitly
labelled `head`, `layer`, or `tail`. None of the controlled, single-template,
multi-context, segmented, or crossed candidates accidentally satisfied this
policy; both kickstart candidates did.

This supplies the independent body-semantic confirmation that the period-25
identity sequence previously lacked. For this evidence class, each full suffix
period is now a high-confidence replay-unit **candidate**, while the retained
trailing launches remain an explicitly incomplete candidate. The remaining gate
is integration safety: materialize candidate occurrences and diagnostics first,
then prove that projecting them into protected `ReplayUnit` intervals does not
regress existing activity lineage, H/L/T anchors, or incomplete-tail handling.

The native IR, Ascend adapter, and result-JSON contracts have regression tests
for replay body canonicalization and body-backed slot roles. The full enabled
native suite passes 47/47, and the eight-profile validation matrix was rerun
after the final implementation: capture/replay template labels still agree on
both controlled ranks, only the two kickstart candidates satisfy the H/L/T
shape policy, and all incomplete suffixes remain visible.

## Recognition Results Instead Of Exceptions

Incomplete coverage is now represented as data rather than an exception or a
partially fabricated replay unit. Every accepted periodic candidate has an
ordered region ledger with three possible outcomes:

```text
recognized_complete_pattern
  -> one exact full pattern occurrence

unrecognized_leading_context
  -> launches before the selected exact periodic suffix
  -> no expected launch count is claimed

unrecognized_incomplete_tail
  -> a valid observed prefix shorter than one expected pattern
  -> observed and expected launch counts remain explicit
```

This distinction is intentional: an unrecognized region is a successful
analysis result saying that TraceLoom has preserved the observed graph launches
but cannot honestly promote the region to a complete composition occurrence.
It does not abort ingestion, hide the launch/body ledger, infer missing graph
work, or create a partial `ReplayUnit`.

On the kickstart traces this produces:

| Device | Recognized complete decode patterns | Unrecognized leading context | Unrecognized tail |
| --- | ---: | ---: | ---: |
| 0 | 8 | 25 launches | 11/25 launches |
| 1 | 6 | 25 launches | 10/25 launches |

The tail bodies are exact prefixes (`decode-head` followed by repeated
`decode-layer`) but the remaining layers and final tail graph are absent from
the recorded trace. Their graph content is recognized; their higher-level
composition completeness is not. This is the precision boundary the result
must expose rather than turn into control-flow failure.

## Hierarchical Semantic Replay Units

Keeping every H/L/T anchor directly in the outer token grammar made the exact
unit boundary safe but hid the repeated decode step: eager `MatMul`,
`AllReduce`, and `BroadcastTo` anchors sit between complete ReplayUnits, while
`kNoCross` correctly prohibited an outer candidate from crossing each unit.
Relaxing that boundary would make compression prettier by weakening the
evidence contract, so it was rejected.

The implemented projection instead introduces a semantic grammar level. Each
body-confirmed exact ReplayUnit becomes one atomic outer symbol, keyed by its
`GraphTemplateId`, while retaining its ordered H/L/T expansion. The exact
protected intervals are consumed only after validating that the full interval
belongs to the same ReplayUnit and that its composition region and template
are valid. Generic or incomplete graph intervals remain protected and are not
promoted.

This makes both levels visible on the kickstart traces:

```text
ReplayUnit T1
  H
  Rep x23(L)
  T

Rep x8 / Rep x6
  MatMul
  AllReduce
  BroadcastTo
  ReplayUnit T2
    H
    Rep x23(L)
    T
```

The two roots remain `67,662,917 us` and `56,556,040 us`; exact membership is
still 225 and 175 launches, and the incomplete suffixes remain unrecognized.
All eight bounded profiles retain their previous root wall-clock totals.

Cost conservation is now checked rather than merely observed. Structural
children must exactly tile each parent token span, every occurrence has one
matching coverage row, and normalized native root cost must equal the unioned
per-device wall-clock envelope. A fixture with overlapping graph compute and
communication reports the same `50 us` root before and after semantic
hierarchy lowering, while a perturbed non-conserved packet is rejected.

## Multi-Stream Replay Body Topology

The earlier replay-body builder followed only the model stream containing the
launch completion record. That is sufficient for single-stream graphs but
silently drops sibling streams belonging to the same captured graph instance.
The crossed HCCL profile made the loss concrete: every graph instance owns two
captured model streams, one compute lane and one communication lane, while the
old body template contained only the three compute operations.

Body reconstruction now selects the complete `CapturedGraphStream` set for a
linked graph instance, filters tasks by the launch model id and exact launch
envelope, and canonicalizes each lane independently. Lane order is a sorted
multiset of normalized sequences, so raw stream ids and timing do not become
template identity. This intentionally claims an **unordered lane topology**,
not dependency or concurrency edges that the current evidence does not prove.
Legacy launches without a captured instance retain the explicit
`single_model_stream` policy.

Normalized lane sequences contain both:

- compute op type, compute task type, and runtime task type; and
- communication family plus communication task type, with volatile HCCL name
  suffixes removed.

Variable control rows without compute or communication semantics remain
excluded. Templates and launch bodies separately publish compute,
communication, normalized-total, and stream counts.

On the crossed two-graph profile, both launches now share one exact template
with two lanes and 17 normalized tasks: three compute operations and fourteen
HCCL communication steps. The communication lane contains the stable
`Reduce_Inline`, `Memcpy`, `Notify_Record`, and `Notify_Wait` sequence instead
of disappearing. A generated exact-HLT fixture uses six graph instances with
two streams each and proves that one-shot prefill, three decode units, and the
unrecognized final H/L prefix retain the same promotion semantics under
multi-stream bodies.

All eight bounded real profiles retain their prior candidate/unit counts and
root wall-clock totals. The topology boundary is deliberate: TraceLoom keeps
the simple unordered lane projection and does not reconstruct cross-stream
dependency direction or concurrency. That machinery would increase semantic,
maintenance, and compatibility risk without changing the current template or
H/L/T decisions. It should be reconsidered only for a concrete consumer backed
by an authoritative dependency source, never inferred from timestamps alone.

## Typed Unknown Compatibility Ledger

Native JSON was already honest about leading context, incomplete tails,
missing replay-body evidence, and body mismatches, but the compatibility
sidecar exposed only promoted ReplayUnits. Unknown structure is now carried in
the additive `traceloom_aclgraph_reconstruction_region` table. Every region
publishes its stable typed status, reconstruction policies, launch-occurrence
bounds, observed/expected launch counts, and interval; recognized regions are
included too, so the table is a complete ledger rather than an error-only log.

This is intentionally separate from `traceloom_cuda_graph_replay`. Unknown
regions have no fabricated semantic event, so existing event joins and graph
replay invariants remain exact. Synthetic coverage exercises all seven status
values. The two kickstart sidecars report 10 regions (`9` exact plus an
`11/25` incomplete tail) and 8 regions (`7` exact plus a `10/25` incomplete
tail), while retaining only 9 and 7 rows respectively in the replay table.

## Split SQLite Promotion

Split CANN profiles no longer stop after normalizing `AscendTask`. TraceLoom now
reconstructs the same launch and body evidence from:

- `ApiData` for execute calls, capture slots, host threads, and blocking-sync
  activity boundaries;
- `CaptureStreamInfo` for model groups and complete captured stream sets;
- `AscendTask` plus `TaskInfo` for control and compute tasks; and
- `HCCLTaskSingleDevice` for stable communication family/task identity.

Communication rows use the split device task type (`SDMA`, `Write Value`, or
`Notify Wait`) while compute rows retain the host runtime task type. This
matches the monolithic `TASK.taskType` contract and prevents schema layout from
changing an otherwise identical exact body hash.

A paired portable fixture now proves monolithic/split equality for launch
identity, activity membership, compute+communication body templates, and the
complete exact H/L/T result (`2` candidates, `4` exact units, one typed
incomplete tail). Across five retained real split profiles, every body-template
hash matches its monolithic counterpart. The crossed HCCL profile retains the
same 2-lane, 3-compute, 14-communication template. Weak legacy fixed-cardinality
ReplayUnits are intentionally not synthesized on the split path when no exact
H/L/T composition exists.

The adapter golden suite also deletes both normalized lanes from one decode
launch while leaving graph identity intact. That region is emitted as
`unrecognized_missing_body_evidence`; the surrounding exact units remain
promoted and the weaker legacy projector does not reinterpret the gap.

## Schema Capability Gate Addendum

The later capability audit found one overclaim in the promotion evidence
above. The two kickstart databases have no companion `CaptureStreamInfo`
database. Their 25-launch periodicity and observed completion-stream bodies
are real, but the artifacts cannot prove that the visible stream is the whole
captured graph. The earlier 9/7-unit promotion therefore treated an observed
single-stream projection as a complete graph body.

TraceLoom now requires a complete captured stream set and complete identity
coverage for every observed task in those streams. Missing or incompatible
compute/communication tables are tolerated as schema inputs, but an
unclassified body task makes the region
`unrecognized_missing_body_capability`; absence of a communication table may
still prove an observed zero only when every body task is otherwise classified.
`CANN_API`/`ApiData` remains optional because device execution order is an
explicit, independently named order policy.

The eight-profile audit now has the following result: six profiles preserve
their previous unit/region outcome, while the two kickstart profiles emit
`10 + 1` and `8 + 1` typed unknown regions (missing body capability plus one
missing completion region) and zero graph replay rows. This supersedes the
kickstart promotion/count claims in the chronological sections above. A fresh
LLM graph trace with complete capture-stream metadata is required before those
H/L/T shapes can again be called exact.
