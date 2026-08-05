# Partition-local pattern reduction preregistration

Status: committed before observing the optimized-path result

## Decision and change under test

The immutable baseline showed that owned-range scans are deterministic but that
materializing every occurrence followed by one global sort prevents useful
pattern-pipeline scaling. The follow-up keeps the same scan semantics and moves
`reduce_candidates` into each owned partition. Only compact summaries and typed
diagnostics cross the worker boundary; the final merge is ordered by candidate
key and first source position.

Decide whether this change preserves the baseline answer while converting the
algorithm into a practically scalable deterministic map/reduce path.

## Fixed parity and workload contract

- use exactly the baseline's 100,000, 1,000,000, and 4,000,000-token generated
  sequences, motif, eight protected intervals, candidate lengths, 4,096-token
  partitions, three-token halo, thread counts, five runs, and balanced schedule;
- candidate occurrence count, diagnostic count, reduced count, diagnostic hash,
  and reduced-summary hash must match
  `kunpeng920-pattern-scaling-baseline.json` at every point;
- measure the complete production `scan_and_reduce_candidate_partitions` call,
  including ThreadPool construction, local scan and reduction, ordered summary
  merge, and ambiguous-key removal;
- compare retained peak RSS and one-thread map/reduce time with the immutable
  baseline. Sequence construction and hashing remain outside the stage timer.

## Expected observation

On four million tokens:

1. eight threads achieve at least 4.0x map/reduce speedup;
2. one-thread local map/reduce is at least 2.0x faster than the baseline global
   scan-plus-reduce path; and
3. peak RSS is at most 50% of the baseline's maximum retained RSS.

## Evidence against the claim

- any count or hash mismatch is a correctness failure;
- less than 2.0x eight-thread map/reduce speedup is a scaling failure;
- less than 1.25x one-thread improvement or less than 25% RSS reduction means
  the local reduction does not remove the diagnosed amplification;
- a result between the failure and expected thresholds is partial and must not
  be described as satisfying the preregistered target.

## Budget and stop rule

Run the same 90 fresh processes. Stop on a five-minute sample, 16 GiB RSS,
swapping, or a 30-minute campaign. Preserve the baseline regardless of outcome.

## Reproduction

```bash
cmake -S . -B build/pattern-scaling-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACELOOM_NATIVE_BUILD_TESTS=OFF \
  -DTRACELOOM_NATIVE_BUILD_BENCHMARKS=ON
cmake --build build/pattern-scaling-release \
  --target traceloom-native-pattern-scaling-benchmark -j8
examples/paper_artifacts/tools/benchmark_pattern_local_reduce_scaling.py \
  --benchmark \
    build/pattern-scaling-release/native/traceloom-native-pattern-scaling-benchmark \
  --runs 5 \
  --output /tmp/traceloom-pattern-local-reduce-scaling.json
```
