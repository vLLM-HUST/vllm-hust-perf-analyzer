# Import runtime scheduling context

Use when extending or consuming the optional scheduler/worker context bridge,
changing the vLLM hook, or interpreting a context-associated device event.
Start with `docs/runtime-context.md` and `integrations/vllm/README.md`; the
native implementation is `native/src/compat/scheduler_context.cpp`.

## Boundaries that prevent expensive rediscovery

- TraceLoom remains an offline analyzer. Prefer the optional package
  `--scheduler-cls traceloom_vllm_scheduler.TracingAsyncScheduler` (or the
  synchronous `TracingScheduler`) for patch-free scheduler-only collection.
  It subclasses the selected upstream implementation without changing decisions.
  The explicit source patch is a separate execution-linked mode.
  No runtime package or shared source checkout should be modified merely to
  import context or run the CPU acceptance tests.
- Scheduler-only injection does not mutate SchedulerOutput or transport identity;
  imported steps with zero recorded executions must not imply zero device work.
  For the separate execution-linked mode, transport identity must be a real
  `SchedulerOutput` dataclass field.
  Dynamic attributes can disappear under serialization. The source audit checks
  the actual patched class through pickle and msgpack without importing vLLM.
- EngineCore has both ordinary and batch-queue scheduling paths. Record after
  their final schedule result, before dispatch; do not wrap only a base Scheduler
  method and miss subclass postprocessing. Use the common worker-wrapper seam
  rather than adding instrumentation to the large GPU/NPU runner.
- Request payload kind is not lifecycle state: the new-request payload may also
  carry resumed work. Missing resume/preemption data stays null. Post-schedule
  computed-token counts may be optimistic; they are not completed device work.
- The first cache observation is explicitly `after_schedule`. Pool capacity can
  include reserved blocks. Do not derive hit rate or utilization from these
  counts, or replace an unavailable count with zero.
- Exact profiler marker names establish identity. Same-source, same-thread
  profiler-host containment defines a bounded launch scope; provider-supported
  correlations then expose device links. No external monotonic/profiler clock
  subtraction, cross-thread guess, or nearest-time match is admitted.
- A supported marker does not establish complete step membership. Later
  unwrapped later dispatch, hidden replay members and unsupported submission threads can
  remain outside the current bridge. Preserve missing-step/marker, ambiguity,
  capture-loss and unclosed states; expose zero-execution steps with a left join.
- Context is imported into the temporary AugDB after its ordinary catalog,
  before atomic publication. A bad context must not replace existing output or
  alter profiler/context inputs. With no `--context`, baseline surfaces stay
  unchanged. Keep exact imported lines and source/line IDs for audit.

## Direct verification

`ctest --preset dev-tests -R scheduler_context_tests` runs the real Python
exporter, synthetic Ascend profile, native CLI and SQL reverse audit without
vLLM, torch or an accelerator. Run the full native preset and Release build for
native changes. Source patch checking is separate:
`integrations/vllm/check_source_contract.py /explicit/vllm/checkout` requires
msgspec in a CPU environment and edits only temporary copies.

The initial source boundary is vLLM
`752a3a504485790a2e8491cacbb35c137339ad34`; it is not a universal compatibility
claim. CPU fixture acceptance does not certify an actual Ascend capture's marker
layout, provider links or recording overhead. Before promoting a real runtime
result, pin its deployment and validate that exact evidence; do not launch NPU
work just because the importer needs tests.

## Scheduler injection acceptance

`check_scheduler_source_contract.py /explicit/vllm/source` is the patch-free
CPU source audit. On upstream `752a3a504485790a2e8491cacbb35c137339ad34`,
it verified the CLI seam, executed the real class resolver, checked the actual
AsyncScheduler ancestry and preserved the unpatched SchedulerOutput's serialized
state. Scheduler computations themselves were not executed. The Python suite
now also covers final-result recording, argument/output preservation, disabled
mode, exceptions, no duplicate legacy EngineCore hook, and native import of both
nonempty and empty decisions without execution records. Package installation was
checked in an isolated CPU environment; the exporter imports without torch/vLLM.

Do not mix the scheduler-only adapter with the legacy execution-linked patch.
Do not wrap a synchronous base when async mode is required. Arbitrary custom
scheduler composition needs a final-return seam of its own; worker class
configuration is left untouched. Live acceptance is bounded below; instrumentation overhead remains unvalidated.

## Real scheduler-only capture (2026-09-15)

Qwen3-0.6B, TP1/eager, in-process V1 engine, stock Ascend worker and injected
TracingAsyncScheduler passed on vLLM `0fc695fc6d1d82e9a5ac6835ac8e4e1c83703665` /
vLLM-Ascend `f4a08bddd0cc65a0bd8c3d377b158ae5ca7527db`. Two sequential requests
(16 generated tokens each, warmup outside capture) produced 34 scheduler records:
2 multi-token, 30 single-token, 2 empty decisions. The producer closed with no
loss. Native import retained all 34 and preserved events, anchors, costs, order
and HPO Occurrences exactly against the context-free analysis of the same input.
No execution records/step-device links exist in this mode; joint-query device
costs remain NULL. This is runtime scheduler export validation, NOT live marker
coverage, per-step cost attribution or overhead acceptance.

Local capture, launch/analysis reproducers, joint SQL and verification:
`/root/my-ascend-workspace/runs/traceloom-qwen-scheduler/20260915T180945Z/`.
Preserve CANN's sourced PYTHONPATH when adding the observer; overwriting it lost
`acl`. For a one-window torch-npu capture use an explicit RECORD_AND_SAVE schedule
and advance it after the measured workload; stopping the default RECORD state
emitted a possible-incomplete-parsing warning. Neither fix changes scheduler
semantics. Keep the monolithic DB's typed incomplete-evidence status.

