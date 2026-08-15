# Ascend SQLite adapter corpus

These UTF-8 SQL scripts are small, reviewable source-profiler fixtures. Tests
materialize them into temporary SQLite databases; generated `.db` files and raw
profiler captures do not belong in the repository.

Each directory describes one coherent base profile. Files under `mutations/`
change exactly one tested boundary and are applied explicitly by the test. Keep
insertion order stable where the adapter's source provenance uses SQLite rowids.

This corpus is intentionally not a universal Ascend schema model. Add a base or
mutation only when an adapter contract needs evidence that a smaller existing
fixture cannot express.
