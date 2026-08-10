# TraceLoom Native

Native C++ analyzer for TraceLoom. This is the recommended entry point for
current Ascend `msprof` profiles.

## Install

### Debian package

Build the installable `traceloom-native` package on Debian or Ubuntu:

```bash
cmake -S native -B build/traceloom-native-package \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACELOOM_NATIVE_BUILD_TESTS=OFF
cmake --build build/traceloom-native-package -j "$(nproc)"
cpack --config build/traceloom-native-package/CPackConfig.cmake \
  -B build/traceloom-native-package
sudo apt install ./build/traceloom-native-package/traceloom-native_*.deb
```

The package installs one stable entry point: `/usr/bin/traceloom`. Verify the
installation with:

```bash
traceloom --version
traceloom --help
```

Remove it with:

```bash
sudo apt remove traceloom-native
```

### Install from source

From the TraceLoom repository root:

```bash
cmake --preset dev
cmake --build --preset dev -j "$(nproc)"
cmake --install build/native --prefix "$HOME/.local"
```

This installs the native command as:

```bash
traceloom
```

The `dev` preset searches `$HOME/.local` through `CMAKE_PREFIX_PATH`, so a
user-local SQLite install such as `~/.local/include/sqlite3.h` and
`~/.local/lib/libsqlite3.a` is enough to enable Ascend `msprof` DB loading.

## Analyze A Profile

Pass either one Huawei `msprof_*.db` file:

```bash
traceloom examples/kickstart_smoke/msprof_raw/PROF_000001_20260609064648517_AJJGNKPPJMEGGLFA/msprof_20260609064817.db
```

or a Huawei profiler bundle directory containing monolithic
`PROF_*/msprof_*.db` files or split `PROF_*/{host,device_*}/sqlite/*.db` files:

```bash
traceloom examples/kickstart_smoke/msprof_raw
```

TraceLoom discovers the DB files automatically and writes reports under a
neighboring `traceloom/` directory:

```text
PROF_.../traceloom/loop_tree_v2.md
msprof_raw/traceloom/device0_loop_tree_v2.md
msprof_raw/traceloom/device1_loop_tree_v2.md
```

A nonempty monolithic `TASK` table takes priority. When it is missing,
TraceLoom emits a split-fallback warning and builds the base timeline from
`AscendTask`, enriched with `TaskInfo`, `HostTask`, and `ApiData` metadata.

Use more or fewer worker threads when needed:

```bash
traceloom /path/to/msprof_output --threads 48
```

## CUDA/Nsight Kernel Preview

The first native CUDA adapter slice accepts an Nsight Systems SQLite export
whose `CUPTI_ACTIVITY_KIND_KERNEL` table contains `start`, `end`, `deviceId`,
and `streamId`. `correlationId`, `demangledName`, `shortName`, and `StringIds`
are optional. Missing names receive stable `cuda_kernel_N` labels, and a
missing correlation id falls back to the source SQLite row id.

```bash
build/native/native/traceloom \
  --source-kind cuda_nsys_sqlite \
  --source-db /path/to/report.sqlite \
  --compat-sidecar-out /tmp/cuda-native-sidecar.db \
  --loop-tree-out /tmp/cuda-loop-tree-v2.md \
  --sidecar-only
```

The CUDA adapter imports kernels plus available runtime API, memcpy, memset,
synchronization, CUDA event, and graph-trace rows. Auxiliary rows retain an
explicit task type and provenance without becoming compute anchors. Graph trace
intervals become CUDA graph replay units; kernels inside those intervals remain
available to the compatibility sidecar as child evidence. Missing optional
tables are valid, while malformed present tables fail with an explicit schema
error.

NCCL-qualified kernels whose labels identify a recognized collective operation
materialize as communication evidence rather than compute anchors. Operation
names are normalized to `AllReduce`, `ReduceScatter`, `AllGather`, `Broadcast`,
or `AllToAll`, while the raw Nsight kernel label and source-row provenance
remain available in the native IR. Bare operation-like labels and NCCL
point-to-point kernels remain ordinary task evidence rather than being promoted
from names alone.

