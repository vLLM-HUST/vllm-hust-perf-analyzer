# Queryable Database Timeline Schema

TraceLoom's primary analytical artifact is a **self-contained queryable database timeline**. The analyzer opens the profiler DB read-only, snapshots its
raw schema and rows into a new file, and appends TraceLoom-owned relations under
the `traceloom_*` namespace. It never modifies the input. Reports are SQL
queries over those relations, not a separate report-specific data model.
The artifact is built at a temporary sibling path and atomically replaces the
destination only after all raw and analytical relations are complete.

Create one with the default command:

```bash
traceloom profile.db
# writes profile-directory/traceloom/analysis.db
```

The result retains every original raw table plus TraceLoom augmentation tables.
Moving or deleting the input does not break hierarchy/cost drill-down or SQL
queries against raw `TASK`, `STRING_IDS`, `COMMUNICATION_OP`, CUPTI, or other
vendor relations. `traceloom_metadata` records `artifact_kind`,
`source_embedded`, original path, size, and SHA-256. The path and digest are
provenance, not query-time dependencies.

For a regular input, TraceLoom snapshots its schema and rows unchanged. For an
Ascend split profile, it copies every constituent SQLite database into the one
artifact under collision-free names. `traceloom_raw_source_database` records
each source path, size, hash, and packaging mode;
`traceloom_raw_table` maps `(source_path, source_table)` to the embedded table
and its explicit preserved-rowid column.

Start by querying `traceloom_analysis_surface`. It catalogs the first-class
hierarchy, occurrence, event, replay, issue, provenance, and raw-evidence
relations together with runnable example SQL. Markdown is an optional human
projection requested with `--loop-tree-out`, not a separate analytical model.

The raw-evidence catalog is itself queryable:

- `traceloom_raw_source_database`: one row per embedded source SQLite file;
- `traceloom_raw_table`: original source path/table to embedded table mapping;
- `traceloom_v_event_source_locator`: normalized event lineage joined to that
  mapping. Its `resolution_status` distinguishes `embedded_raw`, intentional
  `analysis_synthetic`, and a genuine `unresolved` locator. For split profiles,
  query the named embedded table using
  `source_rowid_column = source_key`; SQL cannot dynamically substitute a table
  name, so agents first read the locator row and then issue the bounded raw
  lookup.
- `traceloom_v_runtime_call_source_locator` and
  `traceloom_v_device_work_source_locator`: the same bounded path for both
  endpoints of a runtime/device relation.

`traceloom_operator_audit` is a provider-neutral inventory of concrete
observed operator identities. One row groups an `(operator_name, task_type)`
pair and records `occurrence_count`, `total_duration_ns`,
`graph_body_member_count`, and `anchor_event_count`. It does not use an
allowlist to classify new operators as noise. Availability is explicit in the
`traceloom_metadata` key `operator_audit_status`, and its canonical query is
embedded in `traceloom_analysis_surface`.

## Evidence-role decision audit

The installed projection policy is a flat TSV input table. Analysis can select
a replacement with `--classification-rules`, extend it, and finally overwrite
individual typed fields with repeatable
`--classification-rule-override RULE_ID.FIELD=VALUE`. The policy relation
records the table digest separately from the effective config digest and
canonical override list.

- `traceloom_evidence_role_policy`: one effective policy and its input/config
  provenance;
- `traceloom_evidence_role_rule`: the effective positive, fallback, and typed
  system rule catalog;
- `traceloom_evidence_role_decision`: one typed outcome per normalized event;
- `traceloom_evidence_role_placement`: normalized forward/reverse links to
  anchors, auxiliary regions, exact graph members, replay units, and protected
  intervals;
- `traceloom_protected_interval`: materialized exact or typed-open generic-
  discovery boundaries;
- `traceloom_evidence_role_issue`: missing-capability, conflict, unsupported,
  retained-unplaced, and orphan outcomes;
- `traceloom_v_evidence_role_decision`,
  `traceloom_v_evidence_role_placement`,
  `traceloom_v_evidence_role_structure`, and
  `traceloom_v_evidence_role_cost_coverage`: the narrow canonical query views.

