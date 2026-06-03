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

## Visualization Structure

### `traceloom_viz_node`

The final compressed visualization nodes. Repeat and sequence nodes are kept as
formal nodes so users can query loop structure directly.

Important columns:

- `node_id`: stable TraceLoom node key.
- `local_node_id`: node id used inside the tree JSON, such as `N004`.
- `view_name`: currently `anchor_tree`.
- `node_type`: `Seq`, `Repeat`, `Atom`, `MacroRef`, etc.
- `kind`: cost kind, such as `seq`, `repeat`, `exec`, `comm`.
- `level`: displayed tree level.
- `repeat_count`: parsed repeat factor, when available.
- cost columns copied from node metrics: `total_us`, `compute_us`, `comm_us`,
  `idle_us`, `aux_us`, and average variants.

### `traceloom_viz_edge`

Parent-child edges for the compressed visualization tree.

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

### `traceloom_anchor_primary_node`

Best-effort many-to-one mapping from each anchor to its most specific visible
node. This is a convenience index for highlighting; it is not the authoritative
statistics relation. Use `traceloom_viz_node_anchor` for node cost queries.

## Convenience Views

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

## Design Rules

- Raw profiler tables remain untouched.
- Aux links attach to anchors.
- Anchors are leaves.
- Visualization nodes are compressed structural nodes.
- Node statistics are computed through discrete `node -> anchor` coverage.
- Do not store a single `node_id` on raw events as the source of truth; node
  membership is hierarchical and can be many-to-many.
