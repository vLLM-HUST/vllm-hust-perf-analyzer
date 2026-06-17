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

If the host is only a Docker launcher, set `TRACELOOM_CONTAINER` and
`TRACELOOM_CONTAINER_VLLM_ASCEND_DIR`. In this mode `msprof`, `npu-smi`, CANN,
vLLM-Ascend, the model path, and the workload Python environment are all
resolved inside the already-running Huawei/Ascend container. The host only runs
`docker exec` and `docker cp`. The scripts copy the workload and patch into
`/tmp/traceloom_decode_a2a_buffer_reuse` in that container, collect profiles
there, then copy generated profiles back to
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

Run the one-command TraceLoom flow. It creates editable profile configs, runs
baseline and optimized `msprof` profiles, analyzes the two generated profile
directories, then emits the Decode All-to-All Buffer Reuse comparison summary:

```bash
bash run_decode_a2a_buffer_reuse.sh --env-file reproduce/decode_a2a_buffer_reuse/local.env
```

The generated configs are written to:

```text
out/reproduce/decode_a2a_buffer_reuse/profiles/baseline/traceloom.profile.ini
out/reproduce/decode_a2a_buffer_reuse/profiles/optimized/traceloom.profile.ini
```

For lower-level debugging, the stages can still be run separately. Run macro
A/B workload reports:

```bash
bash reproduce/decode_a2a_buffer_reuse/run_ab_benchmark.sh --env-file reproduce/decode_a2a_buffer_reuse/local.env
```

Collect one baseline profile and one Decode All-to-All Buffer Reuse profile, then analyze them:

```bash
bash reproduce/decode_a2a_buffer_reuse/run_profile_pair.sh --env-file reproduce/decode_a2a_buffer_reuse/local.env
```

The integrated script defaults to
`TRACELOOM_NARROW_WORKER_VISIBLE_DEVICES=1` and
`TRACELOOM_WORKER_MULTIPROC_METHOD=spawn` so vLLM worker probing is isolated
before `msprof` attaches. TraceLoom analysis also defaults to all discovered
devices; set `TRACELOOM_ANALYSIS_DEVICES=3,4,5,6` to pin a physical device set.

Generated outputs are written under `out/reproduce/decode_a2a_buffer_reuse/`.

## Dry Run

Use `--dry-run` to print the commands without touching vLLM-Ascend or invoking
the Ascend profiler:

```bash
bash run_decode_a2a_buffer_reuse.sh --dry-run --env-file reproduce/decode_a2a_buffer_reuse/local.env
bash reproduce/decode_a2a_buffer_reuse/run_ab_benchmark.sh --dry-run
bash reproduce/decode_a2a_buffer_reuse/run_profile_pair.sh --dry-run
```
