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

1. Run the target workload under CANN `msprof`, writing results to an output
   directory such as `runs/reference_decode/msprof_raw`.
2. Confirm that the profile contains `PROF_*/msprof_*.db`.
3. Analyze:

```bash
traceloom runs/reference_decode --out-dir out/reference_decode
```

4. Compare generated tables and readable reports with the paper's expected
   metrics and loop structure.

## CUDA/Nsight Recipe

CUDA support is part of the target public interface. The intended recipe is:

1. Launch the reference workload with `torchrun` or the user's native launcher.
2. Collect a native Nsight Systems profile.
3. Export the profile to the supported TraceLoom input representation.
4. Run TraceLoom and compare the generated outputs with the paper tables.

## Reproduction Contract

The goal is structural and metric reproduction, not byte-identical profile
files. Acceptable differences include hardware SKU, driver/runtime version,
clocking, placement, and workload scale, provided the same loop structure and
reported bottleneck categories are recovered.
