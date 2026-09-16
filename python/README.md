# TraceLoom for Python

**One installation for native timeline analysis, a Python interface, and the
optional vLLM runtime observer.** The wheel includes the production C++17
analyzer, default rule manifests, and model/step YAML configurations. It is not
just a launcher for a separately installed executable.

```sh
python -m pip install traceloom==0.1.0
traceloom /path/to/profile.db --output analysis.db
```

Python >=3.10. Linux wheels are platform-specific; source builds require C++17,
CMake, SQLite and libyaml development libraries. Gzip Perfetto output additionally
uses the system `gzip` executable. No torch, vLLM, or accelerator is needed for
offline analysis. The small `traceloom-vllm-context` dependency supplies runtime
observation; it does not install vLLM or change your serving environment.

## Python analysis interface

```python
from pathlib import Path
import traceloom

# One profiler SQLite file. CLI directory discovery remains available separately.
result = traceloom.analyze(
    "profile.db",
    output="analysis.db",
    threads=4,
    timeout=300,
)
rows = result.query(
    "SELECT * FROM traceloom_v_position_occurrence WHERE position_id = ?",
    ("node-N003",),
)
result.export_perfetto("timeline.perfetto.json.gz")

# Reopen without re-running analysis.
existing = traceloom.AnalysisDatabase(Path("analysis.db"))
```

`query()` uses a read-only SQLite connection, parameter binding and a default
1,000-row limit. Larger results raise rather than silently truncate: narrow the
query or set `limit=` explicitly. This is a local analysis API, not a service
for arbitrary untrusted SQL. Native errors raise `traceloom.AnalysisError` with
`returncode`, `command` and a bounded `diagnostic_tail`. Missing inputs, invalid
arguments and `subprocess.TimeoutExpired` remain their ordinary Python types.
The native process is isolated from the interpreter; structural recovery and
source protection use exactly the same implementation as the CLI.

## Runtime collection, then offline analysis

In an existing compatible vLLM environment:

```sh
export TRACELOOM_CONTEXT_DIR=./traceloom-context
export TRACELOOM_RUN_ID=inference-study-001
vllm serve /path/to/model --async-scheduling \
  --scheduler-cls traceloom.vllm.TracingAsyncScheduler
```

This patch-free entry records scheduler metadata only. It does **not** associate
device execution. For that separate mode use `traceloom.vllm.LinkedTracingAsyncScheduler`
plus the explicit `transport-worker.patch` from the
[integration guide](https://github.com/vLLM-HUST/vllm-hust-perf-analyzer/blob/main/integrations/vllm/README.md).
The original `traceloom_vllm_scheduler.*` class paths remain supported.
Synchronous scheduling uses the corresponding `TracingScheduler` or
`LinkedTracingScheduler` and omits `--async-scheduling`.

After the ordinary profiler capture and context producers have closed:

```python
from pathlib import Path
import traceloom

result = traceloom.analyze(
    "profile.db",
    output="step-analysis.db",
    context=sorted(Path("traceloom-context").glob("*.jsonl")),
    rules_config=traceloom.bundled_rules("scheduler-step"),
)
result.export_perfetto("step-timeline.perfetto.json.gz")
```

Step-constrained recovery requires supported **execution-linked** evidence,
not scheduler-only metadata. It currently supports eager grammar recovery, not
protected replay or simultaneous marked-structure projection. Independently,
`bundled_rules("deepseekv4")` provides model-unit hints; it is not automatic
semantic inference for every model. Missing and ambiguous links remain explicit.

The observer is experimental. Live collection validation is bounded to
Qwen3-0.6B TP1/eager, in-process V1, vLLM `0fc695fc` and Ascend `f4a08bdd`.
Different versions need source-contract checks. Graph/distributed collection,
universal membership completeness and recording overhead are not qualified.
Capturing metadata adds host work. The native analysis runs after capture, not
inside the scheduler hot path. Restart without the custom scheduler class to
remove the adapter; unset `TRACELOOM_CONTEXT_DIR` to disable recording.

[Native analyzer and full documentation](https://github.com/vLLM-HUST/vllm-hust-perf-analyzer)
· [MIT license](https://github.com/vLLM-HUST/vllm-hust-perf-analyzer/blob/main/LICENSE)
