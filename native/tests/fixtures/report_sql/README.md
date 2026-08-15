# Report SQL corpus

This directory is the reviewable input and expected-output contract for the
queries in `docs/report-sql/`.

- `cases.tsv` maps every checked-in report query to the smallest corpus that
  exercises it.
- The corpus files are deterministic UTF-8 SQLite `.dump` scripts, not binary
  databases or raw profiler captures.
- `empty_full.sql` carries the complete empty schema used to prove that every
  report query still prepares with the expected columns and returns no rows.
- `expected/*.snap` records the complete typed query result for its mapped
  corpus. The test compares every column and row, not only a count or first
  row.

The corpus was characterized from the former procedural C++ fixture builders.
Each dump was round-tripped through SQLite and compared by schema and
key-sorted logical content before it was admitted. The narrow reconstruction
fixture deliberately omits two unused legacy views whose definitions referred
to absent event tables; the test now requires every view in every corpus to be
queryable.

Golden updates are never automatic. After an intentional query or corpus
change, build the test executable and run:

```bash
build/native-tests/native/traceloom_native_report_sql_compat_tests \
  --update-goldens
```

Then review the corpus and snapshot diffs before committing. Report queries
must define deterministic ordering; do not hide unstable ordering by sorting
inside the test harness.
