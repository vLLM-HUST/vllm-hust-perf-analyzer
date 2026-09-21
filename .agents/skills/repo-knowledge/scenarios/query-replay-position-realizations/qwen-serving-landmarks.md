# Qwen serving: candidate cycles and stream-local layer landmarks

Use before extending the Qwen overlay or interpreting its layer/step labels.
The config contract is in `configs/README.md`; the opt-in overlay is
`configs/qwen35-serving.yaml`. Do not replace exact graph membership or merge
communication lanes into a model layer by temporal containment.

## Bounded September-21 observation

Input: `/workspace/strengthen-dsv4/runs/qwen-mtp-banked-draft-20260921/`
`native-graph/decode/rank0/PROF_000001_20260921043859530_03606835ORLLJGKB`.
Baseline: `traceloom/grammar-qualified/decode-rank0.db` from main `01b8df2`.
Both bank streams expose the same two body templates: six large and six small
launches. The six outer `_compute_slot_mapping_kernel` symbols are already
normalized exactly; name suffix matching was NOT the missing gate. Each is
followed by the same 22 structural tokens through `ClipByValueV2`. The config
includes this tail, and only the five fully bracketed intervals are labeled
`serving_cycle_candidate`. This is Fletcher-supplied phase interpretation plus
observed repeated sequences, not scheduler identity or a device-completion claim.

Large compute domains have 128 `AddRmsNormBias` endpoints, with alternating
attention/MLP segments; small domains have four, with preparation and sampling
outside them. The outer display `Add` conceals provider distinctions: the
immediate predecessor is `aclnnAdds_AddAiCore_Add`. Replay cost identities retain
this full name. Matching the outer alias in replay silently finds no units;
inspect `reason_code` instead of trusting successful export. The fused residual
norm is a useful boundary landmark, not proof of Python module ownership; the
initial `bf6fb49` convention included it in the unit on its left. Fletcher then
chose independent residual/norm phases: the guarded Adds + AddRmsNormBias pair
now forms `residual_norm`, separate from attention and MLP. Plain seed-only
GemmaRmsNorm stays raw, not mislabeled as a residual operation. A layer container
composes attention → residual_norm → mlp → residual_norm, without duplicating
terminal ownership or splitting one fused kernel into fictional operations.

Source check: pinned local vLLM `752a3a50` Qwen3.5 inherits the Qwen3Next decoder
forward: input norm → attention → post-attention norm → MLP. Ascend `9bf964c`
`AscendGemmaRMSNorm.forward_oot` uses `npu_add_rms_norm_bias` with a residual and
`1.0 + self.weight`. Thus a fused kernel combines previous-output residual work
with next-stage input normalization; its module ownership is not inferred from
which side of a displayed boundary it occupies. The exact preceding Adds is
consistent with weight preparation, not proof that every displayed Add is a
residual add.

The layer rule finds 64 layers per large launch and two per small launch, 396
concrete windows total. Exact ordered layer variants remain separate definitions; boundary placement
can change their count, so do not freeze a template count as model semantics. Communication-only domains retain
the original grammar and typed `model_rules_no_match` status. Names stay
R-domain-qualified; generic global layer indices/cross-bank equivalence are not
introduced. Unknown, preparation, and sampling events remain in their original
streams and timestamps.

## Reproduction and regression boundary

Run the native CLI on the full input with `--threads 8 --rules-config
configs/qwen35-serving.yaml`, writing new DB/Perfetto outputs. Qualified artifacts
are in `traceloom/qwen35-marked/` (initial two-phase convention) and
`traceloom/qwen35-three-phase/` (independent residual/norm), including `verification.json`
and `verify.py`. The check compares all event/anchor/launch/body-member rows,
replay cost membership, the 10,460 displayed event IDs and exact geometry, and
every layer window against its concrete launch/stream/task ordinals. It does not
claim all events belong to a scheduler step or that visible gaps are device idle.

`qwen_serving_projection_test.py` exercises the production CLI pipeline with
the actual config: exact multi-launch protected input, layer geometry and HPO,
unchanged evidence, complete cycle counts, and a broken tail that must block
cross-cycle bridging. Native unit tests cover clipped/missing delimiters,
classifier ambiguity, reversed composition, raw tails, exact variants and
communication-domain fallback. The schema is optional; baseline behavior stays
unchanged when the overlay is absent.

Three-phase acceptance retains 5 candidate cycles and 396 layer windows, with
396 attention, 396 MLP and 792 residual_norm windows. Concrete task-ordinal
ranges partition every layer into four adjacent disjoint children; all 10,460
event identities/timestamps and exact launch/body/cost membership remain equal
to the prior two-phase output. Each residual unit contains precisely the guarded
Adds and fused AddRmsNormBias; neither attention nor MLP contains that fused
kernel. The 89-test suite and Release build pass.

