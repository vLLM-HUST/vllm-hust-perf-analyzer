# Workflow

TraceLoom is built around a simple offline workflow:

```text
msprof output
  -> parser
  -> event database / structured trace
  -> pattern miner
  -> hotspot attribution
  -> report
```

## 1. Collect A Native Profile

Run the workload with Ascend/CANN `msprof` using the profiling setup that
matches your environment. TraceLoom does not require a special runtime wrapper.
The analyzer expects one of these layouts:

```text
<run_dir>/msprof_raw/PROF_*/msprof_*.db
<raw_dir>/PROF_*/msprof_*.db
```

The profile should include task timing and string metadata. Communication
tables improve collective and synchronization diagnosis when available.

## 2. Run TraceLoom Analysis

Install the package and analyze the profile directory:

```bash
python3 -m pip install -e .
traceloom analyze /path/to/msprof_output
```

For large multi-device profiles, narrow the analysis scope:

```bash
traceloom analyze /path/to/msprof_output --top-devices-global 4
traceloom analyze /path/to/msprof_output --devices 3,4,5,6
```

TraceLoom writes the compact bundle to `<raw_dir>/traceloom/` unless
`--out-dir` is provided.

## 3. Inspect The Human Reports

Start with:

- `summary.md`: selected devices and top repeated loop costs;
- `tree-map.md`: readable map of repeated nodes and cost columns;
- `aclgraph_summary.md`: Ascend ACLGraph replay reconstruction, when the input
  profile contains `CaptureStreamInfo` and graph semantic TASK rows;
- generated bundle `README.md`: local inspection commands.

Use the report to identify the main loops, dominant kernel families, and
communication-heavy nodes before drilling into raw events.

## 4. Drill Down With SQL

Each `dbNN.traceloom_augmented.db` contains raw profiler tables plus
TraceLoom-owned tables and views. Starter queries are copied into
`queries/*.sql`.

Example:

```bash
traceloom report out/qwen2/traceloom/db01.traceloom_augmented.db \
  --sql out/qwen2/traceloom/queries/node-cost-breakdown.sql \
  --format md
```

Typical drill-down questions:

- Which repeated node has the largest inclusive duration?
- Which anchors are covered by this node?
- Which auxiliary events are attached to the following anchor?
- Does communication cost cluster near a specific repeated pattern?
- Do reconstructed ACLGraph replay intervals overlap the kernels or collectives
  being inspected?

## 5. Attribute Hotspots

TraceLoom uses normalized kernel names, kernel families, collective anchors,
and optional role overrides to connect low-level events to likely operator
behavior. Treat the result as diagnostic evidence:

- anchor kernels describe the primary runtime skeleton;
- auxiliary/prelude events explain preparation, synchronization, and data
  movement around anchors;
- source/operator attribution candidates should be confirmed with workload
  code and profiler metadata when making optimization decisions.

## 6. Share A Reproducible Diagnosis

For a useful performance report, share:

- the TraceLoom command and options;
- `summary.md` and `tree-map.md`;
- selected SQL report outputs;
- profile metadata and checksums;
- a clear statement of assumptions and unsupported trace fields.

Avoid committing large raw `msprof` databases to the repository.

## 7. Try The Ascend ACLGraph Showcase

The repository data plane includes a curated Ascend graph-mode sample at:

```text
data/experiment-results/ascend_tp2_graph_showcase/showcase.tar.zst
```

It contains raw Huawei `msprof` DBs, TraceLoom full output, ACLGraph
reconstruction files, and a ready-to-open Perfetto/Chrome trace. After unpacking
the archive, inspect:

- `derived/traceloom/summary.md`
- `derived/traceloom/aclgraph_summary.md`
- `derived/traceloom/dbNN.traceloom_augmented.db`
- `derived/perfetto/trace_timeline.json.gz`

In the augmented DB, ACLGraph replay intervals appear as normalized events with
`source_table='ACLGRAPH_REPLAY'` and graph rows with
`graph_provider='aclgraph'`.
