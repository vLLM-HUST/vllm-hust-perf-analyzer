# Idle Evidence Contract (M0)

Status: Draft v4.3 (proposed for M0 approval)

Target: cross-layer device idle-gap and synchronization evidence, as defined in
intellistream/vllm-request-lifecycle-profiler-plugin#2 (M0) and
vLLM-HUST/vllm-hust-perf-analyzer#3 (engineering).

Related design: `notes/rfc-synchronization-gap-attribution.md` (v2 draft).

v4 changes from v3: candidate concept generalized (possible-overlap and
non-robust-delay are candidates too, with `candidate_status`); `record`
folded into `runtime_control_present` and `unknown` handling made explicit;
`host_sync_api_present` requirement unified and cross-domain projection
clarified; Theil-Sen fit fully frozen (marker host time, formulas, median
rule, fit/holdout stratification); epsilon unit-corrected; unified evidence
link table; communication canonicalization conditions; frozen
`analysis_status`; version columns split; RFC 8785 JCS run_id; FAR precision
definition.

v4.1 changes from v4: `host_sync_api_present` now requires robust temporal
overlap after calibrated mapping; `exact_connection_id` is supporting
evidence only and can never replace robust overlap. Clock-model error fields
unified to `absolute_residual_*`.

v4.2 changes from v4.1: analysis span and stream universe defined per logical
`(run_id, device_id)` after split-shard merge; fit/holdout calibration
requires a non-empty validation set (else `alignment_status = invalid`);
host-sync emitted slice = robust interval ∩ remaining gap; versioned, hashed
host-API allowlist ruleset; fixed `scale` serialization and deterministic
timestamp rounding; interval conventions scoped to interval-bearing tables;
`synthetic_only` permitted for correlation-mechanism validation on controlled
fixtures; zero-gap coverage shares defined as `NA`; run-level metadata record
carries `analysis_status` and nullable span boundaries.

v4.3 changes from v4.2: section 10 now REQUIRES a checked-in golden-fixture
executable check for the counterexample "host wait exists but visible idle
is zero" (a host sync API event present in the capture while the device
productive timeline covers the whole analysis span), wired into CI; the
checked-in fixture is `native/tests/fixtures/idle_evidence/
host_wait_zero_visible_idle/` verified by
`traceloom_native_idle_evidence_golden_fixture_tests` in the engineering
repository. Fixture results are contract/example evidence and MUST NOT be
presented as a matched A/B of runtime traces.

## 1. Purpose and Scope

This contract freezes the evidence vocabulary, classification rules, and
attribution discipline for device idle-gap and synchronization-wait analysis.
It answers the question that reviewers will ask first:

> Is the reported idle gap a real wait, or an artifact of cross-clock,
> sampling, or missing events?

The contract binds two parties:

- **Producers** (research, analyzer, attribution rules): output only the
  categories, evidence levels, and relations defined here.
- **Consumers** (engineering implementation, downstream users, reviewers):
  interpret every number through the category x evidence-level matrix.

