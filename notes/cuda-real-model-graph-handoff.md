# CUDA Real-Model Graph Evidence Handoff

Status: complete; integrated as checkout reviewer evidence

Date: 2026-08-05

Audience: the Lumi session working on a local CUDA machine or a school CUDA
server

## Completed return

The 2026-08-05 A800 campaign passed the P1 gate. The immutable bundle is
`traceloom-cuda-real-model-20260805-final.tgz`, SHA-256
`312021850a07ec7d6b5dd2fe394ce07adb5ce42e3367e0fa1d41a1d41e356281`.
Two independent correctness-gated Qwen3.5-0.8B node-level captures each
recover five exact ReplayUnits, one 9,881-member visible-body template, 49,405
source-linked members, and zero reconstruction unknowns. The graph-level
companion exposes five non-exact replay boundaries, as required.

The reviewer-safe reduction and CPU-only verifier now live in
`examples/paper_artifacts/cuda_real_model_graph/`. Full and reduced canonical
semantic JSON are identical after excluding only input path, thread count,
timing, and allocator telemetry.

The optional TP2+NCCL diagnostic is not promoted into success: its raw node
evidence and correctness gate pass, but the shared report path rejects a
structural unit spanning two device sequences. Releasing the live CUDAGraph
before `destroy_process_group` resolves the bounded teardown timeout; this does
not prove that every earlier hang had the same cause.

## Mission

Collect and validate one correctness-gated, single-GPU, real-model CUDA Graph
profile that TraceLoom can analyze offline with exact profiler-visible body
semantics.

This is the only CUDA collection task required for the next paper-strength
checkpoint. Do not begin with tensor parallelism. The previous graph-plus-NCCL
attempt hung; TP diagnosis is a separate optional investigation after the
single-GPU result is complete.

The target claim is deliberately bounded:

```text
On a real model executed through CUDA Graph, an Nsight node-level export gives
TraceLoom sufficient direct evidence to recover repeated profiler-visible
graph bodies, stable templates, launch order, and raw-row provenance. Missing
or ambiguous evidence remains typed unrecognized.
```

This does not claim a hidden CUDA graph definition, framework intent,
cross-stream happens-before, or all-Nsight-version coverage.

## Repository Baseline

Use a TraceLoom revision containing all of these commits or their descendants:

```text
8a9eaa8 Recover exact CUDA Graph visible bodies
9acd561 Use provider-neutral graph report heading
f86a311 Preserve exact graph body evidence
abdd284 Add fail-closed graph body verdicts
04e4182 Capture interleaved structural-unit milestone
```

At handoff time, the integration branch is:

```text
codex/unknown-first-fused-moe-analysis
```

Do not restart from the old `e14c922` baseline described in the workspace-root
handoff. The implementation and evidence boundary have advanced materially.

Build and verify before collection analysis:

```bash
cmake --preset dev-tests
cmake --build --preset dev-tests -j "$(nproc)"
ctest --preset dev-tests
```

TraceLoom is an offline C++ analyzer. It must not acquire a CUDA runtime or GPU
dependency.

## What Is Already Proven

The 2026-08-04 RTX 4090 bundle contains a real
`torch.cuda.CUDAGraph` controlled workload captured by Nsight Systems 2024.4.1.
Its node-level SQLite is 724,992 bytes and has SHA-256:

```text
989e95a82013330aaf4c0eb755e82585ef35a8a9810ccfb8246acd92c59b6ece
```

TraceLoom recovers:

```text
launches:             5
exact ReplayUnits:    5
body templates:       2
unknown regions:      0
template schedule:    0,1,0,0,1
visible body:         3 kernels + 1 memcpy per occurrence
```

Deleting one child or adding an unknown graph-node activity table causes the
affected evidence to fail closed. Do not repeat that controlled experiment
unless a new Nsight version needs a compatibility check.

