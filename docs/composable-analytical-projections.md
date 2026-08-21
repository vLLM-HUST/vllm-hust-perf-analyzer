# Composable Analytical Projections

TraceLoom does not materialize one privileged report. It materializes stable
execution coordinates from which a human, agent, or UI can compose analytical
views.

Every projection makes five choices:

| Axis | Typical choices | Question answered |
| --- | --- | --- |
| **Scope** | hierarchical Position, Occurrence, bounded device window | What part of the observed execution am I selecting? |
| **Population** | one occurrence, all occurrences at the same structural coordinate | Do I want one realized behavior or a statistical population? |
| **Resolution** | folded node, immediate children, anchors/events, exact replay members, raw rows | How far should the view expand? |
| **Observation domain** | device, supported host windows, embedded profiler evidence | Which observation domain should contextualize the scope? |
| **Measure lens** | overlap-safe cost, occurrence cost, scheduled work, bubble cost, host API distribution | Which compatible cost or behavior measure should the view expose? |

Presentation is a sixth, downstream choice: SQL rows, a tree, a timeline, a
Web view, or a paper figure may all project the same selected coordinates.

## Start from one Position

The canonical structural routes are:

```text
hpo_positions -> hpo_occurrences -> hpo_members
              -> tree_edge_roles -> equivalent_tree_edges
hpo_occurrences -> tree_edges -> equivalent_tree_edges
equivalent_tree_edges -> occurrence_host_windows / occurrence_host_context
```

A Position is the reusable coordinate, an Occurrence is one measured
realization, a concrete edge belongs to one measured child stream, and
`edge_role_id` defines contextual structural equivalence. Direct HPO members
retain the storage distinction between structural slot order and repeated
realizations, while the SQL-facing edge projection presents one concrete
`edge_order`. See the
[Hierarchical Position--Occurrence model](hierarchical-position-occurrence.md).

Pick a structural handle from the Position catalog. It retains cost,
occurrence count, parent, path, and preorder placement, so selection does not
fall back to a private tree-table join:

```sql
SELECT position_id, parent_position_id, preorder_idx, symbol, label,
       node_type, repeat_count, occurrence_count,
       round(total_us, 3) AS total_us
FROM traceloom_v_position
ORDER BY total_us DESC, db_idx, tree_id, preorder_idx;
```

Inside the `sqlite3` shell, bind that handle once:

```sql
.parameter init
.parameter set :position_id 'node-N286'
.parameter set :occurrence_id NULL
```

Discover child roles before defining a comparison population:

```sql
SELECT edge_role_id, edge_label, first_edge_order,
       parent_occurrence_count, concrete_edge_count,
       edges_per_parent_min, edges_per_parent_max, population_support
FROM traceloom_v_tree_edge_role
WHERE parent_position_id = :position_id
ORDER BY parent_tree_path, first_edge_order;
```

Equal labels do not imply equal roles. After selecting one `edge_role_id`, use
`equivalent_tree_edges` to compare its concrete population and select an
unusual returned `child_occurrence_id`. Use `tree_edges` when the question is
the ordered texture of one parent Occurrence rather than a population.

Every generated database describes the reusable recipes that accept these
coordinates:

```sql
SELECT projection_name, population_mode, resolution,
       observation_domain, measure_lens, selector_parameters, purpose
FROM traceloom_projection_recipe
ORDER BY display_order;
```

Parameter discovery is relational too; agents do not need to parse prose to
learn selector types or where candidate coordinates come from:

```sql
SELECT parameter_name, sqlite_type, is_nullable, coordinate_kind,
       selection_relation, selection_column, purpose
FROM traceloom_projection_parameter
WHERE projection_name = 'equivalent_tree_edges'
ORDER BY parameter_order;
```

Each recipe also declares which result columns are reusable coordinates:

```sql
SELECT result_column, coordinate_kind, purpose
FROM traceloom_projection_coordinate
WHERE projection_name = 'equivalent_tree_edges'
ORDER BY coordinate_order;
```

The database derives compatible continuations from those input and output
contracts. For example, after comparing equivalent edges, ask which returned
columns can change observation domain:

