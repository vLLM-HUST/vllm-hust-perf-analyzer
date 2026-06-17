# Reference Reproduction

TraceLoom's paper reproduction flow should be lightweight and environment
native. The repository provides workload and profiler recipes, not large
pre-collected profile databases.

## Requirements

- A single-node multi-card Ascend machine for the current Decode All-to-All Buffer Reuse experiment.
- The user's existing Ascend/CANN software stack.
- Python 3.10 or newer for TraceLoom.
- Workload dependencies installed by the user in their normal environment.

## Ascend/CANN Recipe

Reproduce the thesis Decode All-to-All Buffer Reuse evidence tables from the checked experiment
bundle:

```bash
python3 reproduce/run_reference.py decode-a2a-buffer-reuse
```

The default `bundle` mode emits the paper-facing table stored in the experiment
bundle. To recompute that bundle with the current TraceLoom taxonomy:

```bash
python3 reproduce/run_reference.py decode-a2a-buffer-reuse --mode bundle-recomputed
```

If local raw `msprof` DBs are available under `../analyzer/out`, rerun TraceLoom
on the raw profiles:

```bash
python3 reproduce/run_reference.py decode-a2a-buffer-reuse --mode raw-analysis
```

For an external Ascend host, fill in the device set and workload paths:

```bash
cp reproduce/decode_a2a_buffer_reuse/env.example reproduce/decode_a2a_buffer_reuse/local.env
$EDITOR reproduce/decode_a2a_buffer_reuse/local.env
bash reproduce/decode_a2a_buffer_reuse/run_ab_benchmark.sh --env-file reproduce/decode_a2a_buffer_reuse/local.env
```

When the workload runs inside a container, point `TRACELOOM_CONTAINER` at an
already-running Huawei/Ascend container. The benchmark helper uses Docker only
to run the workload and copy reports; TraceLoom itself still analyzes existing
profile artifacts.

For lower-level debugging, collect macro A/B reports separately:

```bash
bash reproduce/decode_a2a_buffer_reuse/run_ab_benchmark.sh --env-file reproduce/decode_a2a_buffer_reuse/local.env
```

Analyze an existing profile:

```bash
python3 reproduce/run_reference.py analyze-msprof \
  /path/to/run-or-msprof-raw-dir \
  --name reviewer_msprof
```

Outputs are written under `out/reproduce/<name>/analysis/`.

## Reproduction Contract

The goal is structural and metric reproduction, not byte-identical profile
files. Acceptable differences include hardware SKU, driver/runtime version,
clocking, placement, and workload scale, provided the same loop structure and
reported bottleneck categories are recovered.

TraceLoom does not ship an Ascend hardware/software stack. The reproduction
contract assumes the reviewer already has a working Ascend/CANN multi-card host
and only needs to provide the device set, model path, and vLLM-Ascend checkout.