The same bundle contains real Qwen3.5-0.8B eager traces, including single-GPU,
TP2, pipeline-style placement, and cached generation. Those traces validate
real-model ingestion and NCCL normalization, but they are not CUDA Graph
evidence.

## Minimum Experiment

### Model path

Prefer the already exercised `Qwen/Qwen3.5-0.8B` ModelScope snapshot if the
CUDA host can run it. A different small, redistributable model is acceptable
when it provides a simpler correct CUDA Graph path; record the exact model
identity and revision.

The minimum acceptable workload is a fixed-shape, real-model forward or decode
step captured into one CUDA Graph and replayed at least five times. A real
fixed-shape decode step with static KV-cache buffers is preferable, but a full
model forward is sufficient for this external-validity row.

Use static input/output buffers and perform warmup before graph capture. Do not
include model loading or graph construction in the analyzed steady-state
window.

### Independent correctness oracle

Before profiling, compare eager and graphed execution using identical inputs.
Record at least:

- input shape, dtype, seed, and model revision;
- exact output tensor shape and finite-value check;
- maximum and mean absolute error;
- cosine similarity where meaningful;
- top-k token/logit agreement or exact generated tokens;
- one deterministic output hash or a small canonical output sample.

Set thresholds before reading the trace. A profile with failed correctness is
not evidence, even if TraceLoom reconstructs it perfectly.

### Replay oracle

Record the expected graph launch count and application-level replay sequence
before running TraceLoom. One nontrivial template repeated five or more times
is enough because the controlled fixture already proves A/B template
discrimination. If two fixed-shape buckets arise naturally, preserve their
expected order rather than forcing a one-template workload.

Add NVTX ranges around warmup, capture, and the profiled replay window as an
independent navigation aid. NVTX must not be the sole evidence used for exact
child membership.

## Nsight Collection

Use an Nsight version that supports node-level CUDA Graph tracing. On the
existing 2024.4 environment, the intended command shape is:

```bash
nsys profile \
  --trace=cuda,nvtx,osrt \
  --cuda-graph-trace=node \
  --sample=none \
  --cpuctxsw=none \
  --force-overwrite=true \
  -o real_model_graph_node \
  python real_model_graph_workload.py
```

Confirm the exact flags with that host's `nsys profile --help`; preserve the
literal successful command in the bundle. Export the `.nsys-rep` to SQLite
with the installed `nsys export` syntax and retain both SHA-256 values.

If affordable, also collect a graph-level companion with
`--cuda-graph-trace=graph`. It helps cross-check launch identity and order, but
it is not expected to expose an exact visible body.

Collect two independent node-level captures after the workload is stable if
the additional cost is small. The first complete capture is the P1 gate; the
second is stability evidence, not permission to tune the analyzer to one
artifact.

## Required Raw Evidence

Inventory every table containing graph, runtime, kernel, memory, correlation,
or node identity. At minimum record schemas and row counts for available forms
of:

```text
CUPTI_ACTIVITY_KIND_RUNTIME
CUPTI_ACTIVITY_KIND_KERNEL
CUPTI_ACTIVITY_KIND_MEMCPY
CUPTI_ACTIVITY_KIND_MEMSET
CUPTI_ACTIVITY_KIND_GRAPH_TRACE
CUDA_GRAPH_NODE_EVENTS or CUDA_GRAPH_EVENTS
StringIds
NVTX_EVENTS
```

Do not assume an absent table means an observed-empty activity class. Preserve
the adapter's distinction between unavailable and empty evidence.

The key direct relation should be:

```text
unique cudaGraphLaunch runtime correlation
  -> correlated device activity
  -> non-null graphNodeId
  -> repeated stable raw node set and normalized visible body
```

Timestamp containment alone is not an exact membership relation.

## TraceLoom Analysis Gate

Run the production CLI on the exported SQLite:

```bash
./build/native-tests/native/traceloom /path/to/real_model_graph_node.sqlite
```

The positive result should satisfy all of the following:

