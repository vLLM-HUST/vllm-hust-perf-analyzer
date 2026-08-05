# CUDA real-model graph reviewer artifact

This checkout artifact packages the accepted single-GPU external-validity
case from the immutable `traceloom-cuda-real-model-20260805-final` bundle. The
original compressed bundle has SHA-256
`312021850a07ec7d6b5dd2fe394ce07adb5ce42e3367e0fa1d41a1d41e356281`.

## Scoped claim

On a correctness-gated Qwen3.5-0.8B full forward captured by a real
`torch.cuda.CUDAGraph`, the Nsight Systems 2024.4.1 node-level SQLite supplies
direct runtime correlation and raw graph-node identity sufficient for
TraceLoom to recover five exact profiler-visible replay bodies, one stable
9,881-member template, launch order, and complete source-row provenance. A
second independent capture reproduces the same semantic shape. The graph-level
companion emits five legacy boundaries and zero exact bodies rather than
inferring membership from timestamps.

This claim does not include hidden CUDA Graph definition nodes, all Nsight
versions, CUDA idle semantics, cross-hardware performance, or CUDA Graph plus
tensor-parallel reconstruction.

## Correctness and environment

The profiled run uses batch 1, sequence length 128, seed 20260805, bfloat16,
and five graph replays on one NVIDIA A800 80GB PCIe GPU. Eager and graph
outputs have zero maximum/mean error, cosine similarity
1.0000000000000002, identical top-10/argmax, and identical tail-logit SHA-256.
The sanitized receipt is `correctness.json`; its original full-bundle manifest
hash is retained inside the receipt. `replay_oracle.json` records that the
single expected template is replayed five times.

The capture environment is CUDA 12.1, driver 535.104.05, PyTorch
2.5.1+cu121, and Nsight Systems 2024.4.1.61. Model weights are not included.
The authors collected these profiler databases and include the reduced SQLite
inputs for research-artifact reproduction. They contain no request corpus,
model weights, credentials, or third-party user data.

## Reduction contract

The three SQLite files retain every row and original `rowid` from the analyzer
tables used by the CUDA/Nsight adapter. Unreferenced strings and unrelated
Nsight metadata tables are omitted. This removes process environment,
collection-host identity, paths, stdout/stderr, and other evidence that cannot
affect the scoped observation. Each `*-reduction.json` records source/output
hashes, byte counts, retained tables, and row counts.

`../tools/reduce_cuda_nsys_sqlite.py` is deterministic and rejects a retained
string containing a credential-bearing URI, secret-like assignment, or home
path. The full and reduced inputs produce byte-identical canonical semantic
JSON after removing only source path, thread count, timing, and allocator
telemetry. Optional reference mode reruns this proof against the immutable
full bundle.

## Reviewer command

```bash
examples/paper_artifacts/tools/verify_cuda_real_model_graph.py \
  --traceloom build/native-tests/native/traceloom
```

The verifier checks hashes and privacy, analyzes all three SQLite files,
repeats the primary analysis, compares Loop Tree and sidecar determinism,
validates all 49,405 exact body-member source rows, checks the direct-evidence
policy ledger, and requires the graph-level companion to remain non-exact.

To recheck full-to-reduced equivalence when the external bundle is available:

```bash
examples/paper_artifacts/tools/verify_cuda_real_model_graph.py \
  --traceloom build/native-tests/native/traceloom \
  --reference-dir /path/to/traceloom-cuda-real-model-20260805-final
```

The separately retained TP2+NCCL trace is not part of this checkout artifact.
Its six raw launches each expose GEMM, 4 MiB D2D memcpy, and NCCL AllReduce
nodes. At the immutable evidence checkpoint, the shared report path rejected a
structural unit spanning two device sequences. The later generic collective-
correspondence work analyzes device sequences independently and exposes the
three paired graph-body AllReduce positions without naming TP or changing this
checkout artifact's frozen claim ledger.
