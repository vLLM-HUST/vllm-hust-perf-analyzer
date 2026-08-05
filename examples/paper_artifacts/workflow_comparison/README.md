# Fixed workflow-comparison receipt

This directory freezes the machine-readable observation used by the paper's
fair workflow comparison. It reuses the immutable stock/fused inputs under
`../ascend_interleaved`; it does not introduce another profiler capture.

Run the receipt from the repository root after building TraceLoom:

```bash
examples/paper_artifacts/tools/verify_workflow_comparison.py \
  --traceloom build/native-tests/native/traceloom
```

The verifier evaluates four views over the same main profiler databases:

1. a conventional top-3 aggregate query;
2. a direct raw-SQL graph-execute candidate query;
3. the same TraceLoom structural grammar with the graph-capability companion
   deliberately withheld, which is the repeat-only ablation; and
4. the complete evidence-linked TraceLoom path.

The repeat-only input is made in a temporary directory by copying only the
main `msprof_*.db`. No row in that database is changed. Withholding the sibling
`host/sqlite/stream_info.db` removes exact capture-instance association while
leaving the raw device events and repeat grammar intact.

`expected.json` records only directly checked values. The interpretive matrix,
scope, and paper-safe conclusions are in
`../../../notes/workflow-comparison-study.md`.