Nsight versions that export `CUPTI_ACTIVITY_KIND_CUDA_EVENT` as an
identity-only lookup table without `start` or `timestamp` keep that table as
inventory evidence and skip timeline materialization; they do not block timed
kernel, synchronization, or CUDA Graph evidence from loading.

## Developer Commands

Build with native tests enabled:

```bash
cmake --preset dev-tests
cmake --build --preset dev-tests -j "$(nproc)"
ctest --preset dev-tests
```

Some deep regression tests use external design fixtures from the larger
research workspace. Fresh standalone clones skip those tests when the fixtures
are not present; normal unit tests still run.

Advanced analyzer output and the inventory tool remain available for
development:

```bash
build/native/native/traceloom \
  --source-db examples/kickstart_smoke/msprof_raw/.../msprof_*.db \
  --out native_result.json

build/native/native/traceloom-native-ascend-sqlite-inventory \
  examples/kickstart_smoke/msprof_raw/.../msprof_*.db
```

To test the core build without SQLite support:

```bash
cmake --preset dev-no-sqlite
cmake --build --preset dev-no-sqlite -j "$(nproc)"
```
Signal classification defaults are inspectable in
`data/default_signal_classification_rules.tsv`. Replace the policy for one run
with `--classification-rules PATH`, or add higher-priority environment-specific
rules with `--extend-classification-rules PATH`. `TRACELOOM_CLASSIFICATION_RULES`
can also select a complete ruleset. Unknown columns, roles, match modes, and
equal-precedence conflicts fail before analysis starts.

### Idle Evidence Semantic Taxonomy

Semantic task roles answer a different question than the structural anchor
rules above: what does a task mean for productive/idle analysis
(`productive_compute`, `productive_comm`, `productive_data_move`,
`visible_wait`, `capture_control`, `record`, `runtime_control`, `unknown`)?
The two taxonomies are independent; a task may be structurally `ignore` and
semantically `visible_wait` at the same time. The evidence contract these
roles feed is `docs/idle_evidence_contract.md` in the
[intellistream/vllm-request-lifecycle-profiler-plugin](https://github.com/intellistream/vllm-request-lifecycle-profiler-plugin/blob/main/docs/idle_evidence_contract.md)
parent repository; the local design draft is
[`notes/rfc-synchronization-gap-attribution.md`](../notes/rfc-synchronization-gap-attribution.md).

Defaults live in `data/idle_evidence_semantic_rules.tsv`, declared with a
`# ruleset_version:` header line and stable, unique `rule_id`s (for example
`wait.event_wait`, `compute.matmul`) so classification lineage stays stable
across file edits. Exact `task_type` rules (priority 200-170) outrank fuzzy
`blob` keyword rules (priority 150-100); there is no catch-all rule, so an
unmatched task is explicitly `unknown` with an empty `matched_rule_id`.
Rules with equal priority must agree on the role, otherwise classification
fails rather than silently picking a file order.

Each classified task yields `role + matched_rule_id`; the classifier reports
`semantic_rules_version` and `semantic_rules_sha256` (SHA-256 of the raw
ruleset file bytes). The `traceloom-native-idle-evidence-audit` tool runs
the classifier over a profile database and reports per-role counts and
durations plus unknown-task detail (top `task_type` / `op_type` by duration):

```bash
traceloom-native-idle-evidence-audit \
  --source-db /path/to/msprof.db [--idle-evidence-rules PATH]
```

An explicit ruleset override that fails to load exits non-zero; it never
silently falls back. `TRACELOOM_IDLE_EVIDENCE_RULES` selects the default
ruleset path. The E2 productive-timeline library
(`build_productive_timelines`) consumes these roles to build per-device
productive timelines and visible gaps. E3
(`build_stream_state_timelines`) builds the observed per-stream partition,
and E4 (`build_idle_explanations`) slices each visible gap into conservative,
mutually exclusive device-evidence categories. The authoritative E1-to-E4
composition is `run_idle_evidence_pipeline`; its results feed the CLI, SQL
sidecar, anchor/node attribution, and Loop Tree summaries. Host correlation
remains a separate later stage that requires calibrated clocks and validated
host/device links.
