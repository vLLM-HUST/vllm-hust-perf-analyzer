import json
import os
import pickle
import queue
import sys
import tempfile
import unittest
from contextlib import contextmanager
from dataclasses import asdict, dataclass, field
from pathlib import Path
from types import SimpleNamespace as NS
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import traceloom_vllm_context as context


@dataclass
class Output:
    num_scheduled_tokens: dict = field(default_factory=lambda: {"private-request": 4})
    total_num_scheduled_tokens: int = 4
    scheduled_new_reqs: list = field(
        default_factory=lambda: [NS(req_id="private-request")]
    )
    finished_req_ids: set = field(default_factory=set)
    preempted_req_ids: object = None
    traceloom_step: dict | None = None


def scheduler():
    return NS(
        requests={"private-request": NS(num_computed_tokens=12, num_prompt_tokens=10)},
        scheduler_config=NS(max_num_batched_tokens=32, max_num_seqs=4),
        kv_cache_manager=NS(
            block_pool=NS(num_gpu_blocks=100, get_num_free_blocks=lambda: 60)
        ),
    )


class ContextTests(unittest.TestCase):
    def setUp(self):
        context._WARNED = False
        self.temp = tempfile.TemporaryDirectory()
        self.env = patch.dict(
            os.environ,
            {"TRACELOOM_CONTEXT_DIR": self.temp.name, "TRACELOOM_RUN_ID": "test-run"},
        )
        self.env.start()
        self.markers = []

    def tearDown(self):
        context.close_all()
        self.env.stop()
        self.temp.cleanup()

    @contextmanager
    def marker(self, name):
        self.markers.append(name)
        yield

    def records(self):
        context.close_all()
        return [
            json.loads(line)
            for f in Path(self.temp.name).glob("*.jsonl")
            for line in f.read_text().splitlines()
        ]

    def test_worker_input_token_offsets_not_optimistic_request_counts(self):
        output = Output()
        output.scheduled_new_reqs = [NS(req_id="private-request", num_computed_tokens=8)]
        context.record_step(scheduler(), output)
        step = next(r for r in self.records() if r["type"] == "step")
        request = step["requests"][0]
        self.assertEqual(request["computed_tokens_after_schedule"], 12)
        self.assertEqual(request["scheduled_token_start"], 8)
        self.assertEqual(request["scheduled_token_start_basis"],
                         "scheduler_output_num_computed_tokens")

    def test_cached_offsets_require_unique_aligned_payload(self):
        output = Output()
        output.scheduled_new_reqs = []
        output.scheduled_cached_reqs = NS(req_ids=["a", "b"], num_computed_tokens=[10, 20])
        self.assertEqual(context._scheduled_token_starts(output), {"a": 10, "b": 20})
        output.scheduled_cached_reqs.num_computed_tokens = [10]
        self.assertEqual(context._scheduled_token_starts(output), {"a": None, "b": None})
        output.scheduled_cached_reqs = NS(req_ids=["a", "a"], num_computed_tokens=[10, 10])
        self.assertEqual(context._scheduled_token_starts(output), {"a": None})
        output.scheduled_cached_reqs = NS(req_ids=["a"], num_computed_tokens=[True])
        self.assertEqual(context._scheduled_token_starts(output), {"a": None})

    def test_identity_transport_and_fanout(self):
        s = scheduler()
        output = Output()
        original = asdict(output)
        self.assertIs(context.record_step(s, output), output)
        transported = pickle.loads(pickle.dumps(output))
        self.assertEqual(transported.traceloom_step, output.traceloom_step)
        for rank in [0, 1]:
            with context.execution_scope(transported, rank, self.marker):
                pass
        records = self.records()
        step = next(r for r in records if r["type"] == "step")
        executions = [r for r in records if r["type"] == "execution"]
        self.assertEqual(len(executions), 2)
        self.assertEqual({r["step_id"] for r in executions}, {step["step_id"]})
        self.assertEqual(len({r["marker"] for r in executions}), 2)
        self.assertEqual(
            step["cache"],
            {
                "observation_phase": "after_schedule",
                "free_blocks": 60,
                "pool_blocks": 100,
            },
        )
        self.assertEqual(step["total_scheduled_tokens"], 4)
        self.assertEqual(step["requests"][0]["computed_tokens_after_schedule"], 12)
        self.assertIsNone(step["preempted_request_ids"])
        self.assertNotIn("private-request", json.dumps(records))
        result = asdict(output)
        result["traceloom_step"] = None
        self.assertEqual(result, original)
        for f in Path(self.temp.name).glob("*.jsonl"):
            self.assertEqual(f.stat().st_mode & 0o777, 0o600)
        self.assertTrue(
            all(r["dropped_records"] == 0 for r in records if r["type"] == "summary")
        )

    def test_disabled_no_files_or_metadata(self):
        with patch.dict(os.environ, {"TRACELOOM_CONTEXT_DIR": ""}):
            output = Output()
            context.record_step(scheduler(), output)
            with context.execution_scope(output, 0, self.marker):
                pass
            self.assertIsNone(output.traceloom_step)
        self.assertEqual(self.records(), [])
        self.assertEqual(self.markers, [])

    def test_unsupported_transport_does_not_break_inference(self):
        output = NS(**asdict(Output()))
        with self.assertWarns(RuntimeWarning):
            context.record_step(scheduler(), output)
        self.assertIsNone(output.traceloom_step)
        self.assertEqual(self.records(), [])

    def test_empty_step_and_missing_cache(self):
        s = scheduler()
        s.kv_cache_manager = None
        o = Output(
            num_scheduled_tokens={}, total_num_scheduled_tokens=0, scheduled_new_reqs=[]
        )
        context.record_step(s, o)
        step = next(r for r in self.records() if r["type"] == "step")
        self.assertEqual(step["requests"], [])
        self.assertIsNone(step["cache"]["free_blocks"])
        self.assertIsNone(step["cache"]["pool_blocks"])

    def test_business_exception_preserved(self):
        o = Output()
        context.record_step(scheduler(), o)
        with (
            self.assertRaisesRegex(ValueError, "original"),
            context.execution_scope(o, 0, self.marker),
        ):
            raise ValueError("original")
        e = next(r for r in self.records() if r["type"] == "execution")
        self.assertEqual(e["status"], "raised")

    def test_marker_failure_is_typed(self):
        o = Output()
        context.record_step(scheduler(), o)

        def broken(name):
            raise RuntimeError("marker broken")

        with self.assertWarns(RuntimeWarning), context.execution_scope(o, 0, broken):
            pass
        e = next(r for r in self.records() if r["type"] == "execution")
        self.assertEqual(e["marker_state"], "unavailable")
        self.assertEqual(e["status"], "returned")

    def test_queue_loss_is_counted(self):
        w = context.Writer(self.temp.name, "run", {}, capacity=1)
        with patch.object(w.queue, "put_nowait", side_effect=queue.Full):
            self.assertFalse(w.emit({"type": "step"}))
        w.close()
        summary = json.loads(w.path.read_text().splitlines()[-1])
        self.assertEqual(summary["dropped_records"], 1)
        self.assertEqual(summary["written_records"], 0)

    def test_scheduler_restarts_do_not_reuse_step_ids(self):
        a, b = Output(), Output()
        context.record_step(scheduler(), a)
        context.record_step(scheduler(), b)
        self.assertNotEqual(a.traceloom_step, b.traceloom_step)

    def test_malformed_identity_does_not_mask_business_exception(self):
        o = Output(traceloom_step={"run_id": "test-run"})
        with (
            self.assertWarns(RuntimeWarning),
            self.assertRaisesRegex(ValueError, "business"),
            context.execution_scope(o, 0, self.marker),
        ):
            raise ValueError("business")

    def test_invalid_step_is_counted_as_loss(self):
        s = scheduler()
        o = Output(num_scheduled_tokens={"private-request": -1})
        with self.assertWarns(RuntimeWarning):
            context.record_step(s, o)
        records = self.records()
        self.assertFalse(any(r["type"] == "step" for r in records))
        self.assertIsNone(o.traceloom_step)
        self.assertEqual(
            next(r for r in records if r["type"] == "summary")["dropped_records"], 1
        )


