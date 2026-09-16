# TraceLoom vLLM runtime plugin

An opt-in scheduler observer for **TraceLoom**, the standalone accelerator
trace analyzer. This package records scheduling context; it does not install
or replace vLLM, torch, a device backend, or the native TraceLoom CLI.

## Install and enable

Use the Python environment that already runs your compatible vLLM installation:

```sh
python -m pip install traceloom-vllm-context==0.1.0
export TRACELOOM_CONTEXT_DIR=/path/to/capture/context
export TRACELOOM_RUN_ID=inference-study-001
vllm serve /path/to/model --async-scheduling \
  --scheduler-cls traceloom_vllm_scheduler.TracingAsyncScheduler
```

For synchronous scheduling use `TracingScheduler` and omit `--async-scheduling`.
Unset `TRACELOOM_CONTEXT_DIR` to disable recording. To remove the adapter entirely,
restart without the custom scheduler class. Existing worker-class selection is
left unchanged; arbitrary custom schedulers are not automatically composable.

## Two explicit collection modes

- **Scheduler-only:** launch-argument injection, no source patch. Records step
  identity, scheduled-token counts, request shapes and available cache counters.
  It does not associate steps with device execution.
- **Execution-linked:** `LinkedTracingAsyncScheduler` / `LinkedTracingScheduler`
  plus the repository's explicit `transport-worker.patch`. Carries step identity
  to workers and emits profiler markers for supported host/device correlation.
  This mode is not patch-free. Use the existing profiler to collect device data.

The native analyzer imports JSONL with repeated `--context` arguments alongside
the profiler database. Unknown or ambiguous associations remain explicit; a
scheduler-only record does not imply zero device work.

## Compatibility and limits

Real capture validation is bounded to Qwen3-0.6B, TP1, eager, an in-process V1
engine, vLLM `0fc695fc6d1d82e9a5ac6835ac8e4e1c83703665` and vLLM-Ascend
`f4a08bddd0cc65a0bd8c3d377b158ae5ca7527db`. Audit the selected runtime before
using a different revision. This is an alpha observer, not an inference
acceleration package or a universal compatibility claim. Recording adds host
work; overhead and graph/distributed capture coverage are not qualified.

Prompt text, generated token values and KV payloads are not intentionally
exported. Request IDs are producer-local pseudonyms. Treat captured metadata as
potentially sensitive and control access to the output directory.

[Full integration guide and patch](https://github.com/vLLM-HUST/vllm-hust-perf-analyzer/blob/main/integrations/vllm/README.md)
· [Native analyzer](https://github.com/vLLM-HUST/vllm-hust-perf-analyzer)
· [Context analysis contract](https://github.com/vLLM-HUST/vllm-hust-perf-analyzer/blob/main/docs/runtime-context.md)

MIT licensed. Built and maintained as part of TraceLoom.
