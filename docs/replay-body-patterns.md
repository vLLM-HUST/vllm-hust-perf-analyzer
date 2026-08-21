# Recursive Replay-Body Positions

An opaque exact graph-replay interval is not sufficient for fine-grained
analysis when its body contains an entire decode step. TraceLoom therefore
applies recursive grammar recovery to the ordered terminal Positions inside
each supported exact replay body, then lowers the result into the same
**Hierarchical Position--Occurrence (HPO)** model used by the ordinary device
timeline. The output exposes reusable Positions, measured Occurrence
populations, direct membership, cost distributions, and source lineage in the
self-contained analysis database.

Grammar patterns and productions are construction mechanisms, not peer
analytical entities. TraceLoom may expose an exact repeated Position without
naming it a transformer layer. A human or agent can map the recovered shape to
a model configuration using external evidence.

## Evidence domain

The replay-internal cost map first aligns exact members by terminal Position.
Grammar recovery then analyzes each complete domain independently:

```text
(graph template, device, composition-slot role, aggregation scope,
 replay-body template, stream)
```

Within a domain, `within_stream_position` supplies the exact horizontal order.
The grammar alphabet is the pair `(identity, member kind)`, so a compute event
and a synchronization event never alias merely because their provider-visible
names match. TraceLoom does not invent a total order between streams.

The terminal sequence enters the same deterministic grammar state machine used
for the ordinary device timeline. Nested `Atom`, `Sequence`, and `Repeat`
productions are then lowered to hierarchical Positions. Cyclic sequences can
have several equivalent phase boundaries; TraceLoom publishes the
deterministic phase supported by its grammar, not a hand-picked model boundary.

## Queryable HPO relations

The canonical relations are:

| relation | meaning |
| --- | --- |
| `traceloom_replay_body_pattern_run` | support and typed outcome of replay-body grammar recovery |
| `traceloom_replay_body_pattern_domain` | one exact per-stream analysis domain and grammar size |
| `traceloom_v_replay_body_position_definition` | one reusable hierarchical Position definition |
| `traceloom_replay_body_position_refinement` | one locally ordered child-Position slot |
| `traceloom_v_replay_body_position_occurrence` | one measured Position Occurrence and its cost summary |
| `traceloom_v_replay_body_position_direct_member` | one direct child Occurrence or terminal aligned Position |
| `traceloom_replay_body_position` | one terminal aligned identity/kind Position and cost distribution |
| `traceloom_replay_body_pattern_issue` | a typed unsupported or rejected boundary |

The drill-down path is:

```text
domain
  -> hierarchical Position
  -> ordered child slots
  -> one or all Occurrences
  -> direct child Occurrences or terminal Positions
  -> exact replay members
  -> embedded profiler source rows
```

Start by checking support and selecting the largest exact domain:

```sql
SELECT domain_id, graph_template_id, replay_body_template_id, stream_id,
       position_count, grammar_description_element_count,
       grammar_description_ratio
FROM traceloom_replay_body_pattern_domain
WHERE support_status = 'supported'
ORDER BY position_count DESC;
```

Read the reusable Positions and their measured populations:

```sql
SELECT p.position_id, p.position_kind, p.label, p.repeat_count,
       p.loop_depth, p.occurrence_count,
       MIN(o.duration_median_sum_ns) AS min_occurrence_median_sum_ns,
       AVG(o.duration_median_sum_ns) AS avg_occurrence_median_sum_ns,
       MAX(o.duration_median_sum_ns) AS max_occurrence_median_sum_ns
FROM traceloom_v_replay_body_position_definition p
LEFT JOIN traceloom_v_replay_body_position_occurrence o
  ON o.position_id = p.position_id
WHERE p.domain_id = :domain_id
GROUP BY p.position_id
ORDER BY p.loop_depth, p.display_depth, p.position_id;
```

Select one Occurrence and inspect only its direct members:

```sql
SELECT slot_ordinal, member_order, member_kind,
       child_position_id, child_occurrence_id,
       terminal_position_id, terminal_identity, terminal_kind,
       terminal_aggregate_id, duration_median_ns
FROM traceloom_v_replay_body_position_direct_member
WHERE parent_occurrence_id = :occurrence_id
ORDER BY slot_ordinal, member_order;
```

For a terminal member, use its `terminal_aggregate_id` with the existing replay
cost/member surfaces. The compatibility
`traceloom_v_replay_body_pattern_source_locator` view remains available when a
complete terminal-span audit is more convenient than one aggregate or event at
a time.

The projection catalog exposes the canonical route as
`replay_hpo_positions -> replay_hpo_refinements -> replay_hpo_occurrences ->
replay_hpo_members`. Returned Position and Occurrence IDs are declared reusable
coordinates. Terminal members return a replay-cost aggregate coordinate that
continues through exact members to `event_audit`.

## Cost and compression meanings

Each terminal Position retains its aligned p25, median, and p75 scheduled-
duration statistics. An Occurrence's `duration_*_sum_ns` columns are sums of
those aligned terminal statistics over its exact span. They are useful
composable scheduled-work lenses, but they are not end-to-end quantiles and do
not claim wall-clock elapsed time across streams.

`grammar_description_element_count` is the number of live grammar nodes plus
macro definitions. `grammar_description_ratio` is
`position_count / grammar_description_element_count`; it describes structural
compression of the terminal sequence, not SQLite file compression.

## Fail-closed boundaries

Hierarchical Position definitions are published only when the source replay-
cost domain is complete and its terminal Position sequence is dense,
zero-based, identity-valid, and kind-consistent. Occurrences of one Position
must realize the same ordered slots, and every repeat iteration must realize
the same body-slot definition. A partial or resource-limited grammar run is not
presented as an exact recovered structure. Unsupported graph-level traces,
malformed domains, and oversized partial discoveries remain explicit
run/domain/issue rows rather than empty successful answers.

The older `traceloom_replay_body_pattern_definition`,
`traceloom_replay_body_pattern_occurrence`, and
`traceloom_v_replay_body_pattern_*` relations remain compatibility storage and
views. Their IDs are aliased as Position IDs by the canonical HPO views; new
analysis code should not promote them back into a separate Pattern entity.

See [Replay-Internal Cost Map](replay-internal-cost-map.md) for the authoritative
member-alignment contract,
[Hierarchical Position--Occurrence](hierarchical-position-occurrence.md) for
the structural model, and
[Composable Analytical Projections](composable-analytical-projections.md) for
the general coordinate UX.
