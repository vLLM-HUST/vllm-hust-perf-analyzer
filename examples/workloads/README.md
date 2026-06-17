# Workloads

Reference workloads should be small local examples for validating input
expectations and parser behavior without becoming a runtime framework.

The open-source repository should keep only source code and commands here.
Generated profiles belong in ignored `runs/`, `out/`, or external artifact
storage.

- `vllm_ascend_smoke.py`: parameterized vLLM/vLLM-Ascend decode workload kept as
  a reference for users who collect profiles outside TraceLoom.
- `torch_npu_distributed_smoke.py`: tiny HCCL/two-rank torch-npu workload for
  local parser and communication smoke checks.
