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
