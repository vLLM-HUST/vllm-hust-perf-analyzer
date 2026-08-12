# TraceLoom Agent Guide

TraceLoom is a native C++17 offline profiler analyzer. Treat it as a
post-processing tool, not as a runtime environment manager.

## Primary Goal

TraceLoom consumes accelerator profiler SQLite artifacts and emits a
self-contained queryable database timeline. The production CLI is:

```bash
traceloom <profile.db-or-profile-dir>
```

Keep this entry point stable. Do not add alternate installed command names.

## Build And Test

```bash
cmake --preset dev-tests
cmake --build --preset dev-tests -j <chosen-jobs>
ctest --preset dev-tests
```

Test a release build without the external fixture suite:

```bash
cmake -S native -B build/release \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACELOOM_NATIVE_BUILD_TESTS=OFF
cmake --build build/release -j <chosen-jobs>
```

Choose build parallelism from CPU affinity, cgroup quota, current load, and
memory. On an otherwise idle host, use roughly half the affinity-available
CPUs; do not let `OMP_NUM_THREADS` or plain container `nproc` silently reduce a
large build to one job.

## Analyze Existing Data

```bash
build/native-tests/native/traceloom /path/to/msprof_raw
```

Valid Ascend inputs include one `msprof_*.db` or a directory containing
monolithic `PROF_*/msprof_*.db` or split
`PROF_*/{host,device_*}/sqlite/*.db` layouts. The default queryable database timeline is a neighboring
`traceloom/analysis.db` or one `analysis_dbNN.db` per discovered analysis
input. Markdown is an explicit human projection requested with
`--loop-tree-out`.

## Repository Hygiene

Do not commit raw profiler databases, generated reports, local environments,
model weights, or benchmark logs. Keep those in ignored build/output paths or
external artifact storage.

Before handing changes back:

```bash
git status --short
ctest --preset dev-tests
```

Prefer nearby native APIs and structured SQLite access over ad hoc parsing.
Keep edits scoped and preserve source-table/row provenance in new adapters.
