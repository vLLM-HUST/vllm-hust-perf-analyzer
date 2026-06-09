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

## Try It

From the repository root:

```bash
python3 -m pip install -e .
traceloom analyze examples/kickstart_smoke/msprof_raw
```

TraceLoom writes the analysis back into:

```text
examples/kickstart_smoke/msprof_raw/traceloom/
```

Start with `summary.md`, then open `tree-map.md`.

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