## Linked capture and task-queue bridge (2026-09-15)

The same pinned live runtime now passed with `LinkedTracingAsyncScheduler`,
`transport-worker.patch` applied only to a Git-archive task snapshot, and the
**stock** vLLM Ascend `start_profile/stop_profile` implementation. The narrow
patch adds only the serialized field and common execute/sample wrapper seams;
it leaves EngineCore and the optimization worker selection alone. Do not use
the legacy EngineCore patch for this route.

Two real surprises matter before future debugging:

- `PYTORCH_API.startNs/endNs` are TEXT on torch-npu 2.10.0.post2; recognize
  canonical int64 decimal text without floating-point conversion. Integer-only
  validation discarded all 66 actual markers.
- Ascend's task queue moves submission from the marked Python thread to a CANN
  dequeue thread. Same-thread direct CANN scope found thousands of host calls
  but **zero** linked devices in this run. Raw queue type 50002,
  `Enqueue@/Dequeue@`, and `CONNECTION_IDS` contain the missing identity.
  Join by that ID, not positional pairing. Scope the enqueue within the marker,
  then CANN within the correlated dequeue; the latter can outlive the marker.
  Require same source/process and unique queue identity. Audit source rows in
  `traceloom_context_queue_runtime`; refuse duplicates and thread mismatches.

Results: 34 scheduler decisions, 34 execute + 32 sample markers, all matched,
zero JSONL losses; 14,144 of 14,178 device events attributed. The provider has
14,176 exact runtime/device links. The two without native links are profiler
controls. The other 32 unassociated events are `EVENT_RECORD` (total 0.30 us),
whose enqueue lies outside supported marker scopes; do not assign the nearest
step. All 11,684 non-control anchors link. Both zero-token decisions still
produce one linked EVENT_RECORD (0.02 us each): **empty scheduling is not a
zero-device-work assertion**.

All 32 concrete `Rep x27` HPO occurrences mapped to 32 distinct nonempty
supplied steps through terminal event IDs, without time-window membership or
model-specific Qwen rules. This demonstrates a useful structural/context join;
it does not certify an inferred repetition as a semantic layer.

The stock profiler emits its RECORD-stop warning (possible incomplete parsing).
Its raw capture and existing offline Db exporter produced the audited input;
the conclusion concerns captured evidence only. Do not advertise universal
completeness, graph/DP/TP>1 coverage, overhead acceptance or critical paths.
Full raw-provider evidence remains accessible alongside the context lanes.
The analyzer still types this single-DB input as incomplete evidence.

Artifact and reproducers:
`/root/my-ascend-workspace/runs/traceloom-qwen-scheduler/20260915T183003Z-linked/`
(`capture.py`, `protocol.json`, `verify.py`, `verification.json`, source
snapshot, raw capture, context.db and Perfetto timeline). Reusable consumer SQL
lives in `integrations/vllm/step-device-summary.sql` (dedup, duration sum,
per-device union and envelope) and `occurrence-step-context.sql` (concrete HPO
traversal). Native context export packs overlaps into independent display lanes.

## Candidate partitioning by supplied step (2026-09-16)

Use configs/scheduler-step.yaml (macro_matching.partition_by: scheduler_step)
with linked context for eager grammar recovery. Binding must happen BEFORE
grammar, after raw-source catalog and runtime rows exist. Register context
recipes only after the ordinary catalog. Materialize shared runtime/device
indexes before binding: moving the join ahead of the old structural-index
phase otherwise made the real capture spend minutes in an unindexed join.

The grammar state carries contiguous source-token partition runs. Unknown
tokens isolate themselves. Candidates and commit plans test the whole original
span, not just the newest adjacent symbols. Definition keys exclude step IDs,
so cross-step structural sharing remains possible. Read-only run producers must
stop at a boundary rather than reject an entire long run and lose its legal
within-step subruns.

A second correctness boundary exists in structural_occurrence_builder.cpp:
state lowering folds adjacent equal live symbols AGAIN for display. Gate that
fold too; otherwise it restores a cross-step Repeat despite legal grammar
candidates. Partition-bounded nonuniform top-level macros remain visible Seq
instances with shared templates. The empty-grammar fallback cannot re-fold
across partitions either. The whole-trace root alone is an unbounded container.

Bounded Qwen result on the identical linked capture: 32 visible macro instances,
two definitions reused 2 and 30 times, no non-root occurrence crossing steps
and no unknown-token bridge. Previously two Rep x15 containers spanned steps
0–15 and 17–32 with shifted bodies. Old N144 occurrence 1's exact event now
belongs inside the step-17 macro. Events, anchors, order, costs and all context
links compare equal. Artifact/reproducer:
 /root/my-ascend-workspace/runs/traceloom-step-partitions/
(analysis.db, timeline.perfetto.json.gz, verify.py, verification.json).

The supported route deliberately rejects protected replay, marked-structure
projection, disabled grammar and standalone legacy/debug outputs; do not
silently apply the rule only to a hidden grammar while showing an unconstrained
alternative tree. Partial captured membership is still not full semantic step
ownership, especially for auxiliary attribution.

Test discovery correction: several earlier AugDb tests were accidentally
indented below the __main__ guard. Keep that guard LAST, and count executable
tests with unittest.TestLoader.discover rather than AST method counts. The
earlier handoff's 45-test claim counted definitions, not discovered tests; the
corrected suite now discovers and executes all 47 cases, including the new
partition integration tests.
