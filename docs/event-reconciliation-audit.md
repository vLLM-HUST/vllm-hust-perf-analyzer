# Auditing Event Reconciliation

Event reconciliation is a narrow, provider-backed transformation for the case
where multiple profiler rows are complementary observations of one physical
device action. TraceLoom never asks users to trust that transformation as a
hidden preprocessing step. The effective policy, its rules, every candidate
decision, every member contribution, the canonical anchor, and the original
source locators are all stored in `analysis.db`.

This guide answers four questions in order:

1. **Which policy did this artifact use?**
2. **What did that policy allow?**
3. **Which candidate groups were reconciled or left independent?**
4. **Which observation supplied timing, symbol, and cost?**

## Mental model

```text
policy
  -> effective rules
       -> candidate-group decision
            -> member observations
                 -> canonical anchor (only when reconciled)
                 -> normalized event -> embedded profiler row
```

| Relation | One row means | Use it when asking |
| --- | --- | --- |
| `traceloom_event_reconciliation_policy` | the effective manifest for this analysis | “Which policy and digest produced this artifact?” |
| `traceloom_event_reconciliation_rule` | one effective provider predicate | “What exact pairing was allowed, and where did the rule come from?” |
| `traceloom_event_reconciliation_decision` | one candidate identity group | “Was this group reconciled, independent, ambiguous, or conflicting?” |
| `traceloom_event_reconciliation_member` | one normalized event in a decision | “Which original row played which role?” |
| `traceloom_v_event_reconciliation` | a member joined to its decision, normalized event, and canonical anchor | “Show me the complete explanation for this event or decision.” |

The first four relations are the normalized audit record. The view is the
normal interactive entry point; it saves users from reconstructing joins.

## Three-step audit

Open the generated artifact read-only:

```bash
sqlite3 -readonly /path/to/traceloom/analysis.db
```

Enable readable output:

```sql
.headers on
.mode box
```

The database also advertises every entry point without requiring this manual:

```sql
SELECT surface_name, relation_name, row_grain, purpose
FROM traceloom_analysis_surface
WHERE surface_name GLOB 'event_reconciliation*'
ORDER BY surface_name;
```

### 1. Confirm the effective policy and rules

```sql
SELECT policy_id, policy_version, manifest_schema, source_manifest,
       manifest_sha256, unmatched_behavior
FROM traceloom_event_reconciliation_policy;

SELECT rule_id, priority, provider_scope, source_domain, task_type,
       generic_context_id, concrete_context_id, min_contained_fraction,
       rule_origin, rule_origin_sha256
FROM traceloom_event_reconciliation_rule
ORDER BY priority DESC, rule_id;
```

`source_manifest` and `manifest_sha256` identify the effective composite
policy. Each rule also retains its own origin and digest, so an overlay cannot
silently impersonate the bundled default.

### 2. Summarize outcomes before opening individual rows

```sql
SELECT status, reason_code, count(*) AS decision_count
FROM traceloom_event_reconciliation_decision
GROUP BY status, reason_code
ORDER BY status, reason_code;
```

Interpret the statuses as follows:

- `reconciled`: one supported, unique pair produced one canonical anchor;
- `independent`: the observed candidate group did not have a supported peer;
- `ambiguous`: more than one candidate could satisfy the relation;
- `conflict`: candidate evidence disagreed on a required invariant.

The latter three outcomes fail open: their original candidates stay
independent in the structural sequence. A missing decision row does not mean
an event was discarded; it normally means the event was outside every
event-reconciliation rule and followed the ordinary projection path.

To rank decisions for inspection, put uncertain outcomes first:

```sql
SELECT decision_id, rule_id, status, reason_code, member_count,
       canonical_event_id, envelope_event_id, canonical_anchor_id,
       contained_fraction
FROM traceloom_event_reconciliation_decision
ORDER BY CASE status
           WHEN 'conflict' THEN 0
           WHEN 'ambiguous' THEN 1
           WHEN 'independent' THEN 2
           ELSE 3
         END,
         db_idx, decision_id
LIMIT 100;
```

