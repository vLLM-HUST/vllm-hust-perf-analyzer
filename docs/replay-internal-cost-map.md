# Replay-Internal Cost Map

The replay-internal cost map exposes TraceLoom's first-class graph-replay
product surface as an auditable, replay-internal structure with fine-grained
cost analysis over exact provider-visible bodies.

## Structural contract

The map preserves, without flattening, the exact replay-internal hierarchy:

```
ReplayUnit (occurrence)
  -> ordered launch/composition slots   (replay_unit_launch_members)
    -> body template                    (ReplayBodyTemplateId)
      -> graph-launch body              (GraphLaunchBodyId)
        -> per-stream ordered members   (lane ordinal + within-stream ordinal)
          -> fine-grained costs and provenance
```

Non-negotiable properties:

- **No flattening.** A multi-launch ReplayUnit (for example H/L/T-style
  head/layer/tail compositions) keeps every launch member in its recorded
  order. Repeated slot roles (for example two layer slots) stay distinct at
  the member level.
- **No 1:1 assumption.** ReplayUnit and body template are never assumed to be
  in a one-to-one relationship. Every launch member resolves its own
  composition slot, its own body template (from the slot), and its own
  observed graph-launch body (from the occurrence). The slot's expected
  template and the observed body template are compared and a mismatch fails
  closed.
- **No workload inference.** The analyzer never decides what a replay
  "means". It consumes only NativeIr tables and preserves provider-visible
  identity: raw stream ids, lane/within-stream ordinals, task/event/source
  ids, kind classification, and identity symbols. It is indifferent to
  provider (Ascend, CUDA, fixture) and to workload semantics.

## Query / result contract (C++)

`build_replay_internal_cost_map(const NativeIr& ir)` in
`native/include/traceloom/analysis/replay_internal_cost_map.h` returns a
`ReplayInternalCostMapResult`:

- `units`: one `ReplayUnitCostBlock` per ReplayUnit occurrence, with
  `launch_members` (one `ReplayLaunchMemberCostRow` per ordered launch
  member), resolved/supported counts, and per-unit reason codes.
- `members`: flat fine-grained `ReplayMemberCostRow` rows (task/event/source
  identity, kind, duration, absolute and body-relative timing, identity
  symbol, raw task id, scheduled-work share).
- `aggregates`: `ReplayAlignedCostAggregateRow` rows aligned under the stable
  role-collapsed structural key (see below) with occurrence counts, duration
  distributions, and scheduled-work shares.
- `issues`: explicit `ReplayInternalCostIssue` rows for missing/ambiguous or
  structurally invalid evidence, missing identity, order gaps, and
  lane inconsistencies.
- `result_reason_codes`: result-level reasons (for example
  `no_replay_units`) so an empty result is never an empty success.

### Deterministic ordering

- Units: table order (ReplayUnit id order).
- Launch members: recorded `member_order` ascending, tie-broken by member id.
  A non-contiguous `member_order` sequence raises a `member_order_gap` issue
  but does not reorder evidence.
- Body members: `(lane_ordinal, within-stream position, start_ns, body member
  id)` — this is the exact provider-visible per-stream sequence.
- Aggregates: structural key order.
- Issues: deterministic encounter order.

### Aligned aggregates: role-collapsed scope and the drill-down contract

An aggregate exists only under the explicit stable structural key:

```
(replay graph template, device, composition slot role, body template,
 stream id, within-stream position, compatible member identity)
```

Every aggregate row carries the explicit aggregation scope
`aggregation_scope = role_collapsed`: the map surface intentionally merges
the structural family of repeated slot roles (for example the Lx35 layer
family) into one aligned family per role/body/stream/position/identity.
`launch_member_count` preserves the multiplicity contributed by every slot
position, so the family's breadth is never hidden.

Drill-down to one particular slot is the **member-row contract**: exact
member rows retain the replay-unit occurrence, the composition slot id, the
`slot_order`, the body id/template, stream, within-stream position, identity,
and source provenance for every launch member. No evidence is lost at the map
surface; a later bounded slice may materialize the same rows into the
compatibility sidecar / augmented database.

- Identity is part of the key, so inconsistent identity can never fuzzy-pair;
  a different identity at the same position produces a separate aggregate.
- If aligned members disagree on `kind` or `lane_ordinal`, the aggregate row
  is kept with `kind_consistent`/`lane_consistent` flags and the duration
  distribution is suppressed (`distribution_supported = false`, statistics
  zeroed): fail closed rather than presenting misleading aligned statistics.
- Aggregates report `member_occurrence_count`, `replay_unit_count`, and
  `launch_member_count`, p25/median/p75 duration statistics over the aligned
  member durations, and the family scheduled-work share (below).

## Cost lenses and epistemic boundaries

Body-level lenses are computed locally from the map's own pre-validated
body membership with the same overlap-aware interval arithmetic as the
shared `build_graph_body_cost_summary` (which keeps its strict base
contract):

- `task_sum_ns`: sum of member durations — scheduled-work evidence. Members
  may overlap, so this is not a wall-clock duration.
- `busy_union_ns`: overlap-safe union of member intervals — removes
  cross-stream double counting.
- `envelope_ns`: observed span from the earliest member start to the latest
  member end — includes gaps.
