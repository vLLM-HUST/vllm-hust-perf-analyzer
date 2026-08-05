# Deterministic pattern strong-scaling preregistration

Status: committed before observing the scaling result

## Decision and claim

Determine whether TraceLoom's owned-partition candidate discovery provides
useful strong scaling once the protected semantic sequence is large enough to
keep workers busy. This experiment may justify a stage-scaling paper claim; it
cannot establish whole-analyzer or grammar-induction scaling.

## Fixed workload

- generated protected sequences of 100,000, 1,000,000, and 4,000,000 tokens;
- deterministic 16-symbol woven motif with eight partition-phase variants;
- eight evenly spaced four-token `no-cross` graph intervals;
- candidate lengths two and three;
- 4,096 owned start positions per partition and a three-token read halo;
- 1, 2, 4, 8, 16, and 32 threads;
- five fresh Release processes per point in alternating forward/reverse order;
- sequence construction and output hashing excluded from stage timings.

The measured scan stage is the production
`scan_candidate_partitions_with_diagnostics` call. It includes ThreadPool
construction, local scans, deterministic partition-order merge, and ambiguous
key cleanup. `reduce_candidates` is measured separately and combined with scan
only by summing the two measured stages.

## Expected observation

1. Candidate occurrences, diagnostics, reduced summaries, and all three hashes
   are identical across every run and thread count.
2. Scaling improves with sequence size as the fixed ThreadPool/merge cost is
   amortized.
3. On four million tokens, eight threads achieve at least 4.0x scan speedup
   (at least 50% parallel efficiency). Results beyond eight threads are
   reported but are not required to remain monotonic.

## Evidence against the claim

- any cross-thread count or hash mismatch;
- less than 2.0x eight-thread scan speedup on four million tokens;
- scaling that does not improve from 100,000 to 4,000,000 tokens; or
- a serial reduce stage that dominates scan enough to erase practical pattern
  pipeline scaling.

An eight-thread result between 2.0x and 4.0x is a partial result, not a success
under the preregistered strong-scaling threshold. We will not change the
workload, metric, or threshold after observing it.

## Budget and stop rule

The fixed campaign is 90 fresh processes. Stop if one sample exceeds five
minutes, peak RSS exceeds 16 GiB, the host begins swapping, or the campaign
cannot complete within 30 minutes. A stopped campaign is insufficient evidence,
not a negative scaling result. No accelerator is required.

## Reproduction

```bash
cmake -S . -B build/pattern-scaling-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACELOOM_NATIVE_BUILD_TESTS=OFF \
  -DTRACELOOM_NATIVE_BUILD_BENCHMARKS=ON
cmake --build build/pattern-scaling-release \
  --target traceloom-native-pattern-scaling-benchmark -j8
examples/paper_artifacts/tools/benchmark_pattern_scaling.py \
  --benchmark \
    build/pattern-scaling-release/native/traceloom-native-pattern-scaling-benchmark \
  --runs 5 \
  --tokens 100000,1000000,4000000 \
  --threads 1,2,4,8,16,32 \
  --output /tmp/traceloom-pattern-scaling.json
```
