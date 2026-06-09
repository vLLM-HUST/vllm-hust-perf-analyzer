# Workloads

Reference workloads should be small launch recipes that exercise accelerator
profilers without becoming a runtime framework.

The open-source repository should keep only source code and commands here.
Generated profiles belong in ignored `runs/`, `out/`, or external artifact
storage.

- `vllm_ascend_smoke.py`: parameterized vLLM/vLLM-Ascend decode workload used
  by the CANN Decode All-to-All Buffer Reuse reproduction scripts.
- `torch_npu_distributed_smoke.py`: tiny HCCL/two-rank torch-npu workload used
  to generate a short real `msprof` bundle for README kickstart material.
