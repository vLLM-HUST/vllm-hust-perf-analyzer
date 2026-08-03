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

## Re-run

```bash
traceloom-native-idle-evidence-audit \
  --source-db examples/kickstart_smoke/msprof_raw/PROF_*/msprof_*.db
```
