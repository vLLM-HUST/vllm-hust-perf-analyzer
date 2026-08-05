# Ascend fresh-TP2 preregistered negative

This checkout-bundled campaign preserves a useful negative result rather than
turning it into a post-hoc success. Before collection, three fresh TP2 captures
were expected to reproduce 30 exact `H + L×35 + T` units and 1,110 ordered
launch members on both ranks, with no unknown region and stable within-rank
topology. All six deterministic workload runs completed, but none met the
preregistered cardinality. The claim is therefore **NOT REPRODUCED**.

The current analyzer still extracts useful, fail-closed evidence from the same
immutable inputs: 27--28 exact period-one units per rank, 28--29 visible graph
launches, and one explicit missing-completion region in four of six profiles.
Five profiles share a two-stream, nine-task graph body; capture 3 rank 1 instead
has a two-stream, 411-task body. This compatibility result does not rescue the
original stability claim.

## What is bundled

Each of the six rank databases is a deterministic full-time-range reduction of
one retained CANN 9 monolithic profile. The project maintainers produced these
profiles and redistribute the reductions as repository test data under the
repository license. The main databases are 10.87--10.98 MiB each (65.64 MiB
combined), plus one 8 KiB numeric `CaptureStreamInfo` companion per rank. The
reducer preserves source rowids and all primary event rows while removing
`HOST_INFO` and `TASK_PMU_INFO` row content. No model weights, prompt text,
generated text, usernames, paths, or host identifiers are included.

`preregistered.tsv` freezes the analysis made at the preregistered TraceLoom
revision. `posthoc-repair.tsv` freezes the separately labeled CANN 9 repair
snapshot. `expected.json` records their hashes, the workload-output hashes,
the immutable full/reduced profile hashes, and the current checkout contract.

## Verify

```bash
examples/paper_artifacts/tools/verify_ascend_tp2_fresh_negative.py \
  --traceloom build/native-tests/native/traceloom
```

The verifier checks database integrity, hashes, privacy omissions, historical
receipt semantics, source-row resolution, current exact/unknown composition,
the cross-capture body mismatch, and—most importantly—that the preregistered
claim remains falsified. A successful verifier means “the negative evidence is
faithfully reproduced,” not “the preregistered stability claim succeeded.”

Maintainers can additionally pass `--reference-root` pointing at the retained
campaign root. The verifier discovers the six raw databases by hash and proves
that their current structural observations equal the bundled reductions.

## Boundary

The result covers one model, runtime, hardware generation, request envelope,
and exactly three captures. It is evidence that TraceLoom preserves unsupported
or incomplete structure and that this stability hypothesis failed here; it is
not a general rate estimate for instability or incompleteness.
