# TraceLoom Agent Guide

This repository is the standalone TraceLoom analyzer. Treat it as an
offline profiler-analysis tool, not as a runtime environment manager.

## Primary Goal

TraceLoom consumes accelerator profiler artifacts and emits evidence tables,
readable loop trees, and summary reports. The currently supported production
input is Ascend/CANN `msprof` SQLite output.

Do not commit raw profiler databases, generated reports, local environment
files, model weights, or benchmark logs. Keep those in ignored output
directories or external artifact storage.

## Setup

From this directory:

```bash
python3 -m pip install -e .
python3 -m traceloom --help
```

For local development without installation:

```bash
PYTHONPATH="$PWD" python3 -m traceloom --help
```

Useful smoke check after code changes:

```bash
python3 -m compileall traceloom reproduce
```

## Analyze Existing msprof Data

Preferred command:

```bash
traceloom analysis /path/to/msprof_raw --out-dir /path/to/analysis
```

Equivalent local-development form:

```bash
PYTHONPATH="$PWD" python3 -m traceloom analysis /path/to/msprof_raw --out-dir /path/to/analysis
```

Valid inputs:

```text
<run_dir>/msprof_raw/PROF_*/msprof_*.db
<raw_dir>/PROF_*/msprof_*.db
```

If a profile is very large, start with bounded analysis:

```bash
traceloom analysis /path/to/msprof_raw \
  --out-dir /path/to/analysis \
  --top-devices-global 4 \
  --max-main-events-per-device 5000 \
  --max-macro-defs 32
```

Use `--devices 3,4,5,6` to pin physical Ascend device IDs.

Key outputs to inspect first:

- `summary.md`
- `device_summary.csv`
- `compute_anchor_loop_costs.csv`
- `*.anchor.tree.readable.md`
- `*.anchor.node_metrics.csv`
- `*.anchor.aux_slots.csv`
- `meta.json`

See `docs/output-schema.md` for the output contract and
`docs/input-profiles.md` for accepted input layouts.

## Profile Inputs

TraceLoom is a post-processing analyzer. Users collect Ascend/CANN `msprof`
profiles outside this repository, then pass the raw profile directory to
`traceloom analysis` or `traceloom analyze`.

## Repository Hygiene

Ignored generated paths include:

- `out/`
- `outputs/`
- `profiles/`
- `artifacts/`
- `*.db`, `*.sqlite`, `*.nsys-rep`, `*.qdrep`, `*.trace`

Before handing changes back:

```bash
git status --short
python3 -m compileall traceloom reproduce
```

When adding features, keep the public CLI stable:

- `traceloom analysis`
- `traceloom analyze`

The `reproduce/` package is optional reference material. Keep it installable,
but do not make the main `traceloom` analyzer depend on hardware-specific
reproduction workflows.

Prefer small, inspectable CSV/JSON/Markdown outputs over binary artifacts.
