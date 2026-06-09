# TraceLoom

[中文说明](README.zh.md)

![Status](https://img.shields.io/badge/status-alpha-orange)
![Python](https://img.shields.io/badge/python-3.10%2B-blue)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Execution semantics reconstruction for Ascend `msprof` traces.

**From raw timelines to actionable performance diagnosis.**

TraceLoom reconstructs execution semantics from low-level Ascend profiling
traces and turns profiler databases into queryable performance diagnosis
artifacts. It helps developers see the real hardware behavior behind
distributed inference runs: dominant kernels, recurring execution patterns,
synchronization stalls, communication bottlenecks, and likely source/operator
causes.

The common workflow is intentionally simple: profile your workload with
`msprof`, give the profile directory to TraceLoom, then read the generated
reports in the same profile directory.

## At A Glance

```text
Ascend msprof database
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
Loop Tree Construction
    |
    v
Anchor-Auxiliary Cost Attribution
    |
    v
Tree Map / SQL Report / Augmented Profile DB
```

TraceLoom compresses dense low-level profiler databases into a small set of
questions developers can act on:

- What is the semantic execution skeleton?
- Which loops or fragments repeat?
- Which kernels, collectives, and synchronization regions dominate cost?
- Which auxiliary or prelude events explain hidden cost around anchors?
- Which nodes should be inspected against raw profiler events with SQL?

## Real Kickstart Bundle

```text
examples/kickstart_smoke/msprof_raw/
  PROF_...device_0.../msprof_20260609062629.db
  PROF_...device_1.../msprof_20260609062627.db

Raw CANN profile databases
  per device: 82 TASK rows
  per device: 8 COMMUNICATION_TASK_INFO rows
  per device: 8 COMMUNICATION_OP rows

TraceLoom output
  per device: 36 normalized events
  per device: 16 semantic anchors
  recovered structure: Repeat x8
    aclnnMatmul_MatMulCommon_MatMulV2
    hcom_allReduce__#_#_#
```

This bundle is committed under [examples/kickstart_smoke](examples/kickstart_smoke).
It is a real two-device Ascend/CANN `msprof` capture collected on an Ascend
910B3 machine from a tiny torch-npu distributed workload. The workload repeats
`matmul -> gelu -> all_reduce`, so the raw databases contain both compute and
HCCL communication behavior.

TraceLoom compresses the two raw device traces into the same comparable
`MatMulV2 -> AllReduce` loop structure. This is the intended first experience:
run TraceLoom on a checked-in profiler database and inspect the result before
profiling your own workload.

Have a try after installation:

```bash
traceloom analyze examples/kickstart_smoke/msprof_raw --devices 0,1
```

The result is written back to:

```text
examples/kickstart_smoke/msprof_raw/traceloom/
```

Open `tree-map.md` there and you should see both devices reconstructed as a
`Repeat x8` execution pattern with `MatMulV2` and `hcom_allReduce` nodes.

## Key Capabilities

- Trace ingestion from Ascend/CANN `msprof` output directories.
- Top-k and cumulative-share hotspot views for noisy multi-device traces.
- Repeated execution pattern discovery over semantic kernel anchors.
- Anchor kernel and auxiliary/prelude kernel separation.
- Kernel timeline aggregation into loop trees, node-cost maps, and SQL views.
- HCCL, communication, and synchronization bottleneck detection through
  collective anchors and auxiliary cost attribution.
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
repeat trees, node-cost tables, anchor-to-event links, auxiliary/prelude
evidence, and queryable augmented databases.

## Method Vocabulary

TraceLoom uses a small vocabulary for execution semantic reconstruction:

- Semantic Execution Skeleton: the main execution backbone formed by compute,
  collective, data movement, and synchronization anchors.
- Semantic Anchor: a semantic token for major compute, communication, or
  synchronization behavior; the basic symbol used by pattern mining.
- Anchor-Auxiliary Attribution Model: the cost model that attaches waiting,
  preparation, runtime calls, and communication fragments to nearby anchors or
  loop nodes.
- Pattern Compression Tree: a loop tree / repeat tree that compresses repeated
  anchor sequences into readable structure.
- Tree Map: a human-readable node-cost map designed for first-pass diagnosis
  and SQL drill-down.

## Quick Start

Install from the repository root:

```bash
python3 -m pip install -e .
```

Analyze an existing Ascend/CANN profile directory:

```bash
traceloom analyze /path/to/msprof_output
```

Most users only need this command. Give TraceLoom the profiler output directory;
it will discover `PROF_*/msprof_*.db`, analyze the trace, and write the result
bundle back next to the original profile.

TraceLoom writes the analysis bundle back into the original profile directory:

```text
/path/to/msprof_output/traceloom/
```

TraceLoom accepts either a raw `msprof` directory or a run directory containing
`msprof_raw`:

```text
<run_dir>/msprof_raw/PROF_*/msprof_*.db
<raw_dir>/PROF_*/msprof_*.db
```

Use `--out-dir <dir>` only when you want to place the results somewhere else:

```bash
traceloom analyze /path/to/msprof_output --out-dir out/qwen2-decode-analysis
```

Common options for larger traces:

- `--top-devices-global 4`: analyze only the highest-ranked devices.
- `--devices 3,4,5,6`: pin analysis to physical device IDs.
- `--kernel-role-file roles.csv`: provide role overrides for local kernels.
- `--output-mode full`: also write legacy CSV/JSON debug exports.

For local development without installation:

```bash
scripts/traceloom-analyze.sh <run_dir-or-raw-msprof-dir>
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
- Report layer: writes augmented SQLite databases, Markdown summaries, SQL
  report queries, and optional full debug exports.
- CLI layer: exposes `traceloom analyze`, `traceloom report`, and optional
  profiling runner commands.

See [docs/architecture.md](docs/architecture.md).

## Outputs

The default bundle is intentionally small:

- `dbNN.traceloom_augmented.db`: one augmented SQLite sidecar per discovered
  `msprof` DB. The original profiler tables remain intact; TraceLoom adds
  `traceloom_*` tables and views.
- `README.md`: generated instructions for inspecting the bundle.
- `summary.md`: selected devices and top loop costs.
- `tree-map.md`: readable node-cost map. Copy a node id such as `N027` into
  SQL queries for drill-down.
- `queries/*.sql`: starter report queries.
- `meta.json`: analyzer parameters and generated paths.

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

See [docs/output-schema.md](docs/output-schema.md) and
[docs/augmented-db-schema.md](docs/augmented-db-schema.md).

## Optional Profiling Runner

TraceLoom can create a profile config and invoke Ascend/CANN `msprof`, but this
is a convenience path. The analyzer core still consumes profiler output after
the workload has run.

```bash
traceloom create config -o traceloom.profile.ini
# edit workload, profiler, Docker, and output paths
traceloom run traceloom.profile.ini
traceloom analyze runs/local-msprof/msprof_raw
```

`traceloom run` invokes Ascend/CANN `msprof` as:

```text
msprof --output=<profile_dir> --application=<workload.command> <profiler.extra_args>
```

If `[docker] enabled = true`, the profiler command is run through `docker exec`
when `container` is set, or through `docker run --rm` when `image` is set.

## Paper Reproduction Model

The repository should not commit large profile databases. Reviewers reproduce
paper results by running the documented reference workload and profiler command
on their own multi-card Ascend/CANN machine, then running TraceLoom on the
resulting profile directory.

Generated reproduction outputs are written under ignored `out/reproduce/` by
default:

```bash
python3 reproduce/run_reference.py decode-a2a-buffer-reuse
python3 reproduce/run_reference.py analyze-msprof /path/to/msprof_or_run_dir --name reviewer_msprof
bash run_decode_a2a_buffer_reuse.sh --env-file reproduce/decode_a2a_buffer_reuse/local.env
bash reproduce/decode_a2a_buffer_reuse/run_ab_benchmark.sh --env-file reproduce/decode_a2a_buffer_reuse/local.env
bash reproduce/decode_a2a_buffer_reuse/run_profile_pair.sh --env-file reproduce/decode_a2a_buffer_reuse/local.env
```

After installation, the same entry point is available as `traceloom-reproduce`.
See [docs/reference-reproduction.md](docs/reference-reproduction.md).

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
- Large raw profile databases are intentionally excluded from the repository.
  Use external artifact storage for real traces and keep only manifests,
  checksums, or small synthetic fixtures in source control.

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
