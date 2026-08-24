# AugDB to Perfetto Timeline

TraceLoom can export its self-contained queryable database timeline (AugDB) as
Perfetto / Chrome Trace JSON. This is a first-class projection of AugDB, not a
second analyzer or a custom frontend.

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

When `--output` is omitted in the second form, TraceLoom writes
`analysis.perfetto.json.gz` beside the input. A `.gz` suffix streams the JSON
through `gzip`; any other suffix writes plain JSON. Both formats open directly
in Perfetto.

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
   retains its source ID, embedded table, and source rowid.

TraceLoom does not infer semantic model layers for this view. A colored interval
means an observed structural subtree occurrence, not a guessed layer name.

## Readable identity and topology color

User-facing titles are short and queryable:

```text
N001 · root
N612 · motif C · body 1/2
N612 · motif C · body 2/2
```

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
