# Input Profiles

TraceLoom analyzes profiler output produced outside the tool.

## Ascend/CANN

The native analyzer accepts one CANN `msprof_*.db` or a directory containing:

```text
<run_dir>/msprof_raw/PROF_*/msprof_*.db
<raw_dir>/PROF_*/msprof_*.db
```

The database should contain task timing and string metadata. Communication and
ACLGraph tables enrich the report when present; missing optional tables do not
prevent the base task timeline from loading.

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
