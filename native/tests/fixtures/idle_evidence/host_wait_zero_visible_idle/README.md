# Idle Evidence Golden Fixture: host wait, zero visible idle

Counterexample class for the idle-evidence contract (section 5, 10, 12):

> A host-side sync API event (`aclrtSynchronizeStream` in `CANN_API`,
> `[500, 4500)`) exists in the capture, yet the device productive timeline is
> back-to-back compute across the analysis span `[1000, 4000)` — the analyzer
> MUST report **zero** `visible_productive_idle`.

This is the counterexample to the naive equivalence "host wait implies device
visible idle". The check in
`native/tests/analysis/idle_evidence_golden_fixture_tests.cpp` verifies both
sides from the same fixture and asserts the counterexample holds:

1. host wait present: `CANN_API` contains an `aclrtSynchronizeStream` row with
   `end_ns > start_ns` (host-side evidence);
2. visible idle zero: the productive timeline over the fixture's device tasks
   is exactly one `productive_active` interval covering the analysis span,
   with no `visible_productive_idle` interval.

The fixture is synthetic (`evidence_label: simulation/model` per
run_metadata.json); it is contract/example evidence and does NOT replace the
matched A/B comparison of runtime traces.

## Files

- `fixture.sql` — the SQL that builds the database (regeneration reference)
- `host_wait_zero_visible_idle.db` — the checked-in golden SQLite fixture
- `ground_truth.json` — expected analysis output (contract section 10 format)
- `run_metadata.json` — fixture provenance (contract section 10)

## Regenerate

```bash
sqlite3 host_wait_zero_visible_idle.db < fixture.sql
```

Keep the checked-in `.db` in sync with `fixture.sql`; the test loads the
checked-in `.db` and compares its analysis against `ground_truth.json`.
