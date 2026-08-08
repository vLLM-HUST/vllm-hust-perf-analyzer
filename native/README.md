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

TraceLoom discovers the DB files automatically and writes self-contained queryable database timelines under a neighboring `traceloom/` directory:

```text
PROF_.../traceloom/analysis.db
msprof_raw/traceloom/analysis_db01.db
msprof_raw/traceloom/analysis_db02.db
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
equal-precedence conflicts fail before analysis starts. Rules may match the
normalized `task_type`, the full evidence `blob`, or an exact raw `operator`
identity. Only an explicit `ignore` rule may remove a concrete operator row;
an unmatched operator remains in the structural sequence.

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
across file edits. Exact `task_type` rules (priority 200-170) and registered
exact `operator` rules outrank fuzzy `blob` keyword rules (priority 150-100);
there is no catch-all rule, so an unmatched task is explicitly `unknown` with
an empty `matched_rule_id`.
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

The production JSON exposes an exact-identity registration census under
`semantic_operator_coverage`. Fuzzy family matches remain useful but do not
hide a previously unseen raw operator name from this audit. Unregistered
concrete operators are preserved by the structural anchor projection unless
an explicit noise rule rejects them.
Graph-contained operators additionally retain exact body membership and cost
under `graph_launch_body_members` and `graph_body_cost_summary`; graph replay
protection therefore cannot make a new kernel disappear from the audit.

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
is now the optional calibrated continuation of that same pipeline. Ascend
production runs load the versioned `idle_evidence_host_api_rules.tsv`, retain
all imported host API rows and connectionId link outcomes, and promote host
evidence only when the clock model and robust-window rules permit it.

### Host-to-device clock calibration

The preferred real-capture input is a runtime bracket TSV produced around
`aclrtRecordEvent` and event synchronization. The narrow record-call bracket
and outer device-marker bracket are distinct. It uses this exact header:

```text
marker_id\thost_before_ns\trecord_after_ns\thost_after_ns\thost_pid\thost_tid\tdevice_id\tstream_id\tcall_site\treturn_status
```

Analyze the matching single-device Ascend profile with:

```bash
traceloom /path/to/PROF_... \
  --clock-marker-brackets /path/to/clock_marker_brackets.tsv \
  --compat-db-out /path/to/report.traceloom.db
```

For every successful bracket, the resolver normally requires non-empty overlap
between `[host_before_ns, record_after_ns)` and exactly one profiled same-thread
`aclrtRecordEvent` row. The outer interval through `host_after_ns` is never used
to identify or train the host-clock leg. Real profiler host API timestamps can
drift away from the caller's `CLOCK_REALTIME` bracket. If
direct overlap is absent, TraceLoom requires an order-preserving bijection:
successful bracket and same-thread record counts must match, both timestamp
sequences must be strictly increasing, and endpoint-affine correction must
leave every API's same-ordinal bracket uniquely nearest. Ambiguity still fails
closed. At least two same-thread pairs are required for this fallback; one
distant bracket plus one API is never enough to identify an affine clock
relation. A unique connectionId/TASK yields `TASK.startNs` as the marker device
timestamp. A uniquely resolved API with no connectionId or TASK is retained as
a rejected marker; multiple matching APIs/TASKs remain fatal. Raw
`aclrtEventGetTimestamp` values are device syscnt and are never interpreted as
profiler nanoseconds. Every resolved marker retains the matched CANN_API host
interval, `direct_overlap` or `ordinal_affine_fallback` resolution method, and
the fallback residual when applicable.

The direct `--clock-markers PATH` option accepts the already-resolved frozen
11-field payload from the idle-evidence contract. Because that payload does not
carry the matched profiler-host interval, it cannot establish the
msprof-host→caller-host leg for real host attribution. It is therefore usable
for controlled fixtures only; `--clock-markers-synthetic` forces the result to
`alignment_status=synthetic_only`. Omitting markers yields
`uncalibrated`, retains host rows and structural links, and emits no
cross-clock interval or delay claim.

Calibration follows idle-evidence-contract-v4.4 and fits two explicit affine
legs with the same deterministic every-fifth holdout. The first observation is
the profiled `aclrtRecordEvent` midpoint paired with the midpoint of its narrow
caller `[host_before_ns, record_after_ns]` bracket. The second observation is
the outer caller `[host_before_ns, host_after_ns]` midpoint paired with device
`TASK.startNs`. Host API timestamps are mapped through `F(p)=f(g(p))`; the
outer midpoint never trains `g`. The sidecar reports both component parameters
and residuals, the composed holdout residual as a non-independent diagnostic,
and both bracket uncertainties. `offset_ns` is the frozen reference-coordinate
delta; the arbitrary affine constant is emitted separately as `intercept_ns`.
Half-even timestamp rounding is used, and
`epsilon_ns = marker_device_residual_p95 + scaled_bracket_uncertainty_p95 +`
`scaled_profiler_caller_residual_p95 +`
`scaled_record_call_bracket_uncertainty_p95`.
Official host-sync evidence uses only the epsilon-shrunk robust overlap;
official queued-task delay additionally requires a unique exact connectionId.
Possible-only overlap and non-robust delay remain candidates and never replace
an E4 explanation slice.
