# TraceLoom Kickstart Profile

This directory contains a real two-device Ascend/CANN `msprof` bundle for a
short vLLM-Ascend inference run.

## Source

- Hardware: Ascend 910B3
- Runtime: vLLM-Ascend
- Model: `/data/shared-models/Qwen2.5-0.5B-Instruct`
- Parallelism: `tensor_parallel_size=2`
- Request shape: 1 prompt, 12 generated tokens
- Captured behavior: engine initialization, HCCL setup, ACL graph
  capture/replay, prefill, and decode

The workload result is recorded in [workload_result.json](workload_result.json).
The checked-in profile databases are:

```text
msprof_raw/
  PROF_000001_20260609064648517_AJJGNKPPJMEGGLFA/msprof_20260609064817.db
  PROF_000001_20260609064648547_OEJLKCHMOPRFGKIB/msprof_20260609064834.db
```

The captures were produced by the project maintainers and are redistributed as
repository test data under the repository license. They contain profiler
records, not model weights or prompt payloads.

This kickstart profile includes ACL graph semantic task names, but it is not
the best fixture for validating native ACLGraph reconstruction because it does
not include the full `host/sqlite/stream_info.db` `CaptureStreamInfo` package
shape. For that, use the curated package under
`examples/paper_artifacts/ascend_interleaved/`.

The databases were deterministically reconstructed before publication. Their
task, API, communication, dependency, string, and PMU evidence is retained;
`HOST_INFO` is empty and referenced strings contain no path or identity-like
values. Source/output hashes and copied row counts are recorded under
`manifests/`.

The recorded reconstruction command for each original database is:

```bash
examples/paper_artifacts/tools/reduce_ascend_sqlite.py SOURCE OUTPUT \
  --start-ns 0 --end-ns 9223372036854775807 \
  --keep-task-pmu --manifest-out MANIFEST
```

## Try It

From the repository root:

```bash
cmake --preset dev
cmake --build --preset dev -j "$(nproc)"
build/native/native/traceloom examples/kickstart_smoke/msprof_raw
```

By default TraceLoom writes the analysis back into:

```text
examples/kickstart_smoke/msprof_raw/traceloom/
```

Open `device0_loop_tree_v2.md` and `device1_loop_tree_v2.md` first.
Generated reports are deliberately not checked in. The verifier below writes
them to a temporary directory instead.

## What To Look For

This pair is deliberately the medium-scale folding artifact rather than the
minimal exact-graph fixture:

```text
device 0 DB: 40,394,752 bytes, 84,928 TASK rows, 518,110 CANN_API rows
device 1 DB: 38,191,104 bytes, 57,455 TASK rows, 518,489 CANN_API rows
```

Current TraceLoom reconstructs the following deterministic structure:

```text
device 0: 86,701 normalized events -> 22,830 semantic anchors -> 507 tree nodes
device 1: 59,226 normalized events -> 21,903 semantic anchors -> 483 tree nodes
pair: 145,927 normalized events -> 44,733 semantic anchors -> 990 tree nodes
```

That is approximately 45 semantic anchors and 147 normalized events per
rendered node. More importantly, both device reports independently recover the
same two nested structures inside the main repeated region:

```text
device 0
  Repeat x29
    Repeat x74
    Repeat x24
      MatMul / Rope / AllReduce / AddRmsNorm / SwiGlu

device 1
  Repeat x29
    Repeat x74
    Repeat x24
      MatMul / Rope / AllReduce / AddRmsNorm / SwiGlu
```

That is the intended kickstart: raw profiler databases become a small,
readable execution structure that developers can compare and drill into.

## Verify The Current Contract

After building the test preset, run:

```bash
examples/paper_artifacts/tools/verify_kickstart_folding.py \
  --traceloom build/native-tests/native/traceloom
```

The verifier checks both database hashes, SQLite integrity, the privacy
boundary, exact current event/anchor/node counts, folding ratios, and the two
nested repeat relationships. This profile intentionally remains negative
capability evidence for exact graph reconstruction: without `stream_info.db`,
graph-like observations stay typed `unrecognized` instead of being promoted.

The checked-in claim is structural compression and faithful visibility, not
analysis throughput, workload-phase naming, or causal performance attribution.
