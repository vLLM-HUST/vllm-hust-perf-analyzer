# Examples

This directory contains lightweight recipes and tiny workload scaffolds. It
does not contain large profiler databases.

- `configs/ascend_msprof_vllm_decode.yaml`: target Ascend/CANN profiling
  recipe for a vLLM decode workload.
- `configs/cuda_nsys_torchrun.yaml`: target CUDA/Nsight profiling recipe.
- `workloads/pytorch_ddp_matmul/`: small PyTorch distributed workload useful
  for smoke profiling on CUDA machines.
