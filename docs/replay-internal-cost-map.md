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
  the member level and are only aligned later under an explicit structural
  key.
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
  symbol, raw task id).
- `aggregates`: `ReplayAlignedCostAggregateRow` rows aligned under the stable
  structural key (see below) with occurrence counts and duration
  distributions.
- `issues`: explicit `ReplayInternalCostIssue` rows for missing/ambiguous
  evidence, missing identity, order gaps, and lane inconsistencies.
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

### Aligned aggregates and fail-closed alignment

An aggregate exists only under the explicit stable structural key:

```
(replay graph template, device, composition slot role, body template,
 stream id, within-stream position, compatible member identity)
```

- Identity is part of the key, so inconsistent identity can never fuzzy-pair;
  a different identity at the same position produces a separate aggregate.
- If aligned members disagree on `kind` or `lane_ordinal`, the aggregate row
  is kept with `kind_consistent`/`lane_consistent` flags and the duration
  distribution is suppressed (`distribution_supported = false`, statistics
  zeroed): fail closed rather than presenting misleading aligned statistics.
- Aggregates report `member_occurrence_count`, `replay_unit_count`, and
  `launch_member_count`, and p25/median/p75 duration statistics over the
  aligned member durations.

## Cost lenses and epistemic boundaries

Body-level lenses are reused from the existing overlap-aware
`build_graph_body_cost_summary`:

- `task_sum_ns`: sum of member durations — scheduled-work evidence. Members
  may overlap, so this is not a wall-clock duration.
- `busy_union_ns`: overlap-safe union of member intervals — removes
  cross-stream double counting.
- `envelope_ns`: observed span from the earliest member start to the latest
  member end — includes gaps.
- `compute_ns`, `communication_ns`, `data_move_ns`: kind lenses summed over
  members of that kind.

These lenses are **not additive or interchangeable**; the JSON surface states
this explicitly. Per-stream rows (`ReplayStreamCostRow`) expose the same
discipline at stream granularity: `task_sum_ns` is the sum of that stream's
member durations and `busy_union_ns` is the overlap-safe union of that
stream's member intervals. Member rows also carry `relative_start_ns` /
`relative_end_ns`, which are a pure re-labeling of provider timestamps
relative to the body's earliest member start — not an invented
wall-clock attribution. No other internal wall-clock attribution is produced.

Member durations are scheduled-work evidence. Aligned aggregates describe
per-position duration distributions; they do not claim that the aligned
members ran concurrently or that their durations add to a wall-clock total.

## Support / reason model

A launch member is `supported` only when every structural step resolves
unambiguously: a valid composition slot, a slot with a valid body template,
exactly one graph-launch body for the occurrence, a body whose template
matches the slot, and body member evidence. Otherwise the member carries
`supported = false` with an explicit `reason_code`:

| reason code | meaning |
| --- | --- |
| `missing_replay_composition_slot` | launch member references an invalid slot |
| `slot_missing_body_template` | slot has no valid body template |
| `missing_graph_launch_body` | occurrence has no graph-launch body |
| `ambiguous_graph_launch_body` | occurrence has more than one body (never picks one) |
| `body_template_mismatch` | observed body template differs from slot template |
| `missing_occurrence_cost_evidence` | body exists but cost summary row is absent |

Issue-only codes (do not revoke support, but are reported explicitly):

- `invalid_body_member_reference`, `missing_member_identity`,
  `duplicate_within_stream_position`, `stream_lane_inconsistency`,
  `member_order_gap`, `orphan_replay_unit_launch_member`,
  `empty_replay_unit`, `no_replay_units`.

A unit is `supported` only when every launch member is supported; units with
zero launch members are explicit `empty_replay_unit` results.

## JSON surface

`traceloom --out PATH` (native result JSON) now includes the
`replay_internal_cost_map` section with `units` (nested launch members and
per-stream rows), flat `members`, `aligned_aggregates`, `issues`, and
result-level counts/reasons. The section is `null` when no native IR is
available. Sidecar/report publication of the map is intentionally not part of
this slice; the analysis API and the native JSON surface are the contract.

## Boundaries

The map does not:

- flatten multi-launch ReplayUnits or assume ReplayUnit/body-template 1:1;
- infer workload semantics or provider-specific meaning;
- invent wall-clock attribution beyond the overlap-safe lenses above;
- compare across profiles or rank devices (see
  `graph_body_performance_comparison` for cross-profile comparison);
- pair occurrences across units by index or guesswork.
