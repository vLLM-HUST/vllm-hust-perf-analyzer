# TraceLoom

`traceloom` is the extracted offline analyzer for Ascend/CANN `msprof`
results.

It is intentionally focused on the path that produced the current best readable
tree:

```text
compute_prelude_timeline.py
```

The package now has a thin config-driven runner for `msprof`, but the analyzer
itself remains offline. Normal use is an editable install followed by direct
analysis of an existing profiler output:

```bash
python3 -m pip install -e .
traceloom analyze <run-dir-or-msprof-raw-dir>
```

Direct analysis consumes an existing raw profiler directory containing:

```text
<run_dir>/msprof_raw/PROF_*/msprof_*.db
```

or a raw directory directly:

```text
<raw_dir>/PROF_*/msprof_*.db
```

By default the output bundle is written to:

```text
<raw_dir>/traceloom/
```

The bundle contains augmented SQLite DBs, `tree-map.md`, `README.md`,
`summary.md`, `queries/*.sql`, and `meta.json`. Use `--output-mode full` to
also write the legacy CSV/JSON debug files.

## Optional Run Config

Example:

```bash
traceloom create config -o traceloom.profile.ini
traceloom run traceloom.profile.ini
traceloom analyze runs/local-msprof/msprof_raw
```

The three commands are intentionally separate:

- `create config` writes an editable INI template;
- `run` starts `msprof` and the configured workload, optionally inside Docker;
- `analysis` reads an existing `msprof` output directory and runs offline
  loop/cost analysis.

Analysis defaults to every discovered device. Use `--devices 3,4,5,6`, or the
matching `[analysis] devices` config key, when a run should be pinned to a
specific physical device set. Set `max_main_events_per_device = 0` or
`max_macro_defs = 0` only for an exhaustive pass over a large profile.

Direct analysis of an existing profile:

```bash
python3 -m traceloom analysis \
  <run_dir-or-raw-msprof-dir> \
  --out-dir /tmp/traceloom_out \
  --output-mode full \
  --max-main-events-per-device 0 \
  --max-macro-defs 0
```

From a checkout, use the wrapper script when the package is not installed:

```bash
scripts/traceloom-analyze.sh <run_dir-or-raw-msprof-dir>
```

The default wrapper configuration is aimed at offline pattern discovery: it
does not truncate main events and lets macro discovery continue while the pair
grammar still has positive gain. Inspect `summary.md`, then query
`dbNN.traceloom_augmented.db` with scripts in `queries/`.

## Scope Boundary

TraceLoom should stay an offline pattern-discovery tool. It reads an existing
profile, writes augmented DB evidence, and exposes SQL-friendly reports.
Visualization is a downstream concern; the analyzer core should not grow more
UI or timeline-export responsibilities during this cleanup.

## Current Loop Algorithm

The analyzer now uses one loop-discovery path:

1. Build the semantic anchor timeline. Compute anchors come from `TASK`;
   collective anchors, including all-reduce, all-gather, all-to-all,
   reduce-scatter, and broadcast, come from Huawei `COMMUNICATION_OP` when
   available, with the older `TASK` coalescing path used only as a fallback.
2. Convert the semantic anchor timeline into a symbol sequence.
3. Repeatedly discover profitable adjacent pairs and promote them to macros.
4. When a macro appears as an adjacent run, promote that run into an LP macro.
5. Build the readable tree directly from the resulting grammar. The tree builder
   does not run a second `ABCABC` detector; `Repeat` nodes come from LP macros.

This keeps the proof story simple: loop evidence is introduced only during
recursive grammar construction, and the tree renderer is a view of that grammar.

## Extracted Files

- `compute_prelude_timeline.py`: current canonical offline analyzer. It builds
  device-level compute/communication anchor timelines, performs semantic
  projection, attaches aux/prelude cost, discovers macro/loop structure, and
  writes readable trees plus CSV/JSON evidence.
- `loop_tree.py`: transitional shared implementation copied from the old stream
  loop analyzer. `compute_prelude_timeline.py` currently reuses its symbol,
  macro, and grammar-only tree functions.
- `msprof_reader.py`: SQLite reading, string-id resolution, task category
  classification, task label resolution, device event loading, and stream
  ranking.
- `io/discover.py`: raw `msprof_*.db` discovery.

## Refactor Direction

This folder is a staging area. The next cleanup should split `loop_tree.py` into
small modules:

- `msprof_reader.py`: SQLite reading, string-id resolution, task label
  resolution. Done as the first split from `loop_tree.py`.
- `symbols.py`: normalization, family detection, symbol assignment.
- `grammar.py`: pair-grammar macro discovery and adjacent macro-run loop
  promotion.
- `tree.py`: AST construction, macro inline, readable rendering.
- `anchor_timeline.py`: the renamed main analyzer pipeline.

The old `analyzer/hprofile` package has been archived under
`../archive/analyzer/`. Treat it as historical reference only; new production
work should happen in `traceloom`.
