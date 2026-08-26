# Perfetto and distributed-rank analysis

## Export one AugDB

Export while analyzing:

```bash
traceloom /absolute/raw/rank0.db \
  --output /absolute/out/rank0.analysis.db \
  --perfetto-out /absolute/out/rank0.perfetto.json.gz
```

Or project an existing AugDB without rerunning analysis:

```bash
traceloom export-perfetto /absolute/out/rank0.analysis.db \
  --output /absolute/out/rank0.perfetto.json.gz
```

Open the `.json.gz` directly in Perfetto. TraceLoom publishes recovered tree
depths, a separate compressed timeline-event lane, and raw provider evidence.
Isomorphic subtrees share motif identity/color. Event labels and slice args
retain query coordinates for returning to AugDB.

Do not interpret absent compressed events as absent raw evidence. Display rules
may suppress annotation/envelope events such as `KERNEL_MIX_AIC/AIV` while the
raw-provider tracks and source locators remain auditable.

## Stack all ranks explicitly

Build one AugDB per rank, verify the rank map, then export:

```bash
traceloom export-perfetto /absolute/out/rank0.analysis.db \
  --output /absolute/out/tp8.observed.perfetto.json.gz \
  --distributed-rank 0=/absolute/out/rank0.analysis.db \
  --distributed-rank 1=/absolute/out/rank1.analysis.db \
  --distributed-rank 2=/absolute/out/rank2.analysis.db \
  --distributed-rank 3=/absolute/out/rank3.analysis.db
```

List every rank exactly once and include rank 0, the current display reference.
Do not infer rank identity from paths. The default aligns each compressed rank
lane at its first timeline event; it is a browsing convention, not an
absolute-clock claim.

## Apply an explicit clock model for collective comparison

When a versioned, auditable collective-end affine receipt exists:

```bash
traceloom export-perfetto /absolute/out/rank0.analysis.db \
  --output /absolute/out/tp8.end-aligned.perfetto.json.gz \
  --distributed-rank 0=/absolute/out/rank0.analysis.db \
  --distributed-rank 1=/absolute/out/rank1.analysis.db \
  --distributed-rank 2=/absolute/out/rank2.analysis.db \
  --distributed-clock-model /absolute/out/collective-end.models.jsonl
```

The JSONL must satisfy `traceloom.distributed-clock-model/v1` and contain one
`metric=end` record for every non-reference rank. TraceLoom fails closed on
missing, duplicate, extra, nonfinite, nonpositive, mixed-status, mixed-domain,
or mixed-contract models. Rank 0 remains identity.

Retain both views when clock alignment affects the diagnosis:

- **observed/default**: rank-local geometry under first-event display alignment;
- **end-aligned**: geometry after the explicit affine display transform.

Every aligned slice retains raw source timestamps, exact model parameters,
model status, marker contract, and receipt SHA-256. Record the receipt path and
digest with the timeline.

## Interpretation boundary

Collective-end alignment is useful because it makes end boundaries visually
comparable and leaves start spread visible. It does not establish:

- actual collective arrival time;
- causal waiting or dependency edges;
- a global host/device clock;
- semantic model-layer identity; or
- calibration quality beyond the receipt's stated evidence status.

`candidate_only` models are explicitly admitted only for display. Do not use
them as production clock calibration or silently rewrite AugDB timestamps.
Validate hypotheses with rank-wise AugDB statistics and raw provider rows.

For the complete file contract and slice args, read
`docs/augmented-perfetto-timeline.md`; for calibration evidence classes, read
`docs/clock-calibration.md`.
