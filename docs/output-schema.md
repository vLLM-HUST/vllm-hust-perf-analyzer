# Output Schema

## Default Report

For one database, TraceLoom writes:

```text
PROF_.../traceloom/loop_tree_v2.md
```

For a profiler directory containing multiple device databases:

```text
msprof_output/traceloom/device0_loop_tree_v2.md
msprof_output/traceloom/device1_loop_tree_v2.md
```

The report contains the compressed execution tree, occurrence and repeat
counts, total wall-clock cost, per-occurrence/per-iteration averages, compute,
communication, idle, active, auxiliary, and self-cost columns.

When at least one exact graph unit exists, `Structural Composition` is the
highest-level ordered map. It partitions every productive anchor exactly once
into `graph_unit`, complete graph-bounded `structural_unit`, or typed
`unrecognized` open-boundary rows. These are neutral observed units, not
workload phases. The Markdown expansion column is abbreviated for readability;
the sidecar retains the complete expansion handles and one membership row per
anchor.

When graph evidence is available, the provider-neutral `Graph Replay
Reconstruction` section summarizes recognized and typed unrecognized regions
plus exact and legacy ReplayUnit counts for ACLGraph or CUDA Graph inputs.

For one SQLite containing multiple devices, the report contains one
device-local Loop Tree and structural partition per observed device sequence.
Node and unit handles are namespaced in the shared sidecar; sequence order is
never synthesized across devices. A `Collective Correspondence` section
summarizes conservative cross-sequence candidates. `graph_body` candidates use
exact visible-body template/member positions, while `loop_structure`
candidates use recovered-loop positions. `complete` means candidate membership
coverage, not proof of a hidden hardware operation, workload phase, or global
causal order. Unequal graph-body occurrence counts fail closed to per-device
singletons.

Ascend reports also contain a `Visible Productive Idle Evidence` section. It
is the device-level E1→E4 explanation partition: profiler-visible wait,
capture/control, runtime-control, and explicit unattributed residual. The
default real-profile `collection_status` is `unknown`, so TraceLoom does not
turn empty observed streams into an absence claim without external capture
completeness evidence. These values describe gaps in visible productive work;
they are not proof of hardware idleness or causality.

The default Ascend report's `Unregistered Operator Audit` is the fail-open
counterpart: it lists concrete raw operator identities without an exact
operator rule, prioritizing operators observed inside graph bodies. Fuzzy
family matches retain their useful semantic role but remain on this list; they
do not silently turn a new raw name into a fully known operator. A nonempty
table is an explicit analysis-coverage warning, not profiler noise to discard.

This section is intentionally separate from the tree's compatibility
`idle_us` cost. The latter is the residual in an anchor's prelude cost packet;
the former is a device-global productive-gap partition and may include visible
wait/control tasks. They must not be substituted for one another.

The `Anchor-Prelude Attribution` subsection intersects E4 slices with the same
disjoint prelude windows used by Loop Tree cost packets, then aggregates those
intersections through existing node/anchor coverage. It reports the exact
device-only residual rather than forcing uncovered time onto a node. Hotspot
rows are hierarchical: a parent's duration includes its descendants, so
parent and child rows are not additive. Nanosecond fields are authoritative;
microsecond fields are readable rounded summaries.

## Explicit Native Artifacts

Use `traceloom --help-advanced` for non-default outputs:

- `--out PATH`: native result JSON;
- `--grammar-debug-out PATH`: grammar-state diagnostics;
- `--compat-db-out PATH`: queryable compatibility SQLite sidecar;
- `--loop-tree-out PATH`: explicit Loop Tree output path;
- `--loop-tree-aux`: include auxiliary attribution in the Loop Tree build.
- `--idle-evidence-rules PATH`: override the Ascend idle-evidence semantic
  ruleset; an invalid override fails rather than falling back silently.

Only one output may target stdout at a time, and explicit output paths require
a single input database.

Native JSON also carries two audit surfaces for graph-heavy comparisons:

