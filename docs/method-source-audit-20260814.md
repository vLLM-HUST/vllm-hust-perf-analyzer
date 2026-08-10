# Method-to-Source Audit (2026-08-14)

This note audits whether TraceLoom's implementation gives the database timeline
the semantics claimed by the paper. It is deliberately not a feature roadmap.
Each item records the analytical contract, the implementation path, a concrete
oracle, and the smallest decision or repair required before submission.

## Audit rules

For each claimed relation or measure, check five things:

1. **Domain:** the observations over which the relation is legal.
2. **Identity:** the stable coordinate to which the result is attached.
3. **Measure:** whether a value is a literal observation, a disjoint
   attribution, an overlay, or an envelope.
4. **Boundary behavior:** unsupported, ambiguous, and residual observations
   must remain typed rather than silently becoming absence or zero.
5. **Oracle:** tests and real-data queries must distinguish the intended
   semantics from a merely deterministic implementation.

## Findings

### A1. Device-local sequence domains are claimed but not enforced

**Severity:** correctness; blocks a general multi-device input claim.

**Claim.** The paper defines one horizontal coordinate per device analysis
scope. Device and rank coordinates compose only through explicit input
identity. Pattern discovery and grammar construction therefore must not cross a
device boundary.

**Current implementation.** `build_flat_anchors()` globally sorts all
`AnchorCandidate`s by `device_id` and then time, and appends one zero-based
`TokenTable.sequence_index`. `ProtectedSequence` retains `device_id`, but
candidate scanning does not reject a candidate that crosses a device change.
`GrammarNode` and `GrammarChunk` carry no sequence-domain identity. Report-tree
materialization later assigns all node, edge, and node-anchor rows the
`primary_device_id(tokens)`, which is the first token's device.

Relevant paths:

- `native/src/analysis/flat_anchor_builder.cpp`
- `native/src/sequence/protected_sequence.cpp`
- `native/src/pattern/candidate_scan.cpp`
- `native/include/traceloom/pattern/grammar_state.h`
- `native/src/pattern/grammar_state.cpp`
- `native/src/compat/report_tree_rows.cpp`

**Why existing checks can miss it.** Root-cost conservation already groups
time bounds by device and sums the envelopes. A cross-device global tree can
therefore conserve total time while still discovering a pattern across two
illegal domains and publishing every row under the first device id. Existing
candidate and grammar fixtures use one device.

**Repair in this branch.** The retained implementation at `ec7fc5f` is adopted
and reconciled with the current cost-policy and augmented-DB paths. Report
tokens are partitioned by observed `device_id`; grammar runs over a dense token
projection for exactly one device; protected intervals that span devices fail
closed; each tree is lowered with its true device id; and multi-device
node/tree keys are device-scoped. The queryable DB may contain several device-
local trees, while an explicit Markdown path requires
`--loop-tree-device-id`. Cross-device or cross-rank comparison remains an
explicit relational operation over those trees.

The bounded-window candidate key now also includes `device_id`. Windows that
touch a device boundary become typed `CandidateCrossesSequenceDomain`
diagnostics and cannot contribute to either device's count. Thus the
construction diagnostic cannot manufacture recurrence by aggregating equal
symbols across two devices.

**Oracles.** Two-device fixtures whose concatenated boundaries would otherwise
create candidates assert device-keyed summaries, typed boundary rejection,
one independently recovered tree per device, scoped node/tree keys, per-device
cost conservation, no cross-device node/anchor links, and fail-closed cross-
device protected intervals. The CUDA compatibility fixture additionally checks
two independently published device trees through SQLite.

### A2. `total_us` at a position is a right-anchored disjoint attribution

**Severity:** semantics/documentation; current RQ2 real-data result remains
valid.

**Claim.** A position carries the local transition before its right anchor plus
the anchor's disjoint occupied-time share. This is an additive analytical lens,
not the raw duration of the anchor event. Raw scheduled duration remains
queryable as evidence.

**Current implementation.** `compute_prelude_costs()` classifies the non-anchor
interval before a token and attaches it to that right token.
`compute_timeline_anchor_costs()` sweeps anchor intervals per device and assigns
each occupied slice once. Communication takes precedence over compute; when
several active owners have the same category, the smallest token ordinal owns
the slice. `token_cost_packet()` adds the prelude partition to that disjoint
anchor share. `traceloom_tree_node_occurrence` then sums position packets.

Relevant paths:

- `native/src/compat/report_tree_rows.cpp`
- `native/src/compat/sidecar_tree_views.cpp`
- `docs/db-timeline-schema.md`
- `docs/workflow.md`

**What is already clean.** The database documentation distinguishes additive
`total_us = compute_us + comm_us + idle_us` from the non-additive `self_us` and
`aux_us` overlays. Unit tests cover communication-over-compute precedence,
overlap-safe root conservation, and prelude classification.

