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
