# Input Profiles

TraceLoom analyzes profiler output produced outside the tool.

## Ascend/CANN

Current supported input is CANN `msprof` SQLite output. TraceLoom discovers DBs
from either layout:

```text
<run_dir>/msprof_raw/PROF_*/msprof_*.db
<raw_dir>/PROF_*/msprof_*.db
```

The analyzer expects the profile DBs to contain task and string metadata. When
Huawei communication tables are available, collective anchors are built from
them; otherwise the analyzer falls back to task coalescing.

## CUDA/Nsight

CUDA/Nsight support is a target interface, not the current parser. The intended
path is:

1. collect a native Nsight Systems profile from the user's workload;
2. export or read a timeline representation that can be mapped to Perfetto /
   Chrome Trace style events;
3. normalize kernels and collectives into TraceLoom semantic anchors;
4. run the same loop tree, metrics, report, and augmented timeline exporters.

## Artifact Policy

Raw profiles are usually large and often contain private workload details. Do
not commit them to the open-source repository. Keep only small synthetic
fixtures, checksums, manifests, and reproduction instructions in source.
