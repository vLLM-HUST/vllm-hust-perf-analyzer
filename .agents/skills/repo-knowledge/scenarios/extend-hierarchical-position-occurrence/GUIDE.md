# Extend hierarchical Position--Occurrence

Use this scenario when changing TraceLoom's canonical structural query model,
adding a Position/Occurrence projection, or judging whether a legacy tree,
Pattern, coverage, replay, bubble, or host relation is direct structural
membership.

## Start from the accepted model

The source of truth is
`native/include/traceloom/analysis/structural_position_model.h` and
`native/src/analysis/structural_position_model.cpp`. The model has exactly
three primitive relations:

```text
Refines(q, k, p)    Position q exposes child Position p at structural slot k
Realizes(o, q)      Occurrence o is one measured realization of Position q
Member(m, o, k, r)  m is measured member r at slot k of Occurrence o
```

`k` and `r` are different invariants. A sequence assigns a distinct slot to
each ordered child and normally has `member_order=1`. A repeat preserves its
body slots across iterations and uses the iteration as `member_order`. An Atom
has one terminal member at slot 0. Do not flatten repeat iterations into new
structural slots or use one integer for both meanings.

Containment and transitive anchor coverage are derived relations. In
particular, `traceloom_tree_node_anchor` may contain anchors reached through
several child Positions and is not a direct-member table.

Grammar Patterns, macros, and productions remain recovery mechanisms. After
lowering they are not semantic peers of Position and Occurrence. Legacy
`traceloom_replay_body_pattern_*` tables remain compatibility storage only;
canonical replay code uses the Position aliases and HPO relations.

## Preserve conformance and paths

Every Occurrence of one composite Position must realize the same ordered slot
signature. Repeat iterations must be dense, unique, within the declared count,
and realize the same body slots. `StructuralOccurrenceGraph` edge order must
agree with the child Occurrence's edge order before HPO lowering.

Generic SQL lowering is owned by
`native/src/compat/structural_position_rows.cpp`. Preserve both paths:

- `rooted_position_path` changes only with structural refinement;
- `occurrence_path` adds slot/member coordinates and therefore distinguishes
  repeated realizations.

A terminal member's path must extend the complete parent Occurrence path, not
restart from the parent's short ID. Local Position IDs are database-timeline
coordinates; cross-run matching requires a separately validated neutral key
and compatible support contract.

## Use the canonical query surfaces

New generic analysis begins with:

```text
hpo_positions -> hpo_refinements -> hpo_occurrences -> hpo_members
```

The backing relations are `traceloom_v_position`,
`traceloom_position_refinement`, `traceloom_v_position_occurrence`, and
`traceloom_v_position_member`. Replay-body analysis uses the corresponding
`replay_hpo_*` recipes and
`traceloom_v_replay_body_position_{definition,occurrence,direct_member}` plus
`traceloom_replay_body_position_refinement`.

Keep projection metadata and continuations self-describing. A Position result
must continue to refinement and Occurrence queries; an Occurrence must continue
to direct members; terminal evidence must continue through the relevant event
or replay-cost coordinate. The metadata key `structural_coordinate_model`
identifies the shipped contract as `hierarchical_position_occurrence_v1`.

### Present a single ordered edge plane to SQL consumers

Keep `k` and `r` distinct in canonical HPO storage, but do not force an AugDB
query to reconstruct a concrete tree walk from both axes. The SQL-facing route
is:

```text
hpo_positions -> tree_edge_roles -> equivalent_tree_edges
hpo_occurrences -> tree_edges -> equivalent_tree_edges
```

`traceloom_execution_tree_edge` materializes one parent-to-child Occurrence
edge with a single `edge_order`; `edge_ordinal_in_role` is derived from that
order. `traceloom_execution_tree_edge_role` materializes contextual structural
equivalence classes. Only equal `edge_role_id` populations are valid to
aggregate. Never infer equivalence from an operator label: the checked-in
kickstart profile has two distinct `AllReduce` roles at first-edge orders 8
and 13 under `node-N286`.

