# Ascend mapped-gather perturbation

This checkout-bundled pair freezes a controlled, correctness-gated structural
perturbation. It compares a deliberately fragmented span-copy implementation
with the mapped-host `KvCacheBlockGather` operator for the same 16 MiB logical
K/V transfer. The profiles were produced by the project maintainers and are
redistributed as repository test data under the repository license. They
contain profiler records, not model weights or prompt payloads.

Across five warm-up and thirty measured iterations, both output oracles pass.
The raw profiler rows expose the preregistered mechanism exactly:

| Variant | Target task rows | Target API rows | Artifact-scoped Loop Tree |
| --- | ---: | ---: | --- |
| span copy | 35,840 16 KiB H2D copies | 35,840 | `Rep x35840 -> MEMCPY_ASYNC` |
| mapped gather | 70 gather kernels | 70 | `Rep x70 -> KvCacheBlockGather` |

The target multiplicity therefore falls by exactly 512x. TraceLoom emits no
graph unit for either non-graph input. `KvCacheBlockGather` has an exact
productive-compute and structural-anchor registration; it is not silently
accepted by a fuzzy keyword rule or left in the unregistered-operator audit.

## Positive selection, not global noise-policy drift

Generic `MEMCPY_ASYNC` rows remain filtered from TraceLoom's default semantic
sequence because globally promoting every copy would add substantial noise to
ordinary serving reports. This artifact supplies the explicit
`target-signal-rules.tsv` extension that promotes the preregistered span-copy
target. The same deterministic rule file is applied to both variants. This
makes filtering a reviewable positive-selection decision instead of silently
dismissing a target that matters to the experiment.

## Inputs and reduction

The main databases are 7.12 MiB and 124 KiB. Each is a deterministic
full-time-range reduction of the retained root `msprof_*.db`:

- every `TASK` and `CANN_API` row is retained;
- dependent compute/memcpy rows and referenced strings are retained;
- original source table rowids are preserved;
- all original table schemas remain present;
- `HOST_INFO` and `TASK_PMU_INFO` row content is omitted; and
- path-, email-, and identity-like strings are rejected by the verifier.

The sanitized capture receipts retain correctness, workload shape, immutable
source/build hashes, and all thirty measured-loop samples while omitting local
paths, pointers, process IDs, and registration handles. The local timing ratio
is descriptive microbenchmark evidence only; it is not an end-to-end serving
speed claim.

## Reproduce

```bash
examples/paper_artifacts/tools/verify_ascend_mapped_gather.py \
  --traceloom build/native-tests/native/traceloom
```

Maintainers with the retained full databases may additionally prove complete
full/reduced equality for the frozen source and analyzer observations:

```bash
examples/paper_artifacts/tools/verify_ascend_mapped_gather.py \
  --traceloom build/native-tests/native/traceloom \
  --reference-span /path/to/span/msprof_20260804231955.db \
  --reference-mapped /path/to/mapped/msprof_20260804232014.db
```

The result establishes a bounded local mechanism change with direct raw-row
provenance. It does not establish production serving speedup, graph behavior,
or causal attribution outside the fixed harness.
