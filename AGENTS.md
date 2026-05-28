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

## Collecting Profiles

TraceLoom can generate an editable config and invoke `msprof`, but it does not
install CANN, drivers, Docker images, models, or vLLM.

```bash
traceloom create config -o traceloom.profile.ini
# edit workload.command, paths.profile_dir, paths.analysis_dir, profiler.extra_args
traceloom run traceloom.profile.ini
traceloom analysis runs/local-msprof/msprof_raw --out-dir runs/local-msprof/analysis
```

`traceloom run` calls:

```text
msprof --output=<profile_dir> --application=<workload.command> <profiler.extra_args>
```

If the workload must run inside an existing container, configure the `[docker]`
section. Prefer `docker exec <existing-container>` over creating a new container
unless the user explicitly provides image, volume, device, network, and CANN
mount details.

## Reproduction Scripts

The decode all-to-all buffer reuse reproduction is under
`reproduce/decode_a2a_buffer_reuse/`.

Use:

```bash
cp reproduce/decode_a2a_buffer_reuse/env.example reproduce/decode_a2a_buffer_reuse/local.env
# edit local.env for the local Ascend/CANN host, model path, and device set
bash run_decode_a2a_buffer_reuse.sh --env-file reproduce/decode_a2a_buffer_reuse/local.env
```

Local env files matching `reproduce/decode_a2a_buffer_reuse/local*.env` are
ignored and must remain machine-local.

## Repository Hygiene

Ignored generated paths include:

- `out/`
- `outputs/`
- `profiles/`
- `artifacts/`
- `*.db`, `*.sqlite`, `*.nsys-rep`, `*.qdrep`, `*.trace`
- `reproduce/decode_a2a_buffer_reuse/local*.env`

Before handing changes back:

```bash
git status --short
python3 -m compileall traceloom reproduce
```

When adding features, keep the public CLI stable:

- `traceloom analysis`
- `traceloom analyze`
- `traceloom create config`
- `traceloom run`

Prefer small, inspectable CSV/JSON/Markdown outputs over binary artifacts.
