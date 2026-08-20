# Tutorial: From Profile To Queryable Database Timeline

TraceLoom is a native offline analyzer:

```text
msprof SQLite output
  -> source discovery and normalization
  -> semantic anchor timeline
  -> repeated-pattern grammar
  -> overlap-safe cost attribution
  -> queryable database timeline
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

Run the workload with an Ascend/CANN profiler such as `msprof` or the official
torch-npu profiler workflow. TraceLoom does not launch the workload or require
a runtime wrapper. Supported production layouts include:

```text
<run_dir>/msprof_raw/PROF_*/msprof_*.db
<raw_dir>/PROF_*/msprof_*.db
<profile_root>/ASCEND_PROFILER_OUTPUT/ascend_pytorch_profiler_*.db
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

Use `--threads N` to control parallelism. TraceLoom writes self-contained queryable database timelines under a neighboring `traceloom/` directory by default:

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

## 4. Select A Scope And Compose A Projection

Open the database timeline through its self-describing catalogs:

```bash
sqlite3 analysis.db 'SELECT * FROM traceloom_projection_recipe ORDER BY display_order;'
sqlite3 analysis.db 'SELECT * FROM traceloom_projection_parameter ORDER BY projection_name, parameter_order;'
sqlite3 analysis.db 'SELECT * FROM traceloom_analysis_surface;'
sqlite3 -header -column analysis.db < docs/report-sql/tree-map.sql
```

Start with an outer `Repeat xN` and retain its `node_id`. The recipes let the
same selected scope switch between one realized occurrence and all
occurrences, remain folded or expand to children/events, enter supported host
windows, and change measure lens without rebuilding its boundary. See
[`composable-analytical-projections.md`](composable-analytical-projections.md)
and the executable [`database-timeline tour`](../examples/db-timeline-tour).

Read the cost columns using these rules:

- `total_us` is a disjoint wall-clock union, so overlapping streams are not
  counted twice;
- ordinary-node averages divide by occurrence count;
- Repeat-node averages divide by occurrence count and body repeat count;
- compute, communication, idle, auxiliary, and self columns use the same
  denominator as the node's average total.

This normalization makes a loop node directly comparable with its loop body.

## 5. Drill Down When Needed

The database is already portable and queryable. Request Markdown only as an
explicit human projection:

```bash
traceloom /path/to/msprof.db \
  --loop-tree-out /tmp/loop_tree_v2.md \
  --output /tmp/traceloom-analysis.db
```

`--loop-tree-out` defaults to the bounded `compact` human view. It aggregates
repeated leaf operators, exposes the live grammar and macro definitions, and
keeps source token/anchor ranges for drill-down. Use `--loop-tree-view
expanded` for every positional tree row or `--loop-tree-view both` to append
that exact tree after the compact summary. The queryable database always keeps
the expanded `native_report_tree` relations regardless of the Markdown choice.

It contains embedded raw profiler tables as well as the TraceLoom hierarchy and
cost relations, so source references remain queryable after inputs are moved.
Run
`traceloom --help-advanced` for grammar and auxiliary-attribution options.
Peripheral debug exporters consume this database rather than linking to an
in-memory TraceLoom result.

## 6. Share A Reproducible Diagnosis

Share the TraceLoom version and command, the analysis database, selected SQL
results, and profile metadata/checksums. Avoid committing large raw profiler
databases or generated reports to this repository.

## 7. Uninstall

```bash
sudo apt remove traceloom-native
```

After removal, `/usr/bin/traceloom` should no longer exist.