Keep the two thin coordinate tables materialized. Reconstructing roles from
HPO membership inside every view query made the real role-catalog query take
seconds. Also index the selectors used by public recipes as leftmost columns:
`parent_occurrence_id` for one concrete stream and `edge_role_id` for one
equivalent population. On the same 25,771-edge artifact this changed one
336-edge stream from about 101 ms to 3.8 ms and a 696-edge cost population from
about 518 ms to 28 ms; the 14-row role catalog became sub-millisecond after a
warm page cache.

Cost lookup crosses two deliberately different view names. Semantic Position
rows use `anchor_tree`, while `traceloom_viz_node_anchor` rows for this tree use
the `traceloom_semantic_tree.semantic_projection` value
`native_report_tree`. Carry that mapping through the tree catalog; do not join
the two tables by equal `view_name`, and do not omit view identity and silently
mix an unrelated projection. Compute, communication, and uncovered cost
partition supported child total; auxiliary cost remains a separate,
non-additive lens.

### Migrate a legacy lens without deleting its evidence

Retire competing navigation nouns, not analytical verbs. A specialized host,
bubble, replay, collective, or audit relation may remain authoritative or
materialized after it stops being a default UX entry. Re-key its public recipe
to Position, Occurrence, or edge coordinates; remove the physical relation
only after it has no unique evidence, consumer, or measured materialization
value.

The first accepted bridge is:

```text
equivalent_tree_edges.child_occurrence_id
  -> occurrence_host_windows / occurrence_host_context
  -> host_window_calls
  -> runtime_call_audit
```

The bridge preserves typed unsupported/empty host windows and bounded
query-time runtime-call intersection. It removes the user-visible translation
from `occurrence_id` back to `node_id + occurrence_idx`.

Compatibility recipes remain executable but are demoted after the canonical
route with `display_order >= 100` and an explicit `compatibility:` purpose.
Prefer this reversible catalog change before deleting a relation or breaking a
consumer.

Push the selected Occurrence coordinates into
`traceloom_v_node_host_interval` before joining the result. A direct outer join
from one selected Occurrence caused SQLite to materialize the global host view
and took roughly 300 ms for one row on the checked-in kickstart artifact. A
`selected_interval AS MATERIALIZED` CTE whose `WHERE` predicates use scalar
values from the selected Occurrence preserved the node-anchor and interval
indexes and took roughly 0.2 ms after a warm cache. Query UX migration must
preserve bounded execution shape as well as row semantics.

## Keep compatibility contained

Existing tree, Loop Tree, `scope_*`, replay-pattern, bubble, host, and coverage
surfaces remain supported until their consumers migrate. Add HPO through
coherent helpers rather than extending already oversized compatibility files.
In particular, keep HPO catalog additions in
`augmented_position_projection_catalog.*`; do not grow the general projection
catalog past its current yellow-line size.

Observed boundary: the repository currently centralizes all native libraries,
tools, and test-target registration in `native/CMakeLists.txt` (1,299 lines at
this change). HPO had to register its owned sources and tests there, so the
file grew by the smallest target-list amendment. Treat this as recorded build-
organization pain, not permission for feature logic in that file; a coherent
CMake target split is a separate maintenance change rather than hidden scope in
a semantic-model patch.

## Verify a change

Build with bounded parallelism, then run at least:

```bash
cmake --build /tmp/traceloom-hpo-build -j <chosen-jobs>
ctest --test-dir /tmp/traceloom-hpo-build --output-on-failure \
  -R 'structural_position_model|structural_projection_rows|replay_body_pattern|sidecar_materializer'
```

Require fixtures for:

1. a sequence with distinct ordered child slots;
2. a repeat whose one body slot has several ordered realizations;
3. terminal membership reaching normalized event/source evidence;
4. replay HPO refinements, Occurrences, and terminal aggregate coordinates;
5. projection continuation routes and the model metadata key;
6. rejection of mismatched slots, repeat iterations, or occurrence-edge order;
7. contextual edge-role equality plus derived ordinal-in-role; and
8. cost lookup that excludes a same-node/same-occurrence row from an unrelated
   projection view.

Before handoff, run the full native test preset, a release build, the
repository-knowledge validator, `git diff --check`, and inspect file sizes so a
new feature has not silently enlarged a legacy file beyond the 800-line yellow
boundary.
