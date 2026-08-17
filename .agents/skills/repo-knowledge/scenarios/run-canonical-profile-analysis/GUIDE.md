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

## Never materialize the global interval/activity relation

`traceloom_anchor_host_activity` is a many-interval-to-many-call projection.
Its size is not bounded by either the anchor count or runtime-call count:
ordered anchor endpoints can span large portions of a thread or process
runtime timeline. The accepted invariant is stronger than a size guard: do not
materialize this global relation at all. Materialize runtime calls, auditable
endpoint relations, and typed host intervals; keep
`anchor_host_activity_materialization_state=query_time_only` and the legacy
activity/API-summary tables empty. A zero row count there means **not
materialized**, never “no host calls.”

Recover selected activity through `host_window_calls`,
`traceloom_v_anchor_host_activity`, or `scope_host_context`. These routes first
bind one interval or a selected structural scope and then use
`idx_traceloom_runtime_call_time(db_idx, provider, clock_domain, start_ns,
end_ns)` plus the interval's thread/process policy. Do not remove the selector
and treat an expressible full view as a safe artifact.

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

Observed with the predecessor size guard at TraceLoom `ac1c650`:

- the analyzer's selected-anchor upper bound was 3,368,005,894 rows against a
  1,000,000-row limit;
- host activity/summary was typed withheld, not partially emitted;
- the full 3,060,101,120-byte queryable DB completed in 108.596 s;
- the structural baseline contained 415,856 events, 118,854 anchors, 340 replay
  units, one complete semantic tree, and 56,166 unknown-first anchors retained
  for attribution.

These are bounded observations for the named bytes and analyzer lineage, not
universal performance or cardinality guarantees. They explain why the final
contract removed global activity materialization entirely; they are not
performance receipts for the query-time implementation.

## Bounded query-time projection observation (2026-08-17)

On the same exp001 bytes and raw inventory, a Release analyzer built from
parent `ac1c650` plus dirty runtime patch
`52f732d19e68455c49186e6edb002978dd82fda627f3d32a712abf9991afefd9`
completed in 104.980 seconds with a 3,058,196,480-byte DB. Metadata reported
`query_time_only`, zero materialized activity rows, and zero materialized API
summaries.

A late same-thread interval selected from 1,108,204 runtime calls returned its
two overlapping calls in 0.204 seconds; the readable view returned the same
count in 0.136 seconds, and both plans used
`idx_traceloom_runtime_call_time`. An initial bubble recipe still hid a global
aggregate behind a view and was interrupted after 30.185 seconds. Binding
`node-N028` first, forcing selected-position -> selected-bubble -> runtime-call
join order with `CROSS JOIN`, and adding the structural-position index changed
the same query to 0.003 seconds with both the position and runtime-time indexes.

This is a bounded implementation observation, not a universal latency or
canonical-determinism claim. Preserve the explicit CTE bounds, `CROSS JOIN`
ordering, and plan assertions when changing these recipes.

## Verify a change

1. Unit-test complete, partial, and isolated-DB input classification.
2. Require empty compatibility activity/API-summary tables and
   `anchor_host_activity_materialization_state=query_time_only`.
3. Exercise a selected supported interval and require exact query-time calls,
   half-open boundary behavior, database/provider/clock isolation, scope-policy
   filtering, deterministic observed order, and indexed query planning.
4. Run the complete native test preset.
5. On the bounded real profile, query one selected interval or structural scope
   with a wall-time/output limit and inspect `EXPLAIN QUERY PLAN`; never run the
   full activity view as a validation shortcut.
6. Only after a clean exact-commit run succeeds, compare two independently
   generated canonical summaries and one byte-identical DB-only input run.
