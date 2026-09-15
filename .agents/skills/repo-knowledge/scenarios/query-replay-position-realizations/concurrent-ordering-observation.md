# Capture ordering versus device interleaving — bounded 2026-09-14 observation

Use before proposing to stabilize flat grammar by replacing device-time order
with host launch time. This is experimental evidence, not a production contract.

Local inspected source: `ca378c4eca6442bd2c0b1f1d9d16041af730cec5`.
Inputs under `/workspace/my-ascend-workspace/runs/`:
`agent-trace-acceptance/20260910-owner-local-ep-profile`,
`agent-trace-acceptance/20260910-global-prefill-8k-stable`, and
`remote-kv-serving/20260912T062441Z`, ranks 0–7 each. Remote-KV databases were
produced using export-provenance analyzer `37323af55aeb5851b9a70b97155f5eacf104eafc`,
not regenerated here. All report incomplete evidence and zero exact replay
units (host stream_info.db absent): this tests the flat fallback.

Read-only TASK→HostTask mapping used unique (device, stream, task, context,
connection); COMMUNICATION_OP→HCCLOP used unique (device, connection) plus exact
or explicitly decorated operation name. Raw and monolithic group names differ;
do not reject source joins merely for anonymized group-name string inequality.
Do not replace uniqueness with nearest-time guesses. Host records can be reused
by multiple actual replay occurrences: their timestamp is capture/submission
metadata, not a fresh replay launch. Scope instances before ordering members.

HcPre/HcPost on a TASK stream defined query windows, NOT proven membership.
Selected anchors started and ended inside; right-crossers counted separately.
Across owner/prefill8k/remote_kv respectively: 4,310/2,160/1,798 windows;
4,310/2,160/30 fully host mapped; 12/8/1,770 with positive cross-stream interval
overlap. The first two are mostly serial at canonical-anchor resolution.
Remote-KV had 173,539 unmapped anchors and 14,351 right-crosser incidences.

An owner 321-anchor candidate across eight ranks had identical lane sequences,
complete host mapping, no tied timestamps/duplicate host IDs/right-crossers.
Rank0 `anchor-3460..anchor-3780` versus rank6 `anchor-3924..anchor-4244` exchanged
TensorMove and ReduceScatter in device order. Capture order removed the exchange.
Native fixture grammar (16 runs) gave 2 distinct full debug outputs in device
order, 1 in host order; both modes had 79 live nodes and 9 macros. All expanded
back exactly to input symbols. No compression improvement. Fixtures used logical
monotonic timestamps: grammar-only evidence, not full pipeline/cost validation.
Tested existing native fixture binary SHA-256:
`3c6e1e4c3ad56d8dcf4c9719422641cd1712f4591c550553a25a894528885d16`.
This pins the binary, not its unverified build-to-source correspondence.

Important boundaries:
- Capture order moved TensorMove after HcPost despite execution inside its
  measured window. Structure order is not geometry or temporal containment.
- Remote-KV: all 392 repeated capture-boundary groups differed in full symbol
  sequence; 387 looked stable after discarding unmatched work. That is a
  filtering illusion, not concurrency support. Missing work includes controls
  and communication; labeling versus physical execution causes remain unknown.
- Within-rank source-member-stable groups in owner (1,467) and prefill8k (728)
  already had stable device sequences. Demonstrated benefit is cross-rank only.
- Same symbol bags / lane-sequence multisets are candidates, not semantic identity.
- Flat sorting and exact replay recovery differ: the latter already uses
  stream-local domains and within-stream positions.
- Structural projection overlap accounting chooses an active token owner using
  token index. A comparator change may change per-node costs even with constant
  total union. Check costs independently; do not infer causality from total sort.

Full report and bounded reproducible audit are retained at
`/root/my-ascend-workspace/runs/traceloom-ordering-study/20260914/REPORT.md`;
its sibling `study.py` records SQLite reads and source keys; `verify.py` checks
8,268 saved window permutations and all 16 grammar round trips. No NPU execution,
production source changes, or paper edits were part of this observation.

## Donor full-sequence follow-up (2026-09-15)

A distinct positive compression result is now available. Dataset:
`/workspace/my-ascend-workspace/runs/donor-c32-diagnosis/20260912-c32-profile`,
ranks 0–7, AugDB producer `37323af` as above; DeepSeek-V4-Flash-0731-w8a8,
TP8/EP, dspark speculation. Each rank has 20,730 anchors; 20,727 uniquely join
TASK/COMMUNICATION_OP.connectionId to CANN_API `launch`. All 2,058 communication
anchors match. One launch thread, no ties/reused rows, no per-stream host-time
descents. Three unmapped profiler/placeholder anchors are retained at original
positions as reordering barriers, not dropped. This is a one-to-one launch
record experiment, not the prior reused HostTask capture-order mapping.

Full-sequence native grammar test, same pinned binary and logical timestamps,
no supplied layer/step labels, all 16 exact expansion checks pass. Count final
nodes plus dictionary RHS symbol slots (not file bytes): device costs by rank
`774,1007,774,1136,774,774,964,806`; host all `772`. Sum 7009→6176 (11.88% smaller);
rank3 improves 32.04%. Full grammar JSON variants across ranks 5→1. Host outputs
all have 90 root nodes, 313 macros, 682 dictionary RHS slots.

