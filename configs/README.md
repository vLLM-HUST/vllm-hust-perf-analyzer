# Staged analysis rules

Use `traceloom PROFILE --rules-config configs/deepseekv4.yaml`. This is an
optional model overlay, not a replacement for TraceLoom's analysis algorithms.
Omitting it preserves the default policies and unconstrained macro grammar.

```yaml
schema: traceloom-analysis-rules-v1
macro_matching:
  ordered_markers:
    - id: hc_pre_post
      before: HcPre
      after: HcPost
```

## Four separate stages

1. `event_reconciliation`: reconcile duplicate observations of one execution.
2. `classification`: choose structural participation while retaining evidence/cost.
3. `symbol_normalization`: map observed labels to comparable structural symbols.
4. `macro_matching`: constrain candidates over **normalized structural symbols**.

The first three built-in policies live in `native/data/default_*_rules.yaml`.
They are authoritative YAML inputs, not generated documentation or copies of TSV
files. Provider scope remains explicit on each rule; defaults cover the same
Ascend/CUDA/Hygon cases as before this format migration. No model is inferred
from a file name, and no default policy is selected by the model's name.

Omitted stages inherit the built-in policy (or the corresponding existing
`TRACELOOM_CLASSIFICATION_RULES`, `TRACELOOM_SYMBOL_RULES`, or
`TRACELOOM_EVENT_RECONCILIATION_RULES` environment-selected base). A supplied
policy stage has `mode`, `metadata`, and `rules`:

```yaml
schema: traceloom-analysis-rules-v1
symbol_normalization:
  mode: extend
  metadata:
    policy_id: example.model-symbols
    policy_version: "1"
  rules:
    - rule_id: example.matmul-alias
      priority: 110
      provider_scope: ascend
      source_domain: task
      field: selected
      match: exact
      pattern: MyModelMatMul
      structural_symbol: MatMul
      note: Explicit model-specific alias
```

- `extend`: add new rule IDs; a collision with a base ID is an error.
- `override`: replace complete rules by existing IDs; an unknown ID is an error.
- `replace`: replace the entire stage, including its policy metadata/fallbacks.
  An empty `rules: []` is a deliberate identity/fallback-only policy, not a
  request to reload defaults.

For extend/override, the base's fallback behavior remains in force. Both policy
identities/digests are retained; the model metadata identifies the contribution.
Rules are full records, not partial patches. Use the built-in YAML files as the
field reference: metadata fields and rule fields are validated by the same typed
policy engines as legacy inputs. `note` and unused reconciliation identity text
fields may be omitted; classification `required_fields` may be omitted for none.
`generic_context_id` and `concrete_context_id` remain explicit (use -1 where
inapplicable). Policy values are single-line scalars without surrounding spaces.

Existing stage-specific priority/conflict handling does not change. In particular,
equal-priority conflicting symbol aliases preserve the observed symbol instead
of guessing. A configuration cannot bypass unique identity/containment checks,
protected replay semantics, or the evidence/cost retention contract. A YAML file
is not an arbitrary expression or plugin execution mechanism.

## Compatibility and reproducibility

The old `--classification-rules`, `--symbol-rules`, and
`--event-reconciliation-rules` flags (and their extension flags) accept `.yaml`,
`.yml`, or legacy TSV files. Their historical extension semantics remain intact;
notably the legacy reconciliation extension is an overlay. Standalone YAML
policies use `schema: traceloom-policy-v1`, `kind` equal to their stage name, and
`metadata`/`rules` as in the built-in files.

The old `--match-rules` still accepts its original `traceloom-match-rules-v1`
document with top-level `ordered_markers`; the fixture analyzer retains this
macro-only input. It does not run the event normalization/classification stages.
The updated `deepseekv4.yaml` is for the production `--rules-config` entry point.
Do not mix `--rules-config` with legacy rule flags (including field overrides):
this fails explicitly rather than introducing order-dependent precedence.
`--structural-order host-launch` is orthogonal and can be used with either route.

