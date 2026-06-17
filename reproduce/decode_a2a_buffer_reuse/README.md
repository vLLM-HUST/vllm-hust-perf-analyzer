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
`TRACELOOM_CONTAINER_VLLM_ASCEND_DIR`. In this mode `npu-smi`, CANN,
vLLM-Ascend, the model path, and the workload Python environment are all
resolved inside the already-running Huawei/Ascend container. The host only runs
`docker exec` and `docker cp`. The scripts copy the workload and patch into
`/tmp/traceloom_decode_a2a_buffer_reuse` in that container, then copy generated
reports back to `out/reproduce/decode_a2a_buffer_reuse/` on the host.

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

To compare local profiles, collect raw `msprof` output outside TraceLoom, then
run the analyzer on the existing profile directories:

```bash
python3 reproduce/run_reference.py analyze-msprof /path/to/msprof_raw --name local_decode_profile
```

TraceLoom analysis defaults to all discovered devices; set
`TRACELOOM_ANALYSIS_DEVICES=3,4,5,6` to pin a physical device set.

Generated outputs are written under `out/reproduce/decode_a2a_buffer_reuse/`.

## Dry Run

Use `--dry-run` to print the benchmark command without touching vLLM-Ascend:

```bash
bash reproduce/decode_a2a_buffer_reuse/run_ab_benchmark.sh --dry-run
```
