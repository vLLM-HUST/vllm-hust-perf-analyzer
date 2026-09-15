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
