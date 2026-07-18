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
The checked-in raw profile databases are:

```text
msprof_raw/
  PROF_000001_20260609064648517_AJJGNKPPJMEGGLFA/msprof_20260609064817.db
  PROF_000001_20260609064648547_OEJLKCHMOPRFGKIB/msprof_20260609064834.db
```

This kickstart profile includes ACL graph semantic task names, but it is not
the best fixture for validating native ACLGraph reconstruction because it does
not include the full `host/sqlite/stream_info.db` `CaptureStreamInfo` package
shape. For that, use the curated package under
`data/experiment-results/ascend_tp2_graph_showcase/`.

## Try It

From the repository root:

```bash
cmake --preset dev
cmake --build --preset dev -j "$(nproc)"
build/native/native/traceloom examples/kickstart_smoke/msprof_raw
```

TraceLoom writes the analysis back into:

```text
examples/kickstart_smoke/msprof_raw/traceloom/
```

Open `device0_loop_tree_v2.md` and `device1_loop_tree_v2.md` first.

## What To Look For

The raw profile is noisy enough to be representative:

```text
device 0 DB: 84,928 TASK rows, 518,110 CANN_API rows, 1,775 HCCL task rows
device 1 DB: 57,455 TASK rows, 518,489 CANN_API rows, 1,773 HCCL task rows
```

TraceLoom compresses it into comparable device structures:

```text
device 0: 33,964 normalized events -> 11,008 semantic anchors -> 58 tree nodes
device 1: 32,220 normalized events -> 10,510 semantic anchors -> 54 tree nodes
selected raw table rows -> structural nodes: 1.18M+ -> 112 (~10,500:1)
```

The useful showpiece is the shared nested loop pattern:

```text
device 0
  N014 Repeat x36
    N017 Repeat x24
      MatMul / Rope / HCCL AllReduce / AddRmsNorm / SwiGlu

device 1
  N010 Repeat x36
    N013 Repeat x24
      MatMul / Rope / HCCL AllReduce / AddRmsNorm / SwiGlu
```

That is the intended kickstart: raw profiler databases become a small,
readable execution structure that developers can compare and drill into.

## Example Findings

This profile is small enough to inspect, but large enough to show the analysis
shape TraceLoom is designed for:

- Both devices recover the same nested Pattern Compression Tree:
  `Repeat x36` outside, `Repeat x24` inside.
- The outer repeated region dominates structural time: about 89% on device 0
  and 91% on device 1.
- The inner layer block accounts for about 75-77% of structural time across the
  two devices.
- HCCL AllReduce appears inside the repeated layer body, so communication is
  visible in the same structure as MatMul, Rope, normalization, and activation
  kernels.
- The result is small enough for review: 112 structural nodes across two
  devices, with SQL links back to the original profiler events.
