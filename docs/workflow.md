# Workflow

TraceLoom is a native offline analyzer:

```text
msprof SQLite output
  -> source discovery and normalization
  -> semantic anchor timeline
  -> repeated-pattern grammar
  -> overlap-safe cost attribution
  -> Loop Tree report and optional SQLite evidence
```

## 1. Collect A Profile

Run the workload with Ascend/CANN `msprof`. TraceLoom does not launch the
workload or require a runtime wrapper. Supported production layouts include:

```text
<run_dir>/msprof_raw/PROF_*/msprof_*.db
<raw_dir>/PROF_*/msprof_*.db
```

## 2. Run TraceLoom

```bash
traceloom /path/to/msprof_output
```

For one database:

```bash
traceloom /path/to/PROF_.../msprof_YYYYMMDDHHMMSS.db
```

Use `--threads N` to control parallelism. TraceLoom writes reports under a
neighboring `traceloom/` directory by default.

## 3. Read The Loop Tree

Start with `loop_tree_v2.md` or `deviceN_loop_tree_v2.md`. Compare
`avg_total_us`, `avg_compute_us`, `avg_comm_us`, and `avg_idle_us` across
sibling nodes. Repeat-node averages are normalized per loop-body iteration.

## 4. Drill Down When Needed

The normal report is intentionally compact. For queryable evidence, request a
native compatibility sidecar:

```bash
traceloom /path/to/msprof.db \
  --compat-db-out /tmp/traceloom-sidecar.db \
  --loop-tree-out /tmp/loop_tree_v2.md
```

Inspect it with `sqlite3` or another SQLite client. Source references retain
the original database, table, and row identifiers.

## 5. Share A Reproducible Diagnosis

Share the TraceLoom version and command, the Loop Tree report, selected SQL
results, and profile metadata/checksums. Avoid committing large raw profiler
databases or generated reports to this repository.
