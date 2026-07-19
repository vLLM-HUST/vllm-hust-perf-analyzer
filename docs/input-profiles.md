# Input Profiles

TraceLoom analyzes profiler output produced outside the tool.

## Ascend/CANN

The native analyzer accepts one CANN `msprof_*.db` or a directory containing
monolithic or split SQLite output:

```text
<run_dir>/msprof_raw/PROF_*/msprof_*.db
<raw_dir>/PROF_*/msprof_*.db
<raw_dir>/PROF_*/host/sqlite/*.db
<raw_dir>/PROF_*/device_*/sqlite/*.db
```

For each `PROF_*` directory, the native analyzer prefers a monolithic DB with a
nonempty `TASK` table. If it is absent or unusable, TraceLoom reports a warning
and normalizes the split `AscendTask`, `TaskInfo`, `HostTask`, and `ApiData`
tables instead. The inventory command shows every discovered split table,
schema source, and row count:

```bash
traceloom-native-ascend-sqlite-inventory /path/to/PROF_...
```

The first split-SQLite stage provides the base device timeline and summary.
Fine-grained split communication, graph replay, and PMU attribution remain
incremental; monolithic profiles continue to provide those enrichments when
their corresponding tables are available.

## Hygon/HIP

The same `traceloom <path>` command recognizes supported Hygon `hipprof`
SQLite exports and normalizes their device activity into the native IR.

## Artifact Policy

Raw profiles are often large and can contain private workload details. Do not
commit production traces to this repository. The compact real example under
`examples/kickstart_smoke/` is an intentional exception.

When raw profiles are stored with Git LFS, restore and validate them before
analysis:

```bash
git lfs pull
git lfs fsck --objects
traceloom /path/to/restored/msprof_output
```

Treat the neighboring `traceloom/` report directory as derived output unless
the repository has an explicit artifact policy for it.
