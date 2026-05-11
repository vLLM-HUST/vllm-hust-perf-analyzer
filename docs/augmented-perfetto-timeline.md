# Augmented Perfetto Timeline

TraceLoom should compile its analysis output into Perfetto / Chrome Trace
compatible tracks instead of requiring a custom frontend for every view.

## Modes

- Analyzer-only timeline: loop nodes, macro occurrences, anchors, aux/prelude
  slots, counters, and quality markers without raw profiler events.
- Merged timeline: append TraceLoom tracks to an exported raw profiler timeline
  so users can inspect structural nodes and low-level events in one viewer.

## Track Model

The target visualization is a hierarchy aligned on the original time axis:

```text
TraceLoom / Repeat Summary / depth=0
TraceLoom / Repeat Summary / depth=1
TraceLoom / Repeat Occurrences / depth=1
TraceLoom / Macro Occurrences / depth=2
TraceLoom / Anchors
TraceLoom / Aux Prelude
TraceLoom / Quality
Raw profiler tracks
```

For example, a top-level decode window can contain a repeated decode core loop,
each occurrence can contain lower-level macro events, and the lowest TraceLoom
track can expose semantic anchor events. This lets a developer move from a
large costly repeat directly to the original fine-grained profiler events.

## Event Types

The MVP can use standard Chrome Trace events:

- `X` complete events for repeat windows, macro occurrences, anchors, and
  aux/prelude slots;
- `C` counter events for compute, collective, idle, and auxiliary duration;
- flow events later, if explicit links to raw profiler records are needed.

## Slice Metadata

Analyzer slices should include:

- `node_id`
- `tree_path`
- `kind`
- `repeat`
- `occurrence_index`
- `macro_name`
- `symbol`
- `semantic_role`
- `label`
- `family`
- `start_anchor_index`
- `end_anchor_index`
- `source_global_task_ids`
- `source_stream_ids`
- `source_table`
- `db_path`
- `device_id`
- `global_rank`
- `compute_us`
- `collective_us`
- `idle_us`
- `aux_us`
- `aux_event_count`
- `node_metrics_file`
- `node_anchor_links_file`
- `tree_json_file`

## MVP Plan

1. Generate anchor slices from `*.anchor.steps.csv`.
2. Generate aux/prelude slices from `*.anchor.aux_slots.csv`.
3. Generate repeat and macro occurrence slices from
   `*.anchor.node_anchor_links.csv` and `*.anchor.node_metrics.csv`.
4. Emit analyzer-only Chrome Trace JSON.
5. Add optional merging with an exported raw trace.
6. Add quality slices for truncation, missing tables, no collective anchors, or
   suspiciously low anchor counts.
