# Agent-Native Self-Contained Augmented Database

Status: active design decision and issue #27 implementation handoff

Date: 2026-08-12

## Decision

TraceLoom's augmented database is a primary analysis artifact, not a lightweight
compatibility sidecar and not an index that requires the original profiler
database to remain beside it.

Each analysis creates a new, self-contained augmented SQLite database. It does
not modify the input profiler database. The generated database may deliberately
duplicate source data: disk space is subordinate to queryability, portability,
auditability, and a simple agent workflow. TraceLoom is an offline analyzer that
can regenerate the artifact quickly on the target servers, so minimizing the
augmented database at the cost of external joins is the wrong tradeoff.

Self-contained means that after moving the augmented database away from its
input profile, a consumer can still:

1. navigate the recovered hierarchy;
2. inspect occurrences and cost distributions at every materialized level;
3. expand a selected structure to normalized events;
4. resolve those events to the profiler rows and source evidence retained in
   the artifact; and
5. audit how a displayed statistic was formed, including typed unsupported or
   excluded evidence.

An external source path or hash remains useful provenance, but it is never a
runtime dependency of the accepted query surface. Requiring consumers to
`ATTACH` the original profiler database does not satisfy this contract.

## Product Thesis

Agents naturally formulate a sequence of local analytical questions and use
SQL to test them. TraceLoom should not try to replace that behavior. It should
change the database against which the agent writes SQL.

The intended distinction is:

> Free-form SQL over TraceLoom's materialized hierarchical analytical view is
> the normal workflow. Reconstructing structure by querying vendor profiler
> schemas directly is an escape hatch and an audit action, not the primary
> workflow.

The augmented database is therefore the operational query surface of the
hierarchical cost view. Markdown and JSON are useful renderings, receipts, and
exchange formats; they are not separate sources of analytical truth.

The desired analysis loop is:

```text
find a costly region
  -> inspect ordered children and cost composition
    -> expand repeated occurrences or aligned positions
      -> identify a distributional outlier or structural variant
        -> expand exact members and normalized events
          -> audit the retained profiler rows
```

The reverse direction is equally important:

```text
profiler/normalized event
  -> exact memberships and occurrences
    -> containing or summarizing tree nodes
      -> surrounding structural and cost context
```

## Investigation Baseline

The investigation used the existing issue #27 worktree:

```text
branch: codex/issue-27-augmented-db-drilldown
head:   ead2b218e0cbd9e634f125d93d341d0378a5ad3e
```

Focused replay-map, exact-graph SQL, sidecar writer/materializer, canonical SQL,
retained Ascend TP2, and retained CUDA real-model tests all pass at this head.
This is a strong implementation base, but it has not yet completed the product
contract above.

### Existing general tree-query spine

The augmented database already exposes useful general relations and views:

- `traceloom_v_tree_node` provides a SQL tree map;
- `traceloom_tree_node_occurrence` expands concrete occurrences;
- `traceloom_tree_node_anchor` connects occurrences to anchors;
- `traceloom_v_node_cost` and `traceloom_v_node_aux_cost` expose cost summaries
  and auxiliary evidence;
- normalized events and source locators support further drill-down.

This is a usable foundation, not yet a complete general query contract:

- node handles are deterministic within a report but lack an explicit persisted
  artifact/run namespace;
- not every general hierarchy/cost join consistently uses the full
  `(db_idx, device_id, view_name, id)` identity;
- exact-graph events have a reverse query, while ordinary normalized events do
  not yet have one canonical event-to-occurrence/node surface;
- typed support/issues are not unified across the general node-query spine;
- schema integrity relies mostly on native generation and tests rather than
  relational constraints and canonical orphan audits.

### Issue #28: replay-internal cost semantics

Commits `dc60b81`, `c386a74`, and `d777f53` compute a complete native
`ReplayInternalCostMapResult` and serialize it to result JSON. The map retains:

- independent ReplayUnit, ordered launch-member, composition-slot, graph-body,
  body-template, stream, member, event, and source identities;
- exact multi-launch membership such as `H + Lx35 + T`, without assuming that
  replay, launch occurrence, slot, or body template are one-to-one;