The database never treats absence from identity matching as evidence deletion:
every decision retains timing, cost treatment, and the normalized-event/source
locator path. The checked-in canonical SQL under `docs/report-sql/` covers
forward event explanation, reverse member lookup, provider/policy cost
coverage, unknown anchors within a tree occurrence, positively omitted events
within a between-anchor region, and typed issues.

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

### Runtime-call and device-work relations

TraceLoom keeps the host and device observations as different objects rather
than pretending they are one timestamped event stream:

- `traceloom_runtime_call` is one observed host/runtime API interval. It keeps
  provider, host clock domain, API identity, correlation/connection key,
  process/thread/context/device metadata when observed, and raw-row lineage.
  These runtime-side fields delimit local host observations; they are not
  device-task binding keys.
- `traceloom_device_work` is one normalized device event or graph-launch
  composite that can participate in a host/device relation.
- `traceloom_runtime_device_relation` is one supported, candidate, rejected,
  or open relation outcome. `match_policy` names the provider evidence;
  `evidence_level`, `support_state`, and `cardinality` prevent absence or
  multiplicity from collapsing into an implicit one-to-one join.

CUDA uses profiler `correlationId`; Ascend uses CANN `connectionId` or an
already validated graph-launch adapter relation. These identifiers are matched
only inside one input/source database and retain that database's `db_idx`.
TraceLoom does not bind device tasks to host processes or runtime contexts and
does not infer cross-database rank membership. Timestamp proximity never
produces an exact submission edge. Useful open states such as
`missing_runtime_identifier`, `unmatched_device_work`,
`ambiguous_runtime_candidates`, and `ambiguous_device_scope` remain rows.

Nsight exports may reuse a CUDA `correlationId` within one database. For a
typed `CUPTI_ACTIVITY_KIND_SYNCHRONIZATION` observation only, TraceLoom may use
runtime-interval containment to select a unique call **after** the provider
identifier has formed the candidate set. Such rows are named
`cuda_correlation_id_time_containment`, carry
`direct_identifier_time_disambiguated` evidence, and are
`supported_deterministic` rather than `supported_exact`. Rejected reused-ID
candidates remain visible. Zero or multiple containing candidates remain
ambiguous; timestamps never discover a relation on their own.

One input profile database is the default isolation and ownership boundary.
Multi-process or multi-rank analysis is an explicit composition step: callers
map each source DB to its known rank/process identity and join the resulting
local structural relations. PID/context guessing is not a default analyzer
feature. This follows TraceLoom's compression contract: recover the critical
ordered device structure and its local observation context rather than
reconstructing a hidden global runtime topology.

`traceloom_v_runtime_device` joins the three base relations without changing
their evidence semantics. It is the canonical bidirectional path:

```text
runtime call -> relation outcome -> device work -> normalized event
device work  -> relation outcome -> runtime call
```

`traceloom_v_sync_runtime_call` is the first narrow synchronization surface.
It selects typed CUDA synchronization observations and Ascend
`EVENT_RECORD`/`EVENT_WAIT` device tasks from that same factual relation. It
does not pair record with wait, construct a synchronization region, or explain
idle time. In retained Nsight SQLite exports the CUDA `syncType` enum is decoded
to stable names such as `EVENT_SYNCHRONIZE`, `STREAM_WAIT_EVENT`, and
`STREAM_SYNCHRONIZE` rather than being confused with `StringIds`.

Anchors and auxiliary device events reuse the same relation:

- `traceloom_v_anchor_runtime_call` walks an ordinary anchor or an exact graph
  launch anchor back to runtime calls;
- `traceloom_v_node_runtime_call` places that path inside a tree node,
  occurrence, anchor order, and repeat context. Filter `coverage_kind = 'self'`
  for the node-owned placement; keep ancestor coverage when intentionally
  aggregating a larger structural scope;
- `traceloom_v_aux_runtime_call` walks a device-side auxiliary event back to
  runtime calls.

The node view is bidirectional. Filtering by `node_id` and `occurrence_idx`
answers structure-to-runtime queries; filtering by `runtime_call_id` returns
every recovered structural placement of that call's device work.

Host runtime calls are deliberately not counted as device auxiliary work.
Their intervals remain in the provider host clock and their relation to device
events remains explicit.

The augmented DB materializes three narrow bridges rather than forcing every
consumer to reconstruct them from wide joins:

