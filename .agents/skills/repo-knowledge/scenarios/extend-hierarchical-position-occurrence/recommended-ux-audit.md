# Recommended timeline UX: bounded audit, 2026-09-22

Use before aligning public onboarding/query recipes with the cleaned common-plane
Perfetto view. These are observations at main `57585d9`, not a new contract or
an implementation plan already approved by Fletcher.

## Evidence and limits

Four `decode/mixed × rank0/rank1` AugDBs under
`/root/my-ascend-workspace/runs/qwen-mtp-gdn-fusion-20260921/traceloom-mc2-denoised/`.
Read-only audit script and results: repository `build/ux-audit/{audit.py,results.json}`.
Each database's 43 recipe examples compiled with nullable test bindings. Selected
outer cycle -> occurrence -> members/edge costs/typed host windows and replay
domain -> layer -> occurrence -> four direct children worked. Device evidence
from both routes resolved to one literal embedded TASK row per sampled event.
This is sampled route verification, not execution of every possible population.

## Confirmed gaps

- `docs/composable-analytical-projections.md` first Position SQL uses nonexistent
  `parent_position_id`; the actual display field is `display_parent_position_id`.
  The agent playbook's column list works, but its literal `position-N286` example
  is not the outer `node-N286` namespace. Obtain handles from query results.
- Perfetto documentation filters `args.database_index` and `args.view_name`.
  Replay device events instead carry `db_idx` and omit `view_name`. On each
  decode trace those two predicates retain 1,850 of 9,728 device events; on each
  mixed trace 1,617 of 10,746. All replay device events are lost. Test predicates
  against actual device IDs too; these captures use devices 6/7, not example 0.
- Continuation metadata suggests outer `hpo_members` for replay Occurrence IDs:
  a sampled layer returns zero there, versus four children in
  `replay_hpo_members`, on all four DBs. Coordinate-kind compatibility is not
  enough to establish a supported domain transition.
- `replay_hpo_occurrences` does not advertise the working concrete-evidence
  bridge `replay_body_pattern_members`. That compatibility recipe supplies
  `launch_id`, exact member/event IDs and timestamps; replay HPO occurrences
  alone have aggregate durations, not a concrete launch/time interval.
- README/workflow/tour still teach legacy `node_id + occurrence_idx`, outer
  Repeat selection and the fixed `Rep x24` fixture. Compatibility works, but
  this is not the new view's end-to-end recommended route.
- Public evidence-role documentation says every auxiliary row retains cost
  attribution. MC2 lifecycle detail now uses `retained_as_evidence` instead.
  Display hiding, canonical exclusion, exact membership and raw retention are
  distinct decisions and must not be presented as one filter.
- Perfetto docs say ambiguous/unpaired AivKernel remains visible, while the
  current display policy unconditionally hides normalized AivKernel (and
  KERNEL_AICPU / SQE suffixes) on the primary event plane.

## Source-inspected limitations, not additional runtime experiments

`perfetto_distributed_export.cpp` still reads outer published atom occurrences,
not the single-rank common-plane replay expansion. It is not an equivalent
rank-wise rendering of the new recommended view. `device_event_display_name`
loads the exporter's current default symbol rules, not the AugDB's effective
custom policy. Do not promise identical display/analysis normalization for
custom rules or reproducible rendering across exporter versions.

The HPO model remains useful. Preserve exact contextual populations, separate
outer and replay coordinates, require concrete launch identity for replay timing,
and keep display envelopes (including attention's AllReduce extension) distinct
from structural membership and additive cost. A cycle candidate is not a proven
scheduler step; equal `layer`/`attention` labels do not establish one population.
