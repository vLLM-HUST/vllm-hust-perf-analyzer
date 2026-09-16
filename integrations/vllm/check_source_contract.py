"""Check the explicit patch and actual SchedulerOutput transport, without vLLM imports.

Usage: python check_source_contract.py /path/to/vllm (requires msgspec).
Only three source files are copied into a temporary directory; original untouched.
"""

import ast
import dataclasses
import pickle
import shutil
import subprocess
import sys
import tempfile
import types
import typing
from pathlib import Path

import msgspec

root = Path(sys.argv[1]).resolve()
linked = "--linked" in sys.argv[2:]
patch = Path(__file__).with_name(
    "transport-worker.patch" if linked else "vllm-context.patch"
)
files = [
    "vllm/v1/core/sched/output.py",
    "vllm/v1/engine/core.py",
    "vllm/v1/worker/worker_base.py",
]
with tempfile.TemporaryDirectory(prefix="traceloom-vllm-source-") as directory:
    target = Path(directory)
    for file in files:
        out = target / file
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / file, out)
    subprocess.run(["git", "apply", str(patch)], cwd=target, check=True)
    trees = {f: ast.parse((target / f).read_text()) for f in files}
    cls = next(
        n
        for n in trees[files[0]].body
        if isinstance(n, ast.ClassDef) and n.name == "SchedulerOutput"
    )
    module = types.ModuleType("_traceloom_vllm_transport_fixture")
    sys.modules[module.__name__] = module
    module.__dict__.update(
        dataclass=dataclasses.dataclass,
        NewRequestData=typing.Any,
        CachedRequestData=typing.Any,
        KVConnectorMetadata=typing.Any,
        ECConnectorMetadata=typing.Any,
    )
    code = ast.Module(
        body=[
            ast.ImportFrom(
                module="__future__", names=[ast.alias(name="annotations")], level=0
            ),
            cls,
        ],
        type_ignores=[],
    )
    # Execute only the caller-selected local SchedulerOutput class, not vLLM imports.
    exec(compile(ast.fix_missing_locations(code), files[0], "exec"), module.__dict__)  # noqa: S102
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
        traceloom_step={"run_id": "source-audit", "step_id": "transport-check"},
    )
    for result in [
        pickle.loads(pickle.dumps(output)),
        msgspec.msgpack.decode(
            msgspec.msgpack.encode(output), type=module.SchedulerOutput
        ),
    ]:
        assert result.traceloom_step == output.traceloom_step
    core = trees[files[1]]
    calls = [
        n
        for n in ast.walk(core)
        if isinstance(n, ast.Call)
        and isinstance(n.func, ast.Name)
        and n.func.id == "record_step"
    ]
    assert len(calls) == (0 if linked else 2)
    worker = next(
        n
        for n in trees[files[2]].body
        if isinstance(n, ast.ClassDef) and n.name == "WorkerWrapperBase"
    )
    method = next(
        n
        for n in worker.body
        if isinstance(n, ast.FunctionDef) and n.name == "execute_model"
    )
    assert any(
        isinstance(n, ast.Name)
        and n.id == ("trace_worker_execute" if linked else "trace_execution")
        for n in method.decorator_list
    )
    if linked:
        sample = next(
            n
            for n in worker.body
            if isinstance(n, ast.FunctionDef) and n.name == "sample_tokens"
        )
        assert any(
            isinstance(n, ast.Name) and n.id == "trace_worker_sample"
            for n in sample.decorator_list
        )
    print(
        "PASS: patch, worker hooks, actual SchedulerOutput pickle/msgpack identity; linked="
        + str(linked)
    )
    print(
        "No vLLM/torch import or accelerator execution; unrelated payload types stubbed as Any."
    )
