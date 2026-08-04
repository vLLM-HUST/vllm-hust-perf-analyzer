# Idle Evidence Semantic Taxonomy: Kickstart Coverage Audit

Date: 2026-08-02
Ruleset: `idle-evidence-semantic-v1`
Input: `examples/kickstart_smoke/msprof_raw` (two-device vLLM-Ascend capture)
Tool: `traceloom-native-idle-evidence-audit`

## Purpose

PR-E1 review required evidence that the initial semantic taxonomy lets E2
build a productive timeline without obviously missing major model work.
This audit reports per-role counts and durations plus unknown-task detail,
on the checked-in real profile.

## Evidence status

The results below are **contract/example** evidence on a single checked-in
real capture: they demonstrate that the taxonomy and the productive timeline
pipeline run end-to-end on a real profile. They are NOT a matched A/B
measurement of runtime traces (contract section 11.3), and they do not make
any idle-attribution claim about that capture. The executable counterpart —
the checked-in golden fixture `host_wait_zero_visible_idle` with its CI check
`traceloom_native_idle_evidence_golden_fixture_tests` — enforces the
host-wait / visible-idle measurement boundary on synthetic fixtures
(`evidence_label = simulation/model`); it likewise does not replace matched
A/B of runtime traces.

## Results (after rule expansion)

### Device 1 (`PROF_..._AJJGNKPPJMEGGLFA`, 84,928 tasks)

| role | count | count % | duration_ns | duration % |
| --- | --- | --- | --- | --- |
| productive_compute | 18,554 | 21.8 | 198,182,105 | 3.79 |
| productive_comm | 2,239 | 2.6 | 2,352,513,098 | 45.05 |
| productive_data_move | 9,829 | 11.6 | 13,086,344 | 0.25 |
| visible_wait | 3,784 | 4.5 | 2,472,613,243 | 47.34 |
| capture_control | 38,870 | 45.8 | 90,926,453 | 1.74 |
| record | 5,569 | 6.6 | 99,441 | 0.00 |
| runtime_control | 4,731 | 5.6 | 84,085,931 | 1.61 |
| unknown | 1,352 | 1.6 | 11,077,125 | 0.21 |

### Device 2 (`PROF_..._OEJLKCHMOPRFGKIB`, tasks)

| role | count | count % | duration_ns | duration % |
| --- | --- | --- | --- | --- |
| productive_compute | 17,875 | 31.1 | 194,753,880 | 6.07 |
| productive_comm | 2,137 | 3.7 | 1,437,381,700 | 44.79 |
| productive_data_move | 9,214 | 16.0 | 12,408,600 | 0.39 |
| visible_wait | 3,728 | 6.5 | 1,503,716,700 | 46.86 |
| capture_control | 13,562 | 23.6 | 29,127,460 | 0.91 |
| record | 5,508 | 9.6 | 98,580 | 0.00 |
| runtime_control | 4,278 | 7.4 | 22,710,440 | 0.71 |
| unknown | 1,153 | 2.0 | 8,790,680 | 0.27 |

## Rule expansion driven by the audit

Initial ruleset left 18.1% of tasks (3.7% of duration) unknown on device 1.
The audit showed most unknown duration came from `KERNEL_AIVEC` container
tasks whose `op_type` carries real operator names, plus runtime control
task types. Added (all from trustworthy, specific evidence; no broad
catch-alls, no bare `AI_CORE` -> compute):

- `runtime_control` task types: `MODEL_EXECUTE`, `MODEL_MAINTAINCE`,
  `TASK_TIMEOUT_SET`, `PROFILING_ENABLE`; blob: `hccl_aiv_sync`
  (HCCL internal sync helper).
- `productive_data_move` blob: `tensormove`.
- `productive_compute` blobs: `zeroslike` (ZerosLike), `gather`, `slice`,
  `cast`, `fill`.

Remaining unknown (0.2-0.3% of duration) is deliberate: numeric-ID
`op_type` values, empty op metadata, and generic elementwise names
(`Mul`, `Sub`, `Tile`, `GreaterEqual`) whose substring rules would risk
false positives. E1 does not require zero unknown; the residual is small
and inspectable.

## Notable distribution facts

