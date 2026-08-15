# Composable Analytical Projections

TraceLoom does not materialize one privileged report. It materializes stable
execution coordinates from which a human, agent, or UI can compose analytical
views.

Every projection makes five choices:

| Axis | Typical choices | Question answered |
| --- | --- | --- |
| **Scope** | structural node/pattern, position, occurrence, bounded device window | What part of the observed execution am I selecting? |
| **Population** | one occurrence, all occurrences at the same structural coordinate | Do I want one realized behavior or a statistical population? |
| **Resolution** | folded node, immediate children, anchors/events, exact replay members, raw rows | How far should the view expand? |
| **Observation domain** | device, supported host windows, embedded profiler evidence | Which observation domain should contextualize the scope? |
| **Measure lens** | overlap-safe cost, occurrence cost, scheduled work, bubble cost, host API distribution | Which compatible cost or behavior measure should the view expose? |

Presentation is a sixth, downstream choice: SQL rows, a tree, a timeline, a
Web view, or a paper figure may all project the same selected coordinates.

## Start from one scope

Pick a high-level structural handle from the self-describing scope catalog.
Besides cost and occurrence counts, the catalog returns device-sequence order,
parent, symbol, and anchor extent. An analyst can therefore constrain a
candidate by observed timeline placement before reusing its `node_id`, without
falling back to a private tree-table join:

```sql
SELECT node_id, parent_node_id, display_order, symbol, label,
       occurrence_count, first_anchor_idx, last_anchor_idx,
       round(total_us, 3) AS total_us
FROM traceloom_v_tree_node
ORDER BY total_us DESC, db_idx, device_id, view_name, display_order;
```

Inside the `sqlite3` shell, bind that handle once:

```sql
.parameter init
.parameter set :node_id 'node-N006'
.parameter set :occurrence_idx NULL
```

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
WHERE projection_name = 'scope_members'
ORDER BY parameter_order;
```

Each recipe also declares which result columns are reusable coordinates:

```sql
SELECT result_column, coordinate_kind, purpose
FROM traceloom_projection_coordinate
WHERE projection_name = 'scope_members'
ORDER BY coordinate_order;
```

The database derives compatible continuations from those input and output
contracts. For example, after ranking aligned positions, ask which returned
columns can drive the next query:

```sql
SELECT source_column, target_projection, target_parameter,
       target_parameter_nullable
FROM traceloom_v_projection_continuation
WHERE source_projection = 'position_population'
ORDER BY target_projection, source_column;
```

The result includes `position_occurrences`: its required `node_id` and
`anchor_order` both come from the selected population row. The agent can then
inspect the full corresponding-position population, select an unusual
`occurrence_idx`, and continue from its returned `event_id` to `event_audit`.
A continuation is published only when every non-nullable target coordinate is
available; optional selectors may be retained from analyst state or left
`NULL`.

Copy the `example_sql` for a recipe to run it. SQL remains the semantic
interface: a CLI, agent, notebook, or UI binds the parameters and renders the
returned rows without defining a second analysis API.

## Switch between one occurrence and the population

`scope_occurrences` uses the same query for both readings:

```sql
SELECT node_id, local_node_id, occurrence_idx, repeat_context,
       start_ns, end_ns, anchor_count,
       compute_us, comm_us, idle_us, total_us, self_us, aux_us
FROM traceloom_tree_node_occurrence
WHERE node_id = :node_id
  AND (:occurrence_idx IS NULL OR occurrence_idx = :occurrence_idx)
ORDER BY occurrence_idx;
```

With `:occurrence_idx = NULL`, the rows are the vertical cost population for
the selected structure. Bind `:occurrence_idx = 7` to obtain the seventh
realized occurrence without changing scope identity or reconstructing its
boundary.

## Change resolution without losing the coordinate

Use `scope_hierarchy` to keep the scope folded and read its ordered children.
Use `scope_members` to expand one or all occurrences to anchors and normalized
events. Exact graph/replay relations extend the same route where provider
evidence supports visible members. Event/source locators then continue to the
embedded profiler rows.

The selected structural node remains the coordinate throughout:

```text
node-N006
  -> all occurrences          population view
  -> occurrence 7             one realized view
  -> ordered children         hierarchical view
  -> anchors and events       expanded device view
  -> supported host windows   cross-domain context
  -> profiler rows            evidence audit
