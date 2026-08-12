# Split-fallback communication parity investigation

Date: 2026-08-04

## Question

Why does the split CANN fallback produce a much larger report tree than the
monolithic `msprof_*.db` path for the same TP2 profile, and do the extra rows
carry useful evidence rather than mere noise?

## Preregistered decision test

- **Desired outcome:** make the semantic anchor surface independent of CANN's
  monolithic versus split storage layout without discarding observed
  communication-task detail.
- **Working claim:** the entire anchor-count delta is caused by split CANN
  exposing device communication tasks without materializing their observed
  collective-operation envelope. The tasks should remain trace/auxiliary
  evidence beneath one collective anchor per observed operation.
- **Expected observation:** for TP2 rank 2, `3,655` split communication-task
  anchors collapse to `398` collective-operation anchors, changing `6,376`
  anchors to `3,119` while retaining all `AscendTask` events. The trace-event
  count should increase from `132,894` to `133,292` because the `398`
  operation envelopes become explicit events.
- **Falsifier:** operation evidence cannot be matched unambiguously to task
  groups, task-derived envelopes disagree with monolithic
  `COMMUNICATION_OP`, or graph/analysis-semantic changes beyond storage-layout
  provenance.
- **Budget:** one focused adapter change, synthetic layout-parity tests, and
  validation on both retained TP2 ranks before considering a broader IR or
  report-schema change.

## Baseline evidence

On TP2 rank 2, the split-minus-monolithic anchor delta is exactly `3,257`:

```text
split anchors                         6,376
monolithic anchors                    3,119
split communication task anchors      3,655
monolithic collective op anchors        398
delta = 3,655 - 398                   3,257
```

The split `AscendTask` plus `HCCLTaskSingleDevice` rows form `398` distinct
`(device_id, connection_id)` groups. Each group has exactly one observed
`HCCLOP` row, one stream, and 8--14 device tasks. For all `398` groups, the
task envelope `[min(start), max(end)]` has exactly the same duration as the
monolithic `COMMUNICATION_OP` row.

The raw `HCCLOP.begin/end` timestamps are not in the `AscendTask` timebase, so
they must not be projected directly onto the device timeline. They establish
operation identity; the linked device tasks establish observable geometry.

## Information judgment

The extra rows are not noise. They expose the internal task decomposition of a
collective (for example `SDMA`, `Notify Wait`, and `Write Value`) and therefore
can support task-type cost breakdown and future transport/topology analysis.
They are, however, the wrong granularity for the primary execution-structure
sequence. TraceLoom should preserve them as raw events and auxiliary evidence,
while using the observed HCCL operation as the primary collective anchor. This
matches the existing monolithic representation instead of deleting or hiding
the split-only evidence.

The split HCCL tables also contain richer transport evidence (`plane_id`,
local/remote rank, transport and link type, size, bandwidth, RDMA type, and
notify identity). TraceLoom does not yet materialize those fields into the
sidecar. They are a credible incremental-analysis surface, but are not needed
to repair the primary execution tree and should be added only with an explicit
evidence schema and query use case.

## Implemented normalization

The split adapter now treats `HCCLOP` (or, when the host table is unavailable,
`HCCLOpSingleDevice`) as observed operation-identity evidence. It joins that
identity to communication tasks by `(device_id, connection_id)` and uses the
task envelope and dominant task stream for device geometry. A collective is
materialized only when exactly one operation-evidence row and a non-empty,
positive-duration task envelope exist for the key; ambiguous or incomplete
evidence remains at task granularity rather than being guessed.

The resulting collective event carries the HCCL operation row as primary
provenance. All original `AscendTask` communication events remain in
`traceloom_event`; non-zero-duration task details continue through
`traceloom_aux_link` rather than entering the primary anchor sequence.

## Outcome

**REPRODUCED** on both retained TP2 ranks.

| Rank/device | Events | Anchors | Report nodes | Collective ops | Comm task events | Comm aux links |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2 | 133,292 | 3,119 | 157 | 398 | 3,655 | 3,646 |
| 3 | 236,946 | 3,115 | 155 | 398 | 3,655 | 3,646 |

For each rank, split and monolithic outputs now have:

- identical event, anchor, and report-node cardinality;
- identical anchor `(role, symbol, count)` histograms;
- identical complete structural report-node rows (identity/path, kind, label,
  depth, repeat/occurrence/anchor cardinality, and anchor span); and
- identical collective-duration multisets and aggregate duration.

The nine communication tasks not present in `traceloom_aux_link` have zero
duration; they remain auditable in `traceloom_event` but correctly contribute
no cost. Rank 3 still reports `analysis_status=invalid_input` because of its
previously identified 209 zero-duration unknown tasks. That is independent of
communication-anchor parity; rank 2 remains `ok`.
