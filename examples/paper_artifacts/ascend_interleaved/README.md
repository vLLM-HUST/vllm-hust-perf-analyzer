# Ascend interleaved structural pair

This checked-in pair is a deterministic reduction of two ordinary rank-0
Ascend profiler captures from the same controlled perturbation experiment. It
is a small analyzer artifact, not a benchmark result: the labels `stock` and
`fused` identify the paired runs, but TraceLoom does not infer workload phases
or performance causality from them.

Each profile preserves:

- four `recognized_complete_pattern` ACLGraph regions materialized as exact
  graph units `G1..G4`;
- the complete bounded sequences `U1..U3` between those units;
- all ordered productive-anchor memberships and their source-row provenance;
- the full unit fingerprints and cost fields observed in the retained source
  profiles; and
- open prefix/suffix evidence as typed `unrecognized`, rather than silently
  treating a cropped boundary as complete.

The deliberately small main databases are 1,949,696 bytes (`stock`) and
1,769,472 bytes (`fused`), 3,719,168 bytes combined. Each profile also carries
an 8 KiB numeric `stream_info.db` companion. No report or sidecar is committed.

## Reproduce the report

Build TraceLoom, then run either profile directly:

```bash
cmake --preset dev-tests
cmake --build --preset dev-tests -j "$(nproc)"

build/native-tests/native/traceloom \
  examples/paper_artifacts/ascend_interleaved/stock/PROF_REDUCED/msprof_stock.db
build/native-tests/native/traceloom \
  examples/paper_artifacts/ascend_interleaved/fused/PROF_REDUCED/msprof_fused.db
```

For a clean end-to-end artifact check that leaves outputs in a temporary
directory:

```bash
examples/paper_artifacts/tools/verify_ascend_interleaved.py \
  --traceloom build/native-tests/native/traceloom
```

Maintainers with the retained full-profile sidecars can additionally recheck
the reduction boundary against them:

```bash
examples/paper_artifacts/tools/verify_ascend_interleaved.py \
  --traceloom build/native-tests/native/traceloom \
  --reference-root /path/to/full-sidecars
```

The reference root must contain `stock-r0.db` and `fused-r0.db`.

## Expected structural observation

| Profile | Exact graph units | Complete structural units | `U1` anchors | `U1` total (us) | `U1` compute (us) | `U1` comm (us) | `U1` visible idle (us) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `stock` | 4 | 3 | 1186 | 183646.64 | 34764.94 | 11311.40 | 137570.30 |
| `fused` | 4 | 3 | 994 | 162387.44 | 78738.78 | 61013.54 | 22635.12 |

This table is intentionally observation-only. It demonstrates that a small
artifact can preserve the surprising inter-graph structural difference; any
workload-semantic or causal interpretation must be supplied and tested outside
TraceLoom.

## Construction and audit trail

The per-profile `reduction-manifest.json` records source and output SHA-256,
the inclusive time window, copied row counts, rowid policy, and the
`stream_info.db` hashes. `expected.json` records only stable fields from the
retained full-profile sidecars. Window-relative token ordinals, generated
anchor IDs, and Loop Tree node handles are deliberately excluded; ordered
membership hashes instead bind each unit to stable anchor/event attributes and
original source table/row keys.

The privacy reduction empties `HOST_INFO` and `TASK_PMU_INFO`, prunes
`STRING_IDS` to referenced values, and rejects retained path-, email-, or
identity-like strings during verification. The full reduction contract and
stop rule are in the parent README.
