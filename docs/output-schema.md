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
`traceloom_projection_recipe` describes the primary scope/population/
resolution/domain/lens query compositions and their named selector parameters.
`traceloom_projection_parameter` makes selector type, nullability, coordinate
kind, source, and purpose independently queryable.
`traceloom_projection_coordinate` declares which result columns can be reused
by later projections, while `traceloom_v_projection_continuation` lists target
recipes for which every required coordinate kind is available.

If one source database contains several devices, the artifact contains one
independently recovered structural tree per observed `device_id`. TraceLoom
does not concatenate those device sequences or invent a cross-device order.
Node and semantic-tree keys are device-scoped in that case (for example,
`node-d1-N001` and `native-report-tree-d1`).

## Optional Markdown Projection

`--loop-tree-out PATH` renders a compact human projection over the same
analysis choices. It is a convenience for readers; the queryable database
timeline remains the complete analytical product and drill-down surface.
For a multi-device source database, add `--loop-tree-device-id N`; one Markdown
path cannot ambiguously name several device-local trees.

## Explicit Projections And Compatibility Artifacts

Use `traceloom --help-advanced` for non-default outputs:

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

The database carries two audit surfaces for graph-heavy comparisons:

- `graph_launch_body_members` retains every observed body task with launch,
  lane/order, raw operator, source row, interval, and task kind;
- `graph_body_cost_summary` reports per-occurrence scheduled-task sum,
  cross-stream busy union, observed envelope, compute/communication/data-move
  components, plus all-body and exact-ReplayUnit distributions. Unequal exact
  occurrence counts are evidence, not a schema error; consumers compare
  distributions rather than pairing occurrences by index.

`traceloom_v_replay_position_realization_member` is the nested structural
plane inside one protected graph/replay Position. It preserves exact graph-body
membership and raw source locators, orders members by observed timestamps with
deterministic tie-breakers, and retains lane-local coordinates. The view also
publishes `policy_role` beside `final_role`: atomic protection may change the
flat role to `protected_boundary`, but it does not erase an internal operator
or collective's policy-level anchor identity. `observed_order` and
`observed_relation_to_previous` describe interval geometry only; they do not
assert a dependency or causal edge. Recursive replay-body grammar remains
per-stream and does not consume this cross-stream display order.

The queryable database timeline exposes `traceloom_operator_audit`, a factual
inventory of every concrete observed operator identity and task type. It
reports occurrence count, total duration, graph-body membership, and anchor
membership without using a provider allowlist to decide whether an operator is
important. New operator identities therefore remain visible and sortable.

`traceloom_metadata` binds structural output to the selected evidence-role
projection through `evidence_role_policy_id`,
`evidence_role_policy_version`, and `evidence_role_manifest_sha256`. The same
See [`evidence-role-projection.md`](evidence-role-projection.md) for the role
and fallback contract.

The complete audit relation is additive and does not overload the coarse
`traceloom_event.role` field. `traceloom_evidence_role_decision` contains one
typed outcome per normalized event; policy/rule catalogs explain stable IDs;
`traceloom_evidence_role_placement` links outcomes to anchors, auxiliary
regions, graph body members, replay units, and protected intervals; and
`traceloom_evidence_role_issue` retains conflict, unsupported, missing-
capability, and orphan outcomes. `traceloom_v_evidence_role_cost_coverage`
makes retained cost outside identity matching explicit.
Structural-symbol normalization is independently auditable:

- `traceloom_symbol_normalization_policy` identifies the versioned policy;
- `traceloom_symbol_normalization_rule` catalogs explicit provider rules and
  typed identity/unsupported fallbacks;
- `traceloom_anchor_symbol_normalization` records the decision actually used
  for every structural anchor;
- `traceloom_v_anchor_symbol_lineage` joins decisions to their rule and anchor
  interval;
- `traceloom_v_symbol_normalization_placement` connects the same decision to
  recovered node, occurrence, and position coordinates;
- `traceloom_v_symbol_variant_cost` groups occurrence counts and anchor costs
  by structural position and concrete observed backend label.

The model deliberately separates symbol comparability from structural
correspondence. A rule may map several supported backend labels to one
`structural_symbol`, but only the recovered node/occurrence/position relations
say that two concrete anchors play the same role. Unrecognized labels use the
identity fallback; they are not guessed into a known class. Each decision also
retains `source_path`, `source_table`, and `source_key` for raw-evidence
drill-down.

The default input is `native/data/default_structural_symbol_rules.tsv`.
`--symbol-rules` replaces it and `--extend-symbol-rules` composes a
higher-priority extension. `source_manifest`, `manifest_sha256`, `rule_origin`,
`rule_origin_sha256`, and `source_line` record the exact effective input in the
database artifact. A zero-rule replacement is a valid identity-only policy.
Equal-precedence runtime ambiguity is identity-preserving and appears as a
typed `conflict` decision whose `candidate_rule_ids` names every competing
rule.

The replay-cost relations expose ReplayUnit -> ordered launch/composition
slots -> body template -> per-stream ordered members -> fine-grained costs and
provenance. See [replay-internal-cost-map.md](replay-internal-cost-map.md) for
the role-collapsed aligned-aggregate key (repeated slot roles merge with
`launch_member_count` multiplicity; exact member rows retain slot id/
`slot_order` as the drill-down contract), the cost-lens boundaries (kind sums
partition scheduled `task_sum` but are not an additive wall-clock
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
