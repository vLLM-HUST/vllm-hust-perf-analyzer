# Reference Reproduction

TraceLoom's paper reproduction flow should be lightweight and environment
native. The repository provides workload and profiler recipes, not large
pre-collected profile databases.

## Requirements

- A multi-card CUDA or Ascend machine, depending on the reproduced experiment.
- The user's existing CUDA/Nsight or Ascend/CANN software stack.
- Python 3.10 or newer for TraceLoom.
- Workload dependencies installed by the user in their normal environment.

## Ascend/CANN Recipe

Analyze an existing profile:

```bash
python3 reproduce/run_reference.py analyze-msprof \
  /path/to/run-or-msprof-raw-dir \
  --name reviewer_msprof
```

Profile and analyze a workload:

```bash
python3 reproduce/run_reference.py ascend-msprof \
  --name ascend_reference \
  -- \
  python3 /path/to/workload.py --arg value
```

Outputs are written under `out/reproduce/<name>/analysis/`. The raw msprof
profile is written under `out/reproduce/<name>/msprof_raw/` when the script
collects the profile itself.

## CUDA/Nsight Recipe

CUDA support is part of the target public interface. The intended recipe is:

```bash
python3 reproduce/run_reference.py cuda-nsys \
  --name cuda_reference \
  -- \
  torchrun --nproc_per_node=2 examples/workloads/pytorch_ddp_matmul/train.py
```

This currently collects the profile and writes a manifest. Full CUDA/Nsight
analysis is enabled after the Nsight adapter is implemented.

## Reproduction Contract

The goal is structural and metric reproduction, not byte-identical profile
files. Acceptable differences include hardware SKU, driver/runtime version,
clocking, placement, and workload scale, provided the same loop structure and
reported bottleneck categories are recovered.
