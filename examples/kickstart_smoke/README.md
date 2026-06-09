# TraceLoom Kickstart Smoke

This directory contains a real two-device Ascend/CANN `msprof` smoke profile
and the TraceLoom analysis generated from it.

The workload is `examples/workloads/torch_npu_distributed_smoke.py`: a tiny
two-rank torch-npu program that repeats `matmul -> gelu -> all_reduce`.

The checked-in capture was generated on an Ascend 910B3 machine with two
visible devices:

```bash
msprof --output=/tmp/traceloom_kickstart_smoke/msprof_raw \
  --hccl=on \
  --runtime-api=on \
  --task-time=on \
  --type=db \
  --application=/tmp/run_traceloom_smoke.sh
```

The workload command inside `/tmp/run_traceloom_smoke.sh` was:

```bash
ASCEND_RT_VISIBLE_DEVICES=0,1 \
ASCEND_VISIBLE_DEVICES=0,1 \
HCCL_OP_EXPANSION_MODE=AIV \
torchrun --standalone --nnodes=1 --nproc-per-node=2 \
  /workspace/torch_npu_distributed_smoke.py \
  --iters 6 --warmup 2 --size 256
```

## Re-run The Analysis

From the `traceloom/` repository root:

```bash
traceloom analyze examples/kickstart_smoke/msprof_raw \
  --devices 0,1 \
  --max-main-events-per-device 0
```

## What To Inspect

- `msprof_raw/traceloom/summary.md`: confirms TraceLoom analyzed two devices.
- `msprof_raw/traceloom/tree-map.md`: shows both devices compressed into the same
  `Repeat x8` structure.
- `msprof_raw/traceloom/queries/*.sql`: starter SQL reports. Running the
  analysis command locally also creates queryable `dbNN.traceloom_augmented.db`
  files in the same directory.
- `msprof_raw/PROF_*/msprof_*.db`: original CANN profiler databases.

The captured raw DBs each contain 82 `TASK` rows, 8 communication task rows, and
8 communication op rows. TraceLoom normalizes each device into 36 events and 16
semantic anchors, then recovers the repeated `MatMulV2 -> AllReduce` execution
structure.
