# Examples

This directory contains lightweight recipes, sample report text, and tiny
workload scaffolds. It intentionally does not contain large profiler
databases.

- `kickstart_smoke/`: real two-device Ascend/CANN `msprof` smoke profile and
  TraceLoom output for a deterministic first run.
- `configs/ascend_msprof_vllm_decode.yaml`: target Ascend/CANN profiling
  recipe for a vLLM decode workload.
- `configs/cuda_nsys_torchrun.yaml`: target CUDA/Nsight profiling recipe for a
  planned input adapter.
- `workloads/vllm_ascend_smoke.py`: small vLLM-Ascend smoke workload scaffold.

For the fastest first run, start with `kickstart_smoke/`.
