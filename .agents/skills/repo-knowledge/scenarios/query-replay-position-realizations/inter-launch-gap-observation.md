# Ordinary work inside a multi-launch replay envelope

Observed 2026-09-21 in the existing rank-0/device-6 export:
`/root/my-ascend-workspace/runs/qwen-mtp-device-service-20260921/device-length-v3/traceloom/mtp-device-length-service-20260921-v3-decode-rank0.perfetto.json.gz`.
Use the adjacent `-summary.json` for launch bounds; target/draft labels below
are that report's labels, not newly established model semantics.

## Observed in the artifact

- Raw `TASK` row 17926 is `aclnnMatmul_MatMulCommon_MatMulV2`, device 6,
  stream 46, globalTaskId 5855. Relative start is 410402057 ns
  (JSON `ts=410402.057` microseconds), duration 1021.160 microseconds.
  It is absent from `traceloom.timeline_event`.
- Launch 8 ends at 410387.036 us; launch 9 starts at 412466.978 us.
  This gap contains 83 named raw TASK slices and zero primary event slices.
- Exported replay-member arguments group launches 0--3, 4--7, and 8--11
  into replay units 0, 1, and 2 respectively.
- Across the nine inter-launch gaps inside those units, 855 named raw TASK
  slices have no primary event slices. The two gaps between units contain
  238 named raw TASK slices and 238 primary event slices.
- "Named" excludes raw fallback names `Task <numeric>`; these counts are
  not a denominator for complete raw-event coverage or additive duration.

Task-local reproducer and compact evidence:
`/workspace/traceloom-continuous-replay/build/timeline-gap-investigation/{audit.py,audit.json}`.

## Source diagnosis and evidence boundary

At main `1694706`, `flat_anchor_builder.cpp` builds per-device spans from
whole replay-unit launch events. With `skip_events_covered_by_replay_units`
(enabled by the CLI), `event_is_covered_by_replay_unit` suppresses TASK anchors
by full temporal containment, not exact graph-body membership.
`perfetto_replay_export.cpp` restores only exact launch/body members; the
ordinary exporter consumes tree atoms. Work between constituent graph launches
therefore has neither an ordinary anchor nor an exact member to render.

This source mechanism explains the within-unit versus between-unit signature.
The original analyzer commit and this artifact's event/anchor/role SQL rows
were **not verified**: its metadata points to an AugDB under
`/workspace/my-ascend-workspace/runs/qwen27-partition-serving/mtp-device-length-service-20260921-v3/traceloom/`,
which was unavailable in the investigation filesystem. Do not describe this
as a completed original-AugDB provenance audit.

For a repair, preserve atomic grammar protection separately from visible
ordinary work. Do not label temporally contained work as graph membership or
indiscriminately restore raw controls/envelopes. Audit
`evidence_role_decision_rows.cpp::replay_memberships_for_event` too: it can
mark temporal containment as exact when the enclosing replay has a composition
region. Add a multi-launch fixture with ordinary work between launches and
check projection coverage, ownership, and duplicate suppression independently.

## Implemented membership correction

`ReplayEventMembershipIndex` now supplies the same relation to anchor building,
auxiliary cost eligibility, and evidence-role audit. Exact units cover their
body tasks, matched MODEL_EXECUTE/NOTIFY_WAIT/NOTIFY_RECORD tasks, and synthetic
unit event. No temporal-containment fallback is used for exact units. Legacy
opaque replay envelopes keep their prior contract. Completion controls are
consumed through existing matched IDs; this does not strengthen an ordered
fallback into exact provider identity or assert that every provider emits the
same control chain.

Multi-launch compositions remain structural groupings. Their envelopes and
grammar protection are not event ownership: inter-launch ordinary anchors can
now occur within the protected region. Unknown non-member tasks continue to
follow the existing unknown-first classification policy, not an exporter-only
filter. Existing AugDBs require reanalysis, not just re-export.

Regression `native/tests/compat/replay_gap_projection_test.py` analyzes the
`exact_hlt` fixture with `inter_launch_eager.sql`: execute-stream work after
completion and concurrent work on a non-capture stream both retain one ordinary
anchor, non-protected role, and one non-replay Perfetto event. Exact graph bodies
and launch geometry remain unchanged. The regression fails on pre-fix main
`1694706` at the missing-anchor assertion. The native composition test separately
checks matched-control protection and auxiliary cost eligibility.

Bounded forward check on the September-17 dualbank-swe1 candidate and baseline
rank-0 full profiles preserved all 16,177 and 7,749 exact graph members and their
exported geometry. It restored 1,830 and 915 ordinary anchors respectively;
these are not all compute kernels (the candidate additions include provider
AllReduce plus previously envelope-hidden NOP and MEM_WAIT_VALUE observations).
No new classification rules were introduced. Evidence and commands are in
`build/timeline-gap-investigation/{verify-real.py,real-receipt.json}`. This is
not a reanalysis of the September-21 MTP artifact above.