```

Exact replay cost maps are also coordinate-composable rather than a hidden
table family. `replay_cost_units` discovers supported and unsupported exact
ReplayUnit occurrences; `replay_cost_launches` expands one returned
`cost_unit_id` to all ordered slots or one `slot_order`; and
`replay_cost_members` expands a returned `launch_id` to exact member costs and
normalized `event_id` values. An event then continues to `event_audit`.
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
  -> one launch_id                    exact member cost and provenance
  -> one event_id                     embedded profiler-row audit
  -> replay_unit_id placements        structural occurrence / host context
```

Captured topology lanes without scheduled members remain topology, not
zero-cost member rows. A cost unit is supported only after exact task-kind
membership and the nonempty stream/lane mapping pass the replay cost-map
contract.

Supported exact replay bodies add a recursive structural route over those
aligned Positions:

```text
replay_body_domains
  -> replay_body_patterns                 recursive definitions
  -> replay_body_pattern_occurrences      realized Position spans and costs
  -> replay_body_pattern_positions        aligned Position distributions
  -> replay_body_pattern_members          exact members and event coordinates
  -> event_audit                          embedded source rows
```

The route is per stream and never invents cross-stream ordering. A full-body
decode replay can therefore be folded into repeated nested structure, compared
across occurrences, and audited back to embedded raw evidence without leaving
the coordinate contract. See
[Recursive Replay-Body Patterns](replay-body-patterns.md).

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

`scope_host_context` projects the same `:node_id` and optional
`:occurrence_idx` into host windows delimited by runtime endpoints of adjacent
device anchors. Start with `scope_host_windows` when the support state itself
matters: every structural position remains visible as `supported_ordered`,
`missing_endpoint`, `nonmonotonic_host_order`, or another typed boundary.
`scope_host_context` then left-projects profiler-visible API distributions;
unsupported and supported-but-empty intervals remain rows. It does not charge
host duration to the device node or assign a cause.

`bubble_host_context` starts from a recovered structural position, holds that
position fixed across bubble occurrences, and combines overlap-safe uncovered
device cost with supported upstream API-family observations. A selected bubble
can then continue through `bubble_occurrences` to `host_window_calls` and
`runtime_call_audit`. `bubble_hotspots` retains positions whose occurrences
have no supported host window, so changing observation domain never turns an
unsupported population into an empty successful answer.

Cost columns remain named lenses. Scheduled task sums, overlap-safe busy time,
wall-span envelopes, device bubble cost, and host scheduled-call duration are
not silently added or substituted for one another.

## Select a raw device window

A user-selected time interval is also a valid query scope. Bind
`:db_idx`, `:device_id`, `:start_ns`, and `:end_ns`, then use the
`device_window_events` recipe to inspect overlapping normalized events.

Selecting an interval does **not** promote it into a recovered pattern. The
window can be intersected with TraceLoom's materialized structures, but pattern
identity, occurrence correspondence, and hierarchy remain governed by the
analyzer's evidence contracts.

## Product invariant

> TraceLoom materializes not one answer, but reusable coordinates from which
> analysts and agents compose evidence-backed answers.

The original profiler observations remain embedded and authoritative outside
the supported model. Missing or ambiguous relations remain typed unsupported
outcomes rather than empty successful projections.

The RQ2 interaction contract is therefore concrete:

```text
select scope/position
  -> inspect a population
  -> choose an occurrence from returned coordinates
  -> change resolution or observation domain
  -> reach an embedded source row or a typed support boundary
```

No stage reruns structural recovery or silently invents a missing coordinate.
