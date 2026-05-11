# Architecture

TraceLoom is organized around a stable intermediate representation:

1. profile readers discover profiler outputs and load raw events;
2. normalizers map profiler-specific events into semantic anchors;
3. loop analysis compresses anchor sequences into macro and repeat structure;
4. metrics attach compute, collective, idle, and aux/prelude costs to nodes;
5. exporters write tables, readable reports, and augmented timelines.

## Current Module Map

- `traceloom.compute_prelude_timeline`: canonical Ascend/CANN analysis
  pipeline. This is still a large transitional module.
- `traceloom.io.discover`: profile DB discovery.
- `traceloom.loop_tree`: grammar and tree utilities copied from the previous
  analyzer.
- `traceloom.score_view`: optional static HTML debug view.

## Refactor Targets

- `readers/ascend_msprof.py`: CANN SQLite loading and schema diagnostics.
- `readers/cuda_nsys.py`: future Nsight input adapter.
- `anchors.py`: semantic role classification and symbol normalization.
- `grammar.py`: macro discovery and repeat promotion.
- `tree.py`: structured loop tree construction.
- `metrics.py`: cost attribution and quality checks.
- `exporters/perfetto.py`: analyzer-only and merged augmented timeline output.
- `cli.py`: public command-line entry points.
