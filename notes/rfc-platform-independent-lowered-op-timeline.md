# RFC: Platform-Independent Lowered Op Timeline

Status: Draft

Target: TraceLoom core analysis model

## Summary

TraceLoom should analyze a platform-independent timeline of normalized lowered
Torch operations. CUDA, Ascend/CANN, Hygon, and future profiler backends should
be treated as evidence providers that map raw device/host profiler events onto
this intermediate representation.

The current implementation grew from concrete profiler tables. That was useful
for bootstrapping, but it makes the core model too platform-shaped:

- CUDA Graph replay and Ascend ACLGraph replay are handled as separate special
  cases.
- Device kernels, communication tasks, waits, graph control tasks, and report
  rows are mixed in the main pipeline.
- Tree compression consumes platform event rows directly, so each new backend
  must leak its own vocabulary into TraceLoom internals.

The desired direction is:

```text
raw profiler DBs
  -> platform evidence adapters
  -> normalized lowered op timeline
  -> graph/loop/tree/cost analysis
  -> reports and augmented DB
```

In this design, the lowered op timeline is TraceLoom's device-behavior IR.

## Core Idea

The central object should be an ordered timeline of lowered operation
instances:

```text
MatMul, RmsNorm, Rope, Attention, SwiGlu, AllReduce, AllGather, Cast,
Reshape, Gather, Slice, Quant, ...
```

These are not Python-level eager `torch.*` calls. They are the post-lowering
operations that the backend actually schedules and the profiler can observe.
They are still semantic enough for humans and stable enough for cross-platform
comparison.

For example:

- Ascend `aclnnAddmm_MatMulCommon_MatMulV2` should map to normalized
  `MatMul`.
- CUDA GEMM/CUTLASS/cuBLAS-like kernels should map to normalized `MatMul`.
- HCCL/NCCL collective work should map to normalized `AllReduce`,
  `AllGather`, `ReduceScatter`, and related communication ops.
- ACLGraph and CUDA Graph replay should map to graph containers whose internal
  sequence is also expressed as normalized lowered ops.

Raw platform labels remain important, but they belong in the evidence layer,
not in the primary semantic timeline.

## Proposed IR

TraceLoom should distinguish four concepts.

`LoweredOpInstance`: one semantic operation instance in the normalized
timeline.

Suggested fields:

```text
op_id
op_type              # MatMul, RmsNorm, AllReduce, Cast, ...
op_family            # compute, comm, data_move, control, shape, graph
category             # exec, comm, wait, graph, aux
start_ns
end_ns
device_id
stream_scope
symbol
source_confidence
attrs                # optional shape/dtype/layout/provider attributes
```

`RawEvidenceEvent`: one profiler-visible raw event from a backend.

Suggested fields:

```text
provider             # ascend_msprof, cuda_nsys, hygon, ...
source_db
source_table
source_key
raw_label
raw_task_type
start_ns
end_ns
device_id
stream_id
connection_id
metadata
```

`OpEvidenceLink`: a relation between normalized op instances and raw evidence.

Suggested fields:

```text
op_id
evidence_id
relation             # exact, contains, contained_by, overlaps, inferred_from
confidence           # exact, strong, heuristic, weak
reason
```

`GraphReplayInstance`: a graph execution container in the lowered op timeline.

Suggested fields:

```text
graph_id
graph_provider       # cuda_graph, aclgraph, ...
graph_kind
graph_type_symbol
start_ns
end_ns
device_id
primary_stream_id
body_hash
body_signature       # normalized lowered op sequence/counts
control_signature
noise_signature
child_op_ids
evidence_ids
```

Graph replay should be a first-class lowered timeline node, not a host API
projection. Its children should be normalized lowered ops when visible, and its
boundaries should come from device-side graph behavior when available.

## Why This Is The Right Boundary

Lowered ops are the useful middle ground:

- They are closer to model behavior than vendor kernel names.
- They are closer to actual runtime behavior than Python source operations.
- They let TraceLoom compare CUDA and Ascend without pretending their raw
  profiler schemas are the same.
- They preserve enough detail for performance work: graph body shape, repeated
  op sequences, communication placement, aux op overhead, and idle attribution.
- They give reports a stable vocabulary while still allowing raw evidence drill
  down.

This also clarifies a key product promise:

```text
TraceLoom explains model execution structure using normalized lowered ops,
and keeps platform profiler events as auditable evidence.
```

## Adapter Responsibilities

Each platform adapter should do three jobs.

1. Read raw profiler tables into `RawEvidenceEvent`.
2. Classify evidence into normalized lowered op candidates.
3. Link candidates back to raw evidence with confidence and reason.

The adapter may use provider-specific knowledge:

- Ascend/CANN: `TASK`, `COMPUTE_TASK_INFO`, `COMMUNICATION_OP`,
  `CaptureStreamInfo`, graph semantic tasks such as `MODEL_EXECUTE`,
  `MODEL_MAINTAINCE`, `NOTIFY_WAIT`, and `NOTIFY_RECORD`.
- CUDA/Nsight: CUPTI kernel, memcpy, memset, synchronization, CUDA Graph trace,
  NVTX when available.
- Hygon: current kernel and communication label rules.

But after the adapter stage, TraceLoom core should no longer branch on
platform-specific table names for semantic analysis.

## Graph Semantics

CUDA Graph and ACLGraph should be sibling graph providers.

