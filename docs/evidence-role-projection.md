# Evidence-role projection contract

TraceLoom discovers repeated structure from a dense accelerator timeline, but
not every observed row should have equal authority over structural identity.
The evidence-role projection is the versioned contract that decides which
observations participate in identity matching. It is a **structural
projection**, not an evidence-deletion pass: normalized events, cost-bearing
intervals, context, and source-row provenance remain available in the native
IR and the self-contained queryable database timeline.

The public contract is provider-neutral. Provider-specific predicates and
their stable identities live in the installed machine-readable **flat TSV
table** (a delimiter-separated table suitable for version review and
spreadsheet editing),
[`native/data/default_signal_classification_rules.tsv`](../native/data/default_signal_classification_rules.tsv).

## Roles and boundaries

| Role or category | Positive admission condition | Identity matching | Cost treatment | Context retained | Provenance retained | Failure behavior |
| --- | --- | --- | --- | --- | --- | --- |
| **anchor** | A supported identity-bearing rule matches | yes | retained for attribution | yes | yes | n/a |
| **auxiliary** | A positively recognized control, movement, wait, or surrounding-work rule matches | no | retained for attribution | yes | yes | missing evidence falls through to a lower rule or fallback |
| **transparent** | A positively recognized nondiscriminating carrier rule matches | no | retained for attribution under the typed policy | yes | yes | missing evidence falls through to a lower rule or fallback |
| **unknown anchor** | No supported non-anchor rule admits the observation | yes | retained for attribution | yes | yes | remains explicit and may disrupt a structural match |
| **protected composite / boundary** | Exact or typed-open provider evidence creates a protected interval | atomic boundary | retained | yes | yes | remains typed open or unsupported; generic discovery cannot cross or fragment it |

A residual gap remains an interval relation; it is not forced into the task
role enum. Protected composites are likewise a boundary category rather than a
task classification. This separation prevents an event-name rule from
weakening provider-backed graph/replay atomicity.

## Unknown-first decision rule

Only a positive `auxiliary` or `transparent` rule can remove an observation
from structural matching. If the required source
fields are absent, the rule does not match: evaluation continues at lower
precedence and then reaches the manifest fallback. The default fallback is:

- every unmatched observation becomes `unknown_anchor`, whether or not its
  provider schema exposes a concrete operator field.

The default Ascend `AI_CORE` carrier rule illustrates the distinction. It is
transparent only when no concrete operator identity is available. With a
concrete but unfamiliar operator, its `defer_to_identity_rules` behavior lets
specific identity rules run and ultimately preserves the operation as an
unknown anchor. A new fusion or runtime path therefore cannot silently vanish
as generic carrier noise.

## Versioned manifest

The v1 manifest schema is `traceloom.evidence-role-policy/v1`. Its metadata
preamble makes these policy-wide facts serializable:

- `policy_id`, `policy_version`, and the SHA-256 of the exact manifest bytes;
- declared provider scopes;
- the unknown-first fallback;
- fallback cost, context, and provenance treatment;
- missing-evidence behavior.

Each rule records:

- a stable `rule_id`, `provider_scope`, and source domain;
- priority plus declaration order;
- the positive predicate (`field`, `match`, and `pattern`);
- `required_fields` as its capability precondition;
- role and structural participation;
- cost, context, and provenance treatment;
- missing-evidence and concrete-identity behavior;
- a human note.

The loader validates the exact schema, metadata, enums, provider scopes,
required-field vocabulary, role/participation agreement, stable rule-ID
uniqueness, and ambiguous duplicate predicates. Invalid policies fail before
analysis starts.

### Precedence and provider scope

Rules are evaluated by descending priority and then manifest declaration
order. Two rules with the same provider, source domain, predicate, and priority
are contradictory and rejected. Higher-priority rules are the explicit
extension mechanism.

Provider-specific rules apply only to matching provider observations; `any`
rules are shared. Production source adapters supply the provider scope through
the normalized source reference. Unknown/fixture sources keep the legacy
provider-neutral behavior for deterministic test and embedding APIs.

Complete replacement manifests can be selected with
`--classification-rules PATH` or `TRACELOOM_CLASSIFICATION_RULES`. A policy can
be extended with `--extend-classification-rules PATH`; the composite policy ID,
version, and digest include both inputs. Legacy seven-column rule files remain
loadable for compatibility, but receive a content-addressed legacy policy
version and deterministic synthetic rule IDs. New policies should use v1.

The final per-analysis override is explicit and stable-ID keyed:

```bash
traceloom profile.db \
  --classification-rules policy.tsv \
  --classification-rule-override rule.id.priority=250 \
  --classification-rule-override rule.id.role=anchor \
  --classification-rule-override rule.id.structural_participation=identity
```

Each `RULE_ID.FIELD=VALUE` changes one typed column after the base table and
optional extension are loaded. Unknown rule IDs, unsupported fields, duplicate
overrides, or a role/participation conflict fail before analysis. The effective
precedence is:

```text
bundled/install default or TRACELOOM_CLASSIFICATION_RULES
  < explicit --classification-rules replacement
  < --extend-classification-rules
  < --classification-rule-override
```

`--classification-rules` therefore explicitly supersedes the environment
selection. The database keeps both the exact flat-table digest and a separate
effective-config digest plus canonical override list; an override never
pretends to be a different input table.

## Audit surfaces

The selected `policy_id`, `policy_version`, and exact manifest SHA-256 are
emitted in native JSON under `anchor_projection` and in the queryable database
timeline's `traceloom_metadata`. `traceloom_evidence_role_policy` and
`traceloom_evidence_role_rule` embed the effective flat table; per-event
decisions, placements, cost coverage, and typed issues live in the corresponding
`traceloom_evidence_role_*` relations. Build statistics separately report
auxiliary, transparent, and unknown-first preservation counts. These values let
an artifact consumer bind recovered structure to the exact projection policy.

The manifest explains **why an observation may participate in structural
identity**. The original row and normalized event remain the authority for
what the profiler observed. Structural compression never claims that omitted
events are physically irrelevant, reconstructs a lossless multi-stream
schedule, or assigns model/source semantics.