```sql
SELECT source_column, target_projection, target_parameter,
       target_parameter_nullable
FROM traceloom_v_projection_continuation
WHERE source_projection = 'equivalent_tree_edges'
ORDER BY target_projection, source_column;
```

The result includes `occurrence_host_windows` and `occurrence_host_context`:
their required `occurrence_id` comes directly from
`child_occurrence_id`. The agent can therefore change from device cost to host
observation without translating the selected Occurrence back into legacy
`node_id + occurrence_idx` coordinates.
A continuation is published only when every non-nullable target coordinate is
available; optional selectors may be retained from analyst state or left
`NULL`.

Copy the `example_sql` for a recipe to run it. SQL remains the semantic
interface: a CLI, agent, notebook, or UI binds the parameters and renders the
returned rows without defining a second analysis API.

## Switch between one Occurrence and the population

`hpo_occurrences` selects one or all realizations of a Position:

```sql
SELECT *
FROM traceloom_v_position_occurrence
WHERE position_id = :position_id
  AND (:occurrence_id IS NULL OR occurrence_id = :occurrence_id)
ORDER BY occurrence_idx;
```

With `:occurrence_id = NULL`, the rows are the complete realization population.
Bind one returned ID to select an exact execution without reconstructing its
boundary. For a contextual child-edge population, use
`equivalent_tree_edges`; it prevents same-label edges from different call sites
from entering one statistical population.

## Change resolution without losing the coordinate

Use `tree_edge_roles` to keep a Position folded while discovering context-safe
child classes. Use `tree_edges` to read one measured child stream and
`hpo_members` to expand one Occurrence to direct child Occurrences or terminal
events. Exact graph/replay relations extend the same route where provider
evidence supports visible members. Event/source locators then continue to the
embedded profiler rows.

The selected coordinates remain explicit throughout:

```text
Position
  -> edge roles                    valid child populations
  -> one parent Occurrence         one realized edge stream
  -> equivalent concrete edges     population view
  -> one child Occurrence           selected outlier
  -> direct events / replay members expanded device view
  -> typed host windows             cross-domain context
  -> profiler rows                  evidence audit
```

The older `scope_*`, anchor-order `position_*`, and Pattern-named replay
recipes remain compatibility projections while consumers migrate. Their useful
analytical methods are re-keyed one at a time rather than deleted with their
old navigation nouns. See the
[AugDB SQL UX migration](augdb-sql-ux-migration.md).

Exact replay cost maps are also coordinate-composable rather than a hidden
table family. `replay_cost_units` discovers supported and unsupported exact
ReplayUnit occurrences; `replay_cost_launches` expands one returned
`cost_unit_id` to all ordered slots or one `slot_order`; and
`replay_cost_members` expands a returned `launch_id` to exact member costs and
normalized `event_id` values on
`traceloom_v_replay_position_realization_member`. That Position-realization
plane interleaves streams by observed timestamps while retaining
`lane_ordinal`/`task_ordinal`, exact membership, source lineage, and both the
nested `policy_role` and protected outer `final_role`. `observed_order` is a
display coordinate, not a dependency or causality claim. An event then
continues to `event_audit`.
`replay_structural_placements` maps a returned `replay_unit_id` back to every
recovered graph-unit `node_id`/`occurrence_idx` that realizes it. Ancestor,
template, and member scopes are not placements. The recipe deliberately
returns a population rather than assuming one replay occurrence has one
graph-unit placement. This reverse route lets a cost-selected replay
occurrence continue to host context.
`scope_exact_replay_members` returns the same `cost_unit_id`, `launch_id`, and
`slot_order` coordinates, so an analyst can branch from one selected tree
occurrence into either replay cost lenses or host context without rediscovering
the replay boundary.

```text
ReplayUnit occurrence / cost_unit_id
  -> ordered launch slots             task_sum / busy_union / envelope
  -> one launch_id                    observed operator/collective member plane
  -> one event_id                     embedded profiler-row audit
  -> replay_unit_id placements        structural occurrence / host context
```

Captured topology lanes without scheduled members remain topology, not
zero-cost member rows. A cost unit is supported only after exact task-kind
membership and the nonempty stream/lane mapping pass the replay cost-map
contract.

Supported exact replay bodies expose the same hierarchical Position--Occurrence
route over aligned terminal Positions:

