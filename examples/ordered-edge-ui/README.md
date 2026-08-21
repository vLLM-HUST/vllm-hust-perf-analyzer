# Ordered Edge UI Experiment

This bounded prototype tests whether TraceLoom's HPO storage can support a
simpler user model:

> one ordered execution tree whose concrete child edges are grouped only when
> they have the same contextual structural role.

It does not change the canonical database schema. The renderer derives one
linear child-edge order from occurrence token coordinates, treats the current
HPO child Position as the edge-role identity, and derives the per-role rank
from concrete order. It then checks that the derived rank agrees with the
stored compatibility `member_order` before rendering anything.

Generate a real-profile prototype after running TraceLoom on the checked-in
kickstart profile:

```bash
python3 examples/ordered-edge-ui/render.py \
  examples/kickstart_smoke/msprof_raw/traceloom/analysis_db01.db \
  build/ordered-edge-ui/index.html
```

The default selector chooses a supported repeated composite with several child
roles and several parent Occurrences. Override it when needed:

```bash
python3 examples/ordered-edge-ui/render.py analysis.db out.html \
  --position-id node-N286
```

The generated HTML is self-contained and uses no JavaScript dependencies or
network resources. It exposes three synchronized readings:

1. one concrete parent's complete ordered child-edge stream;
2. the structurally equivalent edge population selected by one role; and
3. one concrete edge with its derived rank, cost lenses, and HPO provenance.

The edge detail stacks only the overlap-safe primary partition (`compute +
communication + uncovered = total`). `auxiliary` remains a separately labeled
compatible lens because it is not an additive fourth component.

The line wrapping follows a verified repeated role phrase only for visual
legibility. It does not create a second coordinate axis. Switching between one
parent and all parents changes the statistical population while preserving the
selected edge role.

This is a falsification fork. Keep the HPO schema authoritative until the UI
experiment shows whether edge-role equivalence is merely presentation, a
better query surface, or a replacement core model.
