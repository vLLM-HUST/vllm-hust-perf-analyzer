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