```text
replay_body_domains
  -> replay_hpo_positions                 hierarchical Position definitions
  -> replay_hpo_refinements               ordered child slots
  -> replay_hpo_occurrences               measured Occurrences and costs
  -> replay_hpo_members                   child Occurrences or terminal Positions
  -> replay_body_pattern_members          compatibility terminal drill-down
  -> event_audit                          embedded source rows
```

The grammar route is per stream and never invents cross-stream structural
order. It is complementary to the Position-realization plane above: the latter
publishes observed interval geometry across streams but does not feed that time
order back into grammar or infer dependencies. A full-body decode replay can
therefore be folded into repeated nested structure, compared across
occurrences, and audited back to embedded raw evidence without leaving the
coordinate contract. See
[Recursive Replay-Body Positions](replay-body-patterns.md).

Exact replay evidence offers another horizontal coordinate system over the
complete device sequence. The parameter-free `exact_replay_partition` recipe
returns an ordered open/replay/between-replays cost partition for every
supported device tree. Its `tree_id`, `db_idx`, `device_id`,
`coordinate_kind`, and `coordinate_index` are declared reusable coordinates;
protected-interval and replay-unit identifiers preserve the evidence boundary.
The companion `traceloom_v_exact_replay_partition_status` relation explains
why the partition is absent when replay boundaries are missing, invalid, or
overlapping. Thus a workflow comparison can align explicit coordinates before
comparing costs rather than relying on row order or reconstructing intervals
in client SQL.

## Change observation domain or measure lens

`occurrence_host_windows` projects one selected `:occurrence_id` into host
windows delimited by runtime endpoints of adjacent covered device anchors.
Start there when the support state itself matters: every returned position is
`supported_ordered`, `missing_endpoint`, `nonmonotonic_host_order`, or another
typed boundary. `occurrence_host_context` then left-projects profiler-visible
API distributions; unsupported and supported-but-empty intervals remain rows.
It does not charge host duration to the device Occurrence or assign a cause.
The recipe pushes the selected Position/Occurrence coordinates into the host
interval view before intersecting indexed runtime calls at query time; it does
not materialize the global interval/call relation.

The older `scope_host_windows` and `scope_host_context` accept
`node_id + occurrence_idx` and remain compatibility routes over the same host
evidence until consumers migrate.

`bubble_host_context` starts from a recovered structural position, holds that
position fixed across bubble occurrences, and combines overlap-safe uncovered
device cost with supported upstream API-family observations. Its recipe
materializes only the selected position and bubble population as CTEs, then
uses fixed join order and the runtime-time index; it does not aggregate the
global bubble/call relation. A selected bubble can then continue through
`bubble_occurrences` to `host_window_calls` and `runtime_call_audit`.
`bubble_hotspots` retains positions whose occurrences have no supported host
window, so changing observation domain never turns an unsupported population
into an empty successful answer. Generic bubble API-family views remain
query-time relations and should be filtered by structural position or bubble
identity.

Cost columns remain named lenses. Scheduled task sums, overlap-safe busy time,
wall-span envelopes, device bubble cost, and host scheduled-call duration are
not silently added or substituted for one another.

## Select a raw device window

A user-selected time interval is also a valid query scope. Bind
`:db_idx`, `:device_id`, `:start_ns`, and `:end_ns`, then use the
`device_window_events` recipe to inspect overlapping normalized events.

Selecting an interval does **not** promote it into a committed Position. The
window can be intersected with TraceLoom's materialized structures, but
Position identity, Occurrence correspondence, and hierarchy remain governed
by the analyzer's evidence contracts.

## Product invariant

> TraceLoom materializes not one answer, but reusable coordinates from which
> analysts and agents compose evidence-backed answers.

The original profiler observations remain embedded and authoritative outside
the supported model. Missing or ambiguous relations remain typed unsupported
outcomes rather than empty successful projections.

The RQ2 interaction contract is therefore concrete:

```text
select a Position
  -> discover contextual edge roles
  -> inspect one equivalent-edge population
  -> choose a child Occurrence from returned coordinates
  -> change resolution, measure lens, or observation domain
  -> reach an embedded source row or a typed support boundary
```

No stage reruns structural recovery or silently invents a missing coordinate.
