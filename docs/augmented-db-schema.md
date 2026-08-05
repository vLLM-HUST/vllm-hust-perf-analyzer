# Augmented DB Schema

TraceLoom's primary output model is an augmented SQLite database. The analyzer
keeps the original `msprof` tables intact and appends TraceLoom-owned tables
under the `traceloom_*` namespace. Reports are SQL queries over those tables,
not a separate report-specific data model.

The default implementation writes one sidecar copy per discovered msprof DB:

```text
<analysis_dir>/db01.traceloom_augmented.db
<analysis_dir>/db02.traceloom_augmented.db
```

Each sidecar contains the original raw tables plus TraceLoom augmentation
tables. This preserves the source DB as evidence while still allowing SQL joins
against raw `TASK`, `STRING_IDS`, and `COMMUNICATION_OP` tables.

## Core Tables

### `traceloom_event`

Normalized profiler events used by TraceLoom. Rows can point at raw `TASK`,
raw `COMMUNICATION_OP`, or synthetic events created by TraceLoom.

Important columns:

- `event_id`: stable TraceLoom event key.
- `source_table`: `TASK`, `COMMUNICATION_OP`, or `SYNTHETIC`.
- `source_key`: best-effort raw identity string.
- `start_ns`, `end_ns`, `dur_us`.
- `category`: `exec`, `comm`, `wait`, or other normalized category.
- `role`: TraceLoom event role, such as `compute`, `collective`, `data_move`.
- `semantic_role`: `anchor`, `aux`, `transparent`, or `raw`.
- `label`, `family`, `task_type`.

### `traceloom_event_source`

Lineage for synthetic or coalesced events. For example, a collective anchor
created from `COMMUNICATION_OP` can link back to the underlying task
`globalTaskId` and streams.

### `traceloom_anchor`

The semantic anchor sequence. Every anchor is a leaf event in the final
visualization.

Important columns:

- `anchor_id`: stable TraceLoom anchor key.
- `anchor_idx`: 1-based order in the selected device timeline.
- `event_id`: normalized event backing this anchor.
- `symbol`, `role`, `label`, `family`.

### `traceloom_aux_link`

Auxiliary/prelude attribution. Aux events attach to the following anchor, not
directly to loop nodes.

Important columns:

- `anchor_id`: target anchor.
- `aux_event_id`: auxiliary event attached to the anchor.
- `link_type`: currently `prelude`.
- `reason`: semantic-role reason from the classifier.

### `traceloom_cuda_graph_replay`

Graph replay evidence. CUDA rows correspond to normalized `CudaGraphReplay`
events backed by `CUPTI_ACTIVITY_KIND_GRAPH_TRACE`. Ascend rows correspond to
synthetic `ACL_GRAPH_REPLAY` events reconstructed from device-side
`CaptureStreamInfo`, `TASK`, and `ascend_task.db` evidence.

Important columns:

- `graph_event_id` / `event_id`: stable TraceLoom event key for the replay.
- `graph_provider`: `cuda` or `aclgraph`.
- `graph_kind`: provider-specific replay kind.
- `correlation_id`, `graph_id`, `graph_exec_id`, `context_id`: parsed Nsight
  graph identity fields; for ACLGraph these are best-effort synthetic graph
  identifiers.
- `start_ns`, `end_ns`, `dur_us`.
- `enclosed_event_count`, `enclosed_kernel_count`: best-effort envelope counts.

For native ACLGraph reconstruction, host-side `aclmdlRIExecuteAsync` rows are
used only to partition complete replay waves. The reported interval is the
device-side `TASK` envelope, so host launch overhead and inter-wave gaps do not
inflate graph execution cost. When `MODEL_EXECUTE.connectionId` evidence is
available, host launches without a matching device execution are excluded.
`raw_json` records the reconstruction mode, template hash, and recovered
capture-group size.

### `traceloom_cuda_graph_envelope`

Best-effort links from a graph replay event to visible TraceLoom events
associated with the replay. CUDA rows first use direct time containment, with a
same-stream `post_replay_segment` fallback for Nsight exports that emit GPU
activity immediately after the graph trace interval. ACLGraph rows use
device-time overlap between the reconstructed replay interval and normalized
TraceLoom events.

Important columns:

- `graph_event_id`: replay event id.
- `child_event_id`: contained CUDA activity event id.
- `graph_provider`: `cuda` or `aclgraph`.
- `relation`: `time_contained`, `time_overlap`, or `post_replay_segment`.
- `start_offset_us`, `end_offset_us`: child timing relative to graph replay
  start.

Native ACLGraph envelope `raw_json` retains the child's `SourceRef` and source
row identifiers so every overlap relation can be drilled back to profiler
evidence.

### `traceloom_aclgraph_reconstruction_region`

