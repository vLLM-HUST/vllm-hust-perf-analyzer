# Query-driven analysis playbook

## Start with contracts, not table guessing

Open the AugDB read-only:

```bash
sqlite3 -readonly /absolute/run/rank0.analysis.db
```

Discover supported surfaces and recipes:

```sql
.headers on
.mode box
.nullvalue NULL

SELECT surface_name, relation_name, purpose
FROM traceloom_analysis_surface ORDER BY surface_name;

SELECT projection_name, population_mode, resolution,
       observation_domain, measure_lens, selector_parameters, purpose
FROM traceloom_projection_recipe ORDER BY display_order;
```

Before running a recipe, inspect its selector and returned-coordinate contract:

```sql
SELECT parameter_name, sqlite_type, is_nullable, coordinate_kind,
       selection_relation, selection_column, purpose
FROM traceloom_projection_parameter
WHERE projection_name = :recipe
ORDER BY parameter_order;

SELECT result_column, coordinate_kind, purpose
FROM traceloom_projection_coordinate
WHERE projection_name = :recipe
ORDER BY coordinate_order;

SELECT example_sql
FROM traceloom_projection_recipe
WHERE projection_name = :recipe;
```

Use the published `example_sql`; SQL is the semantic interface. Do not recreate
boundaries or population membership in client code when a recipe exists.

## Select a structural Position

Start broad and bound the result:

```sql
SELECT position_id, local_node_id, display_parent_position_id,
       display_path, depth, node_type, label, repeat_count,
       occurrence_count, round(total_us, 3) AS total_us,
       round(total_us / max(occurrence_count, 1), 3) AS mean_realization_us
FROM traceloom_v_position
ORDER BY total_us DESC, db_idx, device_id, preorder_idx
LIMIT 30;
```

Choose a `position_id` that matches the question. Prefer repeated Positions
with enough Occurrences for a distribution when investigating variance. Record
the complete `display_path`, not only the label.

Bind coordinates explicitly in the SQLite shell:

```sql
.parameter init
.parameter set :position_id 'position-N286'
.parameter set :occurrence_id NULL
```

Run `hpo_occurrences` for one or all realizations. Define typical/outlier
selection before inspecting raw events: median, quantile, robust z-score, or a
domain threshold are valid; picking the most visually dramatic sample first is
not a statistical definition.

## Preserve contextual equivalence

Equal operator or subtree labels can occur at different structural sites.
Discover edge roles before combining children:

```sql
SELECT edge_role_id, edge_label, first_edge_order,
       parent_occurrence_count, concrete_edge_count,
       edges_per_parent_min, edges_per_parent_max, population_support
FROM traceloom_v_tree_edge_role
WHERE parent_position_id = :position_id
ORDER BY parent_tree_path, first_edge_order;
```

Use `equivalent_tree_edges` for one context-safe population. Use `tree_edges`
only for the ordered child texture of one parent Occurrence.

## Continue without translating coordinates

Ask the database which returned coordinate can feed the next projection:

```sql
SELECT source_column, target_projection, target_parameter,
       target_parameter_nullable
FROM traceloom_v_projection_continuation
WHERE source_projection = :recipe
ORDER BY target_projection, source_column;
```

Typical routes are:

```text
hpo_occurrences -> hpo_members -> event_audit
equivalent_tree_edges -> selected child_occurrence_id -> occurrence_host_windows
replay_cost_units -> replay_cost_launches -> replay_cost_members -> event_audit
bubble_hotspots -> bubble occurrences -> host_window_calls -> runtime_call_audit
```

Use `event_audit` and `runtime_call_audit` rather than joining provider tables
by an assumed row ID. A valid locator states the source database/table/key,
embedded table, row-id column, and resolution status. Query the literal
embedded row only after checking `resolution_status='embedded_raw'`.

## Read cost and missingness correctly

- `total_us` is a disjoint interval union. It avoids double-counting concurrent
  streams.
- `compute_us`, `comm_us`, `idle_us`, `aux_us`, and `self_us` share the node's
  denominator. `idle_us` is uncovered interval cost, not proof of hardware idle.
- Repeat-node averages divide by occurrence count and repeat count, so a repeat
  is comparable with one body iteration.
- Unknown, excluded, auxiliary, ambiguous, and unsupported rows are evidence
  states. Do not coerce them to zero or silently discard them.
- Observed timestamp order is geometry, not dependency or causality.

## Separate statistics from temporal audit

Use SQL or a dataframe/plotting tool for distributions, quantiles, rank
comparisons, cost mixtures, and structural-position heatmaps. Export the exact
selected coordinates and query text with the plot.

Use Perfetto after the statistical question identifies a scope or outlier. Its
job is to show interval geometry and nearby raw evidence, not to replace the
population definition.

## Use executable examples before inventing SQL

- `examples/db-timeline-tour/tour.sql` demonstrates scope, population,
  occurrence selection, position excess, host context, and raw-row audit.
- `docs/report-sql/` contains bounded reports for tree maps, occurrences,
  cost breakdowns, runtime/device relations, bubbles, replay, reconciliation,
  and evidence-role audit.
- `docs/composable-analytical-projections.md` defines the current coordinate
  model and compatibility projections.

Copy and parameterize these queries before writing a parallel client-side
analysis model.
