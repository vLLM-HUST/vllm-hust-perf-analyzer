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
the global structure and compresses their internal repetition. In the current
report, inspect the root sequence for `graph_unit` nodes separated by nonempty
`Seq`/`Repeat` structures; the graph-reconstruction count alone is not the full
execution timeline.

A useful first-pass review transcribes that highest-level composition into a
wide table. The neutral `structural_unit` wrapper below is the planned
reader-facing promotion; current reports may show the same body expanded as a
sequence containing `Repeat x47`:

| order | node | kind | run | structural fingerprint | task count | shape signature | total_us | evidence |
| ---: | --- | --- | ---: | --- | ---: | --- | ---: | --- |
| 0 | `G1` | `graph_unit` | 1 | `graph:T1/body:B1` | 1024 | `S-graph-1` | measured | `exact` |
| 1 | `U7` | `structural_unit` | 1 | `H7 (contains Repeat x47)` | 2106 | `S471` | measured | `complete` |
| 2 | `G1` | `graph_unit` | 3 | `graph:T1/body:B1` | 1024 each | `S-graph-1` | measured | `exact` |
| 3 | `U8` | `structural_unit` | 1 | `H8 (contains Repeat x47)` | 2105 | `S472` | measured | `complete` |

This is an observation format, not a workload-semantic classifier. TraceLoom
may report profiler-native graph identity, concrete operators, raw shapes,
cardinality, timing, repetition, and provenance. It does not decide that `G1`
is a decode phase, that `U7` is a prefill phase, or that either node caused an
end-to-end change.

Use the Loop Tree and evidence database together: expand `U7`, verify its
`Repeat x47` body and source rows, then combine those observations with external
workload metadata. A human or agent can supply and test the interpretation while
keeping it visibly separate from TraceLoom's recovered structure. See
[`notes/interleaved-structural-units-milestone.md`](notes/interleaved-structural-units-milestone.md)
for the motivating case and implementation TODO.

## Checked-In Kickstart Profile

The repository includes a real two-device vLLM-Ascend profile under
[`examples/kickstart_smoke`](examples/kickstart_smoke). Try it with:

```bash
traceloom examples/kickstart_smoke/msprof_raw
```

The capture contains more than 1.18 million selected profiler rows. TraceLoom
compresses them into 112 structural nodes and recovers the same nested
`Repeat x36 -> Repeat x24` transformer pattern on both devices.

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