- `graph_launch_body_members` retains every observed body task with launch,
  lane/order, raw operator, source row, interval, task kind, and (where a
  provider taxonomy is validated) semantic rule lineage;
- `graph_body_cost_summary` reports per-occurrence scheduled-task sum,
  cross-stream busy union, observed envelope, compute/communication/data-move
  components, plus all-body and exact-ReplayUnit distributions. Unequal exact
  occurrence counts are evidence, not a schema error; consumers compare
distributions rather than pairing occurrences by index.

Native JSON also emits `structural_units` whenever native JSON is requested.
Its rows use the same IDs, fingerprints, token bounds, costs, evidence status,
boundary policy, and expansion handles as the Markdown and sidecar views.
Exact per-anchor membership is normalized in the sidecar rather than repeated
as a large JSON array.

## Graph-Body Comparison JSON

`traceloom compare BASELINE CANDIDATE --same-workload --out PATH` writes
`graph-body-comparison-v1`. Its top-level `verdict` is one of `faster`,
`slower`, `equivalent`, or `inconclusive`. A positive direction means the
candidate duration is lower than baseline. The artifact includes:

- the explicit workload-attestation bit, aggregation policy, confidence,
  minimum-effect band, and bootstrap iteration count;
- typed `reason_codes` for every refusal or downgrade;
- baseline/candidate rank count, device set, exact sample count, body stream
  count, task-kind counts, and replay-unit launch arity;
- every selected input profile and its exact body-template/sample identity;
- envelope, busy-union, task-sum, compute, communication, and data-move median
  deltas with confidence intervals and per-metric verdicts.

The current automatic selector is intentionally narrow: exactly one exact
body template per profile and one graph launch per exact ReplayUnit. A
multi-launch composition or multiple exact templates returns
`inconclusive`/typed refusal until a workload-independent selector exists.
Multi-rank reduction requires equal exact sample counts within each variant;
counts may differ across variants. A confident envelope direction is downgraded
when task sum or busy union confidently moves the opposite way.

For Ascend, `semantic_operator_coverage` lists concrete operator identities
without exact identity registration, including their fuzzy family rule and how
often they occur inside graph bodies. Structural filtering is fail-open for
such operator rows: only an explicit ignore rule may remove one from the anchor
sequence. This deliberately prefers a noisy sequence over silently losing a
newly introduced kernel.

The checked-in SQL audit surfaces are:

- `docs/report-sql/reconstruction-capability-matrix.sql` for exact, typed
  unknown, and legacy ACLGraph capability outcomes;
- `docs/report-sql/structural-composition.sql` for the neutral ordered unit map
  and its membership-count conservation surface;
- `docs/report-sql/structural-composition-audit.sql` for exact unit/order,
  membership, identity, evidence-policy, interval, cost, and expansion
  invariants;
- `docs/report-sql/idle-evidence-summary.sql` for paper-ready E4 category
  totals and shares; and
- `docs/report-sql/idle-evidence-audit.sql` for exact partition, lineage, and
  anchor/root conservation checks.

These queries are executable goldens: the native SQL compatibility test checks
their columns and known-good outputs, and deliberately corrupts one idle
interval to prove the audit changes from `PASS` to `FAIL`.

The idle audit's `PASS` is a sidecar-integrity result, not a replacement for
`analysis_status` or `collection_status`. Consumers must inspect all three:
an internally conserved `invalid_input` run remains an auditable negative
result, not positive semantic evidence.

## Provenance Contract

Native events and sidecar rows retain source kind, source path, source table,
and source row identifiers. Reports are summaries; use these links to confirm
diagnoses against the raw profiler evidence.

CUDA node-level exact units expose `cuda_runtime_correlation` launch matching,
`cuda_graph_node_set` identity, `observed_stream_set_unordered` body topology,
and `direct_observed_graph_launch` composition boundaries. These policy names
make the provider-specific evidence route auditable without weakening the
shared exact ReplayUnit contract.

## Compatibility Rule

The schema is still alpha. Public releases should version table, column, JSON,
and report-field contracts before downstream systems depend on every name.
