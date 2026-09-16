"""Opt-in --scheduler-cls adapters for stock and execution-linked vLLM V1.

Select the class matching the runtime's scheduling mode. Import this module only
inside the chosen vLLM environment; installing the package does not install vLLM
or load an accelerator. Tracing* is patch-free; LinkedTracing* requires transport.
"""

from traceloom_vllm_context import record_step
from vllm.v1.core.sched.async_scheduler import AsyncScheduler
from vllm.v1.core.sched.scheduler import Scheduler


class _RecordingScheduler:
    _traceloom_scheduler_injection = True
    _traceloom_transport_identity = False

    def schedule(self, *args, **kwargs):
        # Run the selected implementation, including async postprocessing, once.
        # Business exceptions propagate unchanged; observer failures are isolated
        # by record_step. Preserve business fields; linked mode sets only its ID field.
        output = super().schedule(*args, **kwargs)
        record_step(
            self,
            output,
            transport_identity=self._traceloom_transport_identity,
            _injected=True,
        )
        return output


class TracingScheduler(_RecordingScheduler, Scheduler):
    """Synchronous scheduler with after-schedule JSONL context export."""


class TracingAsyncScheduler(_RecordingScheduler, AsyncScheduler):
    """AsyncScheduler subclass: retain async scheduling and output bookkeeping."""


class _ExecutionLinkedScheduler:
    _traceloom_transport_identity = True

    def __init__(self, *args, **kwargs):
        from vllm.v1.core.sched.output import SchedulerOutput

        if "traceloom_step" not in getattr(SchedulerOutput, "__dataclass_fields__", {}):
            raise RuntimeError(
                "Execution-linked TraceLoom requires transport-worker.patch; "
                "use TracingScheduler/TracingAsyncScheduler for patch-free capture"
            )
        super().__init__(*args, **kwargs)


class LinkedTracingScheduler(_ExecutionLinkedScheduler, TracingScheduler):
    """Synchronous scheduling plus explicit identity transport (patch required)."""


class LinkedTracingAsyncScheduler(_ExecutionLinkedScheduler, TracingAsyncScheduler):
    """Asynchronous scheduling plus explicit identity transport (patch required)."""
