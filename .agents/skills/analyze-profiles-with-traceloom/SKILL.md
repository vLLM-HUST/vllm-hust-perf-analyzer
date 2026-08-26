---
name: analyze-profiles-with-traceloom
description: Build or install TraceLoom, turn existing Ascend/CANN or CUDA/Nsight profiler databases into self-contained queryable database timelines (AugDBs), and use their structural, statistical, host/device, raw-provenance, Perfetto, and distributed-rank projections. Use when an agent must inspect profiler output with TraceLoom, compare repeated execution occurrences or ranks, diagnose timeline behavior, audit a finding back to provider rows, or produce a reproducible trace-analysis handoff.
---

# Analyze Profiles with TraceLoom

Treat the AugDB as the analysis product. Select a structural coordinate, compare
its measured Occurrences, then drill one finding back to embedded raw evidence.
Use Perfetto to inspect temporal geometry; use SQL over AugDB for statistics.

## Run the workflow

### 1. Establish the evidence boundary

- Work from an existing profiler DB or profiler output directory. TraceLoom is
  an offline analyzer; do not make it own workload launch or profile capture.
- Keep the raw input immutable. Record its path, capture/run identity, rank and
  device mapping when applicable, and the exact TraceLoom command and version.
- Treat every rank's timestamps as a separate clock domain until an explicit
  calibration receipt says otherwise.
- Put generated DBs, timelines, and logs in a task-owned output directory, not
  in Git. Never commit raw profiler DBs, AugDBs, or rendered timelines.

Read [intake-and-install.md](references/intake-and-install.md) when choosing an
input layout, finding/building TraceLoom, or collecting provenance.

### 2. Produce one AugDB per analysis input

Prefer the installed CLI when available. Otherwise use an exact local build:

```bash
if command -v traceloom >/dev/null 2>&1; then
  TRACELOOM="$(command -v traceloom)"
else
  cmake --preset dev
  cmake --build --preset dev -j "${BUILD_JOBS:-4}"
  TRACELOOM="$PWD/build/native/native/traceloom"
fi
"$TRACELOOM" --version
```

Analyze one DB with explicit output and bounded parallelism:

```bash
"$TRACELOOM" /absolute/path/to/msprof_YYYYMMDDHHMMSS.db \
  --threads "${TRACELOOM_THREADS:-4}" \
  --output /absolute/task-output/rank0.analysis.db \
  --timings \
  > /absolute/task-output/rank0.stdout.log \
  2> /absolute/task-output/rank0.stderr.log
```

Analyze a directory without explicit output flags; TraceLoom discovers its
inputs and writes neighboring `traceloom/analysis*.db` artifacts. Do not rename
official `ascend_pytorch_profiler_*.db` inputs.

### 3. Accept the artifact before interpreting it

Run the bundled bounded inspector:

```bash
python3 .agents/skills/analyze-profiles-with-traceloom/scripts/inspect_augdb.py \
  /absolute/task-output/rank0.analysis.db --top 12
```

Add `--quick-check` before sharing an artifact and `--sha256` when a digest is
needed. Require:

- `artifact_kind=queryable_database_timeline`;
- an intact `traceloom_metadata` and self-describing projection catalog;
- `source_embedded=true` and populated raw-source catalogs for a portable audit;
- a plausible event/anchor count for the intended rank or device; and
- no analyzer error hidden by an output file left from an older attempt.

If the source metadata or rank mapping does not match the intended capture,
stop. Do not analyze the convenient artifact instead of the correct one.

### 4. Ask one structural question at a time

Start with the database's own interfaces:

```sql
SELECT * FROM traceloom_analysis_surface ORDER BY surface_name;
SELECT projection_name, population_mode, resolution,
       observation_domain, measure_lens, purpose
FROM traceloom_projection_recipe ORDER BY display_order;
```

Then follow this coordinate-preserving route:

```text
Position
  -> all equivalent Occurrences
  -> one selected typical or unusual Occurrence
  -> child Occurrences / terminal events
  -> host windows or replay details when supported
  -> event/runtime source locator
  -> literal embedded provider row
```

Do not dump a large raw table and infer structure by eye. Select a Position
first, state how the comparison population was defined, and retain every ID
returned by the recipes. Read [analysis-playbook.md](references/analysis-playbook.md)
for bounded discovery queries, population statistics, continuations, and raw
audit.

### 5. Use the right presentation surface

- Use SQL/statistical plots for distributions across Occurrences, structural
  positions, ranks, or cost lenses.
- Use `export-perfetto` for raw temporal audit: recovered tree intervals,
  compressed timeline events, and embedded provider evidence share one view.
- Map every distributed rank explicitly. Use the default display only for
  rank-local geometry; use `--distributed-clock-model` when comparing aligned
  collective boundaries and an auditable receipt exists.

Read [perfetto-and-distributed.md](references/perfetto-and-distributed.md)
before exporting multiple ranks or interpreting clock-aligned views.

### 6. Return an auditable diagnosis

Report:

1. the input/run and rank/device mapping;
2. TraceLoom version or Git commit and exact command;
3. AugDB path, size, SHA-256, source hashes, and relevant policy digests;
4. the analytical question, selected Position/Occurrence coordinates, and SQL;
5. the population size, statistic or comparison, and concrete observation;
6. the raw event/runtime locator or explicit reason raw audit is unsupported;
7. the narrow interpretation and alternatives still open; and
8. any Perfetto path and clock-model receipt/status used for display.

Separate **observation** (returned rows), **inference** (best explanation), and
**claim** (what the evidence actually supports).

## Preserve TraceLoom's truth boundaries

- `total_us` is an overlap-safe wall-clock union, not the sum of stream times.
- Repeat averages normalize by both occurrence count and body repeat count;
  ordinary nodes normalize by occurrence count.
- A recovered repeated subtree is an observed execution structure, not an
  inferred model-layer semantic label.
- An uncovered interval or visual white gap is not automatically device idle.
- Equal labels do not define equivalent call sites; use Position or edge-role
  coordinates to build populations.
- Host windows and runtime/device relations are typed evidence with explicit
  missing and ambiguous states, not guaranteed causal attribution.
- Raw/provider evidence remains available even when policy excludes an event
  from structural identity or the compressed display.
- Candidate clock calibration is a display-only mapping. Aligned collective
  ends do not prove rank arrival times, dependencies, or causal waiting.

## Use the repository's executable examples

For a checked-in end-to-end example, read and run
[`examples/db-timeline-tour`](../../../examples/db-timeline-tour). For the full
public contracts, consult:

- [workflow.md](../../../docs/workflow.md)
- [composable-analytical-projections.md](../../../docs/composable-analytical-projections.md)
- [augmented-perfetto-timeline.md](../../../docs/augmented-perfetto-timeline.md)
- [evidence-role-projection.md](../../../docs/evidence-role-projection.md)