class WorkerBridgeTests(unittest.TestCase):
    setUp = ContextTests.setUp
    tearDown = ContextTests.tearDown
    records = ContextTests.records

    def worker(self, result=None):
        class Worker:
            rpc_rank = 0

            @context.trace_worker_execute
            def execute_model(self, output):
                return result

            @context.trace_worker_sample
            def sample_tokens(self, value):
                return value

        return Worker()

    def test_execute_sample_identity_and_phase(self):
        output = context.record_step(scheduler(), Output())
        worker = self.worker()
        worker.execute_model(output)
        self.assertEqual(worker.sample_tokens(42), 42)
        worker.sample_tokens(43)  # no stale identity on a second call
        records = [r for r in self.records() if r["type"] == "execution"]
        self.assertEqual(
            [r["phase"] for r in records], ["execute_model", "sample_tokens"]
        )
        self.assertEqual(
            {r["step_id"] for r in records}, {output.traceloom_step["step_id"]}
        )

    def test_returned_output_has_no_sample_identity(self):
        output = context.record_step(scheduler(), Output())
        worker = self.worker(result=42)
        self.assertEqual(worker.execute_model(output), 42)
        worker.sample_tokens(44)
        records = [r for r in self.records() if r["type"] == "execution"]
        self.assertEqual([r["phase"] for r in records], ["execute_model"])

    def test_overwritten_pending_state_refuses_sample_identity(self):
        output = context.record_step(scheduler(), Output())
        worker = self.worker()
        worker.execute_model(output)
        context._WARNED = False
        with self.assertWarns(RuntimeWarning):
            worker.execute_model(output)
        worker.sample_tokens(44)
        records = [r for r in self.records() if r["type"] == "execution"]
        self.assertEqual(
            [r["phase"] for r in records], ["execute_model", "execute_model"]
        )

    def test_business_failure_clears_pending_sample_and_propagates(self):
        output = context.record_step(scheduler(), Output())
        worker = self.worker()
        worker.execute_model(output)
        error = ValueError("business failure")

        @context.trace_worker_execute
        def fail(self, output):
            raise error

        with self.assertRaises(ValueError) as caught:
            fail(worker, output)
        self.assertIs(caught.exception, error)
        worker.sample_tokens(1)
        records = [r for r in self.records() if r["type"] == "execution"]
        self.assertEqual([r["status"] for r in records], ["returned", "raised"])
        self.assertTrue(all(r["phase"] == "execute_model" for r in records))

    def test_empty_execute_does_not_reuse_previous_sample_identity(self):
        output = context.record_step(scheduler(), Output())
        worker = self.worker()
        worker.execute_model(output)
        empty = Output(total_num_scheduled_tokens=0, num_scheduled_tokens={})
        context.record_step(scheduler(), empty)
        worker.execute_model(empty)
        worker.sample_tokens(1)
        records = [r for r in self.records() if r["type"] == "execution"]
        self.assertEqual(len(records), 2)
        self.assertTrue(all(r["phase"] == "execute_model" for r in records))


if __name__ == "__main__":
    unittest.main()
