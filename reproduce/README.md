# Reproduce

`run_reference.py` is the one-command reproduction entry point. By default it
writes generated profiles, analyzer outputs, and manifests under:

```text
traceloom/out/reproduce/
```

That directory is ignored by git.

After `python3 -m pip install -e .`, the same script is available as
`traceloom-reproduce`.

## Analyze An Existing Ascend/CANN Profile

```bash
python3 reproduce/run_reference.py analyze-msprof \
  /path/to/run-or-msprof-raw-dir \
  --name reviewer_msprof
```

The TraceLoom outputs are written to:

```text
out/reproduce/reviewer_msprof/analysis/
```

## Profile And Analyze An Ascend/CANN Workload

Activate the user's CANN environment first, then run:

```bash
python3 reproduce/run_reference.py ascend-msprof \
  --name ascend_reference \
  -- \
  python3 /path/to/workload.py --arg value
```

The script calls `msprof`, writes raw profile data under
`out/reproduce/ascend_reference/msprof_raw/`, then runs TraceLoom and writes
analysis outputs under `out/reproduce/ascend_reference/analysis/`.

Extra profiler flags can be passed with repeated `--msprof-arg`:

```bash
python3 reproduce/run_reference.py ascend-msprof \
  --msprof-arg=--some-msprof-flag=value \
  -- \
  python3 /path/to/workload.py
```

## CUDA/Nsight Profile Collection

CUDA/Nsight analysis is still a planned adapter. The script can collect a native
Nsight profile and write a manifest now:

```bash
python3 reproduce/run_reference.py cuda-nsys \
  --name cuda_reference \
  -- \
  torchrun --nproc_per_node=2 examples/workloads/pytorch_ddp_matmul/train.py
```

The generated manifest records that CUDA analysis is not complete yet.

## Dry Run

Use `--dry-run` to print commands and write manifests without invoking
profilers or the analyzer:

```bash
python3 reproduce/run_reference.py ascend-msprof --dry-run -- python3 workload.py
```