- `compute_ns`, `communication_ns`, `data_move_ns`: kind lenses summed over
  members of that kind. When every member is classified they **partition the
  body's scheduled `task_sum_ns`**, so they are additive in that
  scheduled-work sense; they are **not** an additive wall-clock decomposition
  and are **not interchangeable** with `busy_union_ns` or `envelope_ns`.

Per-stream rows (`ReplayStreamCostRow`) expose the same discipline at stream
granularity: `task_sum_ns` is the sum of that stream's member durations,
`busy_union_ns` is the overlap-safe union of that stream's member intervals,
and the kind lenses partition that stream's scheduled task_sum. Member rows
also carry `relative_start_ns` / `relative_end_ns`, which are a pure
re-labeling of provider timestamps relative to the body's earliest member
start — not an invented wall-clock attribution. No other internal wall-clock
attribution is produced.

### Scheduled-work share

Each member row materializes an explicit scheduled-work share:

```
scheduled_work_share_ppm = member duration / owning body task_sum * 1,000,000
                           (integer parts-per-million, truncated)
scheduled_work_denominator_body_task_sum_ns = owning body task_sum   (exact)
```

The share is **scheduled-work evidence only** — it is never a wall-clock or
overlap-safe attribution. Per body, the member shares sum to 1,000,000 within
integer truncation. A zero denominator never manufactures a value:
`scheduled_work_share_supported = false` and the share stays zero.

Aligned aggregates materialize the same contract for the role-collapsed
family: `scheduled_work_share_ppm = sum of member durations / sum of owning
body task_sums * 1,000,000`, with
`scheduled_work_denominator_body_task_sum_ns = sum of owning body task_sums`.
The family share is unsupported (zero) whenever any aligned member's owning
body had a zero task_sum.

Member durations are scheduled-work evidence. Aligned aggregates describe
per-position duration distributions; they do not claim that the aligned
members ran concurrently or that their durations add to a wall-clock total.

## Support / reason model

A launch member is `supported` only when every structural step resolves
unambiguously and the body membership is complete and well formed: a valid
composition slot, a slot with a valid in-range body template, exactly one
graph-launch body for the occurrence, a body whose template is valid and
matches the slot, nonempty body membership with valid task/event references,
no duplicate (stream, within-stream position), no lane-inconsistent stream,
and body cost evidence. Otherwise the member carries `supported = false` with
an explicit `reason_code`:

| reason code | meaning |
| --- | --- |
| `invalid_unit_graph_template` | replay unit references an invalid/out-of-range graph template |
| `invalid_launch_occurrence` | launch member references an invalid/out-of-range occurrence |
| `missing_replay_composition_slot` | launch member references an invalid slot |
| `slot_missing_body_template` | slot has no valid in-range body template |
| `missing_graph_launch_body` | occurrence has no graph-launch body |
| `ambiguous_graph_launch_body` | occurrence has more than one body (never picks one) |
| `body_template_mismatch` | observed body template invalid or differs from slot template |
| `empty_graph_launch_body` | body has no member rows (never supported zero-cost) |
| `missing_body_member_evidence` | body membership has invalid task/event references |
| `duplicate_within_stream_position` | body repeats a (stream, position) key; no partial aggregates |
| `stream_lane_inconsistency` | a stream maps to multiple lane ordinals; per-stream sequence ambiguous |

Issue-only codes (do not by themselves revoke support, but are reported
explicitly):

- `invalid_body_member_reference` (including orphaned member rows whose body
  id is invalid/out of range — those rows are excluded from every body's
  membership and never reach aggregate keys), `invalid_member_identity`,
  `missing_member_identity`, `member_order_gap`,
  `orphan_replay_unit_launch_member`, `empty_replay_unit`, `no_replay_units`.

Structural foreign keys/ranges are validated before use: invalid sentinels or
out-of-range ids never enter aggregate keys. Malformed IR never raises an
exception from this analyzer: whole-body lenses are computed only from the
pre-validated membership of supported bodies, so partial evidence can never
masquerade as complete cost. The shared `build_graph_body_cost_summary`
keeps its strict base behavior unchanged.

A unit is `supported` only when every launch member is supported; units with
zero launch members are explicit `empty_replay_unit` results.

## JSON and augmented-SQL surfaces

`traceloom --out PATH` (native result JSON) now includes the
`replay_internal_cost_map` section with `units` (nested launch members and
per-stream rows), flat `members`, `aligned_aggregates` (with
`aggregation_scope: "role_collapsed"` and scheduled-work share fields),
`issues`, and result-level counts/reasons. The section is `null` when no
native IR is available.

The augmented database publishes the same authoritative result without
re-deriving it in SQL: `traceloom_replay_cost_{unit,launch,stream,member}` keep
exact occurrence costs, `traceloom_replay_cost_aggregate` keeps the aligned
role-collapsed distributions, `traceloom_replay_cost_aggregate_member` records
their exact contributors, and `traceloom_replay_cost_issue` preserves typed
negative results. `traceloom_v_node_replay_cost_member` composes these rows
with concrete Loop Tree occurrences and raw-source locators.

## Boundaries

The map does not:

- flatten multi-launch ReplayUnits or assume ReplayUnit/body-template 1:1;
- infer workload semantics or provider-specific meaning;
- invent wall-clock attribution beyond the overlap-safe lenses above;
- treat kind sums as an additive wall-clock decomposition;
- compare across profiles or rank devices (see
  `graph_body_performance_comparison` for cross-profile comparison);
- pair occurrences across units by index or guesswork.
