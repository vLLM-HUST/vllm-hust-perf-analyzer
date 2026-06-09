# Demo

This demo uses representative placeholder numbers. It is intended to show the
shape of a TraceLoom diagnosis without committing a large or private Ascend
profile database.

## Input

An Ascend/CANN `msprof` capture from a distributed LLM decode workload:

```text
runs/qwen2-decode/msprof_raw/
  PROF_000001_.../
    msprof_20260601.db
```

The trace contains:

- one rank-local `msprof_*.db`;
- task timing rows;
- kernel string metadata;
- communication task information;
- repeated decode iterations.

## Command

```bash
traceloom analyze runs/qwen2-decode/msprof_raw \
  --top-devices-global 4 \
  --out-dir out/qwen2-decode-analysis
```

## Output Bundle

```text
out/qwen2-decode-analysis/
  README.md
  summary.md
  tree-map.md
  meta.json
  db01.traceloom_augmented.db
  queries/
    repeat-overview.sql
    node-cost-breakdown.sql
    node-events.sql
```

## Example Findings

### Top Kernels

| Rank | Kernel family | Representative kernel | Total duration | Share |
| --- | --- | --- | ---: | ---: |
| 1 | matmul | `aclnnMm_MatMulCommon_MatMulV2` | 184.2 ms | 38.7% |
| 1 | attention | `FusedInferAttentionScore` | 96.4 ms | 20.2% |
| 1 | collective | `aiv_all_reduce_bfloat16_t` | 71.8 ms | 15.1% |
| 1 | norm | `AddRmsNormBias` | 29.6 ms | 6.2% |

### Repeated Patterns

| Node | Pattern | Repeats | Avg total | Notes |
| --- | --- | ---: | ---: | --- |
| `N027` | norm -> matmul -> swiglu -> matmul -> allreduce | 35 | 142.8 us | Decode MLP block |
| `N041` | attention -> matmul -> allreduce | 35 | 88.1 us | Attention/output projection |
| `N052` | short host/device prelude before collective | 35 | 9.6 us | Possible synchronization preparation |

### Suspected Bottlenecks

- Collective anchors consume a visible share of repeated decode-node time.
- Auxiliary/prelude events cluster before all-reduce anchors.
- Some communication fragments are short individually but repeat on every
  decode iteration.

### Source / Operator Attribution

| Kernel family | Likely operator area | Evidence |
| --- | --- | --- |
| matmul | MLP projection / output projection | normalized matmul anchor symbols |
| attention | fused decode attention | `FusedInferAttentionScore` kernel name |
| collective | tensor-parallel all-reduce | all-reduce kernel and communication role |
| norm | RMSNorm and QKV preparation | norm-family kernel names |

## Interpretation

The report does not claim to optimize the model automatically. It narrows the
investigation from a raw timeline to a small set of repeated structures:

- inspect `N027` and `N041` first because they dominate repeated runtime;
- check whether all-reduce timing varies across ranks;
- validate auxiliary/prelude costs around collective anchors;
- compare the same SQL reports before and after a runtime or operator change.

See [../examples/sample_report.md](../examples/sample_report.md) for a
report-style version of this demo.