Sorting does NOT solve semantic hierarchy. Post-hoc alternating HcPre/HcPost
attention+MoE candidates (one SparseAttnSharedkv and MoeGatingTopKHash each)
give 276 layer candidates/rank. Exact symbol variants by device rank:
`6,19,6,37,6,6,18,9`; host all6. Host sizes×counts: 54×120,72×119,53×18,55×12,
76×6,73×1 (singleton includes retained PLACE_HOLDER_SQE). All candidates stay
contiguous in either ordering. Yet recursive grammar inspection finds zero
complete-layer-span nodes in either order. Sequence equivalence is available;
greedy grammar phrase boundaries are not semantic layer boundaries.

Six metadata-run starts and six rejection-sampling markers support five complete
marker-to-marker cycles, each 43 layer candidates → sampling → 3 candidates,
on all eight ranks. Target/draft interpretation is inferred and consistent
with speculation config. These are phase landmarks, NOT certified scheduler
step-start times. No explicit layer/step annotations were found in the inspected
PYTORCH_API names. All six sampling markers are inside top-level macros; exact
sample-to-sample intervals are not grammar nodes, though expansion preserves
all events. Do not mistake smaller output for a layer/step-aligned hierarchy.

Full report/scripts/results:
`/root/my-ascend-workspace/runs/traceloom-ordering-study/20260915-donor/REPORT.md`.
The justified next design is scope-aware template sharing with explicit phase
coordinates, not merely comparator replacement; exact runtime boundaries and
cost validation remain separate gates. No production code changed in this study.

## Integrated opt-in and boundary obstruction (2026-09-15 follow-up)

The local source now supports `--structural-order host-launch` (default device).
It permutes tokens, not anchors; `traceloom_structural_order` records the mapping
and source launch evidence. See README for admission/fallback limits. Exact
replay/protected inputs remain untouched. Temporal ownership uses observed
anchor order, and grammar/compact spans now use min/max envelopes.

Fresh full-native Release donor rank3/rank0 runs reproduce 1136→772 and 774→772
structural symbol slots. Events, anchors and every per-anchor cost-breakdown
row are identical between modes. Final grammar round trips preserve all20,730
anchors, with20,727 host-supported and3 fallback rows. Native suite has82 tests.
Receipts and the full investigation:
`/root/my-ascend-workspace/runs/traceloom-ordering-study/host-integration/REPORT.md`.

Actual recursive merge audit (native rank0 host grammar): macro20 first crosses
240/276 candidate layer starts by merging `HcPost (HcPre RmsNorm)`. Later
macro309 is an831-symbol phrase spanning the preceding cycle's tail and next
preparation prefix at all five internal candidate preparation boundaries.
Greedy compression rewards these phase-shifted phrases and never splits them
back to semantic boundaries. The default producer chain lacks a generic
repeated-block proposal pass;20,730 tokens are below its50,000 discovery cap.

Whole candidate cycles also really differ: lengths3458/3450/3460/3450/3460,
with differences in preparation (Fill/metadata/Cast/BroadcastTo/ZerosLike and
an initial placeholder). Exact whole-string equality is not semantic step
skeleton equality. No scheduler-step annotation was found in inspected
PYTORCH_API names. Native `step_idx` is presently an event/anchor ordinal,
not a scheduler step. Sampling is inside a43-layer→sampling→3-layer cycle,
not an established end-of-step. Do not mislabel manually protected cuts as
boundary discovery, or attribute these remaining issues to sort nondeterminism.

## Optional YAML hints (2026-09-15)

`--rules-config configs/deepseekv4.yaml` supplies loose ordered-marker hints.
The original experiment used the legacy `--match-rules` macro-only schema;
current configuration and compatibility details live in `configs/README.md`.
Read `configs/README.md` for `unmatched_marker_order_v1`: single-sided fragments
can grow; unmatched Post→Pre is rejected before ranking, including partial
macro wrappers; already balanced macros remain sealed and can combine at higher
levels. This is NOT mandatory completeness or an all-level no-cross fence.
Exact YAML bytes are in AugDB metadata; rules are in grammar debug output.

Full native donor rank3 (host ordered), off→on: total structural slots772→756;
exact post-hoc layer candidate nodes0→251/276, sharing3 macro templates;
exact sublayer candidates0→270/552. All events, anchors, anchor costs and ordering
are identical between arms. Every rule-enabled macro satisfies its rule, and
expansion preserves every token. Eight host-order donor fixture sequences also
produce the same rule-enabled output (756 slots), with full round trips.

The25 non-exact layer scopes are meaningful residuals: six uncommon third-layer
variants grouped with an earlier prefix;18 draft-region scopes recovered with
one leading TensorMove (54-event nodes versus53-event evaluation scopes); one
unique placeholder-bearing73-event scope stays decomposed. Do not tune away
retained evidence or overinterpret marker-only evaluation boundaries as truth.
No scheduler-step discovery claim follows from these layer-alignment results.
Report/reproducer:
`/root/my-ascend-workspace/runs/traceloom-ordering-study/yaml-rules/REPORT.md`.