One YAML document, at most 1 MiB. Unknown/duplicate keys, wrong types, invalid
rules, and unsupported schemas fail before loading the profile. `.yaml`/`.yml`
select the YAML format for standalone policies; other extensions use legacy TSV.
No includes, merge keys, custom tagged constructors, or scripts are evaluated.

The AugDB retains the exact model document under `analysis_rules_yaml` and its
schema under `analysis_rules_semantics`. Existing policy/rule audit tables retain
the resolved policy, IDs, digest, and decisions; rule source lines refer to the
original YAML, not an intermediate representation. Policy content digests now
identify YAML bytes and therefore intentionally differ from the former TSV bytes.

## `unmatched_marker_order_v1`

Each original `before` contributes +1, each `after` contributes -1, and other
symbols contribute zero. A macro carries its net **unmatched** count. Complete
pairs already formed inside a macro cancel to zero and remain sealed during
higher-level matching. A candidate is rejected if an unmatched `after` prefix
would be followed by unmatched `before` markers. Single-sided candidates are
allowed. Multiple rules are independent and must all permit a candidate.

Examples (`P=HcPre`, `Q=HcPost`, `X` is unmarked):

- `P + X`, `X + Q`, `P + Q`: allowed.
- `Q + P`, `Q + macro(P X)`: rejected.
- `macro(P X Q) + macro(P X Q)`: allowed; higher-level structure is possible.
- `Q + macro(P X Q)`: allowed: the existing complete pair is sealed, leaving
  only the unmatched `Q`. This differs intentionally from checking the entire
  raw expansion again at every level.
- No mandatory pair completeness, strict nesting validation, model-specific
  layer count, whole-layer boundaries, or scheduler-step labels are inferred.

Pair candidates are filtered **before** counting/ranking them. Commit validation
also enforces the rule, including higher-level and run replacements. Repeating
one symbol/macro cannot reverse its unmatched-marker direction. This is a
compositional, parse-history-aware restriction, not an all-level no-cross fence:
a successfully formed complete macro can participate in a larger macro.

Rules currently apply to the outer structural grammar. Existing exact protected
replay units remain opaque and the separate replay-body recovery is unchanged.
A marker absent from an input simply contributes no matches; inspect the input
symbols and resulting structure instead of assuming that a rule was effective.

## Input and audit

For the legacy macro-only document, use one YAML document, at most 1 MiB. Unknown/duplicate fields, missing fields,
duplicate rule IDs, identical before/after symbols, wrong node types and invalid
YAML fail explicitly. No scripts, includes or arbitrary expressions are executed. libyaml handles YAML syntax, including quoting and comments.

The AugDB metadata keys `match_rules_yaml` and `match_rules_semantics` retain the
exact supplied document and interpretation. Grammar debug JSON includes resolved
rules under `algorithm.ordered_markers` when enabled. Pair matching uses frequency ranking and the minimum-two acceptance criterion
below: a permitted candidate is not guaranteed to be selected. Compare with the no-rule baseline to judge the hint's effects.

Hints are user-supplied prior knowledge. Report them with the result; do not
present constrained recovery as discovering layer/step boundaries from timing
alone. The sample is installed under `share/traceloom/models/deepseekv4.yaml`.

## `expanded_suffix_v1`

`macro_matching.suffix_markers` accepts `{id, marker}` records over normalized
structural symbols. A candidate containing that marker is allowed only if its
expanded marker occurrences form a terminal contiguous suffix. `A B S`, `A S S`,
`S S`, and marker-free `A B` are allowed; `S A` and `A S B` are not. Summaries
propagate through all grammar macro levels: repeating a mixed `macro(A S)` is
also forbidden. Unlike the ordered-marker rule, completed units are NOT sealed
or exempted. Exact externally protected replay units remain opaque; this rule
neither inspects their bodies nor changes their protection.

```yaml
macro_matching:
  suffix_markers:
    - id: slot_mapping_cycle_end
      marker: _compute_slot_mapping_kernel
```

