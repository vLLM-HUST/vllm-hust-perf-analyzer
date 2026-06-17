### Target repository

vllm-hust-perf-analyzer

### Problem statement

## Background

TraceLoom currently attaches prelude gaps and unattributed time around anchors to the compressed loop tree. This is useful because it shows where waiting or non-anchor time occurs in the model execution structure.

However, some of this time is currently reported as idle or prelude gap even when the device may have executable queued work but is blocked by synchronization, dependency, runtime scheduling, or host/device coordination behavior.

## Problem

The current analysis can locate unattributed gaps structurally, but it cannot always explain their cause.

In particular:

- device-side gaps may include synchronization waits, queue dependencies, runtime scheduling delay, or host/device coordination
- profiler-visible events may not directly expose all wait causes
- some waiting behavior is currently indistinguishable from generic idle time
- reports may make large `idle_us` or `prelude_gap_us` values visible without enough semantic breakdown

This is acceptable for a first version, but it limits how confidently users can interpret the root cause of large gaps.

## Proposal

Investigate engineering directions for finer-grained attribution of device-side idle/prelude gaps.

Potential directions include:

- correlating device timeline gaps with host runtime API events
- recognizing `aclrtSynchronize*`, stream/event wait, record, notify, or dependency-related runtime calls
- matching host-side blocking intervals to nearby device-side gaps
- distinguishing generic unattributed gaps from likely synchronization waits
- adding a separate category such as `sync_wait_us`, while keeping unresolved time as `unattributed_gap_us`
- preserving the current conservative behavior when no reliable signal is available

## Expected Outcome

TraceLoom should continue to report structural gap attribution, but with clearer semantics:

- confirmed compute time remains compute
- recognized communication/synchronization waits are broken out when possible
- unresolved gaps are reported with conservative naming such as `unattributed_gap_us`
- reports avoid overclaiming that all unattributed time is true hardware idle

## Notes

This does not need to fully solve synchronization attribution immediately. A useful first milestone is to rename and document unresolved gaps more conservatively, then incrementally carve out better-attributed categories as reliable signals become available.



### Proposed solution

## Possible Implementation Steps

1. Survey available msprof host/device tables for synchronization-related records.
2. Build a small set of traces where synchronization behavior is known or reproducible.
3. Add experimental correlation logic behind a flag.
4. Report both the old aggregate gap and any newly attributed `sync_wait_us`.
5. Document limitations clearly in the generated summary and README.

### Expected impact and tradeoffs

_No response_