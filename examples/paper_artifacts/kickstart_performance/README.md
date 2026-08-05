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