Both lists are optional and can be combined; rule IDs must be unique across
lists. The macro-only legacy document also supports top-level `suffix_markers`.
All candidate producers filter before ranking, and commits recheck constraints.
This is a candidate restriction, not automatic region/step annotation; a marker
run is not evidence of a scheduler boundary. No fixed run length is assumed.

The shipped DeepSeek overlay additionally classifies exact operator `TensorMove`
as auxiliary while retaining raw evidence, timing, and cost attribution. This
is model-scoped, not a global change to unknown-event handling.

## Pair discovery and compression cost

Pair discovery now accepts two or more occurrences, ranked by frequency and
then the existing deterministic tie-breaks. Singletons remain excluded. The old
minimum-four gate could prevent locally nonshrinking intermediate pairs from
forming larger repeated structures. The diagnostic `gain` remains the legacy
clipped `max(occurrences - 3, 0)` estimate, but no longer gates pair acceptance;
it is neither measured byte savings nor the root-plus-dictionary-RHS slot delta.
This change does not guarantee better compression on every input.

AugDB metadata records `pair_min_occurrences=2` and
`suffix_marker_semantics=expanded_suffix_v1`; grammar debug JSON also exposes
the threshold and configured suffix rules. Full YAML remains in the existing
provenance fields. No generic multi-node repeated-block discovery or semantic
step-boundary projection is added by these changes.

## Explicit model structure: typed units and compositions

`structure` supplies semantic boundaries directly, independently of macro
compression. This optional projection uses the existing Position/Occurrence
model and standard tree/Perfetto export; it does not invent a parallel ownership
model or reinterpret grammar Patterns as semantic types.

```yaml
structure:
  unit:
    id: hc_sublayer
    begin: HcPre
    end: HcPost
    labels:
      - label: attention
        contains_any: [SparseAttnSharedkv]
      - label: moe
        contains_any: [MoeInitRoutingV3]
  compositions:
    - label: layer
      sequence: [attention, moe]
```

A complete, non-nested begin/end pair includes both markers. Matching uses exact
normalized structural symbols, in the selected structural order (host-launch
when requested). A classifier matches if ANY listed symbol occurs inside that
unit. Exactly one matching classifier assigns its label; zero or multiple
matches retain an `unclassified` or `ambiguous` unit. Those reserved labels never
participate in compositions. A composition requires adjacent, contiguous units
in exactly the configured type order. Intervening raw events, unknown units,
and reversed type order are barriers. There is no silent skipping or fixed
model layer count. Multiple applicable compositions remain uncombined.

One marker-pair unit rule and one level of composition are supported in this
version. Invalid/duplicate labels, unknown composition members, identical
markers, empty classifier lists, and unknown configuration fields fail at load.
Nested or unmatched runtime markers remain ordinary event evidence rather than
fabricated units. No complete matches or incomplete/ambiguous recognition is
reported as `model_rules_partial` in the tree header's discovery status; normal
configured recognition is `model_rules_explicit`. Devices with exact protected
replay intervals retain the ordinary protected recovery path and report
`model_rules_unsupported_protected_replay` instead of applying marker semantics.

With structure enabled, the semantic tree contains named `layer`, `attention`,
and `moe` sequence Positions (or the configured labels) and original atom
members. Identical ordered structures share contextual Position definitions;
the same semantic label with different contents has separate definitions.
Use `traceloom_v_position`, Position refinements/occurrences/members and the
ordinary tree node/occurrence views to query definitions, instances and source
anchors. The `native_report_tree` subtree tracks in Perfetto display the named
units and compositions directly. Auxiliary evidence/cost remains attributable;
raw event rows, structural ordering and anchor cost records are unchanged.