Both should output the same graph IR:

```text
GraphReplayInstance(provider=..., body_signature=..., child_op_ids=...)
```

Provider-specific differences remain in evidence:

- CUDA may have explicit CUPTI graph replay rows.
- Ascend may require reconstruction from capture streams and device-side graph
  semantic tasks.
- Some providers may expose graph launch without internal children.
- Some providers may expose graph children without a clean replay boundary.

The core should support all of these by using relation/confidence fields rather
than platform-specific branches.

Graph typing should use normalized lowered op body signatures. Control tasks,
notify tasks, capture waits, memcpy, and other runtime events can remain part
of graph evidence and optional signatures, but the primary graph type should be
derived from the normalized body sequence.

## Loop Tree Implications

The loop tree should be built from lowered op instances, not raw backend rows.

That means:

- Node symbols refer to normalized lowered op keys.
- Node cost tables aggregate normalized op instances.
- Raw evidence is available through links, but does not define tree identity.
- Graph replay can appear as an atom node when graph-level behavior is the
  intended analysis grain.
- Graph children can be expanded when the report wants internal execution
  order.

This gives TraceLoom two useful views over the same IR:

1. Graph-as-atom view: useful for high-level decode/prefill structure.
2. Graph-expanded view: useful for graph body cost and repeated-op analysis.

Both views should share node ids and evidence links where possible.

## Augmented DB Direction

The augmented DB should eventually expose neutral tables:

```text
traceloom_lowered_op
traceloom_raw_evidence
traceloom_op_evidence_link
traceloom_graph_replay
traceloom_graph_child
```

Existing CUDA-named graph tables/views can remain as compatibility aliases
during migration.

Reports should prefer neutral filenames:

```text
graph_events.csv
graph_envelope_events.csv
report_devN.md
```

Provider-specific outputs such as ACLGraph reconstruction summaries can remain
as extra evidence reports, but they should not be the primary data model.

## Migration Plan

### Phase 1: Introduce The IR Without Changing Behavior

- Add dataclasses for lowered ops, raw evidence, evidence links, and graph
  replay instances.
- Convert current step rows to/from this IR at the pipeline boundary.
- Keep existing reports and DB output stable.
- Add tests that current Ascend graph reports and existing tree node ids remain
  stable.

### Phase 2: Move Provider Logic Behind Adapters

- Move Ascend ACLGraph reconstruction behind a graph/lowered-op provider.
- Move CUDA Graph replay extraction behind the same provider interface.
- Move platform label canonicalization into provider adapters.
- Make the main pipeline consume provider-neutral lowered ops and graph replay
  instances.

### Phase 3: Make The DB And Reports Provider-Neutral

- Add neutral graph/lowered-op tables to augmented DB.
- Keep old CUDA graph views as compatibility views.
- Rename primary graph CSV outputs to neutral names.
- Update SQL examples and docs to query provider-neutral tables.

### Phase 4: Improve Semantic Fidelity

- Add optional shape/dtype/layout attributes when a backend exposes them.
- Preserve aux ops such as cast, reshape, gather, slice, quant, and elemwise
  work as lowered ops instead of dropping them as noise.
- Add confidence-aware reports so users can distinguish exact evidence from
  heuristic inference.
- Support graph type comparison by normalized body sequence, with optional
  control/noise signatures for deeper diagnostics.

## Design Principles

`Semantic first, evidence always`: reports should speak in normalized lowered
ops, but every claim should be traceable to raw profiler evidence.

`Provider parity`: CUDA Graph and ACLGraph are graph providers, not separate
features with separate core paths.

`No host-only graph projection`: graph intervals should come from device-side
graph behavior when possible. Host APIs can provide context, not the primary
timeline boundary.

`Aux is not junk`: shape, cast, gather, quant, slice, and other auxiliary ops
may be critical for graph performance and should remain representable in the
IR.

`Uncertainty is data`: inferred mappings should carry confidence and reasons
instead of being silently treated as ground truth.

`Stable human vocabulary`: the report should prefer names such as `MatMul`,
`RmsNorm`, `AllReduce`, and `ACLGraphType G001` over vendor-internal kernel
strings, while preserving raw names for drill-down.

## Open Questions

- Should the primary lowered op timeline be sequence-based, interval-based, or
  both?
- How much shape/dtype information can each backend expose reliably?
- Should graph atom nodes and graph-expanded nodes be two report views over the
  same IR, or two separate materialized timelines?
- How should TraceLoom represent fused kernels that correspond to multiple
  lowered ops?
- How should TraceLoom represent one lowered op that maps to multiple raw
  kernels across streams?
- What confidence thresholds should be used before an inferred op participates
  in loop-tree compression?

## Near-Term Refactor Consequence

The immediate refactor should not start by creating more CUDA/ACLGraph-specific
helpers. It should first introduce the provider-neutral graph/lowered-op model,
then migrate the current code into that shape.

The practical first slice is:

1. Add `traceloom/graph/` or `traceloom/ir/` dataclasses.
2. Make ACLGraph and CUDA Graph output the same graph replay structure.
3. Make the main pipeline merge graph atoms through a provider-neutral function.
4. Keep current report output stable while the internals become less
   platform-shaped.

This keeps the experimental graph work moving while aligning TraceLoom with the
larger goal: a platform-independent profiler built around normalized lowered
operation timelines.
