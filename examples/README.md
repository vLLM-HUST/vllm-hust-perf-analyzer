# Examples

This directory contains the checked kickstart profile used for a deterministic
first run.

- `kickstart_smoke/`: real two-device Ascend/CANN `msprof` smoke profile and
  TraceLoom output for a deterministic first run.

For the fastest first run, start with `kickstart_smoke/`.

For a larger curated Ascend graph-mode showcase with complete Huawei profiler
package layout, ACLGraph reconstruction, TraceLoom full output, and a
Perfetto/Chrome trace, use:

```text
../data/experiment-results/ascend_tp2_graph_showcase/
```

That package is stored in the repository data plane instead of this examples
directory because it is a heavier downloadable artifact.