The compact grammar remains separately queryable but no longer decides these
semantic boundaries. `--loop-tree-no-grammar` therefore does not disable explicit
model structure. Omit `structure` to retain the previous grammar-derived tree.
AugDB records the full supplied YAML plus `model_structure_semantics` and
`model_structure_rule_id`. These are user-supplied model hints, NOT independent
layer discovery or scheduler-step ground truth. This release does not compose
or annotate steps.

## Keep macro candidates inside supplied scheduler steps

Opt in with the standalone model document in scheduler-step.yaml:

```yaml
schema: traceloom-analysis-rules-v1
macro_matching:
  partition_by: scheduler_step
```

Supply execution-linked scheduler/worker JSONL through --context. This is a
candidate-occurrence constraint, not a timestamp cut or an inferred step count.
Partition membership comes from supported step/device event identities with
closed, lossless scheduler and worker producers. Every source token in a macro
instance must occupy one contiguous known partition; unknown tokens are isolated
barriers. A known step that reappears after another partition does not bridge it.

The identity is NOT included in the macro-definition key: identical bodies
within different steps can still share definitions. Both run producers split at
partition boundaries, pair candidates are filtered, and the commit planner
rechecks the source-token span, including already merged macros. The synthetic
whole-trace root is a container and intentionally spans steps; it is not a
candidate macro.

This route supports eager and exactly linked protected replay in grammar-based
AugDB analysis and Perfetto export. Every protected unit must have one known
step identity across its launch-owned tokens; missing/conflicting evidence is
rejected, not guessed or split. It also rejects missing supported context,
grammar-disabled mode, explicit marked-structure projection and independent
legacy/debug outputs rather than silently ignoring the constraint. Other marker matching
rules can be combined with partition_by. Omit the field to retain unconstrained
recovery; importing context alone does not enable it. The macro-only legacy
rules document also accepts top-level partition_by: scheduler_step.

The state-to-HPO display projection honors the same boundary: it cannot fold
equal adjacent live macros back into a cross-partition Repeat. Nonuniform
top-level macros remain visible Seq instances; identical instances reuse the
same structural template without merging their event membership.

## Qwen3.5 serving landmarks (opt-in, capture-derived)

Use `--rules-config configs/qwen35-serving.yaml` for the observed banked Qwen
MTP decode layout. The installed copy is `share/traceloom/models/qwen35-serving.yaml`.
This is a model-hint projection, not universal Qwen support or scheduler truth.
It does not require changing default classification or dropping unknown events.

The generic `structure.unit.mode: cycle_end` accepts `id`, `label`, and a
nonempty `end_sequence` instead of paired begin/end/classifiers. Matching is
exact in the outer structural token order. Only the intervals between two
complete observed end sequences receive a named Position; the right sequence
is included, and the first prefix and last suffix remain ordinary evidence.
An observed first marker with an unmatched tail breaks recognition, rather than
silently bridging two cycles. For this capture the end sequence is slot mapping
plus its 22 following structural tokens, through `ClipByValueV2`. These are
**candidate serving cycles**, not completed-device scheduler steps.
Unlike paired model rules, cycle grouping may contain protected replay launch
anchors: it groups whole atomic tokens and does not reinterpret body membership.
The compact grammar remains separately available. Explicit cycle grouping is
not compatible with scheduler-step partitioning; choose the evidence appropriate
to the question rather than silently mixing the two boundary definitions.

With an optional `begin`, `cycle_end` first requires that marker, then searches
for the contiguous `end_sequence` through variable preparation tokens. A second
begin, a graph anchor, or a device change aborts an unfinished search and clears
the prior boundary so a missing cycle cannot be bridged. An observed broken
suffix also clears the chain; unseeded suffixes are ignored. Graph anchors remain
legal between completed boundaries. An unfinished final preparation is diagnosed
and left raw. Without `begin`, the original exact end-sequence mode is unchanged.

