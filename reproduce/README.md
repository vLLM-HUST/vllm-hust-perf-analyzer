# Reproduce

`run_reference.py` is the one-command reproduction entry point. By default it
writes generated profiles, analyzer outputs, and manifests under:

```text
traceloom/out/reproduce/
```

That directory is ignored by git.

After `python3 -m pip install -e .`, the same script is available as
`traceloom-reproduce`.

## Reproduce Thesis Patch006 Tables

The current thesis experiment is CANN-only. To regenerate the paper-facing
Patch006 macro/micro comparison tables from the checked experiment bundle:

```bash
python3 reproduce/run_reference.py paper-patch006
```

Artifacts are written to:

```text
out/reproduce/paper_patch006/
```

This deterministic mode consumes the archived experiment bundle under
`../template-of-thesis/experiments-data/run_20260507_npu3456`. It reproduces the
paper table values stored with that bundle.

To recompute the checked bundle with the current TraceLoom taxonomy:

```bash
python3 reproduce/run_reference.py paper-patch006 --mode bundle-recomputed
```

If local raw `msprof` DBs are available under `../analyzer/out`, rerun the
TraceLoom analysis first and then regenerate the tables:

```bash
python3 reproduce/run_reference.py paper-patch006 --mode raw-analysis
```

For a fresh Ascend host, copy and fill the CANN recipe environment first:

```bash
cp reproduce/cann_patch006/env.example reproduce/cann_patch006/local.env
$EDITOR reproduce/cann_patch006/local.env
bash reproduce/cann_patch006/run_ab_benchmark.sh --env-file reproduce/cann_patch006/local.env
bash reproduce/cann_patch006/run_profile_pair.sh --env-file reproduce/cann_patch006/local.env
```

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

## Dry Run

Use `--dry-run` to print commands and write manifests without invoking
profilers or the analyzer:

```bash
python3 reproduce/run_reference.py ascend-msprof --dry-run -- python3 workload.py
```