- `traceloom_anchor_runtime_relation`: anchor to relation outcome;
- `traceloom_anchor_host_interval`: adjacent anchor pair and its typed host
  endpoint interval;
- `traceloom_anchor_host_activity`: profiler-observed runtime calls overlapping
  a supported interval, in observed start order.

The last relation can be large because one asynchronous host interval may
contain many runtime calls. This is intentional: TraceLoom pays the interval
join once while producing the read-mostly augmented DB, persists indexes and
planner statistics, and makes repeated agent queries ordinary relational
lookups. The compact input profile remains the transport/source artifact; the
augmented DB is the analysis-optimized product.

### Host runtime behavior between device anchors

When two adjacent device anchors each resolve to exactly one supported runtime
endpoint, materialized relation `traceloom_anchor_host_interval` and its
readable view `traceloom_v_anchor_host_interval` expose the ordered host
interval between the end of the left endpoint call and the start of the right
endpoint call. The view prefers same-thread scope, then same-process scope, and
otherwise reports the shared provider clock domain. Unsupported cases remain typed as
`missing_endpoint`, `ambiguous_endpoint`, `incompatible_host_domain`, or
`nonmonotonic_host_order`.

`traceloom_anchor_host_activity` stores the narrow interval/call links;
`traceloom_v_anchor_host_activity` joins their readable fields and returns
every **profiler-observed runtime call** overlapping a supported interval. This lets
a query ask whether the same device structure is accompanied by a launch
burst, synchronization calls, or different runtime-call distributions across
occurrences. It does not assert what unprofiled CPU code did between calls and
does not label a device gap's cause. Clock calibration is needed only for
genuinely cross-clock measures such as enqueue-to-execute latency; it is not
required to query ordered calls inside one host clock domain.

The readable view retains full call duration and also materializes the call's
clipped `observed_overlap_us` plus a `contained`/`boundary_overlap` relation.
Runtime calls may nest or overlap each other, so summing either duration is a
scheduled-call measure, not an overlap-safe host busy union.

`traceloom_v_node_host_activity` adds node, occurrence, anchor order, and
repeat context to the left endpoint of the same relation. Its explicit
`placement_semantics = 'after_anchor_interval'` means contextual placement:
the runtime calls follow that node-owned anchor within the supported host
interval. Their duration is not CPU cost owned by the node. Filtering
`coverage_kind = 'self'` avoids repeating the same anchor through ancestor
coverage when comparing equivalent structural positions. The view also exposes
the right anchor's identity and the host-interval width; occurrence comparisons
must retain these fields so a changed structural neighbor is not mistaken for
a changed node-owned CPU cost.

### Structure-conditioned bubble statistics

`traceloom_v_structure_bubble_occurrence` exposes overlap-safe uncovered device
time immediately before each self-owned anchor occurrence. It keeps the
recovered `structural_position_id`, occurrence and anchor bounds, compute/comm/
aux cost lenses, and the status of the host observation interval delimited by
the adjacent anchors' supported runtime endpoints. `bubble_us` is device cost;
`host_interval_us` is a contextual host-clock interval and is not charged as
device cost.

`traceloom_anchor_host_api_summary` is a compact intermediate relation built
while TraceLoom constructs host-activity links. Its grain is one supported host
interval and one public API family. It preserves call count, distinct API-name
count, scheduled call duration, and clipped scheduled overlap. This avoids
rejoining millions of activity links to the provenance-heavy runtime table for
every analytical query.

`traceloom_v_structure_bubble_api_occurrence` associates those family summaries
with individual bubble occurrences. `traceloom_v_structure_bubble_api_stats`
then aggregates equivalent recovered structural positions and reports bubble
cost, host-observation coverage, API-family presence, average counts, and
scheduled duration measures. Presence against all bubbles and against only
host-observable bubbles are separate columns so unsupported host endpoints do
not silently become zero API activity.

`traceloom_v_structure_bubble_runtime_call` is the exact drill-down surface for
a chosen `bubble_id`; it retains runtime source table/key and observed order.
None of these relations assigns `scheduler_delay`, `synchronization_cause`, or
another causal label. They expose the distributions an engineer or agent needs
to test such hypotheses. Calls may nest or overlap, so scheduled duration sums
are not overlap-safe host busy time.

The canonical workflow is in
`docs/report-sql/structure-bubble-statistics.sql`.