The Qwen rule uses `_compute_slot_mapping_kernel` followed eventually by
`Equal → MaskedFill → ClipByValueV2`. This common suffix covers both older
GatherV3 publication and newer `slots_kernel` publication, without fixing all
metadata work in between. Windows still run from the end of one validated
preparation boundary through the end of the next: they are device-side candidate
cycles, not CPU scheduler identity or a claim that preparation belongs to the
preceding model invocation. Six complete boundaries yield five full windows;
initial preparation and the trailing execution remain outside full-cycle labels.
A cycle node's temporal box remains the envelope of all its member anchors.
A provider collective interval can cross the ending landmark and therefore
extend that box into the next cycle; do not read the envelope as the boundary
instant or scheduler-step latency. Exact marker/anchor coordinates define the
candidate partition, and raw member intervals are never clipped to fit it.

`replay_structure` has the same unit/composition schema as `structure`, but is
applied independently to each exact replay-body stream. Its `end_delimited`
mode uses `begin` as an initial/reset marker, `end` as the included right
boundary, and optional `end_predecessor` as an exact adjacent guard. A supported
end seeds the next unit; a new begin resets pending preparation, which stays
raw. A clipped first unit is not manufactured from the graph start. Incomplete
suffixes remain raw. Classification and adjacent composition have the same
ambiguity and exact ordered-signature rules as paired units. A stream with no
recognized units keeps its ordinary grammar and reports `model_rules_no_match`;
matched streams report `model_rules_explicit` or `model_rules_partial` in the
replay domain's `reason_code`. Invalid replay evidence is still rejected before
model matching. Cycle mode is outer-only.

The supplied rule seeds at `GemmaRmsNorm` and ends at `AddRmsNormBias` preceded
by `aclnnAdds_AddAiCore_Add` or the rank1 CANN `Add` kernel family.
Model matching defaults to exact provider strings, not outer display aliases.
The Qwen rule opts into `name_normalization: ascend_decorated_kernel`, reusing
TraceLoom's CANN fingerprint/lowering-mode/layout suffix parser for marker and
classifier matching only. Arbitrary underscore suffixes are not stripped.
Raw names, atom labels, replay cost identities, and exact ordered definition
signatures remain unchanged; equal kernel families do not merge variants.
`end_predecessor_any` provides nonempty alternatives for the adjacent guard
and is mutually exclusive with `end_predecessor`; both require `end_delimited`.
The Qwen `Add` alternative is admitted only immediately before `AddRmsNormBias`,
not as proof that arbitrary Add kernels implement residuals. `CausalConv1d` or `ScatterPaKvCache` identifies
attention; `SwiGlu` identifies MLP. With `boundary_label: residual_norm`, the
exact predecessor plus fused norm form an independent sibling unit, excluded
from both attention and MLP. The composition is
`attention → residual_norm → mlp → residual_norm`. No fixed layer count or
inferred cross-stream AllReduce membership is used. Seed-only `GemmaRmsNorm`
operations stay raw (they have no residual input), as do preparation and sampling
outside complete units. The enclosing `layer` is a landmark-based container,
not a claim that its final fused norm belongs to that Python decoder module.

`boundary_label` is optional and only valid for `end_delimited` mode. It must
not collide with classifier labels or reserved ambiguity labels. Without it,
the historical included-right-delimiter convention is unchanged. With it,
classification examines only the body before the boundary; composition can
reference the boundary label like any other unit. A valid boundary remains
visible even after a clipped/unsupported body, but unknown or missing bodies
cannot form a complete layer. The token intervals of the three phase kinds are
disjoint: sum phases at one level, never add their parent layer again.

The full YAML is stored in `analysis_rules_yaml`; outer boundary mode and replay
rule identity/semantics are separate metadata. Realizations use the existing
HPO relations and concrete launch/member Perfetto geometry. Same-label layer
variants remain separate Positions unless their exact ordered signatures agree.
The September-21 banked-draft rank-0 acceptance observed 5 complete candidate
cycles and 396 layer windows across 12 launches (64 per large graph, 2 per small
graph), with all 10,460 device-event identities and timestamps unchanged.
Different captures must be checked for marker coverage and partial/no-match
states before reusing these hints.
