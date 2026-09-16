"""CPU contract tests: fake scheduler bases, real adapter/exporter/importer."""

import importlib.util
import json
import os
import sqlite3
import sys
import tempfile
import types
import unittest
from contextlib import contextmanager
from dataclasses import asdict
from pathlib import Path
from unittest.mock import patch

from test_context import Output, context, scheduler


@contextmanager
def adapter_module():
    class Scheduler:
        def __init__(self, output):
            self.__dict__.update(scheduler().__dict__)
            self.output = output
            self.calls = []

        def schedule(self, *args, **kwargs):
            self.calls.append((args, kwargs))
            return self.output

    class AsyncScheduler(Scheduler):
        def schedule(self, *args, **kwargs):
            output = super().schedule(*args, **kwargs)
            # Simulate work in the async implementation after the base returns.
            output.total_num_scheduled_tokens = 7
            output.num_scheduled_tokens = {"private-request": 7}
            return output

    modules = {}
    for name, cls in [("scheduler", Scheduler), ("async_scheduler", AsyncScheduler)]:
        name = "vllm.v1.core.sched." + name
        modules[name] = types.ModuleType(name)
        setattr(modules[name], cls.__name__, cls)
    modules["vllm.v1.core.sched.output"] = types.ModuleType("vllm.v1.core.sched.output")
    modules["vllm.v1.core.sched.output"].SchedulerOutput = Output
    spec = importlib.util.spec_from_file_location(
        "traceloom_vllm_scheduler",
        Path(__file__).resolve().parents[1] / "traceloom_vllm_scheduler.py",
    )
    module = importlib.util.module_from_spec(spec)
    with patch.dict(sys.modules, modules):
        spec.loader.exec_module(module)
        yield module, Scheduler, AsyncScheduler


def plain_output():
    # No trace transport field, matching an unpatched SchedulerOutput contract.
    values = asdict(Output())
    values.pop("traceloom_step")
    values["scheduled_new_reqs"] = Output().scheduled_new_reqs
    return types.SimpleNamespace(**values)


class SchedulerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.env = patch.dict(
            os.environ,
            {
                "TRACELOOM_CONTEXT_DIR": self.temp.name,
                "TRACELOOM_RUN_ID": "scheduler-injection",
            },
        )
        self.env.start()
        context._WARNED = False

    def tearDown(self):
        context.close_all()
        self.env.stop()
        self.temp.cleanup()

    def records(self):
        context.close_all()
        return [
            json.loads(line)
            for p in Path(self.temp.name).glob("*.jsonl")
            for line in p.read_text().splitlines()
        ]

    def test_sync_unchanged_output_and_argument_forwarding(self):
        with adapter_module() as (module, base, _):
            output = plain_output()
            before = dict(vars(output))
            instance = module.TracingScheduler(output)
            self.assertIsInstance(instance, base)
            self.assertIs(instance.schedule(True, throttle=False), output)
            self.assertEqual(vars(output), before)
            self.assertEqual(instance.calls, [((True,), {"throttle": False})])
            # A legacy EngineCore hook must not double-record an injected step.
            context.record_step(instance, output)
        records = self.records()
        steps = [r for r in records if r["type"] == "step"]
        self.assertEqual(len(steps), 1)
        self.assertEqual(steps[0]["total_scheduled_tokens"], 4)
        self.assertEqual(
            records[0]["metadata"]["association_contract"], "scheduler_only"
        )
        self.assertFalse(any(r["type"] == "execution" for r in records))

    def test_async_records_final_result_once(self):
        with adapter_module() as (module, _, base):
            output = plain_output()
            instance = module.TracingAsyncScheduler(output)
            self.assertIsInstance(instance, base)
            self.assertIs(instance.schedule(), output)
            self.assertEqual(len(instance.calls), 1)
        steps = [r for r in self.records() if r["type"] == "step"]
        self.assertEqual(len(steps), 1)
        self.assertEqual(steps[0]["total_scheduled_tokens"], 7)

    def test_disabled_no_output_mutation_or_files(self):
        with (
            adapter_module() as (module, _, _),
            patch.dict(os.environ, {"TRACELOOM_CONTEXT_DIR": ""}),
        ):
            output = plain_output()
            self.assertIs(module.TracingScheduler(output).schedule(), output)
            self.assertFalse(hasattr(output, "traceloom_step"))
        self.assertEqual(self.records(), [])

    def test_business_exception_is_not_recorded_or_swallowed(self):
        error = ValueError("scheduler error")
        with (
            adapter_module() as (module, base, _),
            patch.object(base, "schedule", side_effect=error),
        ):
            with self.assertRaises(ValueError) as caught:
                module.TracingScheduler(plain_output()).schedule()
            self.assertIs(caught.exception, error)
        self.assertEqual(self.records(), [])

    def test_observer_failure_preserves_result(self):
        with (
            adapter_module() as (module, _, _),
            patch.object(context, "_scheduler_writer", side_effect=OSError),
        ):
            output = plain_output()
            with self.assertWarns(RuntimeWarning):
                self.assertIs(module.TracingScheduler(output).schedule(), output)
            self.assertFalse(hasattr(output, "traceloom_step"))

    def test_empty_step_imports_without_inventing_execution(self):
        from test_augdb import AugDbTests

        fixture = AugDbTests()
        fixture.setUp()
        try:
            with adapter_module() as (module, _, _):
                output = plain_output()
                output.num_scheduled_tokens = {}
                output.total_num_scheduled_tokens = 0
                output.scheduled_new_reqs = []
                instance = module.TracingScheduler(plain_output())
                instance.schedule()
                instance.output = output
                instance.schedule()
            context.close_all()
            # Import ONLY the scheduler-only producer, not the fixture worker.
            inputs = [
                p
                for p in (fixture.root / "context").glob("*.jsonl")
                if json.loads(p.read_text().splitlines()[0])["metadata"].get(
                    "association_contract"
                )
                == "scheduler_only"
            ]
            self.assertEqual(len(inputs), 1)
            fixture.run_cli(inputs=inputs)
            with sqlite3.connect(fixture.out) as db:
                self.assertEqual(
                    db.execute(
                        "SELECT total_scheduled_tokens,recorded_executions FROM traceloom_v_scheduler_step_context ORDER BY ordinal"
                    ).fetchall(),
                    [(4, 0), (0, 0)],
                )
                self.assertEqual(
                    db.execute(
                        "SELECT COUNT(*) FROM traceloom_v_context_device_work"
                    ).fetchone(),
                    (0,),
                )
        finally:
            fixture.tearDown()

    def test_linked_async_transports_final_identity(self):
        with adapter_module() as (module, _, base):
            output = Output()
            instance = module.LinkedTracingAsyncScheduler(output)
            self.assertIsInstance(instance, base)
            self.assertIs(instance.schedule(), output)
            self.assertIsNotNone(output.traceloom_step)
            context.record_step(instance, output)  # legacy hook cannot duplicate
        records = self.records()
        self.assertEqual(
            records[0]["metadata"]["association_contract"], "scheduler_output_field"
        )
        self.assertEqual(len([r for r in records if r["type"] == "step"]), 1)

    def test_linked_requires_explicit_transport_field(self):
        with (
            adapter_module() as (module, _, _),
            patch.object(
                sys.modules["vllm.v1.core.sched.output"],
                "SchedulerOutput",
                types.SimpleNamespace,
            ),
            self.assertRaisesRegex(RuntimeError, "transport-worker.patch"),
        ):
            module.LinkedTracingScheduler(plain_output())
