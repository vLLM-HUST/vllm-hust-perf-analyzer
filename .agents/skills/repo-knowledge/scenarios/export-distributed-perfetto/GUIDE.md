# Export or interpret distributed Perfetto lanes

Use this scenario when extending the Perfetto exporter with distributed
TraceLoom timelines or when a compressed rank view suggests a phase, skew, or
anomaly worth auditing.

## Current contract

The public contract is owned by `docs/augmented-perfetto-timeline.md`; read it
and the exporter source rather than copying behavior from this guide. The
important inquiry boundaries are:

- rank identity is caller-provided (`RANK=TIMELINE.db`), never inferred from
  paths, PID, or timestamps;
- every mapped DB must expose `traceloom_v_tree_node` and
  `traceloom_tree_node_occurrence`;
- the reference AugDB supplies the rank-0 structural tree and embedded raw
  evidence; distributed inputs replace its ordinary single flat atom row with
  one flat atom-occurrence lane per rank;
- `first_timeline_event_per_rank` translates each rank's first published atom
  occurrence onto rank 0's first atom occurrence and preserves later
  within-rank elapsed times and durations;
- `--distributed-clock-model MODELS.jsonl` is the recommended explicit opt-in
  for collective comparison when an auditable end-affine receipt exists; it
  keeps rank 0 as identity, maps every other display lane with its `metric=end`
  model, and retains raw source timestamps plus the receipt SHA-256;
- this is a display alignment, not proof of a shared absolute clock, semantic
  event equivalence, or cross-rank causality;
- every displayed event retains rank, timeline DB SHA-256, original timestamps,
  source node/occurrence coordinates, repeat context, anchor range, and
  composable cost fields so a visual hypothesis can be audited at source.

Stable TraceLoom event labels are intentional: Perfetto uses them as reusable
color seeds across all rank lanes. Keep a single track per rank while atom
occurrences are non-overlapping. Do not substitute a collective-only lane for
this view; communication is one natural part of the full event texture rather
than the comparison object.

## Clock-calibration recovery boundary

The retired idle/gap-analysis line contained a useful independent clock core.
Its original author is Luqhhh; the authoritative historical sequence is
`5c2e4e1`, `3b659e9`, `f1a1dc8`, then `21cefbd` on
`origin/feat/host-device-clock-calibration`. Current main recovers its robust
Theil--Sen affine fit, deterministic every-fifth holdout, uncertainty receipt,
and half-even mapping as `analysis/clock_calibration.*`, without restoring the
retired gap-explanation product around it.

Keep two boundaries explicit when applying that core to distributed traces:

- structural collective correspondence may create auditable candidate marker
  pairs, but timestamp proximity must never create correspondence;
- a candidate fit may diagnose constant offset or drift; it may drive only the
  explicit Perfetto display opt-in and cannot move production timestamps until
  marker identity is validated independently.

The current TP8 capture predates explicit per-rank marker receipts. It can
support a candidate offset/drift audit and motivate a future calibrated
capture, but the first-event display alignment must not be silently relabelled
as calibrated evidence.

### TP8 candidate clock audit (2026-08-26)

Evidence lives at
`window/traceloom/clock-audit-candidate-v1` under the bounded run named below.
Its manifest binds all eight raw DB paths, sizes, and SHA-256 values; the audit
uses the recovered C++ calibration core and labels every model
`candidate_only`. The exact family+group sequence contained 10,065/10,065
matching ordinals on every rank: 7,194 AllToAllV, 1,452 AllReduce, and 1,419
AllGather. A uniform 511-marker sample supplied the fit with deterministic
every-fifth holdout; all ordinals supplied full residual and spread checks.

Observed, without applying any fit, cross-rank collective-end spread was
already tight for AllReduce (p50 10.08 us, p95 14.65 us) and AllGather
(p50 13.34 us, p95 15.24 us). The end-derived candidate fit changed those
values only by a few microseconds and estimated per-rank reference offsets
between -1.85 and +1.55 us. AllToAllV was broader (raw end p50 22.30 us,
p95 716.74 us) and the fit did not improve it.

By contrast, after the same end-derived candidate mapping, collective-start
spread across all families had p50 416 us and p95 5.13 ms. Some ordinals ended
within roughly 10 us while their observed starts differed by tens or even
about 100 ms. This rejects a simple large constant rank-clock offset as the
explanation for the visual start mismatch and exposes a useful candidate
signal of rank-dependent collective entry/wait behavior. It does not yet prove
arrival time or waiting cost: explicit per-rank clock markers and provider
interval-semantics validation remain required before a paper claim or
production timestamp rewrite.

## Real TP8 observation (2026-08-26)

This is a bounded observation, not an accepted performance explanation.

- Input run:
  `/root/my-ascend-workspace/.runs/controller/dsv4-large-prefill-profile-20260826T004640Z`
