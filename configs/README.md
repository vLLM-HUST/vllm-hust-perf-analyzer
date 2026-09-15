# Optional model matching hints

Pass `--match-rules FILE.yaml` to `traceloom` (or the native fixture analyzer).
Omitting the flag leaves grammar matching unchanged. Match exact **normalized
structural symbols**, not provider labels or generated macro names.

```yaml
schema: traceloom-match-rules-v1
ordered_markers:
  - id: hc_pre_post
    before: HcPre
    after: HcPost
```

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

Use one YAML document, at most 1 MiB. Unknown/duplicate fields, missing fields,
duplicate rule IDs, identical before/after symbols, wrong node types and invalid
YAML fail explicitly. No scripts, includes, regexes or arbitrary expressions are
executed. libyaml handles YAML syntax, including quoting and comments.

The AugDB metadata keys `match_rules_yaml` and `match_rules_semantics` retain the
exact supplied document and interpretation. Grammar debug JSON includes resolved
rules under `algorithm.ordered_markers` when enabled. Pair matching still uses
its existing frequency/gain criterion: a permitted candidate is not guaranteed
to be selected. Compare with the no-rule baseline to judge the hint's effects.

Hints are user-supplied prior knowledge. Report them with the result; do not
present constrained recovery as discovering layer/step boundaries from timing
alone. The sample is installed under `share/traceloom/models/deepseekv4.yaml`.