The typed ACLGraph capability and recognition ledger. Every exact or rejected
composition region retains its recognition `status`, candidate policies,
observed/expected launch counts, and exact device interval. A row with
`status LIKE 'unrecognized_%'` is an auditable negative result, not a promoted
ReplayUnit.

Use `docs/report-sql/reconstruction-capability-matrix.sql` to reduce this
ledger and the promoted replay rows into one capability row per sidecar. It
distinguishes capability absence, contradictory/incomplete body evidence,
missing completion evidence, exact promotion, and the explicit legacy path.

## Device Idle Evidence

The following tables materialize the Ascend E1→E4 device-only evidence path.
They are absent or empty for providers without a validated semantic taxonomy.
Nanosecond columns are authoritative; `duration_us` is a readable derivative.

### `traceloom_run_metadata`

One deterministic analysis record containing status, analysis span, contract
and ruleset versions/hashes, collection status, source identity, and canonical
metadata JSON. `run_id` is the lowercase SHA-256 of that canonical JSON with
the `run_id` field excluded.

### `traceloom_device_interval`

The exact device timeline partition into `productive_active` and
`visible_productive_idle`. Each interval carries its clock domain and contract,
semantic-rules, and attribution-rule versions.

### `traceloom_stream_state`

Per-observed-stream mutually exclusive state partitions. Universe size,
observed-universe scan completeness, and externally attested collection status
travel with every row so an empty stream interval cannot silently become an
absence claim.

### `traceloom_idle_explanation`

The exact E4 partition of every visible productive gap. Each slice links to its
owning `gap_interval_id` and records frozen category, evidence level/relation,
alignment status, reason, collection status, and version fields.

### `traceloom_evidence_link`

Exact source-row lineage for explanation evidence and E2/E3 provenance.
`device_event_coverage` links carry a positive overlap extent. Diagnostic
`relation='none'` links deliberately carry null overlap fields and do not
support an explanation claim.

### `traceloom_anchor_idle_explanation` and `traceloom_node_idle_explanation`

Exact anchor-prelude intersections and their hierarchical Loop Tree
aggregation. Node rows are not additive across parents and children; the sum
over root nodes equals the anchor-attributed total, while the difference from
device gap duration remains an explicit device-only residual.

Two checked-in report queries form the golden audit surface:

- `docs/report-sql/idle-evidence-summary.sql` emits paper-ready category,
  evidence, exact duration, and visible-gap share rows.
- `docs/report-sql/idle-evidence-audit.sql` checks interval/explanation
  arithmetic, per-gap coverage, non-overlap, stream adjacency, lineage,
  evidence extents, anchor/node references, and root conservation. Its final
  `audit_status` is `PASS` only when every checked invariant holds.

`audit_status` and `analysis_status` answer different questions. `PASS` means
the materialized tables are internally well formed and conserve their stated
partition; it does not override `analysis_status=invalid_input` or establish
collection completeness. A positive semantic result requires both a passing
audit and an analysis/collection status sufficient for that specific claim.

## Visualization Structure

### `traceloom_viz_node`

The final compressed visualization nodes. Repeat and sequence nodes are kept as
formal nodes so users can query loop structure directly.

Important columns:

- `node_id`: stable TraceLoom node key.
- `local_node_id`: user-facing node id used inside the readable tree, tree
  JSON, cost tables, and SQL drill-down views, such as `N004`. The same
  `Nxxx` value must name the same node everywhere in one report.
- `raw_node_id`: when present inside node JSON payloads, this is the builder's
  pre-normalization id. It is provenance only and is not the SQL/report key.
- `view_name`: currently `anchor_tree`.
- `node_type`: `Seq`, `Repeat`, `Atom`, `MacroRef`, etc.
- `kind`: cost kind, such as `seq`, `repeat`, `exec`, `comm`, or `graph`.
  `kind='graph'` rows are device-side graph replay overlays such as Ascend
  ACLGraph replay intervals.
- `level`: displayed tree level.
- `repeat_count`: parsed repeat factor, when available.
- cost columns copied from node metrics: `total_us`, `compute_us`, `comm_us`,
  `idle_us`, `aux_us`, and average variants.

### `traceloom_viz_edge`

Parent-child edges for the compressed visualization tree.

### `traceloom_structural_unit`

The neutral, graph-centered top-level partition of the productive anchor
sequence. It is present when exact graph units are available. A row has one of
three `kind` values:

- `graph_unit`: a profiler-evidence-backed exact graph unit;
- `structural_unit`: a nonempty productive sequence bounded by adjacent exact
  graph units; or
- `unrecognized`: an observed open prefix or suffix whose missing outer
  boundary prevents complete promotion.

