# TraceLoom

[中文说明](README.zh.md)

![Status](https://img.shields.io/badge/status-alpha-orange)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](native/)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

TraceLoom is a native C++17 offline analyzer for accelerator profiler traces.
It turns dense Ascend/CANN `msprof` SQLite timelines into compact, comparable
Pattern Compression Trees with compute, communication, idle, auxiliary, and
self-cost statistics.

The native implementation is now the only production implementation in this
repository. The installed CLI has one stable name:

```bash
traceloom <profile.db-or-profile-dir>
```

## What TraceLoom Does

```text
profiler SQLite
  -> event normalization and source provenance
  -> semantic anchor extraction
  -> repeated-pattern discovery
  -> overlap-safe timeline cost attribution
  -> Loop Tree report and optional SQLite/JSON evidence
```

Key capabilities:

- Ascend/CANN monolithic and split-SQLite profile discovery, with automatic
  fallback when a usable monolithic `TASK` table is unavailable;
- CUDA/Nsight kernel, runtime, data-movement, synchronization, event, and graph
  replay ingestion through the native adapter;
- native semantic reconstruction for compute, HCCL, synchronization, and
  ACLGraph activity;
- repeated decode/layer structure discovery over semantic anchors;
- overlap-safe wall-clock accounting across concurrent streams;
- repeat-node averages normalized per loop-body iteration;
- provenance links back to the original database, table, and row.

CUDA auxiliary activity stays explicitly typed and traceable without being
misreported as compute. CUDA graph traces are represented as replay intervals
and share the native Loop Tree report surface with eager CUDA and Ascend input.

## Install On Debian Or Ubuntu

Build the `traceloom-native` Debian package from the repository root:

```bash
cmake -S native -B build/traceloom-native-package \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACELOOM_NATIVE_BUILD_TESTS=OFF
cmake --build build/traceloom-native-package -j "$(nproc)"
cpack --config build/traceloom-native-package/CPackConfig.cmake \
  -B build/traceloom-native-package
sudo apt install ./build/traceloom-native-package/traceloom-native_*.deb
```

The package installs only `/usr/bin/traceloom`. Verify it with:

```bash
traceloom --version
traceloom --help
```

Runtime package dependencies are `libc6`, `libstdc++6`, and `libsqlite3-0`.

To uninstall:

```bash
sudo apt remove traceloom-native
```

## Install From Source

```bash
cmake --preset dev
cmake --build --preset dev -j "$(nproc)"
cmake --install build/native --prefix "$HOME/.local"
```

Make sure `$HOME/.local/bin` is in `PATH`, then run `traceloom --version`.

## Quick Tutorial

### 1. Analyze One Database

```bash
traceloom /path/to/PROF_.../msprof_YYYYMMDDHHMMSS.db
```

The default report is written beside the database:

```text
/path/to/PROF_.../traceloom/loop_tree_v2.md
```

### 2. Analyze A Profiler Directory

```bash
traceloom /path/to/msprof_output
```

TraceLoom discovers monolithic `PROF_*/msprof_*.db` files,
`torch_npu.profiler`
`*/ASCEND_PROFILER_OUTPUT/ascend_pytorch_profiler[_<rank>].db` files, and split
`PROF_*/{host,device_*}/sqlite/*.db` layouts, then writes one report per
device/database:

```text
/path/to/msprof_output/traceloom/device0_loop_tree_v2.md
/path/to/msprof_output/traceloom/device1_loop_tree_v2.md
```

Use an explicit worker count for large traces:

```bash
traceloom /path/to/msprof_output --threads 48
```

Within each `PROF_*`, a nonempty monolithic `TASK` table takes priority.
Otherwise TraceLoom emits a warning and builds the base timeline from split
`AscendTask`, `TaskInfo`, `HostTask`, and `ApiData` tables. When present,
`HCCLOP`/`HCCLOpSingleDevice` and `HCCLTaskSingleDevice` recover the same
collective-operation anchors as the monolithic path while retaining the
device-task decomposition as auxiliary evidence. Graph replay uses the same
exact reconstruction contract in both layouts; PMU attribution remains
incremental.

When one profiler SQLite contains multiple devices, TraceLoom keeps one
device-local execution sequence per device instead of inventing a global
order. The report overlays conservative collective correspondence candidates
from exact graph-body positions or recovered-loop positions. It never labels
those relations as tensor parallelism, a model layer, or another workload
phase; that interpretation belongs to downstream agents and humans.

### 3. Read The Loop Tree

Start at the outer `Repeat xN` nodes, then compare their children. The most
useful columns are:

- `total_us`: disjoint wall-clock union; overlapping streams are not counted
  twice;
- `avg_total_us`: per occurrence for ordinary nodes and per loop-body
  iteration for Repeat nodes;
- `avg_compute_us`, `avg_comm_us`, `avg_idle_us`: comparable average cost
  categories;
- `avg_aux_us` and `avg_self_us`: attributed auxiliary and node-owned cost.

### 4. Request Advanced Evidence

The normal workflow only writes the Loop Tree. Use advanced flags when a
single database needs additional evidence:

```bash
traceloom /path/to/msprof.db \
  --loop-tree-out /tmp/loop_tree_v2.md \
  --compat-db-out /tmp/traceloom-sidecar.db \
  --out /tmp/native_result.json
```

Run `traceloom --help-advanced` for grammar diagnostics and auxiliary
materialization options.

Ascend Loop Trees include an observation-backed device summary of visible
productive gaps. It preserves unattributed residual and states its collection
status explicitly; it must not be read as hardware-idle or causal evidence.

## Typical Use: Read Work Between Graph Replays

Do not assume that time between two graph units is idle or overhead. A serving,
training, or pipeline trace may interleave protected graph replays with large
productive sequences that are not graph replays. TraceLoom keeps those tasks in
the global structure and compresses their internal repetition. The report's
`Structural Composition` table is the highest-level ordered map; expand its
Loop Tree handles when the graph-reconstruction count alone does not explain
the full execution timeline.

A useful first-pass review starts from the emitted wide table:

| order | unit | kind | run | family | fingerprint | anchors | shape | total_us | evidence | expansion |
| ---: | --- | --- | ---: | --- | --- | ---: | --- | ---: | --- | --- |
| 0 | `X1` | `unrecognized` | 1 | `XF1` | `H...` | observed | `unavailable` | measured | `unrecognized_open_prefix` | `node-N002#1,...` |
| 1 | `G1` | `graph_unit` | 1 | `GF1` | `H...` | 1 | `unavailable` | measured | `exact` | `node-N1360#1` |
| 2 | `U1` | `structural_unit` | 1 | `UF1` | `H...` | 1186 | `unavailable` | measured | `complete` | `node-N1362#1,...` |

`anchors` counts the projected productive anchors owned by the unit. An exact
graph unit may therefore be one protected anchor whose graph-body tasks remain
available through the graph evidence tables and Loop Tree drill-down.

This is an observation format, not a workload-semantic classifier. TraceLoom
may report profiler-native graph identity, concrete operators, raw shapes,
cardinality, timing, repetition, and provenance. It does not decide that `G1`
is a decode phase, that `U1` is a prefill phase, or that either node caused an
end-to-end change.

Use the Loop Tree and evidence database together: expand `U1`, verify its
`Repeat x47` body and source rows, then combine those observations with external
workload metadata. A human or agent can supply and test the interpretation while
keeping it visibly separate from TraceLoom's recovered structure. See
[`notes/interleaved-structural-units-milestone.md`](notes/interleaved-structural-units-milestone.md)
for the motivating case and current implementation status.

## Checked-In Kickstart Profile

The repository includes a real two-device vLLM-Ascend profile under
[`examples/kickstart_smoke`](examples/kickstart_smoke). Try it with:

```bash
traceloom examples/kickstart_smoke/msprof_raw
```

The sanitized pair contains 78,585,856 bytes of profiler data. Current
TraceLoom maps 145,927 normalized events to 44,733 semantic anchors and folds
them into 990 rendered tree nodes. Both devices recover the same nested
`Repeat x29 -> Repeat x74` and `Repeat x29 -> Repeat x24` structures. Verify
the current contract without committing generated reports:

```bash
examples/paper_artifacts/tools/verify_kickstart_folding.py \
  --traceloom build/native-tests/native/traceloom
```

## Exact Paper Artifact

For a fast, reviewable exact-reconstruction example, the repository also
includes a 3.55 MiB pair of reduced Ascend profiler databases under
[`examples/paper_artifacts/ascend_interleaved`](examples/paper_artifacts/ascend_interleaved).
It preserves four exact graph units plus the complete large/small productive
sequences interleaved between them, with source-row provenance and a
full-profile equivalence manifest. The verifier analyzes both inputs, checks
the structural audit and privacy boundary, and leaves reports outside Git:

```bash
examples/paper_artifacts/tools/verify_ascend_interleaved.py \
  --traceloom build/native-tests/native/traceloom
```

## Development

```bash
cmake --preset dev-tests
cmake --build --preset dev-tests -j "$(nproc)"
ctest --preset dev-tests
```

Some deep regression tests use external research fixtures. Standalone clones
skip those tests when the fixture directory is absent; normal native unit tests
still run.

## Documentation

- [Native analyzer and packaging guide](native/README.md)
- [Step-by-step workflow](docs/workflow.md)
- [Accepted input layouts](docs/input-profiles.md)
- [Output contract](docs/output-schema.md)
- [Native architecture](docs/architecture.md)
- [Loop Tree reading guide](docs/tree-map-guide.zh.md)
- [Publication-readiness roadmap](notes/publication-readiness-roadmap.md)
- [CUDA real-model graph handoff](notes/cuda-real-model-graph-handoff.md)

## License

TraceLoom is released under the [MIT License](LICENSE).