- at least five directly correlated graph launch occurrences;
- at least five exact `CUDAGraph` ReplayUnits;
- at least one stable nontrivial visible-body template;
- recovered launch/template order equals the preregistered application oracle;
- every exact region reports provider `cuda`, direct-observation boundary,
  and graph-node-set identity;
- every body member resolves to an original Nsight table and row;
- no child has two exact owners;
- graph/body/residual cost accounting is nonduplicative;
- a normal Loop Tree is emitted through the shared report path;
- analysis completes offline after the GPU process and CUDA environment are
  gone.

Zero unknown regions are expected only when node-level capability is complete.
If unknowns remain, report their typed statuses and strongest missing relation.
Do not weaken promotion gates to make the row positive.

## Negative And Boundary Checks

Do not spend the first pass on a large mutation matrix. Reuse the existing
controlled corruption tests. For the real-model artifact, perform only these
cheap checks after the positive path works:

1. run the same SQLite twice and require deterministic counts, template IDs,
   statuses, and report topology;
2. analyze the graph-level companion, if collected, and require zero exact
   bodies rather than timestamp-inferred membership;
3. verify one exact unit's complete source-row drill-down manually and by SQL.

Only add a new mutation when the real model exposes a capability or ambiguity
not represented by the controlled fixture.

## Bundle To Return

Return one directory with this shape:

```text
traceloom-cuda-real-model-YYYYMMDD/
  README.md
  SHA256SUMS
  workload/
    real_model_graph_workload.py
    requirements-or-environment.txt
  manifests/
    environment.json
    correctness.json
    replay_oracle.json
    collection_commands.txt
  traces/
    real_model_graph_node.nsys-rep
    real_model_graph_node.sqlite
    real_model_graph_graph.nsys-rep      # optional
    real_model_graph_graph.sqlite        # optional
  analysis/
    trace_inventory.json
    traceloom-result.json
    loop_tree_v2.md
    sidecar.db
    expected-vs-recovered.tsv
    provenance.sql
    provenance-result.tsv
    analysis-cost.json
```

The bundle must not contain model weights, credentials, authenticated URLs,
home-directory paths that reveal secrets, or unrelated system data.

Record the TraceLoom commit and binary SHA-256. Keep the full `.nsys-rep`
outside ordinary Git history. The SQLite may become a repository checkout
artifact if it is reviewer-safe and meets the size/reduction contract in
`notes/publication-readiness-roadmap.md`; do not push it before the integration
owner makes that decision.

## Return Summary

Send the integration owner a concise checkpoint containing:

1. GPU, driver, CUDA, PyTorch/framework, Nsight, and model versions;
2. exact collection/export commands and artifact hashes;
3. eager-vs-graph correctness result;
4. expected and recovered launch/template sequences;
5. exact/unknown region counts and capability ledger;
6. one raw-row provenance example;
7. analysis wall time, peak RSS, input bytes, and output bytes;
8. bundle location and total size;
9. strongest supported paper sentence;
10. any counterevidence or unresolved ambiguity.

Do not return only a branch or a screenshot. The semantic checkpoint is an
immutable SQLite plus oracle, analyzer output, provenance, and a bounded claim.

## Optional Work After The Gate

Only after the real-model single-GPU row passes:

1. collect a second independent capture for stability;
2. try a real decode-step graph if the first result used a full forward;
3. investigate the previous graph-plus-NCCL hang with a minimal reproducer and
   an explicit timeout;
4. attempt CUDA Graph plus TP only if the minimal NCCL capture is known safe.

None of these optional tasks may rewrite or delay the completed single-GPU
evidence bundle.

## Final Boundary

The successful paper statement is not "TraceLoom understands CUDA models."
It is:

```text
The same evidence-bounded reconstruction contract recovers exact
profiler-visible graph structure from real CANN and Nsight artifacts, while
provider capability ledgers expose where either profiler cannot support an
exact claim.
```

That is sufficient cross-platform evidence. Everything beyond it should be
earned separately.
