# Tutorial: From Package To Loop Tree

TraceLoom is a native offline analyzer:

```text
msprof SQLite output
  -> source discovery and normalization
  -> semantic anchor timeline
  -> repeated-pattern grammar
  -> overlap-safe cost attribution
  -> Loop Tree report and optional SQLite/JSON evidence
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

Use `--threads N` to control parallelism. TraceLoom writes reports under a
neighboring `traceloom/` directory by default:

```text
PROF_.../traceloom/loop_tree_v2.md
msprof_output/traceloom/device0_loop_tree_v2.md
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

## 4. Read The Loop Tree

Start with an outer `Repeat xN`, then compare its children. Read the cost
columns using these rules:

- `total_us` is a disjoint wall-clock union, so overlapping streams are not
  counted twice;
- ordinary-node averages divide by occurrence count;
- Repeat-node averages divide by occurrence count and body repeat count;
- compute, communication, idle, auxiliary, and self columns use the same
  denominator as the node's average total.

This normalization makes a loop node directly comparable with its loop body.

## 5. Drill Down When Needed

The normal report is intentionally compact. For portable, queryable evidence
from one regular profiler database, request native JSON and a self-contained
augmented database:

```bash
traceloom /path/to/msprof.db \
  --loop-tree-out /tmp/loop_tree_v2.md \
  --aug-db-out /tmp/traceloom-analysis.db \
  --out /tmp/native_result.json
```

Inspect the augmented database with `sqlite3` or another SQLite client. It
contains the copied raw profiler tables as well as the TraceLoom hierarchy and
cost relations, so source references remain queryable after the input is moved.
Run
`traceloom --help-advanced` for grammar and auxiliary-attribution options.

## 6. Share A Reproducible Diagnosis

Share the TraceLoom version and command, the Loop Tree report, selected SQL
results, and profile metadata/checksums. Avoid committing large raw profiler
databases or generated reports to this repository.

## 7. Uninstall

```bash
sudo apt remove traceloom-native
```

After removal, `/usr/bin/traceloom` should no longer exist.
