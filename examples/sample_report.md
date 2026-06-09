# Sample TraceLoom Report

This is a representative report for documentation. Numbers are placeholders and
should be replaced by real analysis output for a published benchmark.

## Overview

- Workload: distributed LLM decode on Ascend NPUs.
- Input: CANN `msprof` SQLite output.
- Analyzer command: `traceloom analyze runs/qwen2-decode/msprof_raw --top-devices-global 4`.
- Output: augmented SQLite DBs, `summary.md`, `tree-map.md`, and SQL reports.
- Main finding: repeated decode patterns are dominated by matmul, fused
  attention, and collective anchors; short auxiliary events appear consistently
  before collective anchors.

## Top Kernels By Duration

| Rank | Device | Kernel family | Representative kernel | Total duration | Share |
| --- | ---: | --- | --- | ---: | ---: |
| 1 | 6 | matmul | `aclnnMm_MatMulCommon_MatMulV2` | 184.2 ms | 38.7% |
| 1 | 6 | attention | `FusedInferAttentionScore` | 96.4 ms | 20.2% |
| 1 | 6 | collective | `aiv_all_reduce_bfloat16_t` | 71.8 ms | 15.1% |
| 1 | 6 | norm | `AddRmsNormBias` | 29.6 ms | 6.2% |
| 1 | 6 | activation | `SwiGlu` | 24.1 ms | 5.1% |

## Repeated Patterns

| Node | Pattern summary | Repeats | Avg total | Avg aux | Diagnosis |
| --- | --- | ---: | ---: | ---: | --- |
| `N027` | norm -> matmul -> swiglu -> matmul -> allreduce | 35 | 142.8 us | 2.7 us | Dominant MLP decode pattern |
| `N041` | attention -> matmul -> allreduce | 35 | 88.1 us | 1.9 us | Attention/output projection pattern |
| `N052` | prelude -> allreduce | 35 | 27.4 us | 8.6 us | Communication-adjacent auxiliary work |

## Communication And Synchronization Analysis

- Collective anchors account for 15.1% of the sampled device time.
- Auxiliary/prelude cost is low per occurrence but repeats across decode
  iterations.
- The highest auxiliary share appears near `N052`, suggesting a synchronization
  or communication preparation region worth validating against raw profiler
  events.
- Current evidence is conservative: wait time that cannot be attributed to a
  concrete collective remains in idle/prelude buckets.

## Source / Operator Attribution

| Evidence | Likely source/operator area | Confidence |
| --- | --- | --- |
| `FusedInferAttentionScore` anchor | decode attention path | high |
| `aclnnMm_MatMulCommon_MatMulV2` anchors inside repeated MLP pattern | MLP projection layers | medium |
| `aiv_all_reduce_bfloat16_t` collective anchors | tensor-parallel all-reduce | high |
| repeated norm-family anchors | RMSNorm / QKV preparation | medium |

Attribution is based on profiler metadata, kernel names, anchor roles, and
repeated pattern context. Confirm source-level causes with workload code,
framework logs, and profiler metadata before making a production change.

## Optimization Hints

- Compare `N027` before and after MLP or tensor-parallel changes.
- Check whether collective timing is balanced across ranks and devices.
- Inspect auxiliary/prelude events attached to all-reduce anchors.
- Use `node-events.sql` to audit the raw profiler rows covered by `N052`.
- Track the same node-cost queries in CI once representative traces are small
  enough or stored in external artifact storage.

## Reproduction Notes

Keep the report reproducible by recording:

- TraceLoom version and command-line options;
- profile directory checksum or artifact identifier;
- device IDs and rank mapping;
- selected SQL query files;
- known unsupported profiler fields or missing communication tables.
