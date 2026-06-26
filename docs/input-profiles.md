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

CUDA/Nsight support currently accepts SQLite exports produced by Nsight Systems:

```bash
nsys profile --trace=cuda,nvtx,osrt -o run_name <workload command>
nsys export --type=sqlite --force-overwrite=true -o run_name.sqlite run_name.nsys-rep
traceloom analysis /path/to/directory/containing/run_name.sqlite --out-dir /path/to/analysis
```

The adapter reads CUPTI kernel, CUDA Graph replay, memcpy, memset, and
synchronization activity from the SQLite export. CUDA Graph replay rows are
modeled as `CudaGraphReplay` execution anchors; major CUDA kernels and
NCCL-like collectives are normalized into TraceLoom semantic anchors. The CUDA
path reuses the same loop tree, metrics, report, and augmented database
exporters as the Ascend path. NVTX ranges are preserved in the source SQLite and
can be queried directly; first-pass semantic projection is driven by GPU
activity rows.

## Artifact Policy

Raw profiles are usually large and often contain private workload details. Do
not commit production traces to the open-source repository. This repository
keeps one compact real vLLM-Ascend kickstart profile under
`examples/kickstart_smoke/`; larger traces should live in external artifact
storage with checksums, manifests, and reproduction instructions in source.

When a team intentionally keeps large raw profiles in a private repository
through Git LFS, validate that the local checkout contains real file contents
before analysis:

```bash
git lfs pull
git lfs fsck --objects
```

Run TraceLoom with `--out-dir` pointing outside the raw profile tree when the
input directory is source-controlled:

```bash
traceloom analyze experiments/profiler/exp_001/profiler/msprof \
  --out-dir /tmp/traceloom-exp001
```

Treat the generated bundle as derived data. Keep raw LFS artifacts immutable,
and commit generated analysis databases only when they are part of the
repository's explicit experiment artifact policy.
