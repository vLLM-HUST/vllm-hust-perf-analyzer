# Run a canonical real-profile analysis

Use this scenario when a real Ascend profile must become a reproducible
TraceLoom baseline, when a DB-only result is being compared with a full
`PROF_*` input, or when host-activity projection threatens to dominate
materialization.

## Separate input identity from analyzer semantics

A monolithic `msprof_*.db` can carry enough TASK/CANN_API rows for bounded
structural analysis while still being incomplete cross-layer evidence. The
`ascend_full_profile_v1` input contract requires neighboring regular files:

- `host/sqlite/runtime.db`;
- `host/sqlite/stream_info.db`;
- at least one `device_*/sqlite/ascend_task.db`.

The analyzer records the result in `traceloom_metadata` as
`input_evidence_contract`, `input_scope`, `input_evidence_state`, and
`input_missing_components`. A DB copied away from its profile container must
remain `monolithic_db_only` / `evidence_incomplete`; do not compare it with a
full-profile run as an equivalent input. This classification states that the
required components are present, not that every optional capability or
semantic inference is supported.

For a canonical baseline, bind the raw file inventory and analyzer Git commit
before execution. Select summary fields before reading results, run the exact
analyzer commit twice, and compare deterministic summaries. Do not use an old
report to supply expected counts.

## Guard interval/activity expansion before materializing it

`traceloom_anchor_host_activity` is a many-interval-to-many-call projection.
Its size is not bounded by either the anchor count or runtime-call count:
ordered anchor endpoints can span large portions of a thread or process
runtime timeline. Always inspect these metadata keys before consuming the
activity or API-summary tables:

- `anchor_host_activity_materialization_state`;
- `anchor_host_activity_candidate_upper_bound`;
- `anchor_host_activity_materialization_limit`.

The accepted implementation computes a conservative range-size upper bound
before emitting any activity row. When it exceeds the configured limit, it
withholds the complete global activity and summary projection, changes every
otherwise-supported interval to
`supported_ordered_activity_withheld_size_limit`, and records
`withheld_candidate_upper_bound_exceeds_limit`. Empty activity tables in that
state mean **withheld**, never “no host calls.” Runtime calls, endpoint
relations, and typed host intervals remain available.

The projection is all-or-nothing by design. Do not emit an arbitrary prefix of
intervals or calls: that would look like complete observed evidence. A future
interval-local query path can recover selected activity without restoring an
unbounded global expansion.

## Bounded exp001 observation (2026-08-17)

Observed input: `exp_001_overload_profiler`, raw source inventory SHA-256
`68da897bc5273d87dca91ab1943c359775d10f72229872ca26539a5905b2c207`,
monolithic DB SHA-256
`1e7537088a303140c5a5c43f5a2d04f6f0dce3047cd822aa8eeb251f78ad7215`.

Observed before the guard at TraceLoom `d0c62f0`:

- input loading completed in 6.625 s and anchor construction in 3.841 s;
- the run did not finish in 8 minutes;
- an owned-process stack was inside `AnchorHostActivitySqlRow::push_back`;
- a raw-SQL audit over all task endpoints estimated 12,767,955,435
  same-process plus 1,751,247,794 same-thread candidate rank spans.

Observed with the guard in the immediate repair tree:

- the analyzer's selected-anchor upper bound was 3,368,005,894 rows against a
  1,000,000-row limit;
- host activity/summary was typed withheld, not partially emitted;
- the full 3,060,101,120-byte queryable DB completed in 108.596 s;
- the structural baseline contained 415,856 events, 118,854 anchors, 340 replay
  units, one complete semantic tree, and 56,166 unknown-first anchors retained
  for attribution.

These are bounded observations for the named bytes and analyzer lineage, not
universal performance or cardinality guarantees. Freeze the exact repair
commit and repeat before promoting them to a repository golden.

## Verify a change

1. Unit-test complete, partial, and isolated-DB input classification.
2. Exercise a small supported interval and require exact activity and API
   summary rows.
3. Force the activity limit below the same fixture's candidate count; require
   zero partial activity/summary rows, typed interval states, and metadata
   counts/limit.
4. Run the complete native test preset.
5. On the bounded real profile, enforce wall-time and output-size limits, query
   the input/activity metadata first, and preserve a receipt for any timeout.
6. Only after a clean exact-commit run succeeds, compare two independently
   generated canonical summaries and one byte-identical DB-only input run.
