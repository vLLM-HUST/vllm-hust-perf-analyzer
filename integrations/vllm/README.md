# vLLM scheduling context exporter

An optional Python package with patch-free and execution-linked collection modes. The default
recommended route is scheduler-class injection: no source patch, worker change,
global monkey patch or auto-loaded plugin is required. The native TraceLoom
analyzer remains a separate offline tool.

The runtime observer is distributed as
[`traceloom-vllm-context`](https://pypi.org/project/traceloom-vllm-context/).
For source development, use `python -m pip install /path/to/traceloom/integrations/vllm`
instead. The package does not include the native analyzer or the optional vLLM
source patch; those remain in this repository.

## Scheduler plugin: enable through launch arguments

Install into the environment that already runs the intended vLLM version. The
package deliberately does not install vLLM, torch or a hardware-specific runtime:

```bash
python -m pip install traceloom-vllm-context==0.1.0
export TRACELOOM_CONTEXT_DIR=/explicit/capture/context
export TRACELOOM_RUN_ID=inference-study-001
vllm serve /path/to/model --async-scheduling \
  --scheduler-cls traceloom_vllm_scheduler.TracingAsyncScheduler
```

For an intentionally synchronous deployment, use `--no-async-scheduling` and
`--scheduler-cls traceloom_vllm_scheduler.TracingScheduler`. Preserve the existing
mode: do not disable async scheduling just to collect a trace. These classes
subclass the respective upstream V1 scheduler, forward arguments once, call its
complete `schedule()` implementation, then snapshot the result and return the
same object unchanged. Scheduling exceptions propagate; recording failures warn
without failing inference. With `TRACELOOM_CONTEXT_DIR` unset, no files or
metadata are produced. Collection adds host work; it is not zero overhead.

An existing `--worker-cls` (including an optimization worker) is not replaced.
All scheduler processes need the package and environment variables. The class
module imports the chosen environment's vLLM scheduler; the exporter itself has
no torch/vLLM import. Names are ordinary `--scheduler-cls` imports, not a
`vllm.general_plugins` entry point. The adapters do not compose with arbitrary
custom scheduler classes automatically.

This mode exports unique producer-scoped step records, request/token shape and
after-schedule cache counters. It does NOT modify or extend `SchedulerOutput`,
transport step IDs to workers, emit execution markers, or associate device work.
Session metadata says `association_contract: scheduler_only`. Import these files
with the same `--context` interface: recorded steps remain visible with
`recorded_executions=0`, not fabricated empty device work. Scheduler-local ordinal
is not a cross-DP global step coordinate. Missing pool counters remain null.

Do not combine this mode with the legacy EngineCore recording patch below.
A leftover EngineCore `record_step` hook is ignored for these injected classes to
avoid duplicate records; that does not activate legacy worker identity transport.
If another scheduler subclasses a tracing class and changes its output AFTER
`super().schedule()`, the snapshot would precede those changes: such composition
requires its own final-return seam rather than assuming this adapter covers it.

### Compatibility and CPU source audit

The inspected source boundary is vLLM
`752a3a504485790a2e8491cacbb35c137339ad34`. Validate a different runtime rather
than claiming an unrestricted version range: custom scheduler interfaces can
change, even though the CLI accepts a class path.

```bash
python check_scheduler_source_contract.py /explicit/vllm/source
```

This read-only CPU audit checks the CLI seam, executes the actual scheduler
class resolver, checks ancestry against the actual AsyncScheduler definition,
and records an actual unpatched SchedulerOutput without changing its serialized
state. Dependency-heavy scheduler operations are not executed. Unit tests also
exercise sync/async final-result recording, exception isolation, disabled mode,
duplicate-hook protection and scheduler-only JSONL → native AugDB import. The audit itself is not a live capture or overhead measurement. A subsequent
Qwen3-0.6B TP1/eager in-process Ascend capture validated scheduler-only collection
and native import: 34 decisions for two 16-token requests, including two empty
decisions, zero lost records, and unchanged profiler evidence. It did not
establish per-step device attribution or recording overhead.

## Execution-linked scheduler plugin

For step → host → device analysis select `LinkedTracingAsyncScheduler` (or
`LinkedTracingScheduler` for synchronous mode). This is **not patch-free**:
it requires the narrow `transport-worker.patch` against the selected vLLM
checkout. It does not patch EngineCore or replace an optimization worker.

The verified live source boundary is vLLM
`0fc695fc6d1d82e9a5ac6835ac8e4e1c83703665`, vLLM-Ascend
`f4a08bddd0cc65a0bd8c3d377b158ae5ca7527db`. Use a task-owned checkout,
not a shared running installation:

```bash
python check_source_contract.py /explicit/vllm/source --linked
git -C /explicit/vllm/source apply --check /path/to/traceloom/integrations/vllm/transport-worker.patch
git -C /explicit/vllm/source apply /path/to/traceloom/integrations/vllm/transport-worker.patch
# Keep normal profiler configuration and async mode.
vllm serve /path/to/model --async-scheduling \
  --scheduler-cls traceloom_vllm_scheduler.LinkedTracingAsyncScheduler
```

The optional dataclass field transports identity; the common worker wrapper
annotates `execute_model` and the following `sample_tokens` separately.
The audited runner has one outstanding execute/sample state. A second
unsampled execute refuses sampling attribution instead of guessing a FIFO.
Sampling identity is consumed once; exceptions propagate. This does not certify
arbitrary custom workers, pipeline parallelism or speculative decode modes.
Missing the required dataclass field fails linked-class construction explicitly.

Reuse vLLM's existing profiler: configure `profiler_config`, then invoke
`LLM.start_profile()` / `LLM.stop_profile()`. The Ascend wrapper captures
our `record_function` annotations without changes. On the pinned torch-npu
2.10.0.post2, the wrapper exports Text; its existing offline API can additionally
export the database consumed by TraceLoom:

```python
from torch_npu.profiler.profiler import analyse
from torch_npu.profiler import ExportType

analyse("/explicit/capture/profile", max_process_number=1, export_type=ExportType.Db)
```

Preserve the profiler's raw capture for offline export. Do not overwrite CANN's
sourced `PYTHONPATH` when adding the observer or task-owned vLLM checkout.
The observer never starts the profiler or synchronizes the device.

## Legacy execution-linked mode (explicit source patch)

The older optional patch covers EngineCore scheduling paths and the common
WorkerWrapperBase execution entry. Unlike scheduler-only injection, it adds a
transport field and worker profiler markers. It is retained for the separately
validated execution-association path, not required to install the scheduler plugin.

## Integrate in a chosen runtime checkout

Do not patch an already running/shared installation. In the runtime's normal
Python environment, install this directory (or make it available through that
runtime's explicit `PYTHONPATH`). Apply the checked patch to the chosen vLLM
source checkout **after** checking it against that exact revision:

```bash
# Use the intended runtime's Python, not an unrelated host interpreter.
python -m pip install /path/to/traceloom/integrations/vllm

git -C /path/to/vllm apply --check /path/to/traceloom/integrations/vllm/vllm-context.patch
git -C /path/to/vllm apply /path/to/traceloom/integrations/vllm/vllm-context.patch
```

No patch has been applied to an ambient vLLM installation by this feature's
implementation or CPU tests. Other vLLM revisions must be reviewed, not patched
with fuzzy matching or assumed compatible because their package name matches.

The seams are:
1. `SchedulerOutput.traceloom_step` is a real optional dataclass field.
2. `record_step(scheduler, output)` runs immediately after each final
   `schedule()` result and before executor dispatch.
3. `@trace_execution` wraps the common worker execution method and emits the
   transport-linked profiler annotation plus a separate execution record.

The patch requires the exporter module even while recording is disabled; a
missing module is a deployment error, not a reason to silently lose tracing.
Disabled recording creates no files, imports no torch, and leaves decisions
unchanged. It adds only the guarded hook/decorator calls and optional null field.

## Enable for a bounded capture

Set the same run ID and export directory on the scheduler and every worker
process. Distributed machines may use host-local directories, but must keep all
producer files for later import. Reusing a run ID is discouraged; restarting a
scheduler still creates a distinct producer and unique step IDs.

```bash
export TRACELOOM_CONTEXT_DIR=/explicit/capture/context
export TRACELOOM_RUN_ID=inference-study-001
# Start the normal vLLM workload and profiler through the existing launcher.
```

The profiler must capture `torch.profiler.record_function` annotations and the
supported host/provider relations. The exporter does not start or configure the
profiler, initialize a device, synchronize kernels, or acquire hardware.
Clean process shutdown drains the queue with a bounded wait; callers managing
lifecycle directly can invoke `traceloom_vllm_context.close_all()` after recording.
Do not interpret a host call's return as accelerator completion.

The importer consumes all desired producer files explicitly:

```bash
context_args=()
for file in /explicit/capture/context/*.jsonl; do
  context_args+=(--context "$file")
done
traceloom /explicit/capture/profile.db --output /explicit/capture/analysis.db \
  "${context_args[@]}"
```

No request text, token values or cache contents are exported. Request hashes
are producer-local pseudonyms, not anonymous cross-run tracking IDs. Config
collection is allowlisted, not `repr(vllm_config)` or unrestricted serialization.
See [`docs/runtime-context.md`](../../docs/runtime-context.md) for schema,
queries, null/unsupported states, limits, and the partial-membership boundary.

## CPU validation

```bash
python -m unittest discover -s integrations/vllm/tests -v
# Override TRACELOOM_TEST_BINARY when testing a different native build.
ctest --preset dev-tests -R scheduler_context_tests
```

The native CTest entry invokes exporter and AugDB tests without vLLM, torch or an
accelerator. Tests require Python >=3.10; production TraceLoom remains native.

For the pinned source audit, use a separate CPU environment with `msgspec`:

```bash
python check_source_contract.py /path/to/vllm
```

The check copies only three files to a temporary directory, applies the patch,
parses both engine call sites and the common worker hook, and transports the
actual patched SchedulerOutput class through pickle/msgpack. Unrelated payload
types are replaced by `Any`; this is not execution of the actual vLLM engine.
A real profiler capture and instrumentation-overhead check remain necessary
before claiming runtime-level end-to-end or performance validation.

### Initial acceptance (2026-09-15)

On TraceLoom base `926b7e0`, the implementation passed all 86 native CTest
entries, including 25 exporter/importer CPU tests; the Release build, Python
lint and repository-knowledge validation also passed. The pinned vLLM source
contract check passed against `752a3a504485790a2e8491cacbb35c137339ad34`,
including actual SchedulerOutput pickle/msgpack field transport. Installing the
exporter into an isolated CPU environment did not import torch. No shared vLLM
checkout was patched, no accelerator workload was launched, and live capture
coverage and recording overhead have not been measured.

### Optional in-process request receipt key

`traceloom_vllm_context.request_coordinate(scheduler, actual_request_id)` returns
`{run_id, scheduler_id, request_id}` for an already active scheduler recorder,
or `None` if unavailable/closed/disabled. An in-process workload observer can
retain this key beside its own submission/output receipts without exporting
request text, raw IDs or the producer salt. Pass the runtime's actual request
ID (which may differ from the caller's ID), not a guessed ordinal. This does
not associate a remote HTTP client automatically or establish lifecycle timing;
any external receipt's source, clock and semantics must remain explicit.
