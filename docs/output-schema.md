# Output Schema

TraceLoom writes one output bundle per selected device/profile DB plus run-level
summary files.

## Run-Level Files

- `device_summary.csv`: one row per discovered device profile, with rank,
  device, event counts, total time, and selection metadata.
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
- `*.anchor.aux_slots.csv`: auxiliary/prelude events attached to following
  anchors.
- `*.anchor.root_item_metrics.csv`: top-level item metrics for the compressed
  root sequence, when emitted.
- `*.analyzer.timeline.json`: planned analyzer-only Chrome Trace output.
- `*.augmented.perfetto.json`: planned merged raw + analyzer timeline output.

## Compatibility Rule

Public releases should version the output schema before changing column names,
JSON fields, or file naming. Until that version is frozen, downstream scripts
should treat the schema as alpha.