## Fold AIV decorations without inventing cross-stream membership

The three-phase capture publishes the same collective as both TASK/AivKernel
and COMMUNICATION_OP. A bounded raw audit found equal source, connection ID,
device, stream and exact interval (e.g. TASK row162 / COMMUNICATION_OP row451,
connection6213). The primary Perfetto fold uses all of those coordinates plus
collective kind and one-to-one cardinality, never timestamps alone. All 834
visible AivKernel tasks have counterparts: 810 AllReduce and 24 AllGather.
Raw evidence and SQL events/body/cost membership are unchanged. Retain the
surviving event's `display_folded_task_event_id` rather than deleting provenance.

Fletcher selected an attention display convention: extend the compute window
through the unique same-launch AllReduce before its next residual_norm unit.
All 396 attention realizations satisfy it here. This is explicitly a display
association, not new HPO membership or a proven cross-stream dependency; it
must not leak into cost or layer-count analysis. MLP and AllGather are not
phase-associated by this rule. Primary-plane event count becomes 9,626 after
834 duplicate drawings disappear; the raw plane remains 27,735 slices.
Qualified re-export/verification: `traceloom/qwen35-attention-collectives/`.
The AugDB remains the unchanged `qwen35-three-phase/decode-rank0.db`.

## Cross-rank coverage gap in the subsequent fused candidate package

`/root/my-ascend-workspace/runs/qwen-mtp-gdn-fusion-20260921/candidate-timelines.tar.gz`
contains four bf6fb49 exports/summaries, not AugDBs. Rank0 decode/mixed each have
396 named layers; rank1 has zero in both, despite all twelve exact launches and
normal operator populations. Rank1 uses suffixed GemmaRmsNorm/AddRmsNormBias and
classifier identities outside the overlay's exact literals. Do not infer missing
execution or compare layer costs using those asymmetric labels. The three-phase convention alone did not solve this identity-admission gap.
The matching-only repair below retains backend/source identity.

The independent bounded analysis and reproducer live beside that archive at
`timeline-analysis-20260921/findings/{REPORT.md,metrics.json,analyze.py}`. It
separates same-work decode seams from unmatched mixed arrival partitions,
checks every exact member against its launch, and measures matrix-family
interval unions without mixing primary/raw/structural projections. The mixed
candidate's [3,257,97] then [3,3,3,33] differs from the old [3,33,257,97]; both
large steps padding to512 does not make their total-time delta a causal speedup.


### Matching-only CANN preprocessing closes the rank1 landmark gap

The shared `ascend_decorated_kernel_base` parser already served outer structural
normalization; replay model rules had bypassed it. `name_normalization:
ascend_decorated_kernel` now opts marker/classifier matching into that same
fingerprint/lowering-mode/layout syntax. It does not rewrite atom displays,
exact replay symbols, cost members, or ordered definition signatures. Unknown
underscore suffixes remain exact. Default model matching remains `exact`.

Rank1 also exposes the norm predecessor as `Add_<fingerprint>...`, rather than
`aclnnAdds_AddAiCore_Add`. The Qwen rule explicitly admits either through
`end_predecessor_any`, only adjacent to `AddRmsNormBias`; this is a landmark
interpretation, not proof of scalar Adds identity or arbitrary Add residuals.
The two predecessor fields are mutually exclusive. Do not silently apply the
outer generic Add alias to all exact replay evidence.

Bounded validation on the newest fused package: the native marked-structure
matcher consumed each exported exact launch/domain/stream sequence ordered by
dense `position_ordinal`. All four decode/mixed × rank0/rank1 traces yielded
396 layer, 396 attention, 396 MLP, and 792 residual_norm occurrences. Evidence:
`timeline-analysis-20260921/name-normalization/verification.json` beside the
archive, with per-region exact event IDs and reproducer sources. This is native
matching on an exported sequence, NOT a full raw-DB reanalysis. The archive has
no AugDB, and its frozen source path was absent on this machine during repair.

Separate end-to-end CLI validation used the earlier banked-draft full profiles
(decode rank0/rank1, mixed rank1), producing the same phase counts with all12
launches. Outputs/commands/checks are under that capture's
`traceloom/qwen35-name-normalized/`. Decode rank0 event, anchor, graph launch,
body-member, and replay-cost-member rows are exactly equal to its earlier
three-phase AugDB. Native integration tests use actual candidate rank1 spelling
forms and verify equal phase coordinates plus unchanged evidence rows; unit
tests keep decorated exact variants distinct and reject arbitrary suffixes.