**Remaining ambiguity.** The same-category tie break (`*owners.begin()`) is an
implicit positional ownership rule. It is deterministic and makes every parent
exactly composable from children, but a position's `total_us` is not a literal
anchor duration and can depend on projected order when anchors overlap. The
public contract should name this lens and keep raw duration visibly separate;
otherwise a reader can mistake position attribution for an event measurement.

**Real-data check.** In the current kickstart RQ2 scope, all 11,136 body
positions are non-overlapping in projected order. Every position's
`total_us` exceeds raw event duration only because it includes its preceding
transition. The slowest occurrence's largest excess remains the AllReduce at
position 282: attributed `2265.805 us`, raw event `2264.525 us`, and excess
over the position median `1916.198 us`. The claimed signal is therefore a real
communication-duration movement rather than an overlap-ownership artifact.

**Decision to make.** Keep the implementation if the method explicitly names
this as a right-anchored disjoint transition lens and exposes raw event duration
beside it. Otherwise move disjoint busy-union accounting to window/node scopes
and use literal duration at event positions. The first route preserves the
current compositional cost algebra and is the smaller, more coherent change.

### A3. Communication replacement lost its evidence link and became aux cost

**Severity:** evidence-lineage correctness and auxiliary-cost contamination.

**Observed failure.** The flat builder suppresses a low-level task when a
`CommunicationOpRow` on the same device and connection overlaps it, then emits
the communication observation as the structural anchor. The evidence-role
materializer repeated the membership test and stated
`represented_by_communication_anchor`, but did not place the suppressed task at
that anchor. It consequently reported the task as `anchor/orphan`.
Independently, the generic auxiliary writer treated every unanchored device
event as prelude evidence, so the same suppressed communication task was also
attached to the *next* anchor as auxiliary cost.

**Real-data magnitude.** The kickstart database contains 1,776 such orphan
tasks (1,775 communication-looking tasks plus one `MEMCPY_ASYNC`). Every task
has exactly one overlapping `COMMUNICATION_OP`; 1,772 have exactly equal time
bounds and four overlap without equal bounds. All 1,776 communication
observations resolve to anchors (1,773 distinct anchors). The erroneous aux
links add `2,351,432.729 us` of duplicate scheduled overlay to following
anchors. They do not alter the disjoint additive `total_us`, but they make the
auxiliary lens materially misleading.

**Repair in this branch.** `evidence_role_sql_rows.cpp` now retains the exact
matching communication rows, places a suppressed task at each representative
communication anchor, and emits a typed conflict if more than one distinct
representative anchor exists. A focused SQLite regression constructs distinct
TASK and COMMUNICATION_OP observations for one execution, checks that the task
lands on the communication anchor, and rejects an orphan issue. The incremental
build and `traceloom_native_report_sql_compat_tests` pass.

**Completed repair.** A shared `EventCostAttributionMask`, built from the
effective `FlatAnchorBuildConfig`, now gates both auxiliary links and transition
partitions. Communication/replay replacement, host mirrors, reconciled timing
envelopes, identity observations, and evidence-only observations cannot silently
reappear as auxiliary cost. Regenerating both kickstart databases yields zero
communication-replacement orphans and zero communication-replacement auxiliary
links, while materializing 1,776 and 1,774 representative task-to-anchor
placements respectively. The executable projection tour still passes.

### A4. `retained_as_evidence` is parsed and published but not executed

**Severity:** configurable-policy contract; default profiles are unaffected.

The evidence-role manifest exposes `retained_for_attribution` and
`retained_as_evidence`, accepts overrides, and publishes the selected value.
No cost or auxiliary implementation reads that value. Both
`build_aux_attribution_sql_rows()` and prelude cost classification currently
consider all eligible unanchored events regardless of the declared treatment.
The default manifest uses `retained_for_attribution` for every rule, so checked-
in default results do not drift; a custom policy can nevertheless request a
behavior that TraceLoom silently does not perform.

**Repair in this branch.** `EventCostAttributionMask` is now consumed by the
production auxiliary and transition-cost paths. `retained_as_evidence` keeps
the normalized event and source locator but does not create an auxiliary link
or enter the transition overlay. A focused override oracle holds the evidence
inventory fixed while checking that `retained_for_attribution` contributes one
`3.0 us` EVENT_WAIT overlay and `retained_as_evidence` contributes none.

### A5. Auxiliary tail observations already fail closed as typed results

**Severity:** no method defect; add a direct oracle.

`build_aux_attribution_sql_rows()` links an auxiliary observation only to a
later anchor on the same device. A tail observation with no later anchor stays
in `traceloom_event`. The evidence-role materializer records it as
`retained_unplaced` with issue code
`omitted_event_without_auxiliary_link`; it does not throw and does not silently
convert it to zero. In the kickstart database all 92 unplaced auxiliary events
are at or after the last anchor start (88 end after the last anchor), confirming
that this is a real tail boundary rather than an interior placement loss.

