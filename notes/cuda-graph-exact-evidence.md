# CUDA Graph Exact Visible-Body Evidence

Status: reproduced

Date: 2026-08-04

## Claim

For an Nsight Systems node-level CUDA Graph export, TraceLoom can recover each
observed graph launch and its profiler-visible activity body without using
timestamp containment. An exact ReplayUnit is promoted only when direct
runtime correlation, graph-node identity, supported activity coverage, and a
repeated stable body all agree.

This is deliberately a **visible-body** claim. It does not claim to reconstruct
event nodes or other graph definition details that have no corresponding
device activity row in the export.

## Evidence Contract

The exact CUDA path requires all of the following:

1. a nonempty `CUDA_GRAPH_NODE_EVENTS` table, proving that node-level graph
   tracing was enabled;
2. one unique `cudaGraphLaunch` runtime row for a correlation ID;
3. directly correlated kernel or memcpy activity with a non-null
   `graphNodeId`;
4. no nonempty, unsupported activity table carrying `graphNodeId`;
5. a stable raw graph-node set and normalized visible body observed at least
   twice in the artifact.

The body is an unordered set of stream lanes. Order inside each lane is exact;
raw stream identifiers and timing are not part of template identity. Kernels
retain their exact resolved names, memcpy nodes retain copy kind and byte
count, and recognized NCCL kernels retain their communication role.

Missing children, singleton bodies, duplicate launch correlations, or unknown
graph-node activity capabilities produce typed unrecognized regions and no
exact ReplayUnit. Graph-level Nsight tracing remains useful for graph-exec
identity and launch order, but it intentionally supplies no exact body because
that mode omits node activities.

## Immutable Real Artifact

The external evidence bundle is `traceloom-cuda-20260804`, captured with
Nsight Systems 2024.4.1 on an RTX 4090. The evaluated node-level SQLite export
is `traces/sg_graph_node_nsys2024_4.sqlite`, SHA-256
`989e95a82013330aaf4c0eb755e82585ef35a8a9810ccfb8246acd92c59b6ece`.

The analyzer recovers:

```text
directly correlated launches:       5
visible launch bodies:              5
visible body templates:             2
recognized exact regions:           5
unrecognized regions:               0
exact ReplayUnits:                   5
ordered template schedule:          0, 1, 0, 0, 1
```

The schedule exactly matches both the workload oracle and the independent
graph-level export's graph-exec schedule `2, 5, 2, 2, 5`. Each visible body has
one stream lane, three kernels, and one memcpy. The two templates differ in
their GEMM kernel identity and memcpy size.

## Negative Tests

- Deleting one directly correlated kernel from the immutable artifact copy
  leaves that launch as `unrecognized_insufficient_repeat_evidence`; the other
  four independently repeated occurrences remain exact. No unit is fabricated
  for the damaged occurrence.
- Adding a nonempty unknown table with `graphNodeId` changes all five launches
  to `unrecognized_missing_body_capability` and produces zero exact units.
- Checked-in adapter tests also cover a singleton body, duplicate runtime
  correlation, a launch with no visible children, an incomplete supported
  activity schema, contradictory bodies for one raw node set, deterministic
  reload, and the exact `A/B/A/A/B` schedule.

The CUDA-Graph-plus-TP capture attempt in the bundle remains a negative result:
it hung and was terminated. The current evidence therefore supports exact
single-GPU CUDA Graph visible-body recovery and eager TP/NCCL normalization,
not exact CUDA Graph TP reconstruction.
