# CANN Decode All-to-All Buffer Reuse Reproduction

This recipe is for Ascend users who already have a single-node multi-card
Ascend/CANN environment. TraceLoom does not provide drivers, firmware, CANN,
vLLM-Ascend, or a container image.

## Configure

```bash
cp reproduce/decode_a2a_buffer_reuse/env.example reproduce/decode_a2a_buffer_reuse/local.env
$EDITOR reproduce/decode_a2a_buffer_reuse/local.env
```

At minimum, set:

- `TRACELOOM_DEVICES`: comma-separated device set, for example `3,4,5,6`.
- `TRACELOOM_MODEL_PATH`: local model path.
- `TRACELOOM_VLLM_ASCEND_DIR`: vLLM-Ascend checkout used for the Decode All-to-All Buffer Reuse A/B test.

If the workload dependencies live in an already-running container, set
`TRACELOOM_CONTAINER` and `TRACELOOM_CONTAINER_VLLM_ASCEND_DIR` as well. The
scripts will copy the workload and patch into `/tmp/traceloom_decode_a2a_buffer_reuse` in
that container, collect profiles there, then copy generated profiles back to
`out/reproduce/decode_a2a_buffer_reuse/` on the host.

Run commands from the `traceloom/` directory after activating the user's normal
Ascend Python environment, or from the host when `TRACELOOM_CONTAINER` is set.

## Reproduce From Checked Paper Bundle

This does not require Ascend hardware and emits the paper-facing table stored in
the thesis experiment bundle:

```bash
python3 reproduce/run_reference.py decode-a2a-buffer-reuse
```

To recompute the checked analysis bundle with the current TraceLoom taxonomy:

```bash
python3 reproduce/run_reference.py decode-a2a-buffer-reuse --mode bundle-recomputed
```

## Reproduce On A Local Ascend Host

Run macro A/B workload reports:

```bash
bash reproduce/decode_a2a_buffer_reuse/run_ab_benchmark.sh --env-file reproduce/decode_a2a_buffer_reuse/local.env
```

Collect one baseline profile and one Decode All-to-All Buffer Reuse profile, then analyze them:

```bash
bash reproduce/decode_a2a_buffer_reuse/run_profile_pair.sh --env-file reproduce/decode_a2a_buffer_reuse/local.env
```

Generated outputs are written under `out/reproduce/decode_a2a_buffer_reuse/`.

## Dry Run

Use `--dry-run` to print the commands without touching vLLM-Ascend or invoking
the Ascend profiler:

```bash
bash reproduce/decode_a2a_buffer_reuse/run_ab_benchmark.sh --dry-run
bash reproduce/decode_a2a_buffer_reuse/run_profile_pair.sh --dry-run
```