**Oracle.** The small evidence-role fixture now asserts that its existing
`LateTask` is retained with the typed unplaced decision and issue, and has no
auxiliary link. Regenerated kickstart databases retain 92 and 134 such tail
outcomes respectively; none becomes an error or a zero-valued placement.

### A6. The evaluated partition-local reducer was not on `main`

**Severity:** implementation/evaluation fidelity; repaired in this branch.

The production branch still scanned every owned partition in parallel, moved
every candidate occurrence back to one global vector, and globally sorted that
vector in `reduce_candidates()`.  The paper-evaluated implementation at
`7d8768a` instead reduces occurrences inside each partition and merges only
compact `CandidateSummaryRow`s plus typed diagnostics.  The latter is the path
that produced the retained scaling result, but it had remained on historical
analysis branches rather than the current product branch.

**Repair in this branch.** The production and parity-test portion of
`7d8768a` is now adopted with its original Fletcher authorship.  Each start
coordinate still has one partition owner; local summaries are merged in
candidate-key/first-position order, counts and first positions are combined,
and a key blocked by any ambiguous-boundary diagnostic is removed globally.
The aggregate retains the exact accepted occurrence count without retaining
the occurrence-sized intermediate table.  One- and multi-thread summary and
diagnostic parity tests remain executable.

The audit also found an unstated completeness precondition: a deterministic
result can still be incomplete if the read halo is shorter than the longest
forward window at an interior partition seam.  Both partitioned scan APIs now
validate contiguous ownership and sufficient per-partition read reach before
starting workers.  A seam oracle proves that a one-token halo fails for
length-three windows and that a two-token halo retains all 13 length-two/three
windows of an eight-token fixture.

### A7. Recursive grammar commit is deterministic but deliberately serial

**Severity:** claim-boundary clarification; implementation and current paper
agree.

`GrammarStateConfig.worker_count` assigns chunks to logical worker owners and
the read-only rounds expose per-owner candidate buckets, but
`run_adjacent_run_readonly_round()`, `run_pair_grammar_readonly_round()`, and
`run_native_macro_run_readonly_round()` currently traverse one dense frozen
snapshot in the calling thread.  They then reduce with total tie-break orders,
select one global action, revalidate generation/spans/nonoverlap/protected
boundaries, and apply one serial rewrite.  `worker_count` therefore does not
make recursive grammar induction parallel.

This is not currently a paper defect: the implementation section explicitly
calls the partitioned bounded-window path discovery evidence and states that
it does not select grammar productions.  It describes grammar rounds as
globally ordered serial rounds and limits the evaluated speedup to the
map/reduce component.  Existing grammar tests compare exact semantic output
signatures across chunk sizes and worker counts, including pairs and nested
macro-run folding.

### A8. Grammar safety-limit and exception fallbacks are not queryable

**Severity:** typed-support gap; a partial or failed hierarchy can look like an
ordinary complete tree.

After exact-run folding, `run_grammar_state_machine()` stops pair discovery
when the live sequence exceeds `full_discovery_cap`.  It marks the result
`sequence_too_large_for_full_pair_discovery` but also treats that result as
`ok()`.  The sidecar may consequently lower an exact but incomplete run-only
grammar.  Separately, `build_sidecar_report_tree()` catches every grammar
exception and silently emits the flat-token tree.  Neither outcome is carried
into `traceloom_semantic_tree.macro_discovery` or another queryable support
surface.

**Repair in this branch.** Useful exact run-only structure is preserved at the
size limit, but the tree receives a typed partial-discovery diagnostic.
Rejected and exceptional grammar runs fail closed to the flat tree with their
own typed diagnostics. `traceloom_semantic_tree.macro_discovery` now publishes
`native_report_tree_complete`, `native_report_tree_partial_size_limit`, or the
corresponding rejected/exception fallback state per tree. A multi-device
sidecar oracle forces only device 0 through the cap and observes partial versus
complete status independently in SQL.

### A9. Bounded-window discovery is not an augmented-DB relation

**Severity:** paper/product wording; no structural correctness defect.

The bounded length-two/three scan emits reduced summaries and diagnostic
previews in the legacy in-memory JSON result.  It neither chooses recursive
grammar productions nor materializes complete candidate/diagnostic relations
in the queryable database timeline.  The paper currently calls the summaries
"published discovery evidence," which is stronger than the primary product
surface supports.  Before submission, either materialize an explicitly bounded
summary/support surface in the augmented DB or narrow the prose to an audited
construction diagnostic and component-scaling workload.  Do not imply that
the scaling experiment parallelizes recursive hierarchy recovery.

## Next audit slices

1. Device-local structural domains and typed grammar completion status.
2. Occurrence populations, denominator rules, and projection-coordinate
   composability.
3. Replay-unit internal structure and cost lenses.
4. Host/device bridge relations, typed support boundaries, and source
   provenance.
