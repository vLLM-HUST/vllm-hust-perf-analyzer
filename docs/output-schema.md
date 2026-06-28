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

## Optional Collective Tagging

After analysis, run:

```bash
traceloom collective-tag <raw_msprof_dir>/traceloom
```

This adds:

- `traceloom_collective_global_link` inside each `dbNN.traceloom_augmented.db`;
- `global_collectives.db`, with `traceloom_global_collective_summary` and
  member rows;
- `collective_summary.md`, a compact skew and completeness report.

The generated `candidate_collective_key` is a structural candidate built from
matched repeat-loop position, occurrence index, collective type, and order in
that occurrence. Treat it as cross-device correlation evidence rather than a
hard proof.

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

Node id contract: the `Nxxx` ids shown in `*.anchor.tree.readable.md`,
`*.anchor.tree.json`, `*.anchor.node_metrics.csv`, `*.anchor.loop_costs.csv`,
and augmented DB `local_node_id` columns are the same user-facing namespace. If
a JSON node contains `raw_node_id`, that value is only the builder's
pre-normalization provenance id and must not be used as the report key.
When device-side graph replay evidence is available, the readable tree can also
contain `GraphReplay` overlay nodes. These rows use the same `Nxxx` namespace,
appear in the augmented DB with `kind='graph'`, and carry replay timing plus the
covered anchor span when one overlaps the anchor view.
- `*.anchor.root_item_metrics.csv`: top-level item metrics for the compressed
  root sequence, when emitted.
- `*.cuda_graph_events.csv`: graph replay evidence table. For CUDA/Nsight this
  is one row per `CUPTI_ACTIVITY_KIND_GRAPH_TRACE` event. For Ascend/CANN this
  can also contain synthetic `ACL_GRAPH_REPLAY` rows reconstructed from
  device-side `CaptureStreamInfo`, `TASK`, and `ascend_task.db` evidence. Use
  `graph_provider` to distinguish `cuda` from `aclgraph`.
- `*.cuda_graph_envelope_events.csv`: best-effort graph replay envelope links.
  CUDA rows connect a `CudaGraphReplay` event to CUDA activity events associated
  with the replay. Ascend ACLGraph rows connect a reconstructed replay interval
  to visible TraceLoom events that overlap it while preserving raw child-task
  counts and top ops in the replay row.
- `aclgraph_summary.md` / `aclgraph_summary.json`: Ascend ACLGraph
  reconstruction summary, including capture stream counts, model-stream ranges,
  semantic/control task counts, replay intervals, and quality classification.
- `aclgraph_events.csv`: one row per reconstructed ACLGraph replay interval.
- `aclgraph_envelope_events.csv`: overlap links from ACLGraph replay intervals
  to visible TraceLoom events.
- `aclgraph_semantic_tasks.csv`: device-side graph/control TASK rows such as
  `MODEL_EXECUTE`, `MODEL_MAINTAINCE`, `NOTIFY_WAIT`, `NOTIFY_RECORD`, and
  `Notify Wait`.
- `aclgraph_model_streams.csv`: `CaptureStreamInfo` model-stream mappings plus
  observed activity summaries.

The run-level `compute_anchor_loop_costs.csv` concatenates all per-device
`*.anchor.loop_costs.csv` files.
The run-level `cuda_graph_events.csv` concatenates all per-device graph replay
evidence tables, including ACLGraph rows when present.
The run-level `cuda_graph_envelope_events.csv` concatenates all graph envelope
links.

## Compatibility Rule

Public releases should version the output schema before changing column names,
JSON fields, or file naming. Until that version is frozen, downstream scripts
should treat the schema as alpha.
