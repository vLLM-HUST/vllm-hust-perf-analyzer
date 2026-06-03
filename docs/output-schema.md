# Output Schema

TraceLoom defaults to a compact analysis bundle rooted at:

```text
<raw_msprof_dir>/traceloom/
```

Use `--out-dir` to place it elsewhere.

## Default Bundle

- `dbNN.traceloom_augmented.db`: one sidecar copy per discovered msprof DB. Raw
  profiler tables remain intact; TraceLoom adds `traceloom_*` tables and views.
- `README.md`: generated instructions for inspecting this bundle.
- `summary.md`: analyzed devices and top loop-cost summary.
- `tree-map.md`: readable node-cost map. Its `node` column maps to
  `traceloom_v_tree_node.local_node_id` for SQL drill-down. The table keeps
  node label, `depth`, node occurrence count, `avg_total_us`, `avg_aux_us`, and
  `total_us` visible while leaving detailed cost breakdowns, anchor counts, and
  anchor ranges to query scripts. See `docs/tree-map-guide.zh.md` for a
  practical Chinese guide to reading the map and drilling into the augmented DB.
- `queries/*.sql`: starter report queries runnable with `traceloom report`.
- `meta.json`: analyzer options, input paths, elapsed time, generated DBs, and
  query files.

This is the intended public surface. SQL reports should query the augmented DBs
rather than depending on CSV/JSON debug exports.

## Full Debug Export

Run with `--output-mode full` to additionally write the legacy per-device
CSV/JSON/Markdown evidence files. These files are useful while developing the
analyzer, but they are not the default user-facing bundle.

## Run-Level Files

- `device_summary.csv`: one row per discovered device profile, with rank,
  device, event counts, total time, and selection metadata.
- `summary.md`: concise human summary with selected devices and the highest
  aggregate loop costs.
- `meta.json`: analyzer version, command-line parameters, input paths, and
  generation metadata.

## Device-Level Files

- `*.anchor.steps.csv`: flattened semantic anchor sequence used by the loop
  analyzer.
- `*.anchor.symbols.csv`: symbol assignment and normalized label metadata.
- `*.anchor.tree.readable.md`: human-readable loop/repeat report.
- `*.anchor.tree.json`: structured loop tree.
- `*.anchor.node_metrics.csv`: per-node timing and composition metrics.
- `*.anchor.node_anchor_links.csv`: links from tree nodes back to anchor index
  ranges and source events.
- `*.anchor.loop_costs.csv`: filtered repeat-node view over
  `*.anchor.node_metrics.csv`, sorted by inclusive total cost.
- `*.anchor.aux_slots.csv`: auxiliary/prelude events attached to following
  anchors.
- `*.anchor.root_item_metrics.csv`: top-level item metrics for the compressed
  root sequence, when emitted.

The run-level `compute_anchor_loop_costs.csv` concatenates all per-device
`*.anchor.loop_costs.csv` files.

## Compatibility Rule

Public releases should version the output schema before changing column names,
JSON fields, or file naming. Until that version is frozen, downstream scripts
should treat the schema as alpha.