### 3. Drill into one decision or event

Bind a `decision_id` returned above:

```sql
.parameter init
.parameter set :decision_id 'reconciliation-decision-0'

SELECT decision_id, status, reason_code, event_id, member_role,
       contributes_timing, contributes_symbol, contributes_cost,
       observed_symbol, observed_dur_us,
       canonical_event_id, canonical_anchor_id,
       canonical_symbol, canonical_anchor_dur_us,
       source_table, source_key
FROM traceloom_v_event_reconciliation
WHERE decision_id = :decision_id
ORDER BY member_order;
```

For a successful pair, one member normally supplies the timing envelope and
cost while the semantic-detail member supplies the structural symbol. The
contribution flags are the contract; do not infer roles from duration or event
names.

If the investigation starts from an event rather than a decision, use the
projection recipe embedded in the database:

```sql
.parameter set :event_id 'event-109'

SELECT decision_id, status, reason_code, event_id, member_role,
       contributes_timing, contributes_symbol, contributes_cost,
       canonical_anchor_id, observed_symbol, canonical_symbol
FROM traceloom_v_event_reconciliation
WHERE event_id = :event_id
   OR canonical_event_id = :event_id
   OR envelope_event_id = :event_id
ORDER BY decision_id, member_order;
```

The same query is registered as `event_reconciliation_audit` in
`traceloom_projection_recipe`.

## Continue to structure or raw evidence

To find where a reconciled canonical anchor appears in recovered structure:

```sql
SELECT r.decision_id, r.canonical_anchor_id,
       n.node_id, n.occurrence_idx, n.anchor_order, n.coverage_kind
FROM traceloom_event_reconciliation_decision r
JOIN traceloom_tree_node_anchor n
  ON n.db_idx = r.db_idx
 AND n.anchor_id = r.canonical_anchor_id
WHERE r.decision_id = :decision_id
ORDER BY n.node_id, n.occurrence_idx, n.anchor_order;
```

To reach the embedded profiler row for every member:

```sql
SELECT r.decision_id, r.member_role, r.event_id,
       l.source_path, l.source_table, l.source_key,
       l.embedded_table_name, l.source_rowid_column, l.resolution_status
FROM traceloom_v_event_reconciliation r
JOIN traceloom_v_event_source_locator l
  ON l.db_idx = r.db_idx
 AND l.event_id = r.event_id
WHERE r.decision_id = :decision_id
ORDER BY r.member_order, l.source_ordinal;
```

The locator tells the next bounded raw-table query. SQL cannot substitute a
table name dynamically, so inspect `embedded_table_name` and
`source_rowid_column`, then query that exact copied table and row.

## Replacing or extending policy

The bundled manifest is
[`native/data/default_event_reconciliation_rules.tsv`](../native/data/default_event_reconciliation_rules.tsv).
Use it as the schema example.

```bash
# Replace the bundled policy for this analysis.
traceloom profile.db \
  --event-reconciliation-rules /path/to/replacement.tsv

# Add rules or overwrite a bundled rule with the same stable rule_id.
traceloom profile.db \
  --extend-event-reconciliation-rules /path/to/overlay.tsv
```

After either command, rerun steps 1 and 2 against the new `analysis.db` rather
than assuming the file was accepted as intended. The recorded effective
policy, rule origins, and outcome counts are the receipt.

## Scope boundary

Event reconciliation is not a general temporal containment hierarchy. A rule
must name provider evidence that establishes shared identity; interval overlap
alone is insufficient. Reconciliation also does not delete normalized events
or raw rows. Its only structural effect is to let one uniquely supported set
of complementary observations contribute one canonical anchor and one charge
of physical cost.

For neighboring TraceLoom transformations, see
[the evidence-role projection contract](evidence-role-projection.md) and the
structural-symbol audit described in
[the main database-timeline walkthrough](../README.md#audit-structural-symbol-normalization).
