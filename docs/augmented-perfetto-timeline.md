# AugDB to Perfetto Timeline

TraceLoom can export its self-contained queryable database timeline (AugDB) as
Perfetto / Chrome Trace JSON. This is a first-class projection of AugDB, not a
second analyzer or a custom frontend.

## Before exporting

- **Counting kernels?** Views overlap. Filter `args.projection_plane` rather
  than summing every slice; see [the SQL recipe](#avoid-double-counting-the-same-work).
- **Comparing rank timing?** The default removes each rank's initial offset.
  For independent sources, consider [provider timestamps](#preserve-provider-timestamps-for-independent-sources)
  instead; neither option by itself calibrates clocks.

## CLI

Export while analyzing a profile:

```bash
traceloom /path/to/msprof.db \
  --output /tmp/analysis.db \
  --perfetto-out /tmp/analysis.perfetto.json.gz
```

Export an existing AugDB without rerunning analysis:

```bash
traceloom export-perfetto /tmp/analysis.db \
  --output /tmp/analysis.perfetto.json.gz
```

Replace the single flat event track with compressed TraceLoom event tracks from
a distributed run by mapping every rank explicitly to its analysis or sidecar
DB:

```bash
traceloom export-perfetto /tmp/rank0-analysis.db \
  --output /tmp/tp8.perfetto.json.gz \
  --distributed-rank 0=/analysis/rank0.db \
  --distributed-rank 1=/analysis/rank1.db \
  --distributed-rank 2=/analysis/rank2.db
```

Each mapped DB must expose `traceloom_v_tree_node` and
`traceloom_tree_node_occurrence`. The mapping is repeatable and rank identity
is not inferred from paths, PIDs, or timestamps. Rank 0 must be present because
it is currently the display reference rank.

When `--output` is omitted in the second form, TraceLoom writes
`analysis.perfetto.json.gz` beside the input. A `.gz` suffix streams the JSON
through `gzip`; any other suffix writes plain JSON. Both formats open directly
in Perfetto.

### Recommended opt-in for collective comparison

When an auditable affine model receipt is available, explicitly apply its
collective-end model to the distributed display:

```bash
traceloom export-perfetto /analysis/rank0.db \
  --output /tmp/tp8.end-aligned.perfetto.json.gz \
  --distributed-rank 0=/analysis/rank0.db \
  --distributed-rank 1=/analysis/rank1.db \
  --distributed-rank 2=/analysis/rank2.db \
  --distributed-clock-model /analysis/collective-end.models.jsonl
```

This is the recommended opt-in when the immediate task is to compare
collective boundaries across ranks. Aligning the observed collective ends
removes small clock offset/drift from the visual comparison; remaining start
spread then becomes a concrete hypothesis about rank entry or provider-observed
waiting to audit. It is not, by itself, proof of arrival time or causal waiting.

The model receipt is newline-delimited flat JSON. It contains exactly one
`metric=end` record for every non-reference rank; rank 0 remains identity so
its execution tree and raw provider evidence stay on their observed timeline.
Each record has this contract:

```json
{"format":"traceloom.distributed-clock-model/v1","rank":1,"reference_rank":0,"metric":"end","status":"candidate_only","source_clock_domain":"rank-1-device","target_clock_domain":"rank-0-device","marker_contract":"collective-family-group-ordinal-candidate-v1","scale":0.999999926,"reference_source_ns":1787706527296533405,"reference_target_ns":1787706527296533678.9}
```

`status` must be `candidate_only` or `calibrated`. All end records must share
the same status, marker contract, target clock domain, and reference rank.
Missing, duplicate, extra-rank, non-positive, non-finite, mixed-contract, and
unsupported models fail closed before the output is opened. Other metrics may
coexist in the JSONL receipt but are not applied by this UX.

For a non-reference rank, TraceLoom maps each display timestamp with half-even
rounding:

```text
reference_target_ns + scale * (source_ns - reference_source_ns)
```

Every displayed slice retains `source_start_ns`, `source_end_ns`, model status,
marker contract, exact model parameters, and the receipt SHA-256. Trace metadata
also records the receipt path, digest, evidence status, and display boundary.
Candidate models are deliberately admitted only by this explicit Perfetto
display path; they remain forbidden from moving production/global timestamps.

## Visual planes

The export intentionally places two independently useful views on the same
observed time axis:

1. **Execution-tree intervals.** Root/sequence occurrences and direct repeat
   body windows are packed by tree depth. Repeat aggregate bars are replaced by
   the individual body windows that compose them.
2. **Flat TraceLoom events.** Atomic occurrences appear on a separate track, so
   users can compare the structural partition with its event realization.
3. **Raw provider evidence.** When the embedded Ascend tables are present,
   PyTorch API, CANN API, communication-op, device task, and AICORE-frequency
   tracks are exported from `traceloom_raw_table` mappings. Every raw slice
   retains its source ID, embedded table, and source rowid. Device-task names
   on these audit tracks intentionally remain the provider's complete kernel
   labels. The execution-tree and flat TraceLoom planes use normalized
   structural labels; seeing a CANN fingerprint only under `Raw provider ·
   TASK` is therefore expected evidence retention, not semantic-label noise.
4. **Distributed TraceLoom event lanes.** When distributed timelines are
   supplied, the ordinary single flat event track is replaced by one flat rank
   track from each DB's published atom occurrences. The same TraceLoom event
   labels seed colors across ranks, so compute, communication, and other
   observed event textures remain directly comparable.

TraceLoom does not infer semantic model layers for this view. A colored interval
means an observed structural subtree occurrence, not a guessed layer name.

## Distributed alignment and audit boundary

Without `--distributed-clock-model` or `--distributed-alignment provider`,
distributed lanes use
`first_timeline_event_per_rank` display alignment: each rank's first published
atom occurrence is translated onto rank 0's first atom occurrence while all
later within-rank elapsed times and durations remain unchanged. This view
answers “how do the rank timelines evolve after their first observed event?”
It does **not** assert that provider timestamps form a proven global
cross-device clock or that visually adjacent events are causal peers.

Every distributed slice retains enough provider identity to audit or refine a
visual hypothesis:

- explicit rank and source timeline DB path and SHA-256;
- source node ID, local node ID, occurrence index, view, device, and rooted
  role path;
- unmodified source start/end timestamps and either the rank-specific
  first-event anchor or the selected end-affine model coordinates;
- repeat context, anchor range, event category, and composable
  compute/communication/idle/auxiliary statistics.

The rank lanes are therefore a comparative projection over auditable TraceLoom
intervals, not a hidden cross-rank join. A later analysis may establish a
stronger alignment contract from explicit wave or structural identities; this
export does not manufacture one from timestamp proximity.

TraceLoom's recovered affine fitter and the evidence gate for a future
calibrated projection are documented in
[Clock calibration boundary](clock-calibration.md).

## Readable identity and topology color

User-facing titles are short and queryable:

```text
N001 · root
N612 · motif C · body 1/2
N612 · motif C · body 2/2
```

Replay-body positions use domain-qualified titles, such as `R0/N002` and
`R2/N002`. Local `N` numbering restarts in each replay-body stream domain;
equal local numbers in different domains do not identify the same position.
Repeated occurrences within one domain intentionally reuse their position ID.
The full `domain_id` and `position_id` arguments remain the query identities.
A stream-local communication repeat does not imply a global model-layer loop.

A motif class is the canonical ordered rooted topology of node kinds
(`seq`/`repeat`/`atom`). Labels, categories, node IDs, occurrence IDs, and repeat
counts do not affect it. Isomorphic repeat subtrees receive the same compact
per-export alias (`A`, `B`, ...); Perfetto's ordinary name colorizer therefore
assigns them the same visual seed. The alias is presentation-only. The complete
SHA-256 topology signature remains the machine identity.

Each structural slice also retains the AugDB query coordinates in its args:

- `database_index`, `device_id`, and `view_name`;
- `node_id` or `repeat_node_id`;
- `occurrence_idx` or `aggregate_occurrence_idx`;
- `repeat_context`, `body_id`, and `body_ordinal` for derived repeat bodies;
- `rooted_role_path`, `tree_depth`, and the topology SHA-256;
- anchor range and composable compute/communication/idle/auxiliary statistics.

Chrome JSON arguments appear under the `args.*` prefix in Perfetto SQL. For
example:

```sql
SELECT s.name, a.key, a.display_value
FROM slice AS s
JOIN args AS a USING (arg_set_id)
WHERE s.category = 'traceloom.repeat_body_window'
  AND s.name GLOB 'N612*';
```

## Composition contract

A displayed repeat body is derived only from published direct-child
occurrences whose `repeat_context` identifies that body. Its time boundary and
statistics are composed from those children. Export fails closed if the
published children do not provide exactly the repeat bodies promised by the
repeat node. This keeps the visualization aligned with the same independently
composable and auditable intervals exposed by AugDB.

## Avoid double-counting the same work

The JSON is a **multi-view visualization**, not a bag of independent tasks.
A kernel may appear in the device-event plane and again as raw-provider audit
evidence; enclosing structure bars are another view of the same interval.
Every device/distributed event now has `args.projection_plane=device_events`;
raw slices have `raw_provider`, and subtree bars have `structure`. Export prints
a reading note and embeds `metadata.aggregation_boundary` for downstream tools.

For a kernel inventory in Perfetto, choose one plane and one device/view (or one
explicit distributed rank) before summing. For example, for a single-device
export containing just one view:

```sql
SELECT name, COUNT(*) AS calls, SUM(dur) / 1e6 AS task_ms
FROM slice
WHERE EXTRACT_ARG(arg_set_id, 'args.projection_plane') = 'device_events'
  AND EXTRACT_ARG(arg_set_id, 'args.database_index') = 0
  AND EXTRACT_ARG(arg_set_id, 'args.device_id') = 0
  AND EXTRACT_ARG(arg_set_id, 'args.view_name') = 'native_report_tree'
GROUP BY name;
```

Inspect the available device/view coordinates rather than assuming this example
matches your capture. Perfetto SQL `dur` is nanoseconds, while Chrome JSON `dur`
is microseconds. Even within one plane, concurrent task durations can overlap:
the sum is **not** wall time, utilization, or removable latency. Never deduplicate
by name/timestamp; separate genuine events can share both.

`--no-raw-provider` omits raw audit tracks from this export only. It neither
deletes evidence from the database nor removes nested structure bars. The
projection-plane filter remains necessary. Existing default exports keep all
raw evidence for compatibility and auditability.

## Preserve provider timestamps for independent sources

For sources already recorded in a suitable provider time domain, use:

```bash
traceloom export-perfetto rank0.db --output sources.json.gz \
  --distributed-rank 0=rank0.db --distributed-rank 1=rank1.db \
  --distributed-alignment provider
```

This retains cross-rank source offsets with a single shared display origin;
no rank is independently zeroed and no synthetic collective correspondence is
created. Metadata and the visible track title explicitly say **uncalibrated**.
The caller must establish whether provider clocks are comparable; retaining
numbers does not prove that clocks on different hosts agree.

`--distributed-alignment first-event` explicitly selects the historical default.
It removes initial source skew and must not be used to infer concurrent arrival
or waiting of independent clients. The CLI now says this at export time as well
as in `--help`. Either alignment option is mutually exclusive with
`--distributed-clock-model`; incompatible choices, unknown modes, or alignment
without ranks fail before opening the output. Existing model-based exports and
unspecified first-event behavior are unchanged.

## Collective display folding and attention envelopes

The primary plane folds an `AivKernel` TASK into its already-visible AllReduce
or AllGather observation only when embedded provider evidence proves a unique
one-to-one match: same raw source, collective kind, connection ID, database,
device, normalized stream and exact start/end timestamps. Names or temporal
containment alone are insufficient. Missing counterparts, mismatches and
ambiguous pairs remain visible. The raw-provider plane, AugDB events, exact
replay membership and cost tables are unchanged. The surviving collective's
`display_folded_task_event_id` preserves the reverse lookup; an exactly matching
single-terminal replay repeat decoration is also omitted. Multi-member
structures are not removed by this presentation rule.

For explicitly labeled replay `attention` Positions, a unique folded AllReduce
in the gap before the directly adjacent `residual_norm` Position can extend the
attention **display envelope**. Both Positions must share their exact domain and
launch; the collective obtains that launch through its matched exact TASK.
Multiple candidate collectives or competing phases withhold the association.
AllGather, MLP, unrelated launches and overlapping residual work are not assigned
by this rule. The collective stays visible at its own unchanged timestamp.

This is a requested presentation convention, not cross-stream HPO membership,
a dependency edge or additive phase cost. `display_compute_end_ns` retains the
original compute boundary, `display_collective_event_id` identifies the attached
observation, and `display_phase_semantics` states the interpretation. The
collective retains `display_phase_position_id` and `display_phase_launch_id`.
Re-exporting an existing self-contained AugDB is sufficient; no reanalysis or
raw-data deletion is necessary.