`body_fingerprint` is a stable hash of visible operator/category/anchor-kind
identity; graph fingerprints also include the recovered graph-unit structural
label. `family_id` groups identical fingerprints within one report.
`token_start_ordinal` is inclusive and `token_end_ordinal` is exclusive.
`span_us` is the wall-clock envelope, while `total_us` is the additive
compute/communication/idle packet total. `expansion_nodes` contains complete
Loop Tree occurrence handles for drill-down. `shape_signature='unavailable'`
is explicit until shape evidence is materialized rather than inferred.

### `traceloom_structural_unit_anchor`

Exact anchor membership for structural units. Every productive anchor belongs
to exactly one row when the structural partition is present. `anchor_order` is
zero-based within the possibly folded unit and `membership_role` is currently
`observed_member`. Use `docs/report-sql/structural-composition.sql` for the
wide unit view and an immediate `anchor_count`/membership-count check. Use
`docs/report-sql/structural-composition-audit.sql` for the full partition
integrity verdict; `PASS` attests internal conservation, not workload meaning.

### `traceloom_semantic_tree`

Durable header for a recovered semantic execution tree. This table makes the
compressed tree a first-class augmented-DB artifact instead of only a Markdown
or JSON report.

Important columns:

- `tree_id`: stable tree key.
- `view_name`: analyzer view, such as `anchor_tree`.
- `tree_kind`: semantic tree flavor copied from the tree payload.
- `root_node_id`: root row in `traceloom_semantic_node`.
- `semantic_projection`, `macro_discovery`, `auxiliary_attribution`: recovery
  policy metadata.

### `traceloom_semantic_node`

Preorder semantic tree nodes with parent pointers, display path, structural
kind, repeat count, anchor span, cost columns, and hidden auxiliary attribution.
This is the table to query when a report needs the readable tree structure.

Important columns:

- `node_id`: stable node key; intentionally matches `traceloom_viz_node.node_id`
  for the same local node.
- `local_node_id`: same user-facing `Nxxx` namespace as the readable tree and
  cost tables.
- `parent_node_id`, `preorder_idx`, `sibling_order`, `path`: tree structure.
- `node_type`: `Seq`, `Repeat`, `Atom`, etc.
- `semantic_kind`, `label`, `symbol`, `category`: recovered behavior label.
- `repeat_count`, `occurrence_count`, `anchor_count`.
- `first_anchor_idx`, `last_anchor_idx`, `start_ns`, `end_ns`.
- `total_us`, `compute_us`, `comm_us`, `idle_us`, `self_us`.
- `hidden_aux_event_count`, `hidden_aux_us`: auxiliary evidence attributed to
  the node but normally hidden from the main readable tree.

#### Timeline cost contract

The additive node cost is wall-clock based:

- `total_us = compute_us + comm_us + idle_us`.
- Each device-time slice contributes at most once, even when streams overlap.
  Communication takes precedence over compute when classifying an overlapping
  slice; auxiliary execution is classified after communication.
- Wait-only and profiler-silent slices use the `idle_us` bucket because the
  public cost table has no separate additive wait column.
- `aux_us` is an evidence overlay, not another additive bucket. It may overlap
  `total_us` and may be larger than it when several streams overlap.
- `self_us` preserves raw leaf-anchor duration for inspection. It is also an
  overlay and can exceed wall-clock time across overlapping anchors.

`occurrence_count` counts actual expanded structural occurrences of a node.
For a Repeat node, `repeat_count` instead describes how many times its body is
expanded inside one Repeat occurrence. Cost averages for a Repeat node divide
by `occurrence_count * repeat_count`, so they represent one body iteration and
can be compared directly with body-node averages. Other nodes divide by
`occurrence_count`.

### `traceloom_semantic_edge`

Explicit parent-child edges for `traceloom_semantic_node`. Most queries can use
`parent_node_id` and `preorder_idx` from `traceloom_semantic_node`; this edge
table is provided for graph-style joins and tools that prefer edge lists.

### `traceloom_viz_node_anchor`

Discrete node-to-anchor coverage. This is the core statistics relation. Every
visible node stores the anchors it covers, including formal repeat and sequence
nodes. A parent node's coverage is materialized rather than expressed only as a
range or recursive tree traversal.

Important columns:

- `node_id`.
- `anchor_id`.
- `occurrence_idx`: node occurrence in the expanded execution.
- `anchor_order`: order within that node occurrence.
- `coverage_kind`: `self` for leaf anchor nodes, `descendant` for aggregate
  nodes.
- `compute_us`, `comm_us`, `idle_us`, `total_us`, `self_us`, `aux_events`,
  `aux_us`: the exact cost/evidence packet owned by this anchor link. Summing
  these packets by `(node_id, occurrence_idx)` produces
  `traceloom_tree_node_occurrence` without estimating per-occurrence idle time.

### `traceloom_anchor_primary_node`

