# AugDB SQL UX Migration

TraceLoom is converging on one public investigative coordinate system without
discarding the specialized projections that made the existing AugDB useful.
The migration rule is:

> Retire competing navigation nouns; preserve analytical verbs and evidence.

This document records the migration boundary. It is not a table-deletion
schedule.

## Target investigative flow

The default SQL UX should read as one evidence-releasing investigation:

```text
Position
  -> contextual edge roles
  -> one concrete Occurrence edge stream
  -> one equivalent-edge population
  -> one unusual child Occurrence
  -> cost / host / replay / collective lens
  -> literal event, runtime call, or embedded profiler row
```

The coordinate kernel is Position, Occurrence, concrete ordered edge, and
contextual `edge_role_id`. A lens may retain its specialized physical tables,
support states, and cost semantics. It should accept or return coordinates
from this kernel rather than require the user to begin in another structural
model.

## Surface classes

| Class | Treatment | Examples |
| --- | --- | --- |
| Coordinate kernel | primary public navigation | `hpo_*`, `tree_edge_roles`, `tree_edges`, `equivalent_tree_edges` |
| Evidence and audit | retain as authoritative specialized relations | event/source locators, runtime/device relations, graph/replay members, collective pairing, reconciliation and policy decisions |
| Analytical lens | preserve the method; re-key its recipe to Position/Occurrence/edge coordinates | host windows, bubbles, replay cost, symbol variants, synchronization actions |
| Presentation projection | retain as a downstream reading of selected coordinates | annotated and flattened timelines, tree/Markdown/paper figures |
| Compatibility navigation | stop teaching as the default; keep until consumers and tests migrate | `scope_*`, anchor-order `position_*`, Pattern-named replay navigation |
| Internal materialization | may remain for performance even after leaving the public UX | tree-node occurrence/anchor coverage, semantic/viz rows, bubble and replay aggregate tables |

A physical table is removed only when it has no unique evidence, no remaining
consumer, and no measured materialization value. Leaving the default UX is not
the same operation as deleting storage.

The first-generation `scope_*`, anchor-order `position_*`, and node-keyed host
recipes remain executable, but their catalog purpose begins with
`compatibility:` and their `display_order` is at least 100. Thus the default
ordered recipe catalog teaches the Position/Occurrence/edge route first
without breaking existing SQL.

## Current migration matrix

| Existing entry | Valuable analytical verb | New/default route | Current state |
| --- | --- | --- | --- |
| `scope_catalog` | rank coarse structural candidates | `hpo_positions` | compatibility; Position catalog is primary |
| `scope_occurrences` | compare one/all realizations | `hpo_occurrences` | compatibility |
| `scope_hierarchy` | reveal immediate structure | `tree_edge_roles`, `tree_edges`, or `hpo_refinements` | compatibility |
| `scope_members` | reach terminal event evidence | `hpo_members -> event_audit` | compatibility; exact replay remains a specialized branch |
| `position_population` | compare corresponding ordered locations | `tree_edge_roles -> equivalent_tree_edges` | compatibility; edge equivalence replaces label/anchor-order grouping |
| `position_occurrences` | select one outlier without losing context | choose `child_occurrence_id` from `equivalent_tree_edges` | compatibility |
| `scope_host_windows` | retain typed supported/unsupported host intervals | `occurrence_host_windows` | migrated wrapper; legacy recipe retained |
| `scope_host_context` | compare bounded runtime API distributions | `occurrence_host_context` | migrated wrapper; legacy recipe retained |
| `replay_body_pattern*` | recover and audit recursive replay internals | `replay_hpo_*` plus exact replay evidence recipes | partially migrated; Pattern storage remains compatibility and terminal lineage bridge |
| bubble surfaces | quantify recurrent uncovered intervals and host visibility | `tree_edge_roles -> edge_role_bubble_summary -> edge_role_bubbles` | occurrence recipe replaced and removed; legacy global hotspot and API-family aggregate retained |
| replay cost/partition/member surfaces | retain exact protected membership and non-interchangeable cost lenses | branch from selected Occurrence/edge into replay coordinates | specialized evidence retained; continuation still being simplified |
| annotated/flattened timeline | show replay regions and exact members on one plane | presentation after coordinate selection | retained, not a competing data model |

## First completed vertical slice: equivalent edge to host evidence

`equivalent_tree_edges` returns the exact `child_occurrence_id` of every member
of one context-safe edge population. That coordinate now continues directly to:

```text
occurrence_host_windows
  -> host_window_calls
  -> runtime_call_audit

occurrence_host_context
  -> host_window_calls
  -> runtime_call_audit
```

The migrated recipes preserve the old host method's important properties:

- every interval retains a typed support state;
- supported-but-empty and unsupported windows are not hidden;
- runtime calls are intersected at query time after a bounded selection;
- scheduled call overlap is not called overlap-safe host busy time;
- the route does not assign a host cause to device idle; and
- runtime-call source locators remain available for audit.

