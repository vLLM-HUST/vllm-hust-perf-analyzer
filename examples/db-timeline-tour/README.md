# 60-Second Queryable Database Timeline Tour

This tour demonstrates the two analytical directions enabled by TraceLoom's self-contained queryable database timeline:

- **horizontal drill-down**: structure → occurrence → event → embedded raw
  profiler row;
- **vertical comparison**: one recovered structure → all equivalent
  occurrences → comparable cost distribution.

From the repository root, analyze the checked-in two-device profile:

```bash
traceloom examples/kickstart_smoke/msprof_raw --threads 8
```

Then open the first result read-only and run the tour:

```bash
sqlite3 -readonly \
  examples/kickstart_smoke/msprof_raw/traceloom/analysis_db01.db
```

At the `sqlite>` prompt:

```sql
.read examples/db-timeline-tour/tour.sql
```

No SQL expertise is required. The script finds a nested `Rep x24` region,
shows its ordered children, compares all instances of the same recovered
structure, and follows one event to its embedded profiler-row locator.

The checked-in profile is the source of truth for both this tour and
[`docs/assets/queryable-db-timeline.svg`](../../docs/assets/queryable-db-timeline.svg).
Regenerate the image after analyzing the profile with:

```bash
python3 examples/db-timeline-tour/render_showcase.py \
  examples/kickstart_smoke/msprof_raw/traceloom/analysis_db01.db \
  docs/assets/queryable-db-timeline.svg
```

Use `--theme paper` to emit a light, print-friendly SVG from the same query
results. The dark and paper views differ only in presentation; their structure,
costs, occurrence population, and provenance path come from the same database.
