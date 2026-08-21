# Hierarchical Position--Occurrence Model

TraceLoom's structural query kernel is **Hierarchical Position--Occurrence
(HPO)**. A Position is a reusable structural coordinate at any depth. An
Occurrence is one measured realization of that Position. Composite Positions
expose locally ordered child slots; direct membership binds each slot to child
Occurrences or binds an atomic Position to terminal event evidence.

Grammar patterns and productions remain construction mechanisms. They are not
separate analytical entities after structural lowering.

## Three relations

```text
Refines(q, k, p)   composite Position q exposes child Position p at slot k
Realizes(o, q)     Occurrence o is one measured realization of Position q
Member(m, o, k, r) m is measured member r at slot k of Occurrence o
```

`k` and `r` are deliberately different coordinates. A sequence normally has
one member at each of several slots. A repeat has one or more body slots and
multiple ordered realizations of each slot. Local slot order supplies
same-plane structural order; it does not assert timestamp causality.

Containment is derived when a member is a child Occurrence. A Position-bounded
span is valid only between locally ordered slots of the same composite Position
under the same rooted context.

## Canonical generic surfaces

| Relation | Meaning |
| --- | --- |
| `traceloom_v_position` | reusable Position definitions and measures |
| `traceloom_position_refinement` | ordered child slots of composite Positions |
| `traceloom_v_position_occurrence` | measured Occurrences with rooted structural and realization paths |
| `traceloom_v_position_member` | direct child Occurrences or terminal events |

The corresponding recipes are:

```text
hpo_positions
  -> hpo_refinements
  -> hpo_occurrences
  -> hpo_members
  -> event_audit
```

For example:

```sql
SELECT position_id, label, node_type, repeat_count, occurrence_count, total_us
FROM traceloom_v_position
ORDER BY total_us DESC;

SELECT slot_ordinal, child_position_id
FROM traceloom_position_refinement
WHERE parent_position_id = :position_id
ORDER BY slot_ordinal;

SELECT occurrence_id, rooted_position_path, occurrence_path
FROM traceloom_v_position_occurrence
WHERE position_id = :position_id
ORDER BY occurrence_idx;

SELECT slot_ordinal, member_order, member_kind,
       child_position_id, child_occurrence_id, event_id, terminal_symbol
FROM traceloom_v_position_member
WHERE parent_occurrence_id = :occurrence_id
ORDER BY slot_ordinal, member_order;
```

`rooted_position_path` changes only when structural context changes.
`occurrence_path` also includes member ordinals, so repeated realizations of one
slot remain distinct without inventing additional Positions.

`traceloom_tree_node_anchor` remains a useful coverage relation, but it is not
direct HPO membership: a composite node may cover terminal anchors reached
transitively through several child Positions.

## Replay-body surfaces

Recursive replay-body recovery publishes the same model:

| Relation | Meaning |
| --- | --- |
| `traceloom_v_replay_body_position_definition` | hierarchical replay Position definitions |
| `traceloom_replay_body_position_refinement` | ordered replay child slots |
| `traceloom_v_replay_body_position_occurrence` | replay Position Occurrences and cost summaries |
| `traceloom_v_replay_body_position_direct_member` | direct child Occurrences or terminal aligned replay Positions |

The `replay_hpo_*` projection recipes expose this route. Terminal rows retain
the exact replay-cost aggregate coordinate, from which existing member, event,
and source-locator relations continue the audit.

The older `traceloom_replay_body_pattern_*` tables remain compatibility
surfaces. Their definition IDs are exposed as Position IDs by the canonical
views above. They must not be used to reintroduce Pattern as a peer semantic
entity.

## Acceptance invariants

TraceLoom fails closed rather than publishing HPO rows when:

- sibling slot order is not dense and deterministic;
- Occurrences of one sequence Position disagree on their child slots;
- iterations of one repeat Position disagree on the body-slot definition;
- a repeat iteration is missing, duplicated, or outside its declared count;
- a direct member points outside its parent or lacks a typed terminal; or
- a terminal member cannot reach its normalized event/source evidence.

Cross-run or cross-device equality is separate. Local Position IDs are scoped
to one database timeline. A comparison must validate an explicit neutral
structural path/key and compatible support contract before pairing Positions.

## Compatibility and migration boundary

Tree, Loop Tree, replay-pattern, anchor-coverage, graph-body, bubble, and host
relations remain supported projections and evidence surfaces. HPO defines how
their stable structural coordinates and measured realizations compose; it does
not make every legacy relation a direct member relation or make every cost lens
additive.

New analysis code should start from the HPO recipes. Compatibility names may be
retired only after shipped consumers have migrated.
