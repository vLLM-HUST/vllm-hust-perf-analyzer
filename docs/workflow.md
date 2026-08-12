# Tutorial: From Package To Queryable Cost Map

TraceLoom is a native offline analyzer:

```text
msprof SQLite output
  -> source discovery and normalization
  -> semantic anchor timeline
  -> repeated-pattern grammar
  -> overlap-safe cost attribution
  -> self-contained augmented SQLite analysis
```

## 1. Build And Install

On Debian or Ubuntu, build the package from the repository root:

```bash
cmake -S native -B build/traceloom-native-package \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACELOOM_NATIVE_BUILD_TESTS=OFF
cmake --build build/traceloom-native-package -j "$(nproc)"
cpack --config build/traceloom-native-package/CPackConfig.cmake \
  -B build/traceloom-native-package
sudo apt install ./build/traceloom-native-package/traceloom-native_*.deb
```

The package installs one public command:

```bash
traceloom --version
traceloom --help
```

## 2. Collect A Profile

Run the workload with Ascend/CANN `msprof`. TraceLoom does not launch the
workload or require a runtime wrapper. Supported production layouts include:

```text
<run_dir>/msprof_raw/PROF_*/msprof_*.db
<raw_dir>/PROF_*/msprof_*.db
<raw_dir>/PROF_*/host/sqlite/*.db
<raw_dir>/PROF_*/device_*/sqlite/*.db
```

## 3. Run TraceLoom

Analyze a profile directory:

```bash
traceloom /path/to/msprof_output
```

Or analyze one database:

```bash
traceloom /path/to/PROF_.../msprof_YYYYMMDDHHMMSS.db
```

Use `--threads N` to control parallelism. TraceLoom writes self-contained
analysis databases under a neighboring `traceloom/` directory by default:

```text
PROF_.../traceloom/analysis.db
msprof_output/traceloom/analysis_db01.db
```

TraceLoom prefers a nonempty monolithic `TASK` table. If none is usable, it
prints a split-fallback warning and normalizes the base timeline from
`AscendTask`, `TaskInfo`, `HostTask`, and `ApiData`. Available HCCL operation
and task tables recover collective anchors while retaining device-task detail
as auxiliary evidence.

For a self-contained smoke test, run:

```bash
traceloom examples/kickstart_smoke/msprof_raw
```

## 4. Query The Hierarchical Cost Map

Discover the supported analysis entry points, then start with the tree map:

```bash
sqlite3 analysis.db 'SELECT * FROM traceloom_analysis_surface;'
sqlite3 -header -column analysis.db < docs/report-sql/tree-map.sql
```

Start with an outer `Repeat xN`, then compare and drill into its children.
Read the cost columns using these rules:

- `total_us` is a disjoint wall-clock union, so overlapping streams are not
  counted twice;
- ordinary-node averages divide by occurrence count;
- Repeat-node averages divide by occurrence count and body repeat count;
- compute, communication, idle, auxiliary, and self columns use the same
  denominator as the node's average total.

This normalization makes a loop node directly comparable with its loop body.

## 5. Drill Down When Needed

The database is already portable and queryable. Request Markdown or native
JSON only as an explicit human/debug projection:

```bash
traceloom /path/to/msprof.db \
  --loop-tree-out /tmp/loop_tree_v2.md \
  --output /tmp/traceloom-analysis.db \
  --out /tmp/native_result.json
```

It contains embedded raw profiler tables as well as the TraceLoom hierarchy and
cost relations, so source references remain queryable after inputs are moved.
Run
`traceloom --help-advanced` for grammar and auxiliary-attribution options.

## 6. Share A Reproducible Diagnosis

Share the TraceLoom version and command, the analysis database, selected SQL
results, and profile metadata/checksums. Avoid committing large raw profiler
databases or generated reports to this repository.

## 7. Uninstall

```bash
sudo apt remove traceloom-native
```

After removal, `/usr/bin/traceloom` should no longer exist.
