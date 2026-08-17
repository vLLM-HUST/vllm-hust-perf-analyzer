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

A monolithic DB can support bounded compute/event analysis, but a DB copied
away from its neighboring `PROF_*` evidence is not a complete cross-layer
input. TraceLoom records `input_evidence_state=evidence_incomplete` and
`input_scope=monolithic_db_only` when the profile lacks the full-profile-v1
side evidence (`host/sqlite/runtime.db`, `host/sqlite/stream_info.db`, or a
`device_*/sqlite/ascend_task.db`). Query these values in `traceloom_metadata`;
the optional Loop Tree header prints the same contract. Never compare a
DB-only report with a full-profile report as if they were equivalent runs.

For each `PROF_*` directory, the native analyzer prefers a monolithic DB with a
nonempty `TASK` table. If it is absent or unusable, TraceLoom reports a warning
and normalizes the split `AscendTask`, `TaskInfo`, `HostTask`, `ApiData`, and
available HCCL tables instead. `HCCLOP` (or `HCCLOpSingleDevice`) supplies
observed collective identity, while linked `HCCLTaskSingleDevice` and
`AscendTask` rows supply device geometry and task-level auxiliary evidence.
The inventory command shows every discovered split table, schema source, and
row count:

```bash
traceloom-native-ascend-sqlite-inventory /path/to/PROF_...
```

Split communication and exact graph reconstruction feed the same native IR and
report pipeline as their monolithic equivalents. Missing or ambiguous HCCL
operation evidence is retained at task granularity rather than guessed. Rich
transport/topology fields and PMU attribution remain incremental analysis
surfaces.

## Hygon/HIP

The same `traceloom <path>` command recognizes supported Hygon `hipprof`
SQLite exports and normalizes their device activity into the native IR.

## CUDA/Nsight Systems

The native analyzer accepts Nsight Systems SQLite exports containing
`CUPTI_ACTIVITY_KIND_KERNEL`. Runtime, memcpy, CUDA event, synchronization,
and graph tables are loaded when present.

Graph-level tracing supplies graph-exec identity and launch order but omits
node activities, so its ReplayUnits remain legacy rather than exact. For exact
visible-body reconstruction, use an Nsight node-level graph export. TraceLoom
requires a nonempty `CUDA_GRAPH_NODE_EVENTS` table, unique direct runtime
correlation, supported `graphNodeId` activity tables, and at least two matching
observations of the same raw node set and normalized visible body. Unsupported
or incomplete evidence remains a typed unrecognized region.

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

Treat the neighboring `traceloom/` analysis directory as derived output unless
the repository has an explicit artifact policy for it.
