# Recursive Replay-Body Patterns

An opaque exact graph-replay interval is not sufficient for fine-grained
analysis when its body contains an entire decode step. TraceLoom therefore
applies its existing recursive grammar recovery to the ordered Positions inside
each supported exact replay body. The result exposes repeated full-body
structure, occurrence populations, fine-grained cost distributions, and source
lineage in the same self-contained analysis database.

This is structural recovery, not model interpretation. TraceLoom may expose an
exact repeated region without naming it a transformer layer. A human or agent
can map the recovered shape to a model configuration using external evidence.

## Evidence domain

The replay-internal cost map first aligns exact members by Position. Pattern
recovery then analyzes each complete domain independently:

```text
(graph template, device, composition-slot role, aggregation scope,
 replay-body template, stream)
```

Within a domain, `within_stream_position` supplies the exact horizontal order.
The grammar alphabet is the pair `(identity, member kind)`, so a compute event
and a synchronization event never alias merely because their provider-visible
names match. TraceLoom does not invent a total order between streams.

The Position sequence enters the same deterministic grammar state machine used
for the ordinary device timeline. Nested `Atom`, `Sequence`, and `Repeat`
nodes are therefore recovered by one algorithm rather than by a graph-specific
fixed-block detector. Cyclic sequences can have several equivalent phase
boundaries; TraceLoom publishes the deterministic phase supported by its
grammar, not a hand-picked model boundary.

## Queryable relations

The following relations are materialized into every output database:

| relation | meaning |
| --- | --- |
| `traceloom_replay_body_pattern_run` | support and typed outcome of replay-body recovery |
| `traceloom_replay_body_pattern_domain` | one exact per-stream analysis domain and grammar size |
| `traceloom_replay_body_pattern_definition` | one recursive structural definition |
| `traceloom_replay_body_pattern_occurrence` | one realized Position span and its cost summary |
| `traceloom_replay_body_position` | one aligned identity/kind Position and cost distribution |
| `traceloom_replay_body_pattern_issue` | a typed unsupported or rejected boundary |

Convenience views provide the normal drill-down path:

```text
domain
  -> pattern definition
  -> one or all occurrences
  -> aligned Positions
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

Read the recursive patterns and their occurrence-cost populations:

```sql
SELECT pattern_id, node_kind, label, repeat_count, loop_depth,
       occurrence_count, positions_per_occurrence,
       min_occurrence_median_sum_ns,
       avg_occurrence_median_sum_ns,
       max_occurrence_median_sum_ns
FROM traceloom_v_replay_body_pattern
WHERE domain_id = :domain_id
ORDER BY loop_depth, display_depth, pattern_id;
```

Select one occurrence and audit its members back to embedded raw evidence:

```sql
SELECT position_ordinal, identity, kind, duration_ns,
       source_table, source_key, raw_json
FROM traceloom_v_replay_body_pattern_source_locator
WHERE occurrence_id = :occurrence_id
ORDER BY position_ordinal, member_id, source_ordinal;
```

The projection catalog exposes the same route as
`replay_body_domains -> replay_body_patterns ->
replay_body_pattern_occurrences -> replay_body_pattern_positions` or
`replay_body_pattern_members`. Returned IDs are declared reusable coordinates;
each member returns an `event_id` that continues to `event_audit`, and agents
do not need to rediscover joins from SQL text. The direct source-locator view
above is available when a complete occurrence audit is more convenient than
one-event-at-a-time navigation.

## Cost and compression meanings

Each Position retains its aligned p25, median, and p75 scheduled-duration
statistics. An occurrence's `duration_*_sum_ns` columns are sums of those
aligned Position statistics over its exact span. They are useful composable
scheduled-work lenses, but they are not end-to-end quantiles and do not claim a
wall-clock elapsed time across streams.

`grammar_description_element_count` is the number of live grammar nodes plus
macro definitions. `grammar_description_ratio` is
`position_count / grammar_description_element_count`; it describes structural
compression of the sequence, not SQLite file compression.

## Fail-closed boundaries

Pattern definitions are published only when the source replay-cost domain is
complete and its Position sequence is dense, zero-based, identity-valid, and
kind-consistent. A partial or resource-limited grammar run is not presented as
an exact recovered pattern. Unsupported graph-level traces, malformed domains,
and oversized partial discoveries remain explicit run/domain/issue rows rather
than empty successful answers.

See [Replay-Internal Cost Map](replay-internal-cost-map.md) for the authoritative
member-alignment contract and
[Composable Analytical Projections](composable-analytical-projections.md) for
the general coordinate UX.
