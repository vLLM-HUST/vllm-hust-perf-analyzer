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
convention here includes it in the unit on its left.

The layer rule finds 64 layers per large launch and two per small launch, 396
concrete windows total. Four exact layer variants remain in each large compute
domain; each small compute domain has one. Communication-only domains retain
the original grammar and typed `model_rules_no_match` status. Names stay
R-domain-qualified; generic global layer indices/cross-bank equivalence are not
introduced. Unknown, preparation, and sampling events remain in their original
streams and timestamps.

## Reproduction and regression boundary

Run the native CLI on the full input with `--threads 8 --rules-config
configs/qwen35-serving.yaml`, writing new DB/Perfetto outputs. Qualified artifacts
are in `traceloom/qwen35-marked/` beside the baseline, including `verification.json`
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
