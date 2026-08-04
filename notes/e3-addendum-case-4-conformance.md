# E3 Addendum Conformance Case 4 Record

Date: 2026-08-04

## Scope

This record covers the deterministic E3 combination case in which a
legitimate zero-duration profiler point marker carries the Ascend adapter's
unassigned-stream sentinel (`0xFFFFFFFF`). Stream assignment and interval
extent are independent semantic axes.

Input:

```text
analysis span: [100,400)

TASK point marker:
  device_id = 0
  stream_id = 0xFFFFFFFF
  interval = [150,150)
  role = record
  StreamRow omitted

healthy sibling TASK:
  device_id = 0
  stream_id = 3
  interval = [200,300)
  role = productive_compute
```

Expected and observed result:

- E3 run and device status remain `ok`.
- Both `zero_duration_point_event_ignored` and `unassigned_stream`
  diagnostics are present.
- The point marker emits no interval, creates no sentinel timeline, and adds
  no stream-universe membership.
- The healthy stream remains present, so device and run universe size are 1.
- Device and run `observed_universe_scan_complete` are both `false`.

The executable case lives in
`native/tests/analysis/stream_state_timeline_tests.cpp` under the marker
`E3 addendum conformance case 4`.

## Artifact bindings

| Object | Version, path, or digest |
| --- | --- |
| Implementation commit | `adbcde1f3732f89614fc144ac3d65c8170f1ab03` |
| Implementation base | `7fcdc9d6ba8f764faed59834a1cee4648c4b930f` |
| Idle Evidence Contract | parent repository `docs/idle_evidence_contract.md`, Draft v4.3, SHA-256 `8edb42b706b6cab14dfde2b109841cb8af090883c9ea86696ee779de21d0c9ed` |
| Tool-repository contract mirror | `notes/idle-evidence-contract.md`, SHA-256 `b963c2960bf664f74b828fc2f6bfd8dbba66e327dd1f6eed34c1735b8cd979ca` |
| Semantic ruleset | `idle-evidence-semantic-v1`, `native/data/idle_evidence_semantic_rules.tsv`, SHA-256 `93274230064958c6d085adf26d5ab96739b311a3e2ac785af16bb4654aa811e5` |
| Inline fixture digest | SHA-256 `6be1143539f773400ad0531e22e33f12784dae5b7b8a8ee8b9e93d3c78651ec6` |
| E3 addendum | **Not bindable:** no frozen addendum artifact is checked into either repository or available in the inspected refs. |

The inline fixture digest is the SHA-256 of the exact case-4 source block in
the implementation commit, extracted deterministically with:

```bash
git show adbcde1f3732f89614fc144ac3d65c8170f1ab03:\
native/tests/analysis/stream_state_timeline_tests.cpp \
  | sed -n '/E3 addendum conformance case 4/,/\/\/ ---- 15\./p' \
  | sed '$d' \
  | sha256sum
```

## Validation

The following commands completed successfully:

```bash
cmake --build --preset dev-tests -j "$(nproc)" \
  --target traceloom_native_stream_state_timeline_tests
build/native-tests/native/traceloom_native_stream_state_timeline_tests

cmake --preset dev-tests
cmake --build --preset dev-tests -j "$(nproc)"
ctest --preset dev-tests --output-on-failure

cmake -S native -B build/release \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACELOOM_NATIVE_BUILD_TESTS=OFF
cmake --build build/release -j "$(nproc)"
```

Results:

- Targeted E3 executable: passed.
- Development suite: 47/47 enabled tests passed.
- Eleven external-fixture tests were disabled because the configured external
  fixture root was absent.
- Release build without external fixture tests: passed.

## Conformance status

This is executable engineering evidence for case 4 and binds the available
implementation, contract, ruleset, and inline fixture inputs. It is not a
claim of final E3 addendum conformance because the frozen addendum bytes and
SHA-256 are unavailable, and the remaining required deterministic addendum
cases cannot be audited against an absent authoritative artifact.