Out of scope: anomaly search strategy, end-to-end causal models, and
performance fixes (per parent issue #2 boundary). This contract produces
conservative cross-layer **localization** evidence only.

## 2. Normative Terminology

- `analysis span`: the analyzed time range of one logical `(run_id,
  device_id)` pair, constructed AFTER all split profile shards for that
  device are imported and merged. Database identity is retained for lineage
  only; per-shard spans are never used (they would silently omit gaps at
  shard boundaries and break monolithic/split equivalence). Default:
  `[first productive task start, last productive task end)`, or an
  explicitly configured anchor-scoped range. Gaps are defined only inside the
  analysis span.
- `interval`: a half-open integer-nanosecond range `[start_ns, end_ns)` with
  `end_ns > start_ns`, in one `clock_domain`.
- `productive timeline`: the interval union of productive task intervals
  (taxonomy in section 4).
- `gap`: a maximal connected interval in `complement(productive timeline)
  ∩ analysis span`.
- `stream universe`: for one logical `(run_id, device_id)` after shard
  merge, the set of streams that have at least one profiler-visible event
  inside the analysis span. This is an **observed universe**, not the set of
  all runtime streams.
- `collection status`: completeness attestation of the input capture
  (`complete` / `incomplete` / `unknown` / `invalid`), from external or
  collection-side evidence. Not derivable from the trace content alone.
- `link status`: join resolution of a host/device `connectionId` link
  (`unique` / `one_to_many` / `ambiguous` / `unresolved`).
- `analysis status`: frozen per-run enum: `ok` | `no_productive_span` |
  `invalid_analysis_span` | `empty_input` | `invalid_input`.
- `candidate`: a diagnostic-only hypothesis or below-threshold association
  that does not enter the official explanation partition. A candidate may
  carry `correlated` or `inferred` evidence, but never contributes to
  official coverage (sections 6.5, 11).
- `evidence level`: how strongly the evidence supports the category claim
  (section 6).
- `evidence relation`: what kind of structural relation links evidence to the
  interval (section 6).
- `alignment status`: trustworthiness of host/device timestamp mapping
  (section 7).
- MUST / MUST NOT / REQUIRED / MAY have RFC 2119 meaning.

## 3. Timeline Model

### 3.1 interval_kind

Every interval carries exactly one `interval_kind`:

- `productive_active`: covered by the productive timeline.
- `visible_productive_idle`: a gap (see below).

`interval_kind` and the idle explanation (section 5) are **separate fields**.
A gap is never classified as `productive_active`; `productive_active` never
receives an explanation category.

### 3.2 analysis span

- MUST be recorded per run, with one `analysis_status` value from the frozen
  enum (section 2).
- Default as defined in section 2; MUST report span boundaries in output
  metadata.
- If no productive task exists: with no explicit analysis span, emit
  `analysis_status = no_productive_span` and no device intervals; with an
  explicit analysis span, the entire span is one `visible_productive_idle`
  gap and is explained under the normal rules.
- Gaps before the first productive task or after the last productive task
  (boundary edges of the span) MUST be reported as `visible_productive_idle`
  only if inside the analysis span; otherwise they are out of scope and MUST
  NOT be invented.

### 3.3 Half-open interval algebra

- All intervals are half-open `[start_ns, end_ns)`, integer nanoseconds.
- MUST NOT emit zero-length or negative-length intervals.
- Explanation slices MUST NOT overlap each other.
- The union of explanation slices MUST equal the original gap.
- The productive union plus gap union MUST cover the analysis span exactly at
  integer-nanosecond precision. Rounding tolerance applies only to derived
  human-readable microsecond summaries.
- Overlapping tasks on the same stream: represent as a deterministic mutually
  exclusive partition using the `ambiguous_overlap` state:

```text
A [100,180)
B [150,220)
  -> A-only            [100,150)
  -> ambiguous_overlap [150,180)
  -> B-only            [180,220)
```

  Preserve links to both source events A and B. Each stream's state timeline
  remains a mutually exclusive partition.

## 4. Semantic Task Taxonomy

The existing `default_signal_classification_rules.tsv` describes structural
roles (`anchor` / `ignore`). It is a different dimension and MUST NOT be
reused as the productive taxonomy: `ignore` means "not a structural anchor",
not "non-productive" (e.g., `AI_CORE`, `EVENT_WAIT`, `MEMCPY`, `CAPTURE_WAIT`,
`SDMA` are all `ignore` in the current rules).

A separate ruleset is REQUIRED:

```text
idle_evidence_semantic_rules.tsv
```

with roles at least:

```text
productive_compute
productive_comm
productive_data_move
visible_wait
capture_control
record
runtime_control
unknown
```

M0 initial mapping (refinable in the TSV, per-direction detail allowed):

| task_type | role |
| --- | --- |
| `MEMCPY`, `MEMCPY_ASYNC`, `SDMA` | `productive_data_move` |
| `MEM_WRITE_VALUE`, `WRITE_VALUE` | `runtime_control` |
| `EVENT_WAIT`, `NOTIFY_WAIT` | `visible_wait` |
| `CAPTURE_WAIT`, `CAPTURE_RECORD` | `capture_control` |

- `productive_compute` / `productive_comm` MUST be determined by blob/label
  rules in the same TSV (kernel names, HCCL/NCCL patterns).
- Every run MUST output `semantic_rules_version`,
  `semantic_rules_sha256`, and per-event `matched_rule_id`.
- `productive_data_move` classification MUST keep direction/API granularity
  available (H2D/D2D/D2H and sync/async variants may differ semantically);
  the contract fixes the role, not a single unconditional mapping.
- The roles `record` and `unknown` are NOT explanation categories themselves;
  their mapping into explanations is defined in section 5.

## 5. Idle Explanation Categories

Each gap is sliced into explanation intervals. Categories:

| Category | Meaning | Evidence requirement |
| --- | --- | --- |
| `blocked_by_visible_wait` | Gap sub-interval is covered by a profiler-visible wait task (`EVENT_WAIT`, `NOTIFY_WAIT`, sync-family tasks). | device_event_coverage (section 6) |
| `capture_control_present` | Gap sub-interval is covered by capture/control tasks (`CAPTURE_WAIT`, `CAPTURE_RECORD`). | device_event_coverage |
| `runtime_control_present` | Gap sub-interval is covered by visible record or runtime-control tasks that are neither productive, wait, nor capture-control tasks. Applies to roles `record` and `runtime_control`. | device_event_coverage |
| `queued_visible_task_delay` | An enqueue host API is linked to a later visible device task via a unique exact `connectionId`, and a robustly positive delay interval exists between mapped API end and task start. | exact_connection_id (correlated only); section 7.3 |
| `host_sync_api_present` | An allowlisted host synchronization API whose mapped interval robustly overlaps the gap after calibrated clock mapping. Presence does not establish causality; a unique exact `connectionId` may be an additional supporting relation but MUST NOT replace robust overlap. | temporal_overlap (correlated only); exact_connection_id supporting only |
| `no_observed_device_work` | All streams in the stream universe are `empty_observed` over the sub-interval, and collection completeness is attested. | complete_absence_observation (direct) |
| `unattributed_visible_idle` | Everything else: insufficient evidence, profiler blind spots, unexposed queued work, host-side computation, sampling gaps. | none (by definition) |

Rules:

- `unattributed_visible_idle` MUST NOT be auto-attributed by any heuristic,
  pattern inference, or aggregation. A future rule must pass a controlled
  intervention validation before entering this contract (section 13).
- **`unknown`-role tasks**: a visible task classified `unknown` prevents
  `complete_absence_observation` but does not receive a semantic
  explanation. The covered interval remains `unattributed_visible_idle`,
  with the unknown task preserved as supporting diagnostic evidence. An
  `unknown` task MUST NOT be folded into `runtime_control_present`.
- `no_observed_device_work` is usable ONLY when ALL of the following hold:
  `collection_status == complete`; all discovered device shards were
  imported; all required task-bearing tables were readable; no dropped-event
  or truncated-capture condition was reported; `observed_universe_scan_complete`
  is true (every stream in the declared observed universe was scanned). In
  every other case (`incomplete` / `unknown` / `invalid`) the empty sub-
  interval MUST be classified as `unattributed_visible_idle`.
- `no_observed_device_work` means "no profiler-visible device work was
  observed", never "the device was idle" (section 12).
- A wait task covering an interval does not by itself justify "the gap was
  caused by this wait"; the safe statement is "the interval is covered by a
  visible wait task".
- An `exact_connection_id` link does not remove the alignment requirement:
  projecting host intervals onto device gaps across clock domains still
  requires calibrated mapping. Without calibration, the link MAY be
  displayed but cross-domain time coverage MUST NOT be computed.
- The host API allowlist (`host_sync` and `enqueue` families) lives in a
  versioned, hashed ruleset `idle_evidence_host_api_rules.tsv` (fields:
  `api_pattern`, `family` in {`host_sync`, `enqueue`}, `note`), analogous to
  the device semantic rules. Its `host_api_rules_version` and
  `host_api_rules_sha256` MUST be reported per run. Categories that depend on
  the allowlist MUST NOT be enabled without it.

## 6. Evidence Levels and Relations

### 6.1 Evidence levels

| Level | Meaning |
| --- | --- |
| `direct` | The category claim is established entirely from device-clock-domain observations: device-event coverage, or verified absence over the declared observed stream universe. A connectionId join alone is not direct evidence. |
| `correlated` | Temporal or structural association without a dependency edge. |
| `inferred` | Derived from neighborhood, repetition, or pattern context. Candidate-only in M0 (section 6.5). |
| `none` | No evidence supports any category. |

### 6.2 Evidence relations and level mapping

| Relation | Meaning | Level |
| --- | --- | --- |
| `device_event_coverage` | A device-side task row covers the interval. | direct |
| `complete_absence_observation` | Verified absence of any task over the interval across a covered, completeness-attested stream universe. | direct |
| `exact_connection_id` | Profiler-provided `connectionId` join between host API and device task. | correlated |
| `temporal_overlap` | Time overlap between host API and gap (window rules in section 7). | correlated |
| `pattern_context` | Same-thread neighborhood, repeated-body context, or other pattern evidence. | inferred |
| `none` | No relation. | none |

### 6.3 Allowed combinations (normative matrix)

| Explanation category | Allowed level | Required relation |
| --- | --- | --- |
| `blocked_by_visible_wait` | direct | device_event_coverage |
| `capture_control_present` | direct | device_event_coverage |
| `runtime_control_present` | direct | device_event_coverage |
| `queued_visible_task_delay` | correlated | exact_connection_id |
| `host_sync_api_present` | correlated | temporal_overlap (robust); exact_connection_id supporting only |
| `no_observed_device_work` | direct | complete_absence_observation |
| `unattributed_visible_idle` | none | none |

Combinations outside this matrix MUST NOT be emitted. For
`host_sync_api_present`, an `exact_connection_id` link to a nearby task is
supporting evidence only: the API itself must robustly overlap the gap after
calibrated mapping, otherwise the category MUST NOT be emitted.

### 6.4 Join scope and link status

- The initial lookup key is `(run_id, db_idx, raw_connection_id)`; constrain
  further with `device_id`, `context_id`, process/thread identity, and time
  ordering when available.
- `link_status`: `unique` | `one_to_many` | `ambiguous` | `unresolved`.
- Only `unique` (or an explicitly allowed same-device one-to-many relation)
  MAY serve as `exact_connection_id` evidence.
- If one connection ID maps to multiple devices and the host record carries
  insufficient device/context information, the relation is `ambiguous` and
  MUST NOT produce `queued_visible_task_delay` or
  `host_sync_api_present` via `exact_connection_id`.
- Host API device attribution: API carries its own device id -> use it;
  unique connection join -> inherit the target task's device; otherwise
  `device_id = unknown` and the API MUST NOT be used for per-device
  explanations. A host API MUST NOT be attached to all devices merely
  because it temporally overlaps them.

### 6.5 Candidate-only evidence (M0)

A candidate is a diagnostic-only hypothesis or below-threshold association
that does not enter the official explanation partition. M0 candidate sources:

- pattern inference (`inferred` / `pattern_context`);
- possible-overlap host sync candidates (section 7.3);
- non-robust enqueue-to-task delay candidates (section 7.3).

Candidates:

- MUST NOT enter `traceloom_idle_explanation`.
- MUST NOT replace `unattributed_visible_idle`.
- MUST NOT contribute to `S_direct`, `S_correlated`, or `C_explained`
  (section 11).
- MAY be counted separately as diagnostic candidate coverage.
- A candidate with `candidate_level = correlated` is NOT an official
  correlated explanation; entry into the official partition is decided by
  robust gating (section 7.3).

### 6.6 Communication canonicalization

A canonical `TASK`-to-`COMMUNICATION_OP` match REQUIRES, in order:

1. compatible run/db scope;
2. compatible device identity;
3. an unambiguous exact `connectionId` when available;
4. temporally compatible intervals;
5. compatible communication metadata when available.

Degradation rules:

- exact `connectionId` + compatible device -> strongest canonicalization
  match.
- no `connectionId`, but compatible device + strong temporal and metadata
  match -> heuristic duplicate candidate only; MUST NOT be silently
  canonicalized in M0.

Other rules:

- `COMMUNICATION_OP` is the canonical productive communication interval;
  linked `TASK` rows are supporting evidence only.
- A communication `TASK` becomes a canonical productive interval only when no
  matching `COMMUNICATION_OP` exists.
- Canonicalization MUST preserve links to all source rows.
- The same source operation MUST NOT materialize multiple productive
  intervals solely because it appears in multiple profiler tables.
- When duplication cannot be determined, do NOT silently merge; emit an
  ambiguity diagnostic.

### 6.7 Uncertainty reporting

- Distribution summaries (`p50`, `p95`, `max`) and statistical uncertainty
  (bootstrap 95% CI) are different things and MUST be named differently:

```text
mean_wait_us, p50_wait_us, p95_wait_us
mean_wait_us_ci95_low, mean_wait_us_ci95_high
```

### 6.8 Mapping to existing vocabularies

| This contract | TraceLoom RFC v2 | Parent repo (RLCP) |
| --- | --- | --- |
| `direct` | `confirmed` | device-layer localization evidence; still `localization`, never `causal` by itself |
| `correlated` | `contextual` | same |
| `inferred` | `heuristic` | candidate-only in M0; never enters localization output |
| (n/a) | (n/a) | `causal` requires a matched control/intervention (offline-intervention-gate semantics) |
| (n/a) | (n/a) | experiment evidence labels (`real-online`, `existing-server-probe`, `replay`, `simulation/model`, `projected-profile`, `derived-artifact`) classify the experiment; orthogonal to per-interval evidence level |

Every experiment producing idle evidence MUST carry a parent-repo evidence
label, and every interval claim MUST carry a per-interval evidence level.

## 7. Clock Domains and Alignment

### 7.1 Reference-point affine model

Host and device timestamps come from different clock domains. The mapping
function is:

```text
f(h) = d_ref + a * (h - h_ref)
```

with observation model:

```text
d_i = f(h_i) + epsilon_i
```

- `h_ref`: reference host timestamp (ns).
- `d_ref`: corresponding reference device timestamp (ns).
- `a`: clock scale; `drift_ppm = (a - 1) * 1e6`.
- `offset_ns` is DEFINED as `d_ref - h_ref` (the offset at the reference
  origin), not an arbitrary affine intercept.
- `epsilon_i`: per-marker absolute residual; not part of the mapping
  function.

Output fields: `host_to_device_scale`, `host_to_device_offset_ns` (= `d_ref
- h_ref`), `reference_host_ns`, `reference_device_ns`, `drift_ppm`,
`fit_marker_count`, `validation_marker_count`, `absolute_residual_p50_ns`,
`absolute_residual_p95_ns`, `absolute_residual_max_ns`.

### 7.2 Marker protocol and frozen fit

- Marker payload (frozen):

```text
marker_id
host_before_ns
host_after_ns
device_timestamp_ns
host_pid
host_tid
device_id
stream_id (if available)
connection_id (if available)
call_site
return_status
```

- Marker host time (frozen): `h_i = host_before_ns +
  (host_after_ns - host_before_ns) / 2` computed as integer floor division
  (overflow-safe midpoint form). `host_before_ns` / `host_after_ns` bracket
  the device-visible event and define marker bracket uncertainty.
- Fit / holdout split (frozen): sort markers by `h_i`; every fifth marker
  goes to the holdout set; the remaining markers form the fit set; the first
  and last markers MUST remain in the fit set. Fitting and reporting the
  final error on the same markers is forbidden (optimistic error estimates).
  A calibration REQUIRES at least 6 markers and a non-empty validation set;
  otherwise `alignment_status` MUST be `invalid` and no calibrated
  cross-clock explanation may be emitted. Run at least 3 repeated captures;
  report per-capture and pooled distributions.
- Frozen fit formulas:

```text
s_ij = (d_j - d_i) / (h_j - h_i),  h_j != h_i
a    = median{ s_ij }
h_ref = median{ h_i }
d_ref = median{ d_i - a*(h_i - h_ref) }
f(h) = d_ref + a * (h - h_ref)
```

  Median rule (frozen): for an even number of ordered values, the median is
  the arithmetic mean of the two central values.
- Outlier rejection MAY be applied after the initial fit, with rejected
  markers counted and reported; it MUST NOT be applied before the initial
  fit.
- Fit metadata REQUIRED: `fit_method` (= `theil_sen_median`), `fit_method_version`,
  `fit_random_seed` (fixed value `0`; deterministic), `input_marker_count`,
  `inlier_marker_count`, `rejected_marker_count`.
- Determinism statement: the fit procedure is deterministic under the
  numerical and median rules defined here. Implementations MUST match
  golden-fixture tolerances; bit-identical output across languages and float
  implementations is NOT claimed.
- `scale` serialization is fixed: decimal with 12 fractional digits (the
  scale is a ns/ns ratio; ppm-level drift needs ~6 digits, 12 leaves
  headroom). Mapped timestamps `f(h)` MUST be rounded to integer nanoseconds
  with round-half-to-even before any overlap or delay comparison, and golden
  fixtures MUST use the same rounding.

### 7.3 Overlap and delay windows

All uncertainties in device-clock domain.

```text
validation_p95_residual = p95 over holdout markers of
                          abs(device_timestamp_ns - f(h_i))
bracket_uncertainty_device_ns = abs(a) * p95((host_after_ns - host_before_ns)/2)
epsilon = validation_p95_residual + bracket_uncertainty_device_ns
```

**Host-sync overlap** (for `host_sync_api_present`), host interval
`[hs, he)` mapped to `[f(hs), f(he))`:

- **Possible overlap** (candidate discovery only):
  `[f(hs) - epsilon, f(he) + epsilon)` intersects the gap. Possible overlap
  MUST NOT produce a `traceloom_idle_explanation` row; the official
  explanation remains `unattributed_visible_idle`; it MAY produce a
  candidate-only diagnostic row; it MUST NOT contribute to attributable
  coverage.
- **Robust overlap**:
  `[f(hs) + epsilon, f(he) - epsilon)` intersects the gap. Only robust
  overlap MAY produce `host_sync_api_present` (`correlated`,
  `temporal_overlap`), and the emitted explanation slice MUST be the
  intersection of the robust interval and the remaining gap: a one-nanosecond
  intersection must not classify the whole gap.
- If the robust interval is empty (`host interval < 2*epsilon`), the host
  event is candidate-only and MUST NOT produce a correlated explanation.
  Short host events are inherently unrecognizable at this alignment quality;
  this is a documented cost, not a gap to paper over.

**Enqueue-to-task delay** (for `queued_visible_task_delay`; not an
interval-overlap question). For an enqueue API with end `he` linked to a
device task with start `ts` by exact `connectionId`:

```text
possible_delay = [f(he) - epsilon, ts)
robust_delay   = [f(he) + epsilon, ts)
```

`queued_visible_task_delay` MAY be emitted only when ALL hold:

1. the link is unique and exact (`link_status == unique`);
2. the API belongs to the enqueue family defined by the versioned, hashed
   host-API allowlist ruleset `idle_evidence_host_api_rules.tsv` (initial
   entries: `aclrtLaunchKernel*`, `aclrtMemcpyAsync*`);
3. `robust_delay` is non-empty (`ts > f(he) + epsilon`);
4. the emitted explanation is the intersection of `robust_delay` and the
   gap.

Otherwise the delay evidence is candidate-only (`candidate_status =
non_robust_delay`) and MUST NOT cover residual.

### 7.4 alignment_status

```text
alignment_status:
  not_required      # device-only evidence, no cross-domain comparison
  calibrated        # real marker data available
  synthetic_only    # validated on synthetic clock fixtures only
  uncalibrated      # no marker data
  invalid           # marker fit failed or contract violated
```

Behavior when `uncalibrated`:

- Host API rows MAY be imported and displayed.
- Time-overlap-based idle explanations MUST NOT be generated.
- API-to-task delay MUST NOT be computed.
- Synthetic clock fixtures prove the algorithm implementation, not real-trace
  calibration; `synthetic_only` MUST NOT be reported as `calibrated`.
- `synthetic_only` MAY support correlation-mechanism validation on controlled
  synthetic fixtures (their clock model and epsilon are known by
  construction), but MUST NOT support any real-trace cross-clock claim;
  correlation rows from synthetic fixtures are mechanism evidence only.

### 7.5 Deliverable set under the current environment constraint

With no marker-capable environment (`uncalibrated`) and no collection
attestation (`collection_status = unknown`), the M0 deliverable set on real
profiler fixtures is:

```text
blocked_by_visible_wait
capture_control_present
runtime_control_present
unattributed_visible_idle
```

`host_sync_api_present`, `queued_visible_task_delay`, and
`no_observed_device_work` are frozen in this contract but not enabled until
calibration data and collection attestation exist. On controlled synthetic
fixtures (`collection_status = complete` by construction, `synthetic_only`
alignment), `no_observed_device_work` is additionally available. Correlation
rules are delivered in frozen form and validated only on synthetic fixtures,
consistent with the parent-repo instruction to proceed immediately on fixed
fixtures.

## 8. Attribution Algorithm and Priority

1. Classify every task via `idle_evidence_semantic_rules.tsv`.
2. Canonicalize communication intervals (section 6.6).
3. Build the per-device productive timeline (union of canonical productive
   tasks).
4. Extract gaps within the analysis span (section 3.2 covers the no-
   productive-task case).
5. Build per-stream observable state timelines over the stream universe,
   with `ambiguous_overlap` partitioning (section 3.3).
6. Import host API events and resolve `connectionId` links
   (`link_status`, section 6.4) with alignment gating (section 7).
7. Slice each gap into explanation intervals with a deterministic priority:
   `blocked_by_visible_wait` > `capture_control_present` >
   `runtime_control_present` > `queued_visible_task_delay` >
   `host_sync_api_present` > `no_observed_device_work` >
   `unattributed_visible_idle`.
8. Aggregate explanation intervals to anchors, loop-tree nodes, device
   summaries, and SQL views.

Category priority is contract semantics; it MUST NOT be scanned as a tuning
hyperparameter.

## 9. Output Schema

Frozen interval conventions apply to interval-bearing tables
(`traceloom_device_interval`, `traceloom_stream_state`,
`traceloom_idle_explanation`): `[start_ns, end_ns)` half-open, integer
nanoseconds, `end_ns > start_ns`, `clock_domain` column, and the version
columns `contract_version`, `semantic_rules_version`,
`attribution_rule_version` (distinct: contract revision, taxonomy revision,
algorithm revision), and `run_id`.

Non-interval tables (`traceloom_clock_model`, which has two clock domains and
no interval of its own) are exempt from interval columns.
`traceloom_evidence_link` overlap fields are nullable: relations without a
temporal extent (`pattern_context`, `none`) carry null `overlap_start_ns` /
`overlap_end_ns`.

`run_id` MUST be `lowercase_hex(SHA-256(JCS(metadata_without_run_id)))`:
the `run_metadata.json` file canonicalized with RFC 8785 JSON
Canonicalization Scheme (JCS), with the `run_id` field excluded before
hashing, to avoid circular definition.

Tables (engineering implements these; semantics here take precedence over the
RFC where they differ):

- `traceloom_device_interval` — `interval_kind`: `productive_active` |
  `visible_productive_idle` (`analysis_status` lives in
  `traceloom_run_metadata`).
- `traceloom_stream_state` — observable per-stream states with
  `stream_universe_kind`, `stream_universe_size`, `observed_stream_count`,
  `observed_universe_scan_complete`, `collection_status`.
- `traceloom_host_api_event` — normalized host API rows.
- `traceloom_task_api_link` — `connectionId` joins with `link_status`.
- `traceloom_idle_explanation` — category, evidence level, alignment status,
  reason, source links.
- `traceloom_idle_candidate` (diagnostic; new):

```text
candidate_id
gap_interval_id
candidate_category
candidate_level        # correlated | inferred
candidate_relation     # temporal_overlap | exact_connection_id |
                       # pattern_context
candidate_status       # possible_only | non_robust_delay |
                       # pattern_inference
reason
alignment_status
```

- `traceloom_clock_model` (new):

```text
clock_model_id
source_clock_domain
target_clock_domain
mapping_kind
scale
offset_ns                 # d_ref - h_ref
reference_host_ns
reference_device_ns
drift_ppm
fit_method
fit_method_version
fit_random_seed
input_marker_count
inlier_marker_count
rejected_marker_count
fit_marker_count
validation_marker_count
absolute_residual_p50_ns
absolute_residual_p95_ns
absolute_residual_max_ns
alignment_status
```

- `traceloom_evidence_link` (unified for explanations and candidates):

```text
owner_kind              # explanation | candidate
owner_id
evidence_ordinal
source_kind
source_table
source_key
relation
evidence_level
overlap_start_ns        # nullable: null for relations without temporal
overlap_end_ns          # extent (pattern_context, none)
```

- `traceloom_run_metadata` (run-level record; new):

```text
run_id
analysis_status         # ok | no_productive_span | invalid_analysis_span |
                        # empty_input | invalid_input
span_start_ns           # nullable
span_end_ns             # nullable
contract_version
semantic_rules_version
semantic_rules_sha256
host_api_rules_version
host_api_rules_sha256
```

  `traceloom_run_metadata` carries the required `analysis_status` even when
  no device-interval rows exist (e.g. `no_productive_span`, `empty_input`,
  `invalid_input`).

Engineering note: the current `NativeIr` has no `host_api_events`,
`clock_models`, `task_api_links`, `device_intervals`, `stream_states`, or
`idle_explanations`; and the existing `CANN_API` reading serves ACLGraph
capture/replay metadata, not general host-API event import. A dedicated
import path is REQUIRED (tool-repo issue #3).

## 10. Golden Fixtures

Each fixture is a synthetic msprof-schema SQLite database plus
`ground_truth.json`. Every ground-truth interval MUST carry:

```json
{
  "start_ns": 100,
  "end_ns": 200,
  "interval_kind": "visible_productive_idle",
  "explanation_category": "blocked_by_visible_wait",
  "evidence_level": "direct",
  "evidence_relation": "device_event_coverage",
  "alignment_status": "not_required",
  "collection_status": "complete",
  "expected_source_keys": ["TASK:17"]
}
```

Tests MUST assert at least: exact interval boundaries; category; evidence
level; relation; lineage (`expected_source_keys`); explanation slices
non-overlapping; explanation union equals the original gap; productive union
plus gap union covers the analysis span exactly at integer-nanosecond
precision.

Fixture classes:

| Class | Construction | Validates |
| --- | --- | --- |
| `positive` | Known wait injected (wait task coverage, host sync overlap, clean multi-stream) | correct attribution, category priority |
| `adjacent_overlap` | Event boundaries overlapping/nested across streams; ambiguous edges | interval algebra, `ambiguous_overlap` partition, no double counting |
| `event_loss` | Subset of events deleted (wait task, host API, stream rows) | residual preservation, no false attribution from absence, `collection_status` downgrade |
| `clock_drift` | Timestamp transforms (offset, linear drift, jitter) | alignment-error reporting, robust-overlap and robust-delay rules, no drift-induced false `correlated` |

Edge cases REQUIRED in the fixture set: duplicate event rows; invalid / zero /
negative durations; cross-device identical `connectionId` (expect
`link_status = ambiguous`); the same communication represented in both
`TASK` and `COMMUNICATION_OP` (canonicalization, no duplicate productive
intervals); `unknown`-role tasks inside gaps (remain `unattributed_visible_idle`,
diagnostic evidence preserved); analysis-span boundary gaps; no productive
task at all (`no_productive_span` and full-span-gap behaviors); gaps shorter
than the alignment error; same-stream overlapping tasks; monolithic vs split
profile equivalence.

A single canonical generator for the four classes is allowed, but MUST NOT
limit all tests to one scenario: distinct schema boundaries require
independent fixtures. Every fixture ships `run_metadata.json` (parent commit,
fixture class, injected perturbation, ground-truth reference, evidence label;
`simulation/model` for synthetic).

Checked-in fixture and CI check: the counterexample "host wait exists but
visible idle is zero" (a host sync API event present in the capture while the
device productive timeline covers the whole analysis span) is a REQUIRED
executable check. It lives in
`native/tests/fixtures/idle_evidence/host_wait_zero_visible_idle/` (synthetic
msprof-schema SQLite database + `ground_truth.json` + `run_metadata.json`,
fixture class `positive`) and is verified by
`traceloom_native_idle_evidence_golden_fixture_tests`, which runs in CI
(`.github/workflows/native-deb.yml`, job `idle-evidence-golden-check`). The
check asserts BOTH sides of the counterexample: the fixture's host-side sync
API row exists (host wait), and the analyzer reports zero
`visible_productive_idle` over the same fixture (device timeline fully
productive). Fixture results are contract/example evidence and MUST NOT be
presented as a matched A/B of runtime traces.

## 11. Evaluation Metrics

### 11.1 Coverage (mutually exclusive shares)

```text
S_direct     = T_direct     / T_all-gap
S_correlated = T_correlated / T_all-gap
S_residual   = T_residual   / T_all-gap
S_direct + S_correlated + S_residual = 1
C_explained = S_direct + S_correlated
```

In M0 there are no official `inferred` explanations, so these three shares
exhaust the gap time. The stacked bar is `direct | correlated | residual`.
Per-category coverage (each explanation category's share of `T_all-gap`)
MUST also be reported for hotspot analysis.

When `T_all-gap == 0` (fully productive span, or no valid span), the three
shares and `C_explained` are `NA` (not 0, and not silently omitted); pooled
aggregates MUST exclude NA rows and MUST report the number of NA runs.

### 11.2 False attribution

```text
FAR = T_wrongly_attributed / T_attributed
```

`T_attributed = T_direct + T_correlated`. If `T_attributed == 0`,
`FAR = NA`, not 0.

`T_wrongly_attributed` includes any emitted non-residual duration for which
at least one of:

1. the emitted category differs from the ground-truth category; or
2. the emitted explanation extends beyond the ground-truth interval for that
   category (e.g., ground truth `[100,150) blocked_by_visible_wait`, output
   `[100,170)` -> `[150,170)` is wrongly attributed); or
3. the emitted evidence level/relation is stronger than allowed by ground
   truth.

Additionally report separately, so FAR does not hide the category-vs-boundary
distinction:

```text
category_confusion_matrix
boundary_over_attribution_ns
boundary_under_attribution_ns
```

### 11.3 Collection overhead

Matched trace/no-trace runs; report p50/p95 delta of end-to-end latency and
throughput. Without a runnable environment, freeze the definition and report
format; the axis stays empty in the figure.

### 11.4 Curves

The main figure reports `C_explained` and `FAR` over a scan of tunable knobs,
not a single operating point. Scan: `correlation tolerance` (epsilon
multiplier), `minimum evidence threshold`, `marker density`,
`instrumentation level` (the last two require a runnable environment; leave
empty until then). NEVER scan category priority. Never report coverage
without its FAR curve.

## 12. Forbidden Claims

- This contract NEVER produces "true hardware idle". No profiler observation
  proves hardware idleness without an independent hardware occupancy counter,
  full scheduler state, or controlled intervention. The strongest permissible
  statement is: "no profiler-visible device work was observed".
- "idle" without a qualifier (`visible_productive_idle`,
  `no_observed_device_work`, or the specific category) is forbidden.
- Empty intervals MUST NOT be labeled `no_observed_device_work` unless
  `collection_status == complete` and the observed universe was fully
  scanned; otherwise they are `unattributed_visible_idle`.
- Intervals covered by `unknown`-role tasks MUST NOT be labeled
  `runtime_control_present` or `no_observed_device_work`; they remain
  `unattributed_visible_idle`.
- "the gap was caused by this wait" — forbidden without direct dependency
  evidence; permissible: "the interval is covered by a visible wait task".
- "the host synchronized" as causality — host sync API presence is
  `correlated` unless a dependency edge exists.
- "coverage = X%" without the false-attribution curve.
- Any causal claim or performance-improvement claim from profiler/analysis
  output; this contract's evidence is conservative localization evidence.
- Proposed parent-repo `claim_ledger.md` wording:

> M0 produces conservative cross-layer localization evidence for
> profiler-visible productive gaps. It does not establish hardware idleness,
> dependency causality, or performance benefit.

## 13. Change Control

- This contract freezes once approved in the parent-repo issue #2.
- Category, level, relation, window-rule, alignment-status, and forbidden-
  wording changes MUST be decided in the parent issue before engineering
  implementation (per tool-repo issue #3 boundary).
- New residual-attribution rules require a controlled-intervention validation
  first.
- Promotion of candidate-only evidence into official explanations follows the
  same change-control path.

## 14. Deferred Questions

- Nonlinear drift: does the affine model need a piecewise or quadratic
  extension for very long captures, and at what duration?
- Upgrade path from `synthetic_only` to `calibrated`: minimum marker count,
  validation residual bound, and repeated-capture criteria.
- `queued_visible_task_delay` promotion: can same-thread/same-context
  evidence plus exact `connectionId` justify `direct` in future
  instrumentation? M0 keeps it `correlated`.
- `productive_data_move` per-direction mapping detail (H2D/D2D/D2H,
  sync/async) in the semantic rules TSV.
- Stream universe evolution: when can a workload-owned universe (vs observed
  universe) be established without runtime cooperation?
- One-to-many `link_status`: when is a same-device one-to-many relation
  explicitly allowed as exact evidence?
- Heuristic communication canonicalization (no `connectionId`): what
  thresholds make it safe to promote from candidate to canonical in a future
  version?
