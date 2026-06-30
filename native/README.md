# TraceLoom Native

Native C++ analysis core for TraceLoom.

## Build

Use the dev preset:

```bash
cd native
cmake --preset dev
cmake --build --preset dev -j
ctest --preset dev
```

The `dev` preset searches `$HOME/.local` through `CMAKE_PREFIX_PATH`, so a
user-local SQLite install such as `~/.local/include/sqlite3.h` and
`~/.local/lib/libsqlite3.a` is enough to enable the optional Ascend SQLite
adapter.

When SQLite is available, the build also provides a manual inventory smoke
tool for real Ascend profiler databases:

```bash
../build/traceloom-native/traceloom-native-ascend-sqlite-inventory \
  ../examples/kickstart_smoke/msprof_raw/.../msprof_*.db
```

This is intentionally a human inspection tool rather than a hard sample-DB
test; real profiler fixtures can drift as the examples evolve.

The first shallow native analysis executable emits debug JSON using the
`native_in_memory_result_v1` schema:

```bash
../build/traceloom-native/traceloom-native-analyze-db \
  --source-db ../examples/kickstart_smoke/msprof_raw/.../msprof_*.db \
  --threads 8 \
  --out native_result.json
```

This output is a candidate/debug surface. It is not the final TraceLoom report
model and should not be consumed as report-stable semantics yet.

The same tool can optionally emit the Phase 5A grammar state-machine debug
surface as a separate JSON document:

```bash
../build/traceloom-native/traceloom-native-analyze-db \
  --source-db ../examples/kickstart_smoke/msprof_raw/.../msprof_*.db \
  --threads 8 \
  --out native_result.json \
  --grammar-debug-out native_grammar_debug.json
```

The grammar debug output records the algorithm mode, producer sequence, known
native-v1 deltas, engine steps, macro definitions, and final compressed
sequence. It is not the final `PatternModel`. Use either the main result or
the grammar debug output on stdout, not both; `--out - --grammar-debug-out -`
is rejected so the two JSON documents cannot be mixed.

Protected-sequence fixtures can run through the same native pipeline:

```bash
../build/traceloom-native/traceloom-native-analyze-fixture \
  --fixture ../../drafts/refactor/80_tests_fixtures/fixtures/protected_sequence/f2_hard_replay_unit_boundary.json \
  --threads 8 \
  --out native_fixture_result.json \
  --grammar-debug-out native_fixture_grammar_debug.json
```

To test the build without SQLite support:

```bash
cd native
cmake --preset dev-no-sqlite
cmake --build --preset dev-no-sqlite -j
ctest --preset dev-no-sqlite
```

The core target must stay independent of SQLite, Python, JSON, CLI parsers, and
adapter headers. The boundary test enforces this for native core directories.
