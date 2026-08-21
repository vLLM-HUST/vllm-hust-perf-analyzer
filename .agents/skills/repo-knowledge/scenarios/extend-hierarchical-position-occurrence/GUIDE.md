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
5. projection continuation routes and the model metadata key; and
6. rejection of mismatched slots, repeat iterations, or occurrence-edge order.

Before handoff, run the full native test preset, a release build, the
repository-knowledge validator, `git diff --check`, and inspect file sizes so a
new feature has not silently enlarged a legacy file beyond the 800-line yellow
boundary.