- `visible_wait` (EVENT_WAIT/NOTIFY_WAIT) covers ~47% of task duration on
  both devices: this capture is synchronization-dominated, which is exactly
  the M0 subject.
- `productive_comm` is ~45% of duration; `hcom_allReduce_` vector-side
  kernels are the dominant collective (already matched by
  `comm.allreduce`).
- `productive_compute` is only 3.8-6% of duration, consistent with
  compute/communication overlap in collective-heavy vLLM serving.

## E3 real-profile validation

The E3 stream-state pipeline was also run end-to-end on Device 1 after the
stream-universe and source-lineage review fixes:

| field | result |
| --- | --- |
| `run_status` | `ok` |
| `stream_universe_size` | 106 |
| `observed_universe_scan_complete` | `true` |
| `E3_elapsed_ms` | 83 |
| `peak_rss_kb` | 72,900 kB |
| `0xFFFFFFFF COMMUNICATION_OP` rows | 0 |
| `COMMUNICATION_OP` rows | 1,773 |
| TASK rows | 84,928 |

Per-device result:

| device | status | span_start_ns | span_end_ns | timelines | universe_size | scan_complete | diagnostics |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | `ok` | 1780987610113873577 | 1780987677776791382 | 106 | 106 | `true` | 2,070 |

The E3 elapsed time and peak RSS are one-run environment observations, not
portable performance guarantees.

The input contains 2,070 legitimate zero-duration point markers
(`MODEL_MAINTAINCE`, `EVENT_RECORD`, `EVENT_WAIT`, `NOTIFY_RECORD`, and
`PROFILING_ENABLE`). E3 diagnoses these as
`zero_duration_point_event_ignored`: they cannot produce rows in the
interval-bearing stream-state table, do not establish interval-universe
membership, and do not invalidate the scan. Negative-duration rows and
zero-duration productive/unknown rows remain `invalid_event_duration` and
void scan completeness.

## E4 device-only explanation validation

The first official idle-explanation projector now slices every E2 visible
productive gap against the E3 stream-state partitions. It implements the
frozen device-side priority:

```text
blocked_by_visible_wait
  > capture_control_present
  > runtime_control_present
  > no_observed_device_work
  > unattributed_visible_idle
```

The real kickstart captures carry no external collection-completeness
attestation, so the audit deliberately reports `collection_status = unknown`.
Consequently, an all-observed-streams-empty slice remains
`unattributed_visible_idle`; the analyzer does not infer capture completeness
from trace contents. Direct categories preserve exact task/stream lineage and
report device-event coverage, not causality.

| profile/device | visible wait | capture/control | runtime control | unattributed | E4 wall time |
| --- | ---: | ---: | ---: | ---: | ---: |
| `AJJGNKPPJMEGGLFA` / 0 | 95,272,254 ns (0.146354%) | 1,795,652 ns (0.002758%) | 2,990,622 ns (0.004594%) | 64,997,233,915 ns (99.846294%) | 640 ms |
| `OEJLKCHMOPRFGKIB` / 1 | 27,126,000 ns (0.042553%) | 1,722,520 ns (0.002702%) | 2,110,620 ns (0.003311%) | 63,715,019,520 ns (99.951434%) | 421 ms |

These percentages partition visible productive gaps, not total task duration.
In particular, much of the profiler-visible wait-task duration overlaps
productive work on other streams and therefore does not explain a global
productive gap. The two E4 timings are single-run environment observations,
not portable performance guarantees. An indexed interval lookup reduced E4
from 18,009 ms to 640 ms on the larger profile without changing any category,
boundary, duration, or lineage result.

The production Loop Tree attribution pass conservatively maps only exact
intersections with disjoint anchor-prelude windows. On
`AJJGNKPPJMEGGLFA`/device 0 it attributes 65,097,292,403 of
65,097,292,443 visible-gap nanoseconds; the remaining 40 ns stays explicitly
device-only. Node hotspot rows aggregate through the existing anchor coverage
graph and are hierarchical, so parent and child values are not additive.

## Re-run

```bash
traceloom-native-idle-evidence-audit \
  --source-db examples/kickstart_smoke/msprof_raw/PROF_*/msprof_*.db
```
