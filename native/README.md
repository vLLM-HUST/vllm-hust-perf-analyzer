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

When a single profiler database contains multiple devices (for example an
Nsight Systems capture of a DP2 training run), the structural report is
partitioned by observed `device_id`: TraceLoom materializes one independently
recovered tree per device in the queryable database and never emits a combined
cross-device tree. `--loop-tree-device-id N` selects one device for the
optional Markdown projection; an explicit `--loop-tree-out PATH` on a
multi-device database requires that flag, and an unknown device id lists the
available devices.

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
identity. Only a positive auxiliary or transparent rule may remove an
observation from identity matching; every unfamiliar observation becomes an
explicit unknown anchor and may disrupt a match. The policy ID, version, and
exact manifest digest are recorded in output provenance. See
[`docs/evidence-role-projection.md`](../docs/evidence-role-projection.md) for
the provider-neutral contract, accounting behavior, and extension rules.
The manifest is deliberately a flat TSV table. Repeat
`--classification-rule-override RULE_ID.FIELD=VALUE` for explicit per-analysis
typed overwrites after table replacement/extension; the exact table digest and
effective override digest are independently queryable in `analysis.db`.

Structural-symbol normalization is a separate input policy in
`data/default_structural_symbol_rules.tsv`. It determines when concrete
provider/backend labels may share one comparison symbol; it does not decide
whether an event participates in the anchor sequence. Replace it with
`--symbol-rules PATH`, extend it with `--extend-symbol-rules PATH`, or select a
complete manifest with `TRACELOOM_SYMBOL_RULES`. The parser rejects unknown
fields/match modes, duplicate rule IDs, and duplicate equal-precedence match
keys before analysis. If distinct predicates still overlap on a concrete
observation at the same highest precedence, the analyzer preserves that
observation and records a typed conflict rather than choosing silently.
Unmatched symbols follow an identity fallback. The effective policy, manifest
hash, rule catalog, and per-anchor decisions are materialized in `analysis.db`.

Sparse duplicate-observation reconciliation is independently configured by
`data/default_event_reconciliation_rules.tsv`. Use
`--event-reconciliation-rules PATH` for full replacement or
`--extend-event-reconciliation-rules PATH` for an overlay; an overlay row with
the same stable `rule_id` overwrites the default. Reconciliation retains every
normalized event, fails open to independent anchors on ambiguity, and records
policy/rule/decision/member rows plus the member-centric
`traceloom_v_event_reconciliation` audit view.
