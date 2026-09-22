"""Opt-in vLLM scheduler context. No torch import on the scheduler/disabled path."""

from __future__ import annotations

import atexit
import functools
import hashlib
import importlib.metadata
import json
import os
import queue
import re
import threading
import time
import uuid
import warnings
from contextlib import contextmanager, nullcontext
from pathlib import Path

SCHEMA = "traceloom.scheduler.v1"
_WARNED = False
_WRITERS = []
_WORKER_WRITERS = {}
_CONFIG_FIELDS = (
    "max_num_batched_tokens",
    "max_num_seqs",
    "policy",
    "enable_chunked_prefill",
    "async_scheduling",
)


def _enabled():
    return bool(os.environ.get("TRACELOOM_CONTEXT_DIR"))


def _warn():
    # Never include request data, paths, or arbitrary exception text.
    global _WARNED
    if _WARNED:
        return
    _WARNED = True
    try:
        warnings.warn(
            "TraceLoom context recording unavailable; inference continues",
            RuntimeWarning,
            stacklevel=2,
        )
    except Warning:
        # A caller's warnings-as-errors setting must not turn tracing into failure.
        pass


class Writer:
    """Bounded nonblocking producer; one exclusive 0600 JSONL file per producer."""

    def __init__(self, directory, run_id, metadata, capacity=256):
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,128}", run_id):
            raise ValueError("TRACELOOM_RUN_ID must be a nonempty safe run identifier")
        self.pid = os.getpid()
        self.run_id = run_id
        self.producer_id = uuid.uuid4().hex
        self.queue = queue.Queue(maxsize=capacity)
        self.stop = threading.Event()
        self.dropped = 0
        self.written = 0
        self.failed = False
        self.closed = False
        directory = Path(directory)
        directory.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.path = directory / (self.producer_id + ".jsonl")
        fd = os.open(self.path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        self.file = os.fdopen(fd, "w", encoding="utf-8")
        self.file.write(self._encode({"type": "session", "metadata": metadata}))
        self.file.flush()
        self.thread = threading.Thread(
            target=self._drain, daemon=True, name="traceloom-context-writer"
        )
        self.thread.start()
        _WRITERS.append(self)

    def _encode(self, record):
        line = (
            json.dumps(
                dict(
                    record,
                    schema=SCHEMA,
                    run_id=self.run_id,
                    producer_id=self.producer_id,
                ),
                ensure_ascii=True,
                separators=(",", ":"),
                allow_nan=False,
            )
            + "\n"
        )
        if len(line) > 1024 * 1024:
            raise ValueError("context record exceeds 1 MiB")
        return line

    def emit(self, record):
        if self.closed or self.failed or os.getpid() != self.pid:
            self.dropped += 1
            return False
        try:
            self.queue.put_nowait(record)
            return True
        except queue.Full:
            self.dropped += 1
            return False

    def _drain(self):
        try:
            while not self.stop.is_set() or not self.queue.empty():
                try:
                    record = self.queue.get(timeout=0.05)
                except queue.Empty:
                    continue
                self.file.write(self._encode(record))
                self.file.flush()
                self.written += 1
            self.file.write(
                self._encode(
                    {
                        "type": "summary",
                        "written_records": self.written,
                        "dropped_records": self.dropped,
                    }
                )
            )
        except Exception:  # noqa: BLE001 -- observer failures must not change inference
            self.failed = True
            _warn()
        finally:
            self.file.close()

    def close(self):
        if os.getpid() != self.pid or self.closed:
            return
        self.closed = True
        self.stop.set()
        self.thread.join(timeout=2)
        # A timeout/crash leaves no summary: the importer reports unclosed capture.


def close_all():
    for writer in list(_WRITERS):  # noqa: PERF101 -- snapshot across shutdown callbacks
        writer.close()


atexit.register(close_all)


def _integer(value):
    return value if type(value) is int and 0 <= value <= (2**63 - 1) else None


def _cache_state(scheduler):
    pool = getattr(getattr(scheduler, "kv_cache_manager", None), "block_pool", None)
    free = getattr(pool, "get_num_free_blocks", None)
    try:
        available = _integer(free()) if callable(free) else None
    except Exception:  # noqa: BLE001 -- observer failures must not change inference
        available = None
    return {
        "observation_phase": "after_schedule",
        "free_blocks": available,
        # Includes provider-reserved blocks; not a derived utilization ratio.
        "pool_blocks": _integer(getattr(pool, "num_gpu_blocks", None)),
    }


def _scheduler_writer(scheduler):
    writer = getattr(scheduler, "_traceloom_writer", None)
    if writer is not None and writer.pid == os.getpid() and not writer.closed:
        return writer
    config = getattr(scheduler, "scheduler_config", None)
    settings = {}
    for name in _CONFIG_FIELDS:
        value = getattr(config, name, None)
        if value is None or type(value) in (int, bool, str):
            settings[name] = value
    cache_config = getattr(scheduler, "cache_config", None)
    cache_settings = {
        name: getattr(cache_config, name, None)
        for name in ("enable_prefix_caching", "block_size")
    }
    cache_settings = {
        k: v
        for k, v in cache_settings.items()
        if v is None or type(v) in (bool, int, str)
    }
    try:
        runtime_version = importlib.metadata.version("vllm")
    except importlib.metadata.PackageNotFoundError:
        runtime_version = None
    writer = Writer(
        os.environ["TRACELOOM_CONTEXT_DIR"],
        os.environ.get("TRACELOOM_RUN_ID", ""),
        {
            "runtime": "vllm",
            "role": "scheduler",
            "scheduler_config": settings,
            "cache_config": cache_settings,
            "runtime_version": runtime_version,
            "association_contract": "scheduler_only"
            if not getattr(scheduler, "_traceloom_transport_identity", False)
            and getattr(scheduler, "_traceloom_scheduler_injection", False)
            else "scheduler_output_field",
        },
    )
    scheduler._traceloom_writer = writer
    scheduler._traceloom_ordinal = 0
    # Salt never leaves the process; request IDs are linkable only in this producer.
    scheduler._traceloom_salt = os.urandom(32)
    return writer


def _request_pseudonym(scheduler, request_id):
    return hashlib.sha256(
        scheduler._traceloom_salt + str(request_id).encode()
    ).hexdigest()


def request_coordinate(scheduler, request_id):
    """Return a recorded producer-local key for an in-process observer.

    Does not start recording, expose the salt, certify lifecycle state, or
    transport identity between processes. Call with the actual scheduler ID,
    not a client alias that the runtime may have rewritten.
    """
    writer = getattr(scheduler, "_traceloom_writer", None)
    if (not _enabled() or writer is None or writer.closed or writer.failed
            or writer.pid != os.getpid()):
        return None
    return {"run_id": writer.run_id, "scheduler_id": writer.producer_id,
            "request_id": _request_pseudonym(scheduler, request_id)}


def _scheduled_token_starts(output):
    """Worker-input offsets, not optimistic post-schedule Request counters.

    Missing/misaligned/duplicate payload identities stay unknown. Never pair
    cached arrays positionally when their lengths disagree.
    """
    candidates = {}

    def add(request_id, value):
        if request_id is not None:
            candidates.setdefault(request_id, []).append(_integer(value))

    for item in output.scheduled_new_reqs:
        add(getattr(item, "req_id", None), getattr(item, "num_computed_tokens", None))
    cached = getattr(output, "scheduled_cached_reqs", None)
    ids = getattr(cached, "req_ids", None)
    counts = getattr(cached, "num_computed_tokens", None)
    if isinstance(ids, (list, tuple)):
        if isinstance(counts, (list, tuple)) and len(ids) == len(counts):
            for request_id, value in zip(ids, counts):
                add(request_id, value)
        else:
            for request_id in ids:
                add(request_id, None)
    return {key: values[0] if len(values) == 1 else None
            for key, values in candidates.items()}


def record_step(scheduler, output, *, transport_identity=True, _injected=False):
    """Call immediately after the final schedule() result, before dispatch.

    Transport mode requires the explicit SchedulerOutput.traceloom_step field.
    Scheduler-only mode records context without modifying the returned output.
    Does not inspect prompts, token values, KV contents, or full configurations.
    """
    if not _enabled():
        return output
    # An injected scheduler already records its final result. A leftover
    # EngineCore hook must not duplicate it or pretend to transport its identity.
    if not _injected and getattr(scheduler, "_traceloom_scheduler_injection", False):
        return output
    writer = None
    try:
        # Refuse dynamic attributes that transport serializers can silently drop.
        if transport_identity:
            if "traceloom_step" not in getattr(output, "__dataclass_fields__", {}):
                raise ValueError("SchedulerOutput needs the TraceLoom transport field")
            output.traceloom_step = None
        writer = _scheduler_writer(scheduler)
        if len(output.num_scheduled_tokens) > 4096:
            raise ValueError("more than 4096 scheduled requests")
        if _integer(output.total_num_scheduled_tokens) is None:
            raise ValueError("invalid scheduled token total")
        step_id = uuid.uuid4().hex
        ordinal = scheduler._traceloom_ordinal
        scheduler._traceloom_ordinal += 1

        def anonymous(request_id):
            return _request_pseudonym(scheduler, request_id)

        new_ids = {r.req_id for r in output.scheduled_new_reqs}
        requests = []
        token_starts = _scheduled_token_starts(output)
        for request_id, tokens in output.num_scheduled_tokens.items():
            if _integer(tokens) is None:
                raise ValueError("invalid scheduled tokens")
            request = scheduler.requests.get(request_id)
            requests.append(
                {
                    "request_id": anonymous(request_id),
                    "scheduled_tokens": _integer(tokens),
                    "scheduled_token_start": token_starts.get(request_id),
                    "scheduled_token_start_basis": (
                        "scheduler_output_num_computed_tokens"
                        if token_starts.get(request_id) is not None else None
                    ),
                    "payload_kind": "new" if request_id in new_ids else "cached",
                    "computed_tokens_after_schedule": _integer(
                        getattr(request, "num_computed_tokens", None)
                    ),
                    "prompt_tokens": _integer(
                        getattr(request, "num_prompt_tokens", None)
                    ),
                }
            )
        preempted = getattr(output, "preempted_req_ids", None)
        resumed = getattr(
            getattr(output, "scheduled_cached_reqs", None), "resumed_req_ids", None
        )
        record = {
            "type": "step",
            "step_id": step_id,
            "ordinal": ordinal,
            "observed_monotonic_ns": time.monotonic_ns(),
            "total_scheduled_tokens": output.total_num_scheduled_tokens,
            "requests": requests,
            "cache": _cache_state(scheduler),
            "resumed_request_ids": None
            if resumed is None
            else sorted(anonymous(x) for x in resumed),
            "finished_request_ids": sorted(
                anonymous(x) for x in output.finished_req_ids
            ),
            "preempted_request_ids": None
            if preempted is None
            else sorted(anonymous(x) for x in preempted),
        }
        if transport_identity:
            output.traceloom_step = {"run_id": writer.run_id, "step_id": step_id}
        writer.emit(record)
    except Exception:  # noqa: BLE001 -- observer failures must not change inference
        if writer is not None:
            writer.dropped += 1
        _warn()
    return output


@contextmanager
def execution_scope(
    output, worker_rank=None, marker_factory=None, *, phase="execute_model"
):
    """Marker scopes host submission, not GPU completion or cross-thread work."""
    identity = getattr(output, "traceloom_step", None)
    if not _enabled() or identity is None:
        yield
        return
    writer = None
    step_id = None
    scope = nullcontext()
    execution_id = uuid.uuid4().hex
    marker = "traceloom.execution." + execution_id
    marker_state = "unavailable"
    try:
        run_id, step_id = identity["run_id"], identity["step_id"]
        if not isinstance(step_id, str) or not 1 <= len(step_id) <= 128:
            raise ValueError("invalid transported step identity")
        if run_id != os.environ.get("TRACELOOM_RUN_ID"):
            raise ValueError("worker run identity differs from scheduler")
        key = (os.getpid(), run_id)
        writer = _WORKER_WRITERS.get(key)
        if writer is None or writer.closed:
            writer = Writer(
                os.environ["TRACELOOM_CONTEXT_DIR"],
                run_id,
                {"runtime": "vllm", "role": "worker"},
            )
            _WORKER_WRITERS[key] = writer
        if marker_factory is None:
            from torch.profiler import record_function

            marker_factory = record_function
        scope = marker_factory(marker)
        scope.__enter__()
        marker_state = "emitted"
    except Exception:  # noqa: BLE001 -- observer failures must not change inference
        scope = nullcontext()
        scope.__enter__()
        _warn()
    status = "returned"
    try:
        yield
    except BaseException:
        status = "raised"
        raise
    finally:
        try:
            scope.__exit__(None, None, None)
        except Exception:  # noqa: BLE001 -- observer failures must not change inference
            marker_state = "unavailable"
            _warn()
        if writer is not None and step_id is not None:
            writer.emit(
                {
                    "type": "execution",
                    "phase": phase,
                    "step_id": step_id,
                    "execution_id": execution_id,
                    "marker": marker,
                    "worker_rank": _integer(worker_rank),
                    "process_id": os.getpid(),
                    "status": status,
                    "marker_state": marker_state,
                }
            )


def trace_execution(fn):
    @functools.wraps(fn)
    def wrapped(self, scheduler_output, *args, **kwargs):
        with execution_scope(scheduler_output, getattr(self, "rpc_rank", None)):
            return fn(self, scheduler_output, *args, **kwargs)

    return wrapped


def trace_worker_execute(fn):
    """Bridge the wrapper's one outstanding execute/sample state, not a step FIFO."""

    @functools.wraps(fn)
    def wrapped(self, output, *args, **kwargs):
        # A second unsampled execute violates the audited runner's single-state
        # contract. Keep inference behavior, but refuse to guess sampling identity.
        prior = getattr(self, "_traceloom_pending_sample", None)
        self._traceloom_pending_sample = None
        with execution_scope(output, getattr(self, "rpc_rank", None)):
            result = fn(self, output, *args, **kwargs)
        if result is None and output.total_num_scheduled_tokens > 0:
            if prior is not None:
                self._traceloom_pending_sample = False
                _warn()
            else:
                self._traceloom_pending_sample = getattr(output, "traceloom_step", None)
        return result

    return wrapped


def trace_worker_sample(fn):
    @functools.wraps(fn)
    def wrapped(self, *args, **kwargs):
        identity = getattr(self, "_traceloom_pending_sample", None)
        self._traceloom_pending_sample = None
        from types import SimpleNamespace

        with execution_scope(
            SimpleNamespace(traceloom_step=identity or None),
            getattr(self, "rpc_rank", None),
            phase="sample_tokens",
        ):
            return fn(self, *args, **kwargs)

    return wrapped