They remove one UX burden: the user no longer translates an Occurrence back
into legacy `node_id + occurrence_idx` selectors.

The query must push the selected Occurrence coordinates into
`traceloom_v_node_host_interval` before joining. On the checked-in kickstart
artifact, a join that first materialized the global host view took about
300 ms for one row; the bounded form took about 0.2 ms after a warm cache.
Preserving an analytical verb includes preserving its bounded execution shape.

## Second vertical slice: contextual edge role to bubble evidence

The bubble lens now starts from `edge_role_id`, not from the legacy
`structural_position_id`:

```text
tree_edge_roles / equivalent_tree_edges
  -> edge_role_bubble_summary
  -> edge_role_bubbles
       -> occurrence_host_windows / occurrence_host_context
       -> host_window_calls -> runtime_call_audit
```

`edge_role_bubble_summary` counts both positive bubble observations and
concrete edges with no positive bubble. `edge_role_bubbles` preserves every
concrete child Occurrence and labels those two states explicitly; selecting a
returned `child_occurrence_id` or `host_interval_id` therefore does not require
translation through a legacy node selector. `bubble_us` remains the
overlap-safe uncovered device cost immediately before the right, self-owned
anchor. The associated host interval is contextual evidence, not a cause.
The summary's `positive_bubble_host_observation_coverage` is conditional on
positive bubbles; a role with no positive bubble reports zero applicability,
not evidence that all of its ordinary host windows are unsupported.

This slice deliberately does **not** materialize an edge-by-bubble table or a
global bubble-by-runtime-call relation. Git history records that the latter
class of global interval/activity expansion was removed after one real profile
produced a conservative candidate bound of billions of rows. The role recipes
first materialize one selected role, then use the existing
`(db_idx, device_id, view_name, structural_position_id)` bubble index to bound
the population. On the checked-in kickstart artifact, this changed a naive
2,146-edge join from roughly 28--29 seconds to 35--43 ms without adding a
persisted index or relation.

The superseded `bubble_occurrences` recipe is removed from the catalog on this
migration branch; its physical bubble evidence remains. `bubble_hotspots` and
`bubble_host_context` stay because the global hotspot and API-family population
aggregate still answer useful questions that have not yet earned equally
bounded edge-role replacements.

### Bounded dogfood observation (2026-08-21)

A Release build containing this slice regenerated the first checked-in
kickstart profile into a 777,744,384-byte AugDB in 28.788 seconds. Catalog-only
addition did not restore any global host-activity materialization.

Following the catalog rather than handwritten joins:

- `edge_role_bubble_summary` summarized the 2,146-edge `Fill` role under
  `node-N264` in 35.6 ms;
- `edge_role_bubbles` returned all 2,146 concrete child Occurrences in 46.6 ms;
- selecting `node-N265-occurrence-1113` returned one 707,340.233-us positive
  bubble in 13.3 ms, while another member had only 0.020 us;
- `host_window_calls` returned 323 profiler-visible calls from the selected
  707,292.840-us host interval in 62.0 ms, and `runtime_call_audit` resolved a
  call to its embedded `CANN_API` locator in 0.35 ms; and
- a different 696-edge role returned 696 explicit `no_positive_bubble` rows,
  proving that absence was not erased by the lens.

The long host interval contained only about 13.7 ms of summed scheduled call
overlap. That contrast is useful evidence for a follow-up question, but it is
not proof that unprofiled CPU or scheduler work caused the device bubble.
These are bounded observations for the named artifact and build, not universal
latency guarantees.

The dogfood also found that the old tour verifier counted the unfiltered
query-time `traceloom_v_structure_bubble_host_context` view. It consumed one
CPU for more than three minutes before being stopped. The verifier now binds
one small position for the retained API-family lens and follows the new
edge-role route for literal-call audit; it completes in about one second on
the same artifact.

## Deprecation gates

Before demoting an old recipe or relation further:

1. identify the question it answers, not merely its columns;
2. identify any unique evidence, typed support boundary, and cost semantics;
3. provide a coordinate-continuous new route;
4. compare old and new results on a real artifact and a bounded fixture;
5. retain or improve query shape and latency;
6. migrate examples, agent guidance, paper queries, and continuation metadata;
7. keep a compatibility route until no shipped consumer depends on it; and
8. delete physical storage only under the stronger evidence/consumer/performance
   gate above.

## Consequence for evaluation

The system evaluation should follow these investigative transitions rather
than count table names or synthetic query distance. A strong RQ can ask whether
the relational timeline model lets an analyst move from a macro anomaly to a
valid comparison population, then change observation domain and reach literal
evidence without rebuilding structural correspondence in client SQL.

The concrete evidence is the sequence of selected coordinates, typed support
boundaries, bounded SQL recipes, and newly exposed behavior—not the existence
of a large surface catalog.
