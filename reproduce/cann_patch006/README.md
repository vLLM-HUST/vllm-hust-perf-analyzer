# CANN Patch006 Reproduction

This recipe is for Ascend users who already have a single-node multi-card
Ascend/CANN environment. TraceLoom does not provide drivers, firmware, CANN,
vLLM-Ascend, or a container image.

## Configure

```bash
cp reproduce/cann_patch006/env.example reproduce/cann_patch006/local.env
$EDITOR reproduce/cann_patch006/local.env
```

At minimum, set:

- `TRACELOOM_DEVICES`: comma-separated device set, for example `3,4,5,6`.
- `TRACELOOM_MODEL_PATH`: local model path.
- `TRACELOOM_VLLM_ASCEND_DIR`: vLLM-Ascend checkout used for the Patch006 A/B test.

Run commands from the `traceloom/` directory after activating the user's normal
Ascend Python environment.

## Reproduce From Checked Paper Bundle

This does not require Ascend hardware and emits the paper-facing table stored in
the thesis experiment bundle:

```bash
python3 reproduce/run_reference.py paper-patch006
```

To recompute the checked analysis bundle with the current TraceLoom taxonomy:

```bash
python3 reproduce/run_reference.py paper-patch006 --mode bundle-recomputed
```

## Reproduce On A Local Ascend Host

Run macro A/B workload reports:

```bash
bash reproduce/cann_patch006/run_ab_benchmark.sh --env-file reproduce/cann_patch006/local.env
```

Collect one baseline profile and one Patch006 profile, then analyze them:

```bash
bash reproduce/cann_patch006/run_profile_pair.sh --env-file reproduce/cann_patch006/local.env
```

Generated outputs are written under `out/reproduce/cann_patch006/`.

## Dry Run

Use `--dry-run` to print the commands without touching vLLM-Ascend or invoking
the Ascend profiler:

```bash
bash reproduce/cann_patch006/run_ab_benchmark.sh --dry-run
bash reproduce/cann_patch006/run_profile_pair.sh --dry-run
```
