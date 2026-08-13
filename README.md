# TraceLoom

[中文说明](README.zh.md)

![Status](https://img.shields.io/badge/status-alpha-orange)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](native/)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

TraceLoom is a native C++17 offline analyzer for accelerator profiler traces.
It turns raw profiler databases into a self-contained **queryable database timeline**: a coarse-to-fine execution structure, cost distributions, exact
replay internals where evidence permits, and links back to embedded raw rows.

![TraceLoom queryable database timeline: horizontal evidence drill-down and vertical occurrence comparison](docs/assets/queryable-db-timeline.svg)

Every analysis starts from a structural scope and composes a projection. Select
one occurrence to read realized behavior, or range over all occurrences for a
statistical population; keep the scope folded, expand its children and events,
enter supported host-window context, or change the compatible cost lens without
reconstructing its boundary. Horizontal drill-down, vertical comparison,
hierarchical navigation, and cross-domain context are views over the same
coordinates. See [Composable Analytical Projections](docs/composable-analytical-projections.md).

Try the reproducible, beginner-friendly
[`60-second database-timeline tour`](examples/db-timeline-tour) on the checked-in real
profile—no prior SQL experience is required.

The native implementation is now the only production implementation in this
repository. The installed CLI has one stable name:

```bash
traceloom <profile.db-or-profile-dir>
```

## What TraceLoom Does

```text
profiler SQLite
  -> event normalization and source provenance
  -> semantic anchor extraction
  -> repeated-pattern discovery
  -> overlap-safe timeline cost attribution
  -> queryable database timeline
```

Key capabilities:

- Ascend/CANN monolithic and split-SQLite profile discovery, with automatic
  fallback when a usable monolithic `TASK` table is unavailable;
- CUDA/Nsight kernel, runtime, data-movement, synchronization, event, and graph
  replay ingestion through the native adapter;
- native semantic reconstruction for compute, HCCL, synchronization, and
  ACLGraph activity;
- repeated decode/layer structure discovery over semantic anchors;
- overlap-safe wall-clock accounting across concurrent streams;
- repeat-node averages normalized per loop-body iteration;
- first-class analytical projection recipes that compose structural scope,
  occurrence population, hierarchy depth, observation domain, and measure lens;
- provider-aware runtime-call ↔ device-work relations with explicit
  cardinality and open/ambiguous outcomes;
- reverse navigation from device anchors and auxiliary work to host runtime
  calls, including observed host runtime activity between adjacent anchors;
- provenance links back to the original database, table, and row.

CUDA auxiliary activity stays explicitly typed and traceable without being
misreported as compute. CUDA graph traces are represented as replay intervals
and share the native Loop Tree report surface with eager CUDA and Ascend input.
TraceLoom's versioned, unknown-first
[evidence-role projection contract](docs/evidence-role-projection.md) defines
which observations participate in structural identity without deleting cost,
context, or profiler-row provenance.

## Install On Debian Or Ubuntu

Build the `traceloom-native` Debian package from the repository root:

```bash
cmake -S native -B build/traceloom-native-package \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACELOOM_NATIVE_BUILD_TESTS=OFF
cmake --build build/traceloom-native-package -j "$(nproc)"
cpack --config build/traceloom-native-package/CPackConfig.cmake \
  -B build/traceloom-native-package
sudo apt install ./build/traceloom-native-package/traceloom-native_*.deb
```

The package installs only `/usr/bin/traceloom`. Verify it with:

```bash
traceloom --version
traceloom --help
```

Runtime package dependencies are `libc6`, `libstdc++6`, and `libsqlite3-0`.

To uninstall:

```bash
sudo apt remove traceloom-native
```

## Install From Source

```bash
cmake --preset dev
cmake --build --preset dev -j "$(nproc)"
cmake --install build/native --prefix "$HOME/.local"
```

Make sure `$HOME/.local/bin` is in `PATH`, then run `traceloom --version`.

## Quick Tutorial

### 1. Analyze One Database

```bash
traceloom /path/to/PROF_.../msprof_YYYYMMDDHHMMSS.db
```

The default product is a self-contained queryable database timeline written beside the source database:

```text
/path/to/PROF_.../traceloom/analysis.db
```

### 2. Analyze A Profiler Directory

```bash
traceloom /path/to/msprof_output
```

TraceLoom discovers monolithic `PROF_*/msprof_*.db` files and split
`PROF_*/{host,device_*}/sqlite/*.db` layouts, then writes one self-contained
database per discovered analysis input:

```text
/path/to/msprof_output/traceloom/analysis_db01.db
/path/to/msprof_output/traceloom/analysis_db02.db
```

Use an explicit worker count for large traces:

```bash
traceloom /path/to/msprof_output --threads 48
```

Within each `PROF_*`, a nonempty monolithic `TASK` table takes priority.
Otherwise TraceLoom emits a warning and builds the base timeline from split
`AscendTask`, `TaskInfo`, `HostTask`, and `ApiData` tables. When present,
`HCCLOP`/`HCCLOpSingleDevice` and `HCCLTaskSingleDevice` recover the same
collective-operation anchors as the monolithic path while retaining the
device-task decomposition as auxiliary evidence. Graph replay uses the same
exact reconstruction contract in both layouts; PMU attribution remains
incremental.

For a split `PROF_*` layout, all constituent SQLite files are copied into one
portable artifact under collision-free names. For a regular profiler DB, its
raw schema is snapshotted intact.

### 3. Read The Database Timeline

Each database describes its stable entry points:

```bash
sqlite3 /path/to/traceloom/analysis.db \
  'SELECT surface_name, relation_name, purpose FROM traceloom_analysis_surface;'
sqlite3 -header -column /path/to/traceloom/analysis.db \
  'SELECT local_node_id, label, depth, occurrence_count, avg_total_us FROM traceloom_v_tree_node ORDER BY display_order;'
```

Start at an outer Repeat node or another structural scope. The database itself
describes how that scope can be projected:

```sql
SELECT projection_name, population_mode, resolution,
       observation_domain, measure_lens, selector_parameters, purpose
FROM traceloom_projection_recipe
ORDER BY display_order;
```

`traceloom_projection_parameter` gives agents and UIs a normalized catalog of
each selector's type, nullability, purpose, and source relation/column.

Bind one scope once in the `sqlite3` shell, then reuse it across recipes:

```sql
.parameter init
.parameter set :node_id 'node-N006'
.parameter set :occurrence_idx NULL
```

`NULL` selects the full occurrence population; setting
`:occurrence_idx` to a number selects one realized execution. The same
`:node_id` can then remain folded, expand to ordered children or events, enter
supported host windows, or change cost lens. A bounded device window is also a
valid query scope, but selecting it does not promote it into a recovered
pattern. See the [complete projection UX](docs/composable-analytical-projections.md).

Important costs are:

- `total_us`: disjoint wall-clock union; overlapping streams are not counted
  twice;
- `avg_total_us`: per occurrence for ordinary nodes and per loop-body
  iteration for Repeat nodes;
- `avg_compute_us`, `avg_comm_us`, `avg_idle_us`: comparable average cost
  categories;
- `avg_aux_us` and `avg_self_us`: attributed auxiliary and node-owned cost.

#### Audit structural-symbol normalization

TraceLoom keeps two operator identities when constructing the anchor sequence:

- `observed_symbol` is the concrete provider/backend label selected from the
  normalized profiler observation;
- `structural_symbol` is the comparison key used by pattern discovery.

Only explicit, versioned rules loaded from
[`native/data/default_structural_symbol_rules.tsv`](native/data/default_structural_symbol_rules.tsv)
may change the observed label. For example, the current Ascend policy maps the supported `MatMulV1/V2/V3` and
`BatchMatMulV1/V2/V3` labels to the structural symbol `MatMul`. An unfamiliar
label follows the typed identity fallback and remains visible rather than
being fuzzily merged. Equal structural symbols make observations comparable;
they do not establish a family/position correspondence by name alone.
Use `--symbol-rules PATH` to replace this input for one run or
`--extend-symbol-rules PATH` to add higher-priority rules. The effective
manifest identity, SHA-256, catalog, and per-anchor matches are copied into
the resulting database. A replacement manifest may intentionally contain no
rules, which selects identity preservation for every observed symbol. If two
different rules match at the same highest precedence, TraceLoom preserves the
observed symbol and emits the typed `conflict` outcome together with
`candidate_rule_ids`; it never chooses one silently.

Discover the policy, explain one anchor, or compare concrete lowerings at the
same recovered position:

```sql
SELECT policy_id, policy_version, rule_id, provider_scope,
       match_mode, match_expression, structural_symbol
FROM traceloom_symbol_normalization_rule
ORDER BY precedence;

SELECT anchor_id, observed_symbol, structural_symbol, rule_id, outcome,
       source_table, source_key
FROM traceloom_v_anchor_symbol_lineage
WHERE anchor_id = 'anchor-42';

SELECT local_node_id, anchor_order, structural_symbol, observed_symbol,
       occurrence_count, total_us, avg_us, min_us, max_us
FROM traceloom_v_symbol_variant_cost
WHERE structural_symbol = 'MatMul'
ORDER BY total_us DESC;
```

`traceloom_v_symbol_normalization_placement` is the bidirectional bridge from
one decision to `node_id`, `occurrence_idx`, and `anchor_order`. Its
`source_path`, `source_table`, and `source_key` columns retain the concrete
observation locator. See
[`docs/report-sql/symbol-normalization-audit.sql`](docs/report-sql/symbol-normalization-audit.sql)
for a ready-to-run audit.

#### Recommended host/device workflow

Start from a recovered structure rather than from a provider table. First pick
a costly or repeated node, then keep its occurrence and anchor coordinates
while moving into runtime/device evidence:

```sql
SELECT node_id, local_node_id, label, occurrence_count, avg_total_us
FROM traceloom_v_tree_node
WHERE occurrence_count > 1
ORDER BY total_us DESC
LIMIT 20;

SELECT occurrence_idx, anchor_order, anchor_idx, api_name, device_symbol,
       match_policy, support_state, cardinality
FROM traceloom_v_node_runtime_call
WHERE node_id = 'node-N006' AND coverage_kind = 'self'
ORDER BY occurrence_idx, anchor_order, runtime_start_ns;

SELECT sync_kind, api_name, runtime_dur_us, match_policy, support_state
FROM traceloom_v_sync_runtime_call
WHERE support_state IN ('supported_exact', 'supported_deterministic')
ORDER BY device_start_ns
LIMIT 100;

SELECT occurrence_idx, anchor_order, right_anchor_symbol, host_interval_us,
       api_name,
       count(*) AS observed_calls,
       round(sum(observed_overlap_us), 3) AS scheduled_overlap_us
FROM traceloom_v_node_host_activity
WHERE node_id = 'node-N006' AND coverage_kind = 'self'
GROUP BY occurrence_idx, anchor_order, right_anchor_symbol,
         host_interval_us, api_name
ORDER BY occurrence_idx, anchor_order, scheduled_overlap_us DESC;
```

Replace `node-N006` with a returned `node_id`. The first runtime view exposes
provider-supported submission/correlation relations. The synchronization view
is factual action-to-runtime evidence, not record/wait pairing or idle-cause
attribution. The final query reports calls
in the host interval **after** each node-owned anchor; it is contextual runtime
behavior, not CPU cost owned by that node and not an idle-cause claim. Keep
`support_state`, `cardinality`, and source keys in any audit. For recurring
uncovered device intervals, start with
`docs/report-sql/structure-bubble-statistics.sql`. It ranks bubble cost by
recovered structural position, reports host-observation coverage and upstream
API-family distributions, and drills a selected `bubble_id` into exact runtime
calls. These are contextual observations, not a cause label.

A bounded canned projection is available in
`docs/report-sql/node-host-activity.sql`; it selects
the highest-cost repeated atom by default and documents how to substitute a
chosen `node_id`. Runtime
calls may overlap each other; even the clipped overlap sum is scheduled-call
time, not an overlap-safe host busy union.

For the complete composable-projection experience shown above:

```bash
sqlite3 -readonly \
  examples/kickstart_smoke/msprof_raw/traceloom/analysis_db01.db
```

Then run this at the `sqlite>` prompt:

```sql
.read examples/db-timeline-tour/tour.sql
```

### 4. Choose An Explicit Output Path

`--output` changes the first-class database path; `--aug-db-out` remains a
compatibility spelling:

```bash
traceloom /path/to/msprof.db --output /tmp/run.traceloom.db
```

The input is opened read-only. TraceLoom snapshots every raw profiler table
into the new file and appends its `traceloom_*` hierarchy, occurrence, exact
graph, replay-cost, provenance, and typed-issue relations. The resulting file
can be moved away from the input and queried without `ATTACH`:

```bash
sqlite3 /tmp/run.traceloom.db < docs/report-sql/replay-cost-hotspots.sql
sqlite3 /tmp/run.traceloom.db < docs/report-sql/node-replay-cost-members.sql
```

Use `source_table` and `source_row_id` to audit a selected member directly in
the copied vendor table. `traceloom_metadata` records the original path,
byte-size, and SHA-256 as provenance; they are not runtime dependencies.

For split layouts, inspect `traceloom_raw_source_database` and
`traceloom_raw_table` to resolve each original `(source_path, source_table)` to
its embedded table and preserved source-rowid column.

### 5. Request Other Advanced Evidence

Markdown and native JSON are explicit projections/debug products, not a second
analytical model:

```bash
traceloom /path/to/msprof.db \
  --loop-tree-out /tmp/loop_tree_v2.md \
  --output /tmp/traceloom-analysis.db \
  --out /tmp/native_result.json
```

Run `traceloom --help-advanced` for grammar diagnostics and auxiliary
materialization options.

## Checked-In Kickstart Profile

The repository includes a real two-device vLLM-Ascend profile under
[`examples/kickstart_smoke`](examples/kickstart_smoke). Try it with:

```bash
traceloom examples/kickstart_smoke/msprof_raw
```

The capture contains more than 1.18 million selected profiler rows. TraceLoom
compresses them into 112 structural nodes and recovers the same nested
`Repeat x36 -> Repeat x24` transformer pattern on both devices.

## Development

```bash
cmake --preset dev-tests
cmake --build --preset dev-tests -j "$(nproc)"
ctest --preset dev-tests
```

Some deep regression tests use external research fixtures. Standalone clones
skip those tests when the fixture directory is absent; normal native unit tests
still run.

## Documentation

- [Native analyzer and packaging guide](native/README.md)
- [Step-by-step workflow](docs/workflow.md)
- [Accepted input layouts](docs/input-profiles.md)
- [Output contract](docs/output-schema.md)
- [Native architecture](docs/architecture.md)
- [Queryable database timeline guide (Chinese)](docs/db-timeline-guide.zh.md)

## License

TraceLoom is released under the [MIT License](LICENSE).
