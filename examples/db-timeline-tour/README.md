# 60-Second Queryable Database Timeline Tour

This tour demonstrates TraceLoom's core composable-projection UX. It selects
one high-level structural scope once, then derives several views:

- **population**: one recovered structure → all equivalent occurrences →
  comparable cost distribution;
- **realized behavior**: the same scope → occurrence 1 → ordered device
  members;
- **hierarchy**: the same scope → ordered child patterns;
- **cross-domain context**: the same scope → supported host windows → runtime
  API distribution; and
- **audit**: one member → normalized event → embedded profiler row;
- **dynamic branch**: one recurrent device-bubble position → its occurrence
  population → one selected host interval → literal calls → host source row.

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

The submission/artifact-freeze check is executable too:

```bash
python3 examples/db-timeline-tour/verify_tour.py \
  examples/kickstart_smoke/msprof_raw/traceloom/analysis_db01.db
```

It verifies the coordinate contract, representative continuation paths,
scope/population preservation, typed host boundaries, and both device-event
and runtime-call drill-down to a literal embedded raw row.

No SQL expertise is required. The script finds a nested `Rep x24` region,
stores its `node_id` as the selected scope, then changes population,
resolution, observation domain, and measure lens without reconstructing the
region boundary. It also prints the reusable recipes, returned coordinate
contracts, and compatible continuations embedded in the database. Selector
types, candidate-coordinate relations, and supported next queries are
queryable rather than hidden in a separate client API or inferred from SQL
text. Typed missing host endpoints remain visible during the cross-domain
projection.

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
