# Medium-profile analysis-cost receipt

This directory records a descriptive feasibility point for TraceLoom's
repository-bundled medium profiles. Analysis speed is not a central paper
claim, and this receipt is not a cross-system benchmark.

The checked run used a clean analyzer tree at commit `6304039dda17`, a Release
build with GCC 12.3.1, two analysis threads, and five fresh processes per input
on a 192-logical-CPU Kunpeng-920 host. The OS page cache was not cleared. Each
timed process includes input parsing, analysis, Loop Tree and JSON rendering,
and compatibility-sidecar materialization.

| profile | raw SQLite rows | selected events | median wall time (range) | median peak RSS | Loop Tree | compatibility sidecar |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| device 0 | 1,260,826 | 86,701 | 14.898 s (14.780–15.477) | 525.6 MiB | 67,697 B | 270,966,784 B |
| device 1 | 1,232,988 | 59,226 | 10.908 s (10.355–11.225) | 419.5 MiB | 64,757 B | 216,064,000 B |

The ordered run samples, input hashes, exact output sizes, build, kernel, and
host fields are retained in `kunpeng920-release.json`. The first run is kept
explicitly rather than discarded as warm-up.

Reproduce the protocol with:

```bash
cmake -S . -B build/paper-release -DCMAKE_BUILD_TYPE=Release
cmake --build build/paper-release --target traceloom -j2
examples/paper_artifacts/tools/benchmark_kickstart_folding.py \
  --traceloom build/paper-release/native/traceloom \
  --runs 5 \
  --output /tmp/kunpeng920-release.json
```

Interpret the sizes separately. The compact Loop Tree is the human/agent-facing
structural projection. The much larger compatibility sidecar deliberately
materializes normalized events, anchors, relations, and raw-row provenance for
auditing; it is not a compressed storage format.

## Deterministic thread scaling

`kunpeng920-thread-scaling.json` isolates the ordinary report path without the
compatibility sidecar. It uses the same clean Release build and medium pair,
five fresh processes at each of 1, 2, 4, and 8 threads, and a balanced
forward/reverse schedule. Every one of the 40 generated Loop Trees per pair is
byte-identical across runs and thread counts.

| profile | threads | TASK-row stage | complete load | candidate map | end-to-end |
| --- | ---: | ---: | ---: | ---: | ---: |
| device 0 | 1 | 191.538 ms | 375.442 ms | 3.821 ms | 4.652 s |
| device 0 | 8 | 66.487 ms (2.881x) | 250.110 ms (1.501x) | 2.433 ms (1.571x) | 4.507 s (1.032x) |
| device 1 | 1 | 133.569 ms | 314.462 ms | 3.234 ms | 3.522 s |
| device 1 | 8 | 48.954 ms (2.728x) | 233.231 ms (1.348x) | 2.379 ms (1.360x) | 3.460 s (1.018x) |

This is deliberately a stage-scaling result, not a claim of linear whole-tool
speedup. Parallel SQLite rowid shards remove much of the TASK-ingestion cost,
and owned candidate partitions with halos accelerate their map stage while
preserving deterministic output. Global anchor construction, evidence
analysis, reduction, and report materialization dominate these profiles, so
the observed end-to-end improvement is only 1.8--5.5% across the checked
thread counts. The receipt retains all samples and the stage breakdown rather
than hiding that boundary.

Reproduce the protocol with:

```bash
cmake -S . -B build/parallel-scaling-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACELOOM_NATIVE_BUILD_TESTS=OFF
cmake --build build/parallel-scaling-release -j8
examples/paper_artifacts/tools/benchmark_parallel_scaling.py \
  --traceloom build/parallel-scaling-release/native/traceloom \
  --runs 5 \
  --threads 1,2,4,8 \
  --output /tmp/kunpeng920-thread-scaling.json
```

`verify_parallel_scaling_receipt.py` checks source hashes, every summary and
speedup calculation, cross-thread Loop Tree identity, the positive stage
result, and the bounded end-to-end claim.

## Large protected-sequence baseline

`pattern-scaling-preregistration.md` fixes a separate candidate-discovery
strong-scaling protocol before observation. The corresponding immutable
`kunpeng920-pattern-scaling-baseline.json` records 90 fresh processes over
100,000 to 4,000,000 tokens and 1--32 threads. Every count and hash is
thread-invariant, but the preregistered 4.0x eight-thread scan threshold is not
reproduced: the largest case reaches 2.348x.

This negative boundary is more informative than polishing the speedup. Nearly
eight million occurrence rows reduce to only 200 candidate summaries; the
serial global sort takes 5.87 s versus 0.90 s for one-thread scanning and limits
the eight-thread scan-plus-reduce result to 1.152x. The frozen outcome and next
decision are in `pattern-scaling-baseline-outcome.md`. Any local-reduction
optimization must use a new protocol and receipt rather than replacing the
baseline.

## Partition-local map/reduce result

The separately preregistered follow-up keeps the exact owned-range/halo scan but
reduces occurrences inside each partition. Workers return compact summaries
and typed diagnostics; a deterministic key/first-position merge produces the
global result. All counts plus diagnostic and summary hashes match the
immutable baseline across all 90 optimized samples.

| Tokens | Candidate occurrences | 1-thread map/reduce | 8-thread speedup (eff.) | 32-thread speedup (eff.) |
| ---: | ---: | ---: | ---: | ---: |
| 100,000 | 199,949 | 68.242 ms | 5.811x (72.6%) | 5.126x (16.0%) |
| 1,000,000 | 1,999,949 | 686.473 ms | 7.592x (94.9%) | 18.005x (56.3%) |
| 4,000,000 | 7,999,949 | 2,733.900 ms | 7.752x (96.9%) | 24.665x (77.1%) |

At four million tokens, the local-reduction one-thread path is 2.443x faster
than the 6,678.29 ms global-sort baseline before adding threads. The largest
optimized median RSS is 401.5 MiB, 66.1% below the baseline's 1,183.6 MiB. At
eight threads the complete map/reduce stage takes 352.655 ms; at 32 threads it
takes 110.842 ms. This satisfies every preregistered correctness, scaling, and
memory target. The full receipt is
`kunpeng920-pattern-local-reduce-scaling.json`.
