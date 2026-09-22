"""Audit scheduler injection against caller-selected vLLM source, CPU-only.

Usage: python check_scheduler_source_contract.py /path/to/vllm
Executes the actual class resolver and SchedulerOutput definition, but no vLLM
imports or scheduler algorithm. No source edits or accelerator initialization.
"""

import ast
import dataclasses
import importlib
import pickle
import sys
import tempfile
import types
from pathlib import Path
from unittest.mock import patch

import traceloom_vllm_context as context

root = Path(sys.argv[1]).resolve()
config = ast.parse((root / "vllm/config/scheduler.py").read_text())
args = ast.parse((root / "vllm/engine/arg_utils.py").read_text())
assert any(
    isinstance(n, ast.Constant) and n.value == "--scheduler-cls" for n in ast.walk(args)
), "missing CLI scheduler-class entry"
cls = next(
    n
    for n in config.body
    if isinstance(n, ast.ClassDef) and n.name == "SchedulerConfig"
)
resolve = next(
    n
    for n in cls.body
    if isinstance(n, ast.FunctionDef) and n.name == "get_scheduler_cls"
)
output = ast.parse((root / "vllm/v1/core/sched/output.py").read_text())
output_cls = next(
    n
    for n in output.body
    if isinstance(n, ast.ClassDef) and n.name == "SchedulerOutput"
)
# Planned-phase classification consumes worker-input offsets, not the mutable
# scheduler Request counter. Refuse a source boundary that lacks these fields.
for name, required in {
    "NewRequestData": {"req_id", "num_computed_tokens"},
    "CachedRequestData": {"req_ids", "num_computed_tokens"},
}.items():
    cls = next(n for n in output.body if isinstance(n, ast.ClassDef) and n.name == name)
    fields = {n.target.id for n in cls.body
              if isinstance(n, ast.AnnAssign) and isinstance(n.target, ast.Name)}
    assert required <= fields, f"missing worker-input offset contract: {name}"

async_source = ast.parse((root / "vllm/v1/core/sched/async_scheduler.py").read_text())
async_cls = next(
    n
    for n in async_source.body
    if isinstance(n, ast.ClassDef) and n.name == "AsyncScheduler"
)


def execute(nodes, namespace, filename):
    module = ast.Module(
        body=[
            ast.ImportFrom(
                module="__future__", names=[ast.alias(name="annotations")], level=0
            ),
            *nodes,
        ],
        type_ignores=[],
    )
    exec(compile(ast.fix_missing_locations(module), filename, "exec"), namespace)  # noqa: S102


def resolve_name(name):
    module, _, attr = name.rpartition(".")
    return getattr(importlib.import_module(module), attr)


class BaseScheduler:
    pass


base = types.ModuleType("vllm.v1.core.sched.scheduler")
base.Scheduler = BaseScheduler
async_module = types.ModuleType("vllm.v1.core.sched.async_scheduler")
async_module.Scheduler = BaseScheduler
execute([async_cls], vars(async_module), "async_scheduler.py")
namespace = {
    "resolve_obj_by_qualname": resolve_name,
    "logger": types.SimpleNamespace(warning_once=lambda *args: None),
}
execute([resolve], namespace, "scheduler.py")
with patch.dict(
    sys.modules, {base.__name__: base, async_module.__name__: async_module}
):
    adapter = importlib.import_module("traceloom_vllm_scheduler")
    for name, expected_base in [
        ("TracingScheduler", BaseScheduler),
        ("TracingAsyncScheduler", async_module.AsyncScheduler),
    ]:
        chosen = namespace["get_scheduler_cls"](
            types.SimpleNamespace(scheduler_cls="traceloom_vllm_scheduler." + name)
        )
        assert chosen is getattr(adapter, name) and issubclass(chosen, expected_base)

module = types.ModuleType("_traceloom_scheduler_output_audit")
sys.modules[module.__name__] = module
module.dataclass = dataclasses.dataclass
execute([output_cls], vars(module), "output.py")
output = module.SchedulerOutput(
    scheduled_new_reqs=[],
    scheduled_cached_reqs={},
    num_scheduled_tokens={},
    total_num_scheduled_tokens=0,
    scheduled_spec_decode_tokens={},
    scheduled_encoder_inputs={},
    num_common_prefix_blocks=[],
    finished_req_ids=set(),
    free_encoder_mm_hashes=[],
)
before = pickle.dumps(output)
scheduler = types.SimpleNamespace(_traceloom_scheduler_injection=True, requests={})
with (
    tempfile.TemporaryDirectory() as directory,
    patch.dict(
        "os.environ",
        {"TRACELOOM_CONTEXT_DIR": directory, "TRACELOOM_RUN_ID": "source-audit"},
    ),
):
    assert (
        context.record_step(scheduler, output, transport_identity=False, _injected=True)
        is output
    )
    context.close_all()
    assert pickle.dumps(output) == before
    assert len(list(Path(directory).glob("*.jsonl"))) == 1
print(
    "PASS: CLI seam, actual class resolution, actual AsyncScheduler ancestry, unchanged SchedulerOutput"
)
print(
    "Scheduler algorithms and vLLM imports are not executed; no live capture or overhead claim."
)