- per-body and per-stream `task_sum`, overlap-safe `busy_union`, `envelope`, and
  compute/communication/data-move scheduled-work lenses;
- exact member timing and scheduled-work shares;
- explicit `role_collapsed` aligned aggregates with contributor multiplicity
  and p25/median/p75 distributions;
- typed unit/member support and issue rows;
- fail-closed handling of empty or ambiguous bodies, malformed references,
  duplicate stream positions, inconsistent lanes, and other invalid evidence.

This semantic work is accepted as the authority. Consumer SQL must not
recompute or reinterpret it. It is not yet published in the augmented database.

### Issue #29: exact graph hyperlinks

Commits `5a89d22`, `2eeb7a0`, and `ead2b21` materialize the exact structural and
provenance chain through:

- `traceloom_graph_launch`;
- `traceloom_graph_body_member`; and
- `traceloom_v_node_graph_body_member`.

The resulting provider-neutral path connects an anchored tree occurrence to an
exact replay launch, ordered body members, normalized events, and source
locators. It also supports the reverse exact-member-event-to-tree query. Exact
membership comes from Native IR relations, never timestamp containment.

The retained CUDA real-model artifact verifies five exact launches and 49,405
body members, forward/reverse closure, source/event orphan checks, and
byte-identical repeated sidecar generation.

This completes the exact graph structural hyperlink. It does not publish the
replay cost-map distributions, cost lenses, aggregate contributors, or typed
cost-map support/issues computed by #28.

## Remaining Issue #27 Core

The next implementation slice publishes #28 through the #29/common query
spine. The expected normalized surface includes the equivalents of:

- `traceloom_replay_cost_unit`;
- `traceloom_replay_cost_launch`;
- `traceloom_replay_cost_stream`;
- `traceloom_replay_cost_member`;
- `traceloom_replay_cost_aggregate`;
- `traceloom_replay_cost_aggregate_member`; and
- `traceloom_replay_cost_issue`.

Exact table names may change after schema review. The required semantics may
not.

The publisher should build `ReplayInternalCostMapResult` once and materialize
its accepted fields directly. It must reuse #29 launch/member identities and
retain composition-slot id/order and role. Aggregate-to-member contributor
links must be emitted explicitly by native analysis, not recovered later with
fuzzy or composite SQL matching.

Canonical queries must make the following path short and deterministic:

```text
tree-node occurrence
  -> internal cost distribution
    -> aggregate hotspot
      -> exact contributing replay members
        -> normalized events and retained profiler rows
```

The SQL surface must keep these lenses visibly distinct:

- scheduled `task_sum`;
- overlap-safe `busy_union`;
- observed `envelope`;
- compute/communication/data-move partitions of scheduled work; and
- scheduled-work share.

None may be relabeled as causal attribution or an additive wall-clock
decomposition.

## Self-Contained Storage Invariants

1. **Fresh output.** Analysis creates a new database at an explicit output
   path. It never augments or rewrites the input database in place.
2. **No external query dependency.** Every accepted forward/reverse drill-down
   and audit query runs with only the generated database open.
3. **Retained raw evidence.** Profiler tables/rows required to audit emitted
   normalized events are physically retained in the augmented database. A
   source locator must resolve internally.
4. **Complete source catalog.** The database records source artifact identity,
   original path as provenance, hashes where available, provider/schema facts,
   capture metadata, and the mapping from retained rows to their original
   source database/table/row identity.
5. **Multi-source honesty.** Monolithic, split-layout, and provider exports may
   require different physical import layouts. The source catalog and namespaced
   retained tables must prevent collisions. The exact physical naming is open;
   dependence on external `ATTACH` databases is not.
6. **One analytical authority.** Tree maps, Markdown reports, JSON, canned SQL,
   and agent answers derive from the same materialized relations and native
   analysis results.
7. **Typed incompleteness.** Unsupported, ambiguous, absent, and excluded
   evidence remains queryable. Empty success is forbidden.
8. **Determinism.** Artifact-scoped identities, row ordering, aggregate
   contributor membership, and canonical query results are stable across
   repeated analysis of identical inputs.
9. **Reproducibility over compactness.** The database may be large. Compression
   and optional pruning are later optimizations and cannot weaken the default
   evidence/query contract.

