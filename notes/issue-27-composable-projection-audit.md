# Issue #27 Audit: From Node Hyperlinks to Composable Projections

Date: 2026-08-13
Issue: <https://github.com/vLLM-HUST/vllm-hust-perf-analyzer/issues/27>
Audited base: `origin/main` at `2f5a912`

## Finding

Issue #27 is not an unimplemented augmented-database project anymore. Its
original engineering contract---stable tree handles, occurrence expansion,
event/provenance drill-down, exact replay closure, cost lenses, reverse
navigation, self-contained raw evidence, and SQL-first discovery---has been
implemented across the merged #27/#28/#29 lineage and later query surfaces.

What remained implicit was the product-level operation enabled by those
relations:

> A user selects one structural scope and composes analytical projections by
> changing occurrence population, hierarchy resolution, observation domain,
> and compatible measure lens without rediscovering the scope boundary.

The issue's “node as hyperlink” idea is one prerequisite of this operation,
not its final formulation.

## Original acceptance contract against current main

| #27 capability | Current surface | Audit result |
| --- | --- | --- |
| Stable node/query handle and ordered hierarchy | `traceloom_v_tree_node`, `traceloom_v_node_children` | Implemented |
| Node to concrete occurrences | `traceloom_tree_node_occurrence` | Implemented |
| Occurrence to ordered anchors/events | `traceloom_tree_node_anchor`, `traceloom_event` | Implemented |
| Exact replay-internal closure | `traceloom_v_node_graph_body_member`, `traceloom_v_node_replay_cost_member` | Implemented under typed support contracts |
| Reverse event to structure | exact graph reverse views and evidence-role placement/structure views | Implemented with explicit relation kind |
| Cost lenses and contributor explanation | node/anchor cost views and normalized replay-cost relations | Implemented; scheduled work, busy union, and envelope remain distinct |
| Embedded raw-row audit | `traceloom_v_event_source_locator`, runtime/device source locators, raw table catalog | Implemented for regular and split inputs |
| Typed unsupported/issue state | replay, reconstruction, evidence-role, correlation, and host-window issue/status relations | Implemented |
| Self-describing SQL entry point | `traceloom_analysis_surface` | Implemented, but previously listed surfaces independently |
| First-class default artifact UX | default self-contained `analysis.db`; Markdown explicit | Implemented |
| Retained CUDA/Ascend validation | issue checkpoints, repository artifact verifiers, and current product-cost evidence | Implemented for the bounded retained envelopes |

The remaining unchecked boxes in the old issue body are stale bookkeeping,
not evidence that the underlying relations are absent.

The issue's opening terminology is stale as well. The current product object
is a queryable database timeline---a relational model over recovered execution
coordinates---rather than a “view analyzer” whose final output is one
privileged hierarchical rendering. Tree nodes remain first-class handles, but
their purpose is to parameterize many compatible projections.

## UX gap found by the audit

The README and 60-second tour still presented primarily two fixed traversals:
horizontal drill-down and vertical comparison. `traceloom_analysis_surface`
listed many useful relations, but it did not tell a new user that those
relations are orthogonal projections over one selected coordinate.

Consequences:

- users could perform the core interaction, but had to infer it from separate
  SQL files;
- switching `occurrence_idx` between one value and the whole population did
  not appear as one operation;
- hierarchy, exact replay expansion, host context, cost lenses, and bounded
  windows looked like unrelated features; and
- agents were encouraged to find a canned query rather than first declare a
  scope and projection axes.

## Product correction

This branch makes the composition contract artifact-native:

- `traceloom_projection_recipe` records
  `scope_kind × population_mode × resolution × observation_domain × measure_lens`;
- `traceloom_projection_parameter` publishes typed, nullable selectors and
  their candidate-coordinate relations without requiring prose parsing;
- recipes use named SQL selector parameters such as `:node_id` and
  `:occurrence_idx`;
- `:occurrence_idx = NULL` selects the full population, while a concrete value
  selects one realized execution;
- the same node coordinate supports folded occurrence cost, child hierarchy,
  anchors/events, exact replay members, aligned-position statistics, and host
  context;
- structural-position recipes connect bubble populations to supported host API
  observations;
- bounded device windows are explicit exploratory scopes but do not become
  committed patterns merely by selection; and
- `analytical_projection_contract` versions the UX in database metadata.

SQL remains the semantic interface. The recipe catalog does not add a second
CLI/query language; agents, notebooks, and UIs bind the same parameters and
render the returned relations.

## Validation on retained profiles

The implementation was exercised as the UX is intended to be used, not only
prepared syntactically:

- A fresh DB from the checked-in Ascend kickstart profile published 9 recipes
  and 16 typed selectors. One high-level `Rep x74` scope was reused for 29
  folded occurrences, 2,146 ordered members, 74 aligned-position aggregates,
  and 25,961 host-context distribution rows. All nine recipes executed; the
  only empty projection was exact replay, consistent with that input's typed
  `replay_unit_count=0` boundary.
- A fresh DB from the immutable real-model CUDA Graph bundle selected one
  `ReplayUnit` node and returned 49,405 exact members across all five
  occurrences or 9,881 members for occurrence 1. Every member retained a
  unique ordered key and embedded raw locator; one internal event navigated
  back to its exact graph row and `graph_body_member` structural placement.
- The enabled native suite passed 53/53; 11 external-fixture tests remained
  explicitly disabled in the local checkout.

Task-local receipts and generated databases live under
`/tmp/traceloom-composable-projection-{ux,cuda}/`; they are validation
artifacts, not repository inputs.

## Recommended issue disposition

After the composable-projection UX lands and its retained-profile tour passes,
close #27 as completed with a final receipt. Do not keep the old mega-issue open
as a generic wish list. Any future missing relation should become a narrow issue
named after the unsupported projection coordinate or evidence contract.
