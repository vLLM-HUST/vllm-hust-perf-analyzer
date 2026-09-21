# Fusion-final: four-capture boundary and display acceptance

Use alongside `qwen-serving-landmarks.md` when validating the Qwen overlay on
fusion-final or deciding whether a clean Perfetto requires reanalysis.

## Observed input and result

On 2026-09-22, main `9d578a1` plus the upper-device display patch was tested on
all four SQLite profile layouts under the hw3 container path
`/workspace/my-ascend-workspace/runs/qwen27-partition-serving/mtp-gdn-fusion-final-20260921/native-graph/`:
`decode/rank0`, `decode/rank1`, `mixed/rank0`, `mixed/rank1`.
The user-SSH path has prefix `/home/jingyuan/workspace/`, not `/workspace/`.
All 82 SQLite files were copied, preserving the profile layout; non-SQLite
profile files were not copied. Each analysis reported
`input_evidence_state=profile_directory_complete`.

Run the production CLI with `--threads 4 --rules-config
configs/qwen35-serving.yaml`, separate `--output` and `--perfetto-out` per input.
Each input yielded six `_compute_slot_mapping_kernel` starts, six monotonic
contiguous `Equal,MaskedFill,ClipByValueV2` tails, and five completely bracketed,
adjacent anchor partitions labeled `serving_cycle_candidate`. Every capture had
396 layer, 396 attention, 396 MLP and 792 residual_norm display windows, and
12 graph launches. Decode had 3 replay units and mixed had 12: do not confuse
replay-unit count with concrete launch count.

The mixed-rank0 third member envelope extended 61,880.937 us beyond its closing
landmark. Other checked envelopes had no positive overhang. This is not license
to shift the landmark, clip evidence, or call the envelope a scheduler duration.
No exact scheduler markers were available in these captures. These are model
landmark candidates, not scheduler step IDs. Exact attribution still requires
runtime context with exact marker/launch membership; `anchor.step_idx` is not a
scheduler step identifier.

## Independent display acceptance

Compare prepatch and patched exports from the same analysis/config revision,
not an older delivered exporter with different structural projection behavior.
For each of the four captures, the entire `raw_provider` event arrays and
`structure` arrays were exactly equal. Surviving `device_events` were equal
with only the display name removed from comparison: timestamps, durations,
IDs and provenance were preserved. Upper noise disappeared, MatMulV2 labels
became `MatMul`, fused AllReduce semantics remained distinct, and raw process
sort indices stayed below upper tracks.

Mixed-rank1 changed from 20,538 to 10,746 upper events; all 38,104 raw events
and 1,986 structure events remained. It contains 1,646 canonical MatMul events.
The older delivered original analysis DB was also re-exported: its raw array
remained exactly equal to the original delivered file. Re-exporting that older
DB is not equivalent to reanalysis with current landmark/reconciliation rules.

Reproduction evidence is outside Git at the task-local
`/Users/fletcher-tian/Documents/Codex/2026-09-22/bao-b-2/work/`:
`baseline-four/`, `fixed-four/`, `verify_steps.py`, `verify_timeline.py`.
Raw logs and captures are not repository assets. For future verification,
compare planes separately and parse the final ` · `-separated component of
structure names (their prefixes are replay-domain qualified).
