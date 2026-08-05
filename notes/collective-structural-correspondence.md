# Collective Structural Correspondence

## Purpose

TraceLoom may be evaluated on inference workloads, but the analyzer must not
encode inference, tensor-parallel, model-layer, or framework-phase semantics.
Its responsibility is narrower and more reusable:

1. recover each observed device sequence independently;
2. preserve directly observed collective actions and raw-row provenance; and
3. expose conservative cross-sequence correspondence candidates.

Downstream agents and humans may interpret a stable correspondence pattern as
tensor parallelism, pipeline communication, replicated training, or another
workload mechanism. TraceLoom does not choose that label.

## Representation

Multi-device profiles produce one Loop Tree and structural partition per
device sequence. Node and unit handles are globally unique in a shared
sidecar, but ordering remains device-local. TraceLoom does not merge the lanes
into a synthetic total order.

The low-level pattern scanner also rejects candidate windows that cross a
device-sequence boundary. Thus adjacency in flattened storage cannot become a
pattern that exists on neither device.

Collective correspondence is an overlay on those local structures:

```text
device 0:  region -- collective A0 -- region -- collective B0
                         |                         |
device 1:  region -- collective A1 -- region -- collective B1
```

The vertical edges mean “compatible observed correspondence,” not hidden
causality, synchronization, or workload-phase identity.

## Evidence classes

### Exact graph-body position

When node-level graph tracing gives an exact visible body, a collective member
is keyed by:

- exact visible-body template hash;
- per-device occurrence ordinal for that template;
- normalized collective operation;
- collective-member ordinal inside the body.

The member retains its concrete event id, source table, source row, timestamps,
and optional profiler connection/op ids. It is not promoted to a top-level
anchor because it remains inside the protected ReplayUnit.

### Recovered loop position

For visible top-level collective anchors, correspondence uses matching
recovered-loop structure, loop occurrence, normalized operation, and operation
ordinal. This is weaker structural evidence and remains explicitly a
candidate.

## Validation status

`complete`, `partial`, and `singleton` describe candidate membership coverage
against the observed/declared member set. Even `complete` is not a claim that
TraceLoom proved a single hidden hardware operation. Ambiguous or missing
members remain visible rather than being forced into a match.

Graph-body ordinal alignment is enabled only when every observed device has
the same nonzero occurrence count for the exact body template. Unequal counts
split the candidates by device, producing singletons instead of shifting later
occurrences into plausible-looking but unsupported pairs.

Time proximity alone never creates correspondence. Start and duration skew are
measurements reported after structural matching, not matching keys.

## CUDA TP2 checkpoint

The retained A800 TP2+NCCL capture is a useful multi-device, collective-rich
test, not a request for a TP-aware analyzer. On the collective-correspondence
implementation:

- the shared report completes instead of rejecting a structural unit spanning
  two device sequences;
- both device sequences retain independent Loop Trees and structural units;
- all six exact CUDA Graph ReplayUnits remain recovered;
- three visible-body `AllReduce` correspondence groups each contain two
  members and preserve raw kernel-row provenance;
- observed start skew is 28.667, 8.123, and 5.434 us;
- observed duration skew is 17.664, 0.705, and 0.417 us; and
- unrelated initialization/teardown loop candidates remain typed singleton
  evidence instead of being coerced into pairs.

No result row names TP, a transformer layer, or an inference phase.

## Non-goals

- infer tensor-parallel or other workload semantics;
- synthesize a cross-device total order;
- infer causality from overlap or timestamp proximity;
- expand protected graph bodies into top-level anchors;
- treat complete candidate membership as proof of hidden runtime identity; or
- hide unmatched collectives as noise.
