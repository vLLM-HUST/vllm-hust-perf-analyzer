# TraceLoom

[中文说明](README.zh.md)

![Status](https://img.shields.io/badge/status-alpha-orange)
![Python](https://img.shields.io/badge/python-3.10%2B-blue)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Structured trace analysis framework for Ascend inference runtime traces.

**From raw timelines to actionable performance diagnosis.**

TraceLoom is an execution semantic reconstruction engine for low-level Ascend
profiling traces. It turns raw `msprof` databases into queryable performance
diagnosis artifacts, helping developers see what an AI inference runtime is
actually doing: dominant kernels, recurring execution patterns,
synchronization stalls, communication bottlenecks, and likely source/operator
causes.

On the checked-in real vLLM-Ascend kickstart profile, TraceLoom compresses
more than 1.18M selected profiler rows into 112 structural nodes and recovers
the same nested `Repeat x36 -> Repeat x24` execution pattern on two Ascend
devices. The result is a compact Pattern Compression Tree that makes repeated
runtime structure, HCCL communication, and kernel-level hotspots visible at a
glance.

The core flow is:

```text
trace compression -> structure recovery -> cost attribution -> evidence drill-down
```

The common workflow is intentionally simple: collect a native `msprof` profile
outside TraceLoom, give the profile directory to TraceLoom, then read the
generated reports in the same profile directory.

## At A Glance

```text
Raw Ascend msprof trace
    |
    v
Event Normalization
    |
    v
Semantic Anchor Extraction
    |
    v
Pattern Discovery
    |
    v
Pattern Compression Tree Construction
    |
    v
Anchor-Auxiliary Cost Attribution
    |
    v
Evidence-linked Diagnosis Artifact
(Tree Map / SQL Report / Augmented Profile DB)
```

TraceLoom compresses dense low-level profiler databases into a small set of
questions developers can act on:

- What is this runtime actually doing?
- What is the semantic execution skeleton?
- Which loops or fragments repeat?
- Which kernels, collectives, and synchronization regions dominate cost?
- Which auxiliary or prelude events explain hidden cost around anchors?
- Which nodes should be inspected against raw profiler events with SQL?

## Real Kickstart Bundle

```text
examples/kickstart_smoke/msprof_raw/
  PROF_...device_0.../msprof_20260609064817.db
  PROF_...device_1.../msprof_20260609064834.db

Real vLLM-Ascend generation profile
  model: Qwen2.5-0.5B-Instruct
  hardware: Ascend 910B3
  parallelism: tensor_parallel_size=2
  request: 1 prompt, 12 generated tokens

Raw profile scale
  device 0 DB: 84,928 TASK rows, 518,110 CANN_API rows, 1,775 HCCL task rows
  device 1 DB: 57,455 TASK rows, 518,489 CANN_API rows, 1,773 HCCL task rows

TraceLoom compression
  device 0: 33,964 normalized events -> 11,008 semantic anchors -> 58 tree nodes
  device 1: 32,220 normalized events -> 10,510 semantic anchors -> 54 tree nodes
  selected raw table rows -> structural nodes: 1.18M+ -> 112 (~10,500:1)
  recovered shared structure:
    outer decode/warmup block: Repeat x36
      layer block: Repeat x24
        MatMul / Rope / HCCL AllReduce / AddRmsNorm / SwiGlu
```

This bundle is committed under [examples/kickstart_smoke](examples/kickstart_smoke).
It is a real two-device Ascend/CANN `msprof` capture collected on an Ascend
910B3 machine from a short vLLM-Ascend generation run. The workload loads
  `Qwen2.5-0.5B-Instruct` with tensor parallelism across two NPUs and generates
12 tokens for one prompt. The profile intentionally includes engine
initialization, HCCL setup, ACL graph capture/replay, prefill, and decode, so
the raw databases look like real inference profiler output rather than a tidy
synthetic kernel sequence.

For a fuller Ascend graph-mode showcase with complete Huawei `msprof` package
layout, TraceLoom full output, reconstructed ACLGraph replay intervals modeled
as graph atoms, and a ready-to-open Perfetto/Chrome timeline, use the data-plane
package documented at `data/experiment-results/ascend_tp2_graph_showcase/`.
The archive includes `derived/traceloom/aclgraph_summary.md` and
`derived/perfetto/trace_timeline.json.gz`.

TraceLoom compresses the two raw device traces into comparable Pattern
Compression Trees. The showcase result is that both devices expose the same
nested execution pattern: an outer `Repeat x36` region containing a per-layer
`Repeat x24` block with `MatMul`, `AtbRopeKernel`, `hcom_allReduce`,
`AddRmsNormBias`, and `SwiGlu` nodes. This is the intended first experience:
run TraceLoom on a checked-in profiler database and inspect how raw timelines
become a structured diagnosis.

## Example Findings

On the kickstart bundle, TraceLoom immediately reveals:

- The dominant execution structure is a repeated transformer-style region:
  `Repeat x36` accounts for about 89% of device 0 structural time and 91% of
  device 1 structural time.
- The nested `Repeat x24` layer block is the main body inside that region,
  accounting for about 75-77% of each device's structural time.
- HCCL AllReduce appears inside the layer block's critical path, next to
  `MatMul`, `AtbRopeKernel`, `AddRmsNormBias`, and `SwiGlu`.
- Communication cost is not hidden in a separate report: the repeated block
  carries explicit communication columns, with about 15% communication share on
  device 0's outer block and about 9% on device 1's outer block.
- The useful review surface is small: more than 1.18M selected profiler rows
  from `TASK`, `CANN_API`, and HCCL task tables become 112 structural nodes
  across the two devices.

Have a try after installation:

```bash
traceloom examples/kickstart_smoke/msprof_raw
```

The result is written back to:

```text
examples/kickstart_smoke/msprof_raw/traceloom/device0_loop_tree_v2.md
examples/kickstart_smoke/msprof_raw/traceloom/device1_loop_tree_v2.md
```

Open the generated Loop Tree reports and you should see repeated decode/layer
structure, communication nodes, and kernel-level cost columns. That is the
useful compression: hundreds of thousands of low-level rows become a small,
comparable execution structure.

## Key Capabilities

- Trace ingestion from Ascend/CANN `msprof` output directories.
- Top-k and cumulative-share hotspot views for noisy multi-device traces.
- Repeated execution pattern discovery over semantic kernel anchors.
- Anchor kernel and auxiliary/prelude kernel separation.
- Kernel timeline aggregation into Pattern Compression Trees, node-cost maps,
  and SQL views.
- HCCL, communication, and synchronization bottleneck detection through
  collective anchors and auxiliary cost attribution.
- Ascend ACLGraph reconstruction from device-side `CaptureStreamInfo`, `TASK`,
  and `ascend_task.db` evidence. Replays are emitted as normalized
  `ACLGRAPH_REPLAY` timeline events and graph rows with
  `graph_provider='aclgraph'`.
- Source/operator attribution through normalized kernel symbols, role
  classification, and optional kernel-role overrides.
- Human-readable report generation for top kernels, repeated patterns, and
  suspicious synchronization or communication regions.
- Database-backed analysis through augmented SQLite sidecars that preserve raw
  profiler tables and add TraceLoom-owned tables and views.

## Why This Matters

Distributed inference profiles contain huge numbers of low-level runtime
events, but developers usually need answers at a higher level:

- Where is time spent?
- Which execution patterns dominate runtime?
- Which kernels are anchors and which are auxiliary?
- Where do synchronization and communication stalls appear?
- Which source-level operators are likely responsible?

TraceLoom compresses raw execution noise into a structured diagnosis surface:
Pattern Compression Trees, node-cost tables, anchor-to-event links,
auxiliary/prelude evidence, and queryable augmented databases.

## Method Vocabulary

TraceLoom uses a small vocabulary for execution semantic reconstruction:

- Semantic Execution Skeleton: the main execution backbone formed by compute,
  collective, data movement, and synchronization anchors.
- Semantic Anchor: a semantic token for major compute, communication, or
  synchronization behavior; the basic symbol used by pattern mining.
- Anchor-Auxiliary Attribution Model: the cost model that attaches waiting,
  preparation, runtime calls, and communication fragments to nearby anchors or
  loop nodes.
- Pattern Compression Tree: TraceLoom's primary structural artifact. It is not
  a raw execution tree; it is a compressed tree that turns repeated anchor
  sequences into readable runtime structure.
- Tree Map: a human-readable node-cost map designed for first-pass diagnosis
  and SQL drill-down.

## Quick Start

Build and install the native analyzer from the repository root:

```bash
cmake --preset dev
cmake --build --preset dev -j "$(nproc)"
cmake --install build/native --prefix "$HOME/.local"
```

Analyze an existing Ascend/CANN profile DB:

```bash
traceloom /path/to/PROF_.../msprof_YYYYMMDDHHMMSS.db
```

or point TraceLoom at a Huawei profiler bundle directory:

```bash
traceloom /path/to/msprof_output
```

Most users only need this command. The native analyzer discovers
`PROF_*/msprof_*.db`, analyzes the trace, and writes Loop Tree reports back next
to the original profile.

TraceLoom writes reports under a neighboring `traceloom/` directory:

```text
/path/to/PROF_.../traceloom/loop_tree_v2.md
/path/to/msprof_output/traceloom/device0_loop_tree_v2.md
```

Use `--threads` when you want to control native parallelism:

```bash
traceloom /path/to/msprof_output --threads 48
```

The older Python command is retained for legacy bundle generation, SQL
drill-down surfaces, and non-native backend work:

```bash
python3 -m pip install -e .
traceloom analyze /path/to/msprof_output
```

If editable install fails on an older system Python with a `build_editable`
message, upgrade the user-local build tools and retry:

```bash
python3 -m pip install --user --upgrade pip setuptools wheel
python3 -m pip install --user -e .
```

For large shared experiment repositories, especially when raw `msprof` output
is tracked by Git LFS, restore the raw artifact first and run native TraceLoom
on the restored profiler directory:

```bash
git lfs pull
git lfs fsck --objects
traceloom experiments/profiler/exp_001/profiler/msprof
```

The Python compatibility route still supports older options such as
`--out-dir`, `--top-devices-global`, `--devices`, `--kernel-role-file`, and
`--output-mode full`.

Tag candidate cross-device collectives after a Python compatibility analysis
run:

```bash
traceloom collective-tag /path/to/msprof_output/traceloom
```

This writes `global_collectives.db`, `collective_summary.md`, and a
`traceloom_collective_global_link` table in each augmented sidecar DB. The
candidate key is based on matched repeat-loop position, occurrence index,
normalized collective type, and order inside the occurrence.

For local development without installation:

```bash
PYTHONPATH="$PWD" python3 -m traceloom analyze <run_dir-or-raw-msprof-dir>
```

## Example Workflow

```text
Ascend msprof output
  -> parser / DB discovery
  -> normalized event model
  -> semantic anchor timeline
  -> repeated pattern miner
  -> hotspot and auxiliary attribution
  -> augmented SQLite DB + readable reports
```

See [docs/workflow.md](docs/workflow.md) for a fuller walkthrough.

## Architecture

TraceLoom is organized as a layered offline analysis pipeline:

- Parser layer: discovers Ascend/CANN `msprof` databases and validates the
  input layout.
- Event model: normalizes profiler rows into events, anchors, symbols, and
  source links.
- Pattern mining layer: compresses anchor streams into repeated execution
  structure.
- Attribution layer: separates anchor kernels from auxiliary/prelude work and
  attaches cost to loop nodes.
- Report layer: writes native Loop Tree Markdown reports, with compatibility
  bundle and SQL report surfaces still available for legacy workflows.
- CLI layer: exposes the native `traceloom <input>` command. Legacy Python
  commands such as `traceloom analyze`, `traceloom report`, and
  `traceloom collective-tag` remain for compatibility workflows.

See [docs/architecture.md](docs/architecture.md).

## Outputs

The native default output is intentionally small:

- `loop_tree_v2.md`: single-DB report written next to the input DB under
  `PROF_.../traceloom/`.
- `deviceN_loop_tree_v2.md`: one report per discovered DB/device when the input
  is a Huawei profiler bundle directory.

The Python compatibility bundle is retained for legacy SQL drill-down and
non-native backend work. It can include:

- `dbNN.traceloom_augmented.db`: one augmented SQLite sidecar per discovered
  `msprof` DB. The original profiler tables remain intact; TraceLoom adds
  `traceloom_*` tables and views.
- `README.md`: generated instructions for inspecting the bundle.
- `summary.md`: selected devices and top loop costs.
- `tree-map.md`: readable node-cost map. Copy a node id such as `N027` into
  SQL queries for drill-down.
- `aclgraph_summary.md`: Ascend ACLGraph reconstruction summary when the input
  profile contains graph capture stream metadata and graph semantic TASK rows.
- `queries/*.sql`: starter report queries.
- `meta.json`: analyzer parameters and generated paths.

Running `traceloom collective-tag` on the bundle adds:

- `global_collectives.db`: run-level candidate collective summary.
- `collective_summary.md`: readable completeness and skew report.
- `traceloom_collective_global_link`: local anchor-to-candidate mapping table
  in each augmented DB.

Run SQL reports directly from an augmented DB:

```bash
traceloom report /path/to/msprof_output/traceloom/db01.traceloom_augmented.db \
  --sql /path/to/msprof_output/traceloom/queries/repeat-overview.sql \
  --format md \
  -o /tmp/repeat-overview.md
```

Useful query surfaces include:

- `traceloom_v_tree_node`: readable tree node map with labels, depth,
  occurrence counts, anchor/operator counts, and cost columns.
- `traceloom_tree_node_occurrence`: expanded occurrences of each tree node.
- `traceloom_tree_node_anchor`: links from node occurrences back to anchors and
  source events.
- `traceloom_v_cuda_graph_replay`: graph replay evidence. CUDA and ACLGraph
  rows share this view; use `graph_provider` to distinguish them.

See [docs/output-schema.md](docs/output-schema.md) and
[docs/augmented-db-schema.md](docs/augmented-db-schema.md).

## Limitations

- The current production parser targets Ascend/CANN `msprof` SQLite output.
  CUDA/Nsight support is a planned adapter, not the current default parser.
- Source/operator attribution depends on profiler metadata, kernel names, and
  role classification rules. It should be treated as diagnostic evidence, not a
  guaranteed source-line proof.
- Communication and synchronization analysis is conservative. Some wait or gap
  time can remain in idle/prelude buckets when profiler tables do not expose
  enough device-side context.
- The output schema is still alpha. Public releases should version the schema
  before downstream tools depend on every column name.
- The repository includes one compact real vLLM-Ascend kickstart profile. Larger
  production traces should still live in external artifact storage with
  manifests or checksums in source control.

## Roadmap

- More robust trace format support across CANN versions.
- CUDA/Nsight input adapter.
- Better pattern similarity matching for near-identical decode iterations.
- Before/after pull request comparison reports.
- CI regression tracking for profiler-derived metrics.
- Interactive visualization over augmented timeline data.
- Multi-device communication timeline analysis.
- Stronger source/operator attribution with framework-level metadata.

## Project Value

Instead of manually inspecting raw timelines, TraceLoom helps developers move
from dense profiler output to a repeatable diagnosis:

```text
raw timeline -> meaningful execution patterns -> attributed hotspots -> report
```

That makes performance work easier to review, reproduce, and discuss across
runtime, model, and accelerator teams.

## License

TraceLoom is released under the MIT License. See [LICENSE](LICENSE).
