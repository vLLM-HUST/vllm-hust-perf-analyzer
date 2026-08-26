# Intake and installation

## Choose the input

TraceLoom accepts these production inputs directly:

```text
<run>/PROF_*/msprof_*.db
<profile>/ASCEND_PROFILER_OUTPUT/ascend_pytorch_profiler_*.db
<profile>/PROF_*/{host,device_*}/sqlite/*.db
<capture>/*.sqlite exported by Nsight Systems
```

Pass one DB when one exact output path is required. Pass a profiler directory
when TraceLoom should discover all compatible inputs; in that mode omit
`--output`, `--perfetto-out`, and other single-output flags.

For distributed captures, construct an explicit inventory before analysis:

```text
rank | logical/physical device | process/profile root | source DB | clock domain
```

Do not infer rank from lexical file order, timestamps, PIDs, or filenames when
launcher receipts are available. A DB that contains multiple device timelines
is not automatically a distributed-rank aggregate.

Keep the capture command, measured-window definition, model/workload revision,
profiler version, and source DB hashes beside the analysis receipt. TraceLoom
cannot reconstruct missing capture provenance.

## Resolve the executable

1. Prefer an already installed `traceloom` and record `traceloom --version`.
2. In a TraceLoom source checkout, prefer an existing verified build.
3. Otherwise build locally; do not require `sudo` merely to analyze a trace.

```bash
cmake --preset dev
cmake --build --preset dev -j "${BUILD_JOBS:-4}"
build/native/native/traceloom --version
```

Use a job count appropriate to the host rather than blindly consuming every
CPU. To install only for the current user:

```bash
cmake --install build/native --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
traceloom --version
```

For a release build without tests:

```bash
cmake -S native -B build/release \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACELOOM_NATIVE_BUILD_TESTS=OFF
cmake --build build/release -j "${BUILD_JOBS:-4}"
build/release/traceloom --version
```

For a machine-wide Debian/Ubuntu installation, use the repository's CPack path
only when global package authority is explicit:

```bash
cmake -S native -B build/traceloom-native-package \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACELOOM_NATIVE_BUILD_TESTS=OFF
cmake --build build/traceloom-native-package -j "${BUILD_JOBS:-4}"
cpack --config build/traceloom-native-package/CPackConfig.cmake \
  -B build/traceloom-native-package
sudo apt install ./build/traceloom-native-package/traceloom-native_*.deb
traceloom --version
```

Prefer the user-local or exact build-tree executable for task-scoped analysis;
do not mutate a shared machine merely for convenience.

TraceLoom is a C++17 CPU-side offline postprocessor. Do not start an accelerator
runtime or container solely to run it.

## Produce deterministic task-owned outputs

Use absolute, unique paths and preserve logs:

```bash
traceloom /absolute/raw/rank0.db \
  --threads 8 \
  --output /absolute/run/traceloom/rank0.analysis.db \
  --timings \
  > /absolute/run/traceloom/rank0.stdout.log \
  2> /absolute/run/traceloom/rank0.stderr.log
```

Do not reuse an output path after a failed attempt without first distinguishing
the old artifact from the new run. Check the process exit status and the final
`wrote queryable database timeline` receipt.

When analyzing a directory, TraceLoom writes one AugDB per discovered input:

```text
<profile>/traceloom/analysis.db
<profile>/traceloom/analysis_db01.db
<profile>/traceloom/analysis_db02.db
```

Within an Ascend `PROF_*`, a nonempty monolithic `TASK` table takes priority.
Otherwise TraceLoom falls back to the split host/device SQLite layout and says
so in the log. Keep that evidence state in the handoff.

## Receipt checklist

Record at minimum:

```bash
traceloom --version
sha256sum /absolute/raw/*.db
sha256sum /absolute/run/traceloom/*.analysis.db
```

Also retain the command line, stdout/stderr, input-to-rank mapping, output size,
and any non-default classification, symbol, or reconciliation rule files and
their hashes. Never place credentials, raw databases, AugDBs, or generated
Perfetto files in Git.