- Reference AugDB: `window/traceloom/analysis.db` from rank 0.
- Rank 1--7 timelines: `window/traceloom-ranks/rank-N.sidecar.db`, generated
  from exactly one audited `msprof_*.db` for each rank with the same analyzer
  defaults. They occupy 6.8--7.1 GiB each; the current compatibility sidecar is
  not a lightweight way to retain only the tree.
- Analyzer state: working tree based on `63e86fc`; the distributed change was
  not yet a frozen publication commit when this observation was made.
- Export: `window/traceloom/distributed-tp8-all-events.perfetto.json.gz`,
  SHA-256
  `0e5b690824c648b77b8f4b4fff28248847083cc8ec0df39840195ef806c22374`.
- Receipt: 8 rank tracks and 1,766,212 distributed timeline slices. Per-rank
  counts are 196,001; 221,303; 221,303; then 225,521 for ranks 3--7. Export
  completed in 12:37.50 with about 853 MiB peak RSS; most time was spent
  reading and hashing roughly 55 GiB of timeline DBs. The 172 MiB gzip also
  contains the rank-0 tree and embedded raw-provider evidence.

Observed in a full-width compression of the first 40.5 seconds:

1. repeated broad execution-phase blocks and gaps appear on all eight lanes;
2. ranks 1--7 have closely related dense textures, while rank 0 is visibly
   sparser and differs at several boundaries;
3. a late long event-free region is shared by all lanes.

The differing atom counts and textures may reflect execution, capture/evidence
completeness, or reconstruction-boundary differences. A white region means no
published atom occurrence, not device idle. Do not turn these textures into a
paper claim without selecting an explicit structural or wave identity and
auditing the corresponding raw evidence.

The earlier
`distributed-tp8.collectives-only.exploration.perfetto.json.gz` is a preserved
mistaken exploration and is superseded for this use case.

### Reconcile before re-exporting published lanes

The Perfetto exporter consumes the published occurrences already materialized
in each AugDB. Updating the default event-reconciliation policy does not
retroactively change an older AugDB: regenerate every distributed input from
its raw profiler DB before expecting a new reconciliation rule to affect the
rank lanes. Raw provider tracks remain audit evidence and must not be erased
merely because their annotation envelopes are removed from the published
TraceLoom projection.

This distinction was exercised on the TP8 run above after policy v3 added
`ascend.task.mix-aic.context-detail`. All eight raw inputs were reanalyzed at
commit `3b85b90`; every resulting AugDB records policy v3 with manifest SHA-256
`072794b4ef490449932f90f1823136587d6f2c03797ea5e21d91cd6c17518f42`.
The rule reconciled 13,965 `KERNEL_MIX_AIC` envelopes on rank 0, 14,973 on
ranks 1--2, and 15,141 on ranks 3--7. Each AugDB then exposed zero published
tree nodes or occurrences named `KERNEL_MIX_AIC`.

The production-CLI opt-in re-export is
`window/traceloom/distributed-tp8-main-opt-in-end-aligned.perfetto.json.gz`,
SHA-256
`782e5fdc121b386e9ef0b4e3aeff1437c399c34b9131b157e2a9a4fc846777df`.
It was generated directly by the release `traceloom export-perfetto` CLI with
`--distributed-clock-model`; no post-export timestamp rewrite was used. The
seven-record end-model receipt has SHA-256
`0196a4f0f4f15652523d7bcc389c77eb29c74ad0adb5276bd8fc6fd9ea1fae91`.
It contains 1,646,596 distributed slices: 182,036 on rank 0; 206,330 on ranks
1--2; and 210,380 on ranks 3--7. A complete streaming JSON validation found
zero distributed slices named `KERNEL_MIX_AIC` or `KERNEL_MIX_AIV`, while all
slices retained the candidate alignment marker, raw timestamps, and model
digest. All 13,965 rank-0 mixed-AIC envelope source rowids were still reachable
as raw-provider TASK slices even though those slices use their resolved compute
names rather than `KERNEL_MIX_AIC` as the visible label. The 119,616-slice
reduction from the policy-v2 artifact exactly equals the reconciled mixed-AIC
envelope counts. This verifies projection denoising, not deletion of raw
evidence, and does not strengthen the candidate clock model into a
calibrated-time claim.

## Verification when changing the exporter

Run the focused alignment fixture, then the full suite and non-test build:

```bash
cmake --preset dev-tests
cmake --build --preset dev-tests -j 48
ctest --test-dir build/native-tests \
  -R traceloom_native_perfetto_exporter_tests --output-on-failure
ctest --preset dev-tests --output-on-failure
cmake --preset dev
cmake --build --preset dev -j 48
```

For a real run, verify exactly one completed timeline DB for every explicitly
named rank before export. After export, require `gzip -t` to pass, check the
receipt rank/slice counts, and preserve an artifact SHA-256. Generated
Perfetto, sidecar, and preview files belong with experiment evidence and must
not be committed to this repository.