Best-effort many-to-one mapping from each anchor to its most specific visible
node. This is a convenience index for highlighting; it is not the authoritative
statistics relation. Use `traceloom_viz_node_anchor` for node cost queries.

### `traceloom_collective_global_link`

Optional cross-device collective tags. The native analyzer reserves this table
for external correlation tooling; the installed `traceloom` command does not
run a separate collective-tagging workflow.

This table is local to each sidecar DB. It maps a local collective anchor to a
candidate run-level collective key. The key is structural evidence, not a final
proof that the members are the same hardware operation.

Important columns:

- `candidate_collective_key`: run-level candidate key built from loop pair,
  occurrence, collective type, and order inside that occurrence.
- `pair_id`: structural loop-pair identifier shared by matching repeat nodes
  across devices.
- `local_node_id`, `occurrence_idx`, `idx_in_occurrence`: local structural
  position used to build the key.
- `op_type`: normalized collective type such as `allReduce` or `allGather`.
- `anchor_id`, `event_id`, `source_table`, `source_key`: evidence links back to
  TraceLoom and raw profiler rows.
- `connection_id`, `op_id`: best-effort `COMMUNICATION_OP` identifiers when
  they can be recovered from the raw sidecar DB.
- `validation_status`, `confidence`: candidate quality copied from the global
  summary.

## Convenience Views

### `traceloom_v_semantic_tree_node`

Semantic nodes with parent label and percentage fields.

### `traceloom_v_semantic_tree_readable`

One row per readable tree line. The intended export query is:

```sql
SELECT line
FROM traceloom_v_semantic_tree_readable
WHERE view_name = 'anchor_tree'
ORDER BY preorder_idx;
```

TraceLoom creates these views for common report SQL:

- `traceloom_v_tree_node`: primary readable tree-node map. This is the default
  user query surface and mirrors `tree-map.md`. It includes `depth`,
  node occurrence counts, anchor/operator counts, `avg_total_us`, `avg_aux_us`,
  and total cost.
- `traceloom_tree_node_occurrence`: one row per expanded occurrence of a tree
  node, with anchor range and per-occurrence cost.
- `traceloom_tree_node_anchor`: node occurrence to anchor links for drilling
  down from a tree node to anchors and events.
- `traceloom_v_node_anchor_cost`: anchor-event cost per node.
- `traceloom_v_node_aux_cost`: aux/prelude cost per node.
- `traceloom_v_cuda_graph_replay`: CUDA Graph replay rows joined to normalized
  TraceLoom event/anchor metadata.
- `traceloom_v_cuda_graph_envelope`: CUDA Graph replay-to-contained-event
  links joined to normalized child event metadata.
- `traceloom_v_node_cost`: combined node cost and structure fields.
- `traceloom_v_node_children`: ordered child nodes.

Example:

```sql
select *
from traceloom_v_tree_node
where kind = 'repeat'
order by total_us desc;
```

```sql
select
  local_node_id,
  avg_total_us,
  avg_aux_us,
  round(100.0 * compute_us / nullif(total_us, 0), 2) as compute_pct,
  round(100.0 * comm_us / nullif(total_us, 0), 2) as comm_pct,
  round(100.0 * idle_us / nullif(total_us, 0), 2) as idle_pct
from traceloom_v_tree_node
where local_node_id = 'N027';
```

```sql
select
  na.occurrence_idx,
  na.anchor_order,
  a.anchor_idx,
  e.label,
  e.stream_id,
  e.start_ns,
  e.end_ns,
  e.dur_us
from traceloom_tree_node_anchor na
join traceloom_anchor a on a.anchor_id = na.anchor_id
join traceloom_event e on e.event_id = a.event_id
where na.local_node_id = 'N027'
order by na.occurrence_idx, na.anchor_order;
```

## Cross-Device Collective Summary

External cross-device correlation tooling may write a run-level database:

```text
<analysis_dir>/global_collectives.db
```

The primary table is `traceloom_global_collective_summary`.

Important columns:

- `candidate_collective_key`.
- `pair_id`, `occurrence_idx`, `op_type`, `idx_in_occurrence`.
- `member_count` and `expected_world_size`.
- `start_skew_us`: latest member start minus earliest member start.
- `duration_skew_us`: slowest member duration minus fastest member duration.
- `connection_ids`, `op_ids`, `members`, `missing_members`.
- `validation_status`: `complete`, `partial`, or `singleton`.
- `confidence`: conservative score for the structural candidate.

The companion table `traceloom_global_collective_member` contains one row per
local member anchor for drill-down.

## Design Rules

- Raw profiler tables remain untouched.
- Aux links attach to anchors.
- Anchors are leaves.
- Visualization nodes are compressed structural nodes.
- Node statistics are computed through discrete `node -> anchor` coverage.
- Do not store a single `node_id` on raw events as the source of truth; node
  membership is hierarchical and can be many-to-many.
