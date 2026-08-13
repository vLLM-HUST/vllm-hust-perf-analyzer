# Output Schema

## Default Analytical Artifact

For one source database, TraceLoom writes one queryable database timeline:

```text
PROF_.../traceloom/analysis.db
```

For a profiler directory containing multiple device databases:

```text
msprof_output/traceloom/analysis_db01.db
msprof_output/traceloom/analysis_db02.db
```

The queryable database timeline contains embedded raw evidence plus the compressed
execution tree, occurrences, repeat counts, cost lenses, replay internals,
provenance, and typed analysis issues. `traceloom_analysis_surface` is its
self-describing entry-point catalog. `traceloom_raw_source_database` and
`traceloom_raw_table` describe raw packaging, including split profiles.

## Optional Markdown Projection

`--loop-tree-out PATH` renders a compact human projection over the same
analysis choices. It is a convenience for readers; the queryable database
timeline remains the complete analytical product and drill-down surface.

## Explicit Projections And Compatibility Artifacts

Use `traceloom --help-advanced` for non-default outputs:

- `--out PATH`: native result JSON;
- `--grammar-debug-out PATH`: grammar-state diagnostics;
- `--output PATH`: explicit path for the default queryable database timeline;
- `--aug-db-out PATH`: compatibility spelling of `--output`;
- `--compat-db-out PATH`: legacy queryable compatibility SQLite output without
  a raw-table snapshot;
- `--loop-tree-out PATH`: explicit human-readable Markdown projection;
- `--loop-tree-aux` / `--loop-tree-no-aux`: enable or disable auxiliary
  attribution (enabled by default for both database and Markdown projections).

Only one output may target stdout at a time, and explicit output paths require
a single input database.

Native JSON also carries two audit surfaces for graph-heavy comparisons:

- `graph_launch_body_members` retains every observed body task with launch,
  lane/order, raw operator, source row, interval, and task kind;
- `graph_body_cost_summary` reports per-occurrence scheduled-task sum,
  cross-stream busy union, observed envelope, compute/communication/data-move
  components, plus all-body and exact-ReplayUnit distributions. Unequal exact
  occurrence counts are evidence, not a schema error; consumers compare
  distributions rather than pairing occurrences by index.

The queryable database timeline exposes `traceloom_operator_audit`, a factual
inventory of every concrete observed operator identity and task type. It
reports occurrence count, total duration, graph-body membership, and anchor
membership without using a provider allowlist to decide whether an operator is
important. New operator identities therefore remain visible and sortable.

`traceloom_metadata` binds structural output to the selected evidence-role
projection through `evidence_role_policy_id`,
`evidence_role_policy_version`, and `evidence_role_manifest_sha256`. The same
identity appears in native JSON under `anchor_projection`; see
[`evidence-role-projection.md`](evidence-role-projection.md) for the role and
fallback contract.

Native JSON also emits `replay_internal_cost_map`, the replay-internal
query surface: ReplayUnit -> ordered launch/composition slots -> body
template -> per-stream ordered members -> fine-grained costs and provenance.
See [replay-internal-cost-map.md](replay-internal-cost-map.md) for the
result contract, the role-collapsed aligned-aggregate key (repeated slot
roles merge with `launch_member_count` multiplicity; exact member rows retain
slot id/`slot_order` as the drill-down contract), the cost-lens boundaries
(kind sums partition scheduled `task_sum` but are not an additive wall-clock
decomposition), scheduled-work-share denominators, and the fail-closed
support/reason model.



The checked-in SQL audit surface is
`docs/report-sql/reconstruction-capability-matrix.sql`, which summarizes exact,
typed-unknown, and legacy ACLGraph capability outcomes. It is an executable
golden: the native SQL compatibility test checks its columns and known-good
output.

## Provenance Contract

Native events and sidecar rows retain source kind, source path, source table,
and source row identifiers. Reports are summaries; use these links to confirm
diagnoses against the raw profiler evidence.

CUDA node-level exact units expose `cuda_runtime_correlation` launch matching,
`cuda_graph_node_set` identity, `observed_stream_set_unordered` body topology,
and `direct_observed_graph_launch` composition boundaries. These policy names
make the provider-specific evidence route auditable without weakening the
shared exact ReplayUnit contract.

## Compatibility Rule

The schema is still alpha. Public releases should version table, column, JSON,
and report-field contracts before downstream systems depend on every name.