For a single monolithic profiler database, copying the source database and then
materializing TraceLoom-owned tables may be the simplest valid implementation.
For split and multi-source inputs, the precise retained-table namespace and
import transaction remain an implementation question. Both cases must present
one self-contained SQLite artifact per declared analysis scope.

## Agent-Native Delivery Contract

Finishing the schema alone will not make agents use it. The normal CLI must
make the artifact discoverable and self-describing.

The intended bundle shape is approximately:

```text
traceloom/
  manifest.json
  summary.md
  tree-map.md
  db00.traceloom_augmented.db
  db01.traceloom_augmented.db
  queries/
```

The names are not fixed by this note. The behavioral requirements are:

- the default command emits or clearly advertises the augmented database;
- a manifest identifies databases, devices, views, schema version, source
  scope, and recommended entry queries;
- the database contains a self-describing catalog of important relations,
  semantics, support boundaries, and cost lenses;
- canonical SQL is shipped with the artifact or embedded in a query catalog;
- node handles copied from the readable map are directly usable in SQL;
- consumers do not need repository source or vendor-schema knowledge to begin
  analysis.

The existing `--compat-db-out` / compatibility-sidecar language contradicts
this product role. Backward-compatible aliases may remain, but the primary CLI,
documentation, and filenames should call the output an augmented analysis
database rather than a compatibility artifact.

## Documentation Drift Found During Audit

Current documentation describes the augmented database as the primary output
and shows `summary.md`, `tree-map.md`, `queries/`, and
`dbNN.traceloom_augmented.db`. The production CLI currently writes only a Loop
Tree by default and hides database generation behind the advanced
`--compat-db-out` flag.

The schema documentation also says that a sidecar contains the original raw
tables. The current native sidecar materializer instead writes TraceLoom tables,
normalized events, and source locators; it does not generally copy the complete
input profiler database or provide a portable resolver for it. The new
self-contained implementation should make the stronger documented behavior
true, or the documentation must remain explicit about any temporarily narrower
scope while work is in flight.

## Integration State

The issue #27 worktree is based on an older mainline. Its merge base is
`d059b0b`; current `main` has since accepted CUDA and Ascend work through
different PR/squash hashes. A direct merge is inappropriate even though some
patches are semantically or cumulatively equivalent.

Continue implementation from a fresh branch based on current `main`:

1. identify already-landed prerequisite behavior by patch/content, not commit
   subject alone;
2. transplant the unique generic graph/composition prerequisites;
3. transplant the accepted #28 replay-cost-map stack;
4. transplant one, and only one, #29 exact-SQL implementation line;
5. reconcile current multi-device, training, report, and adapter changes;
6. run focused and full native tests plus retained Ascend TP2 and CUDA
   real-model verifiers; and
7. implement the remaining cost-map SQL publisher and self-contained artifact
   delivery on top of that refreshed base.

Known merge-simulation hotspots include native documentation, idle-audit CLI
work, and CUDA adapter tests. Do not infer patch equivalence from matching
subjects alone.

## Acceptance Scenario

The decisive product test should give an agent only a generated TraceLoom
analysis artifact and this bounded task:

```text
Find the most expensive repeated structures. Compare their per-occurrence cost
distributions, identify the largest structural or communication outlier, expand
its exact members, and return the retained profiler rows supporting the result.
```

Acceptance requires the agent to complete the task using the augmented
database and its self-description, without reading TraceLoom source, being told
vendor table names, or opening the original profiler database. Its SQL and
result must remain auditable and repeatable.

## Boundaries

- SQL freedom is a feature; a new TraceLoom query language is not required.
- Automatic cross-run structural matching is a separate product slice.
- The view does not infer model phases, tensor parallelism, source-code
  semantics, cross-stream causality, or profiler-hidden graph definitions.
- Self-contained does not mean lossless replay of every possible vendor UI. It
  means complete retention of the source evidence required to audit every
  TraceLoom-materialized analytical claim, with the default policy favoring
  complete raw-table copying where practical.
- Space-saving modes may be added later only as explicit, typed alternatives;
  they must not silently weaken the default artifact.