Canonical examples are in `docs/report-sql/runtime-device-relations.sql` and
`docs/report-sql/anchor-host-activity.sql`; the occurrence-oriented aggregate
is `docs/report-sql/node-host-activity.sql`.

Implementation lineage: the provider host-API ingestion pattern was adapted
and generalized from
[PR #26](https://github.com/vLLM-HUST/vllm-hust-perf-analyzer/pull/26) by
Luqhhh. TraceLoom uses that ingestion pattern for explicit runtime/device and
anchor-delimited observation relations; it does not adopt PR #26's idle-cause
classification as part of this surface. The scheduler-profiler process/context
binding proposed by
[PR #31](https://github.com/vLLM-HUST/vllm-hust-perf-analyzer/pull/31) remains
an independent opt-in integration and is deliberately not part of this local
timeline model.

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
  identifiers. For exact CUDA replay rows, `correlation_id` is filled from the
  launch occurrence's `raw_launch_connection_id` when exactly one launch
  member is correlated deterministically.
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

### `traceloom_graph_launch` and `traceloom_graph_body_member`

The provider-neutral **exact graph SQL surface**. These two normalized base
relations materialize the exact NativeIr chain
`ReplayUnit -> ordered ReplayUnitLaunchMember -> slot -> GraphLaunchOccurrence
-> GraphLaunchBody -> GraphLaunchBodyMember -> Task -> TraceEvent -> SourceRef`
for both CUDA node-level and Ascend exact replay units. Membership is exact:
every row comes from a `ReplayUnitLaunchMember`, never from timestamp
containment. `traceloom_cuda_graph_envelope` remains the explicitly
temporal/best-effort surface and is not relabeled.

`traceloom_graph_launch` is the launch/anchor relation, one row per exact
launch occurrence:

- `launch_id`: stable key (`graph-launch-<member id>`).
- `graph_event_id`: node-occurrence event (the replay unit's launch event).
- `anchor_id`: promoted tree anchor when the exact member has one; `NULL` for
  launches with no anchor (anchor mapping is exact via the anchor's
  `ReplayUnitLaunchMemberId`, never time-inferred). Empty values materialize
  as SQL `NULL`.
- `replay_unit_id`, `graph_template_id`, `graph_launch_occurrence_id`,
  `replay_body_template_id`, `body_id`: native identity of the exact chain.
- `member_order`, `slot_order`: ordered position inside the replay unit.
- `correlation_id`: raw launch connection id (`raw_launch_connection_id`)
  rendered as text when deterministically available; for CUDA exact launches
  this is the runtime `correlationId` preserved from adapter evidence.
  Absent/undeterministic values materialize as SQL `NULL` (query with
  `IS NULL`, not `= ''`).
- `match_policy`, `association_policy`: direct-correlation evidence semantics.
- `start_ns`, `end_ns`, `dur_us`, `evidence_level` (`exact_direct`).

`traceloom_graph_body_member` is the ordered member relation, one row per
exact body member with lane/task order, kind, event/task/source provenance,
timing, correlation, and raw graph-node identity:

- `member_id`, `launch_id`: stable keys linking to the launch relation.
- `lane_ordinal`, `task_ordinal`, `kind` (`compute`/`communication`/
  `data_move`): exact per-lane order inside the body.
- `event_id`, `task_id`, `source_table`, `source_row_id`, `raw_task_id`:
  provenance back to normalized events and raw profiler rows.
- `graph_node_id`: raw CUDA `graphNodeId` of the activity; `NULL` for
  providers without graph-node identity (Ascend).
- `original_graph_node_id`: `CUDA_GRAPH_NODE_EVENTS.originalGraphNodeId` when
  exactly one non-null original maps to the raw node; `NULL` when the mapping
  is missing or ambiguous, and also when the optional
  `originalGraphNodeId` column is absent from the raw schema (never
  guessed; exact graphNodeId/correlation reconstruction is unaffected).
- `correlation_id`, `match_policy`, `association_policy`, `evidence_level`.

Both relations fail closed: an exact launch member without a body, an
out-of-range id, or a non-contiguous member order throws during
materialization instead of emitting guessed rows. Best-effort replay units
(graph-trace overlap, capture-stream task overlap) and unrecognized/graph-trace
only cases emit no rows here; their temporal envelopes and reconstruction
evidence remain in `traceloom_cuda_graph_envelope` /
`traceloom_aclgraph_reconstruction_region`.

### `traceloom_v_node_graph_body_member`

Canonical **tree-occurrence** view over the exact graph relations. It joins
`traceloom_graph_launch` to `traceloom_viz_node_anchor` on the explicit
`anchor_id` plus composite `db_idx`/`device_id`, and joins launch to member and
member to event with composite keys (`id` + `db_idx` + `device_id`) rather than
bare IDs. Each row is one exact body member of an anchored launch inside a real
tree node occurrence, exposing:

- `node_id`, `view_name`, `occurrence_idx`, `idx_in_occurrence`: the
  containing tree occurrence and the launch anchor's index within it.
- `anchor_order`, `coverage_kind`, `repeat_context`, and `anchor_*` cost
  fields: the promoted anchor's coverage evidence.
- `node_event_id` / `node_launch_id`: the replay unit's launch event and the
  exact launch key.
- all `traceloom_graph_body_member` fields plus the member event's symbol,
  label, task type, and semantic role.

Filtering by `node_id` + `occurrence_idx` (or `node_event_id`) walks a tree
node occurrence to its exact ordered members/events; filtering by `event_id`
walks a member event back to the containing tree occurrences. Exact launches
without a promoted tree anchor remain in `traceloom_graph_launch` /
`traceloom_graph_body_member` only and never appear as node-view rows. The
view is part of the centralized report-view drop/recreate lifecycle
(`materialize_report_compatibility_views`), so definitions cannot go stale.
Indexes cover `graph_event_id`, `anchor_id`, `launch_id`, `event_id`, and
`graph_node_id`.

Canonical queries in `docs/report-sql/node-graph-body-members.sql` and
`docs/report-sql/event-graph-node-occurrences.sql` demonstrate the forward
tree-occurrence drill-down and the reverse event-to-tree navigation.

### Replay-internal cost relations

`traceloom_replay_cost_unit`, `traceloom_replay_cost_launch`,
`traceloom_replay_cost_stream`, and `traceloom_replay_cost_member` publish the
authoritative native replay cost map. A replay unit is never assumed to be one
launch: ordered composition slots, roles, bodies, streams, members, exact
timing, and source identities remain explicit. `task_sum_ns` is scheduled work,
`busy_union_ns` is overlap-safe busy time, and `envelope_ns` is observed span;
they are deliberately separate lenses.

`traceloom_replay_cost_aggregate` publishes deterministic role-collapsed
p25/median/p75 rows. `traceloom_replay_cost_aggregate_member` records every
exact contributor, so SQL never has to guess how a distribution was aligned.
`traceloom_replay_cost_issue` and unit/launch `support_status` columns retain
unsupported or ambiguous evidence as typed analysis results.

`traceloom_v_node_replay_cost_member` composes the cost map with the exact
tree-occurrence/graph-member spine. The canonical flow is:

```text
tree node occurrence -> exact cost member -> normalized event -> raw row
hotspot aggregate -> exact contributors -> same evidence chain
```

See `replay-cost-hotspots.sql`, `node-replay-cost-members.sql`, and
`replay-cost-explain.sql` under `docs/report-sql/`.

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
- `view_name`: currently `native_report_tree` for the primary compressed
  visualization/occurrence surface. This is distinct from the semantic-tree
  projection, whose current view name is `anchor_tree`.
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

### `traceloom_semantic_tree`

Durable header for a recovered semantic execution tree. This table makes the
compressed tree a first-class database-timeline object instead of only a Markdown
or JSON report.

Important columns:

- `tree_id`: stable tree key.
- `view_name`: semantic-tree analyzer view, currently `anchor_tree` (not the
  `native_report_tree` name used by `traceloom_viz_node`).
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
- One source DB is the default process/rank isolation boundary; cross-DB
  identity and rank composition require an explicit caller-provided mapping.
- Do not promote profiler PID/context fields into normalized device events or
  use them to infer an implicit multi-process topology.
- Aux links attach to anchors.
- Anchors are leaves.
- Visualization nodes are compressed structural nodes.
- Node statistics are computed through discrete `node -> anchor` coverage.
- Do not store a single `node_id` on raw events as the source of truth; node
  membership is hierarchical and can be many-to-many.
