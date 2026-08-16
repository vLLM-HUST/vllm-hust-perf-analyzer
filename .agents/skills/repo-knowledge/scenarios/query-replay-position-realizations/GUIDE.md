# Query replay Position realizations

Use this scenario when adding or consuming a projection that must show the
exact members realized inside one recovered replay Position, especially when
operators and collectives must share a visual or analytical plane.

## Preserve both structural truths

Atomic replay protection and nested member identity answer different
questions. `final_role=protected_boundary` protects the outer flat grammar;
it must not erase the member's pre-protection `policy_role` or effective
structural participation. Read both from
`traceloom_evidence_role_decision` rather than inferring identity from a task
name or replacing the final role.

There are also two legitimate orderings:

- grammar/body order is lane-local and must not invent a cross-stream order;
- a Position realization may expose all exact members in deterministic
  `(start_ns, end_ns, lane_ordinal, task_ordinal, member_id)` order, but that
  order is observed timestamp geometry, not dependency or causality.

Keep the original stream, lane, and task coordinates beside observed order and
carry an explicit observation-semantics marker into public projections.

## Bind one realization exactly

Drive inspection from a selected `launch_id`. Join graph-body membership with
both `launch_id` and `member_id`, plus `db_idx` and `device_id`; a member
identifier alone does not express the intended realization boundary. Avoid
using a tree-node expansion as the canonical member population because the
same exact member can legitimately appear through multiple structural
ancestors.

For each returned member, retain:

- its outer Position anchor and replay/slot coordinates;
- exact graph-body membership and interval containment state;
- policy and final role lineage;
- source table/row evidence;
- original stream/lane/task coordinates and deterministic observed order.

Unsupported prerequisites should remove or withhold the view rather than
materialize plausible partial rows.

## Flatten the observation plane, not the ownership model

When ordinary anchors, provider communication operations, replay Positions,
and exact replay-body members must share one plot, do not create a generic
"replay relation" or infer replay membership from timestamp overlap. Use two
explicit resolutions instead:

- `traceloom_v_annotated_anchor_timeline` retains exactly one row per
  `traceloom_anchor`. Exact replay intervals are nullable background-region
  coordinates with
  `replay_annotation_semantics=region_highlight_not_event_ownership`.
- `traceloom_v_flattened_execution_timeline` replaces a supported Position
  anchor with its exact `traceloom_graph_body_member` rows and leaves every
  other mainline anchor at its observed timestamp. Pattern, occurrence, and
  Position coordinates remain columns on the expanded rows.

This split keeps `COMMUNICATION_OP` anchors on the provider's main timeline.
An anchor inside a replay highlight is observable in that region but is not a
graph-body member unless the exact launch/member chain says so. Conversely,
TASK-backed AIV collectives can appear as exact Position members without being
relabelled as `COMMUNICATION_OP` or changing their cost kind.

Region annotation must fail closed before range joining. Require supported
protected intervals, resolvable first/last anchor coordinates, exact boundary
times, and ordered disjoint regions per `(db_idx, device_id)`. If any check
fails, retain the mainline anchors, publish an unsupported reason, withhold
region identifiers, and do not expand Position members. A raw range join can
duplicate anchors under overlapping regions and silently violate the one-row
mainline grain.

The flattened plane is for geometry and rendering, not additive cost. Exact
body members may overlap across lanes, and their Position parent span is
deliberately removed only to avoid double-rendering. Keep
`duration_aggregation_semantics=flat_items_may_overlap_do_not_sum_without_cost_lens`
and use the dedicated cost surfaces for sums.

The owned implementation surfaces are
`native/src/compat/replay_position_views.*`; replay-specific catalog entries
belong in `native/src/compat/augmented_replay_projection_catalog.*` rather
than growing the general projection catalog.

## Keep filtered queries launch-local

A first implementation used window functions over every replay member. On
four approximately 6.8 GiB controlled AIV analysis databases, SQLite did not
push an outer `launch_id` filter through that view; a filtered query still ran
for more than one minute. Do not restore a global window without proving the
query plan and bounded latency on a production-sized database.

The accepted route uses launch-correlated predecessor/rank subqueries backed
by `idx_traceloom_replay_cost_member_observed` on
`(launch_id, db_idx, device_id, start_ns, end_ns, lane_ordinal,
task_ordinal, member_id)`. In the 2026-08-16 bounded check, four read-only
databases returned 112,840 rows across 8,680 launches in 4.101 seconds total.
Every launch had 13 members, one stable identity sequence per database, zero
adjacent overlaps, zero Position-containment failures, and zero missing exact
source endpoints. This is a performance and dataset-envelope observation, not
a general latency guarantee or dependency claim.

The flattened view also needs a bounded plan. SQLite initially reordered its
nested joins from the large event plane and a full AIV read did not complete
in 90 seconds. Preserve the selective
Position -> graph-body-member -> event join order (currently pinned with
`CROSS JOIN` and the exact launch/member indexes). On the accepted 2026-08-16
decisive AIV rank-0 database, the corrected full 36,304-row view counted in
2.657 seconds. Across all eight AIV/FFTS databases, annotation preserved all
7,119 anchors per database and flattening satisfied
`flat = anchors - 2,405 Positions + exact body members`; all 434
`COMMUNICATION_OP` rows remained mainline anchors outside the 65 supported
replay regions. Treat these as bounded dataset observations, not universal
latency or placement guarantees.

## Verify a change

1. Exercise a fixture where lane-major order differs from timestamp order and
   require dense deterministic observed order.
2. Check nested `policy_role` values and outer `final_role` independently.
3. Require exact launch membership, containment classification, and source
   evidence in materializer tests.
4. Inspect `EXPLAIN QUERY PLAN` or bounded wall time for a launch-filtered
   query and a full flattened-timeline query on a production-sized database
   whenever the view shape, join order, or index changes.
5. Require exact anchor-count preservation and
   `flat = anchors - expanded Positions + exact members`; inject overlapping
   replay regions in a fixture and require annotation/expansion to fail
   closed without duplicating mainline anchors.
6. Run the complete native test preset and the release build before handoff.
