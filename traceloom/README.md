# TraceLoom

`traceloom` is the extracted offline analyzer for Ascend/CANN `msprof`
results.

It is intentionally focused on the path that produced the current best readable
tree:

```text
compute_prelude_timeline.py
```

The package does not launch workloads or invoke `msprof`. It consumes an
existing raw profiler directory containing:

```text
<run_dir>/msprof_raw/PROF_*/msprof_*.db
```

or a raw directory directly:

```text
<raw_dir>/PROF_*/msprof_*.db
```

## Run

Example:

```bash
python3 -m traceloom.compute_prelude_timeline \
  <run_dir-or-raw-msprof-dir> \
  --out-dir /tmp/traceloom_out \
  --max-main-events-per-device 0 \
  --max-macro-defs 0
```

The shorter package entry also works:

```bash
python3 -m traceloom <run_dir-or-raw-msprof-dir> --out-dir /tmp/traceloom_out
```

## Timeline View Roadmap

The production visualization path should be an augmented Perfetto/Chrome Trace
export: repeat nodes, macro occurrences, semantic anchors, and aux/prelude slots
should appear as analyzer tracks that can be loaded next to the original
profiler timeline. See `../docs/augmented-perfetto-timeline.md`.

The current score view remains a lightweight static debug view until that
exporter is implemented.

Build an execution score view for one generated tree:

```bash
python3 -m traceloom.score_view \
  /tmp/traceloom_out/db03_rank03_dev4_compute.anchor.tree.json \
  --out /tmp/traceloom_out/db03_rank03_dev4_compute.anchor.score_view.html
```

The score view is a static, self-contained HTML page. It renders compressed
tree leaves as narrow equal-width score columns, colors the kernel-type row by
kernel or communication family, and separates idle time from device-active
time. The bar rows show active average (`compute + communication`), idle
average, active mix (`compute` vs. `communication`), and self active time under
each column. `Repeat` regions are expanded by default and shown with bracket
spans; hovering a column or span shows exact values in the side panel.

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
  macro, grammar-only tree, and msprof DB helper functions.
- `io/discover.py`: raw `msprof_*.db` discovery.

## Refactor Direction

This folder is a staging area. The next cleanup should split `loop_tree.py` into
small modules:

- `msprof_reader.py`: SQLite reading, string-id resolution, task label
  resolution.
- `symbols.py`: normalization, family detection, symbol assignment.
- `grammar.py`: pair-grammar macro discovery and adjacent macro-run loop
  promotion.
- `tree.py`: AST construction, macro inline, readable rendering.
- `anchor_timeline.py`: the renamed main analyzer pipeline.

The old `analyzer/hprofile` package has been archived under
`../archive/analyzer/`. Treat it as historical reference only; new production
work should happen in `traceloom`.
