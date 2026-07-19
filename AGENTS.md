# TraceLoom Agent Guide

TraceLoom is a native C++17 offline profiler analyzer. Treat it as a
post-processing tool, not as a runtime environment manager.

## Primary Goal

TraceLoom consumes accelerator profiler SQLite artifacts and emits evidence
tables and readable Loop Tree reports. The production CLI is:

```bash
traceloom <profile.db-or-profile-dir>
```

Keep this entry point stable. Do not add alternate installed command names.

## Build And Test

```bash
cmake --preset dev-tests
cmake --build --preset dev-tests -j "$(nproc)"
ctest --preset dev-tests
```

Test a release build without the external fixture suite:

```bash
cmake -S native -B build/release \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACELOOM_NATIVE_BUILD_TESTS=OFF
cmake --build build/release -j "$(nproc)"
```

## Analyze Existing Data

```bash
build/native-tests/native/traceloom /path/to/msprof_raw
```

Valid Ascend inputs include one `msprof_*.db` or a directory containing
monolithic `PROF_*/msprof_*.db` or split
`PROF_*/{host,device_*}/sqlite/*.db` layouts. The default output is a neighboring
`traceloom/loop_tree_v2.md` or one `deviceN_loop_tree_v2.md` per discovered DB.

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
