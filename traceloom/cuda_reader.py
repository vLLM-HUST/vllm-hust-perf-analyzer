from __future__ import annotations

import sqlite3
from pathlib import Path
from typing import Dict, List, Sequence, Tuple

from .msprof_reader import StreamEvent, StreamSelection, _canonical_label


CUDA_KERNEL_TABLE = "CUPTI_ACTIVITY_KIND_KERNEL"
CUDA_MEMCPY_TABLE = "CUPTI_ACTIVITY_KIND_MEMCPY"
CUDA_MEMSET_TABLE = "CUPTI_ACTIVITY_KIND_MEMSET"
CUDA_SYNC_TABLE = "CUPTI_ACTIVITY_KIND_SYNCHRONIZATION"
CUDA_GRAPH_TRACE_TABLE = "CUPTI_ACTIVITY_KIND_GRAPH_TRACE"


def is_cuda_profile(db_path: Path) -> bool:
    try:
        with sqlite3.connect(f"file:{db_path.resolve()}?immutable=1", uri=True) as conn:
            return (
                _table_exists(conn, CUDA_KERNEL_TABLE)
                and _table_row_count(conn, CUDA_KERNEL_TABLE) > 0
            ) or (
                _table_exists(conn, CUDA_GRAPH_TRACE_TABLE)
                and _table_row_count(conn, CUDA_GRAPH_TRACE_TABLE) > 0
            )
    except sqlite3.Error:
        return False


def load_cuda_device_events(db_path: Path, device_id: int) -> Tuple[List[StreamEvent], Dict[int, Dict[str, object]]]:
    events, _stream_stats = _load_cuda_events(db_path)
    selected = [ev for ev in events if ev.device_id == device_id]
    selected.sort(key=lambda ev: (ev.start_ns, ev.end_ns, ev.stream_id, ev.global_task_id))
    selected_stats: Dict[int, Dict[str, object]] = {}
    for ev in selected:
        _add_stream_stat(selected_stats, ev)
    return selected, selected_stats


def rank_cuda_streams_global(db_paths: Sequence[Path]) -> Tuple[List[StreamSelection], List[Dict[str, object]]]:
    selections: List[StreamSelection] = []
    for db_idx, db_path in enumerate(db_paths, start=1):
        events, _stream_stats = _load_cuda_events(db_path)
        buckets: Dict[Tuple[int, int], Dict[str, object]] = {}
        for ev in events:
            key = (ev.device_id, ev.stream_id)
            bucket = buckets.setdefault(key, _new_rank_bucket())
            bucket["event_count"] = int(bucket["event_count"]) + 1
            bucket["total_ns"] = int(bucket["total_ns"]) + ev.dur_ns
            if ev.category == "exec":
                bucket["exec_ns"] = int(bucket["exec_ns"]) + ev.dur_ns
            elif ev.category in {"comm", "data_move"}:
                bucket["comm_ns"] = int(bucket["comm_ns"]) + ev.dur_ns
            elif ev.category == "wait":
                bucket["wait_ns"] = int(bucket["wait_ns"]) + ev.dur_ns
            else:
                bucket["other_ns"] = int(bucket["other_ns"]) + ev.dur_ns
            cur_min = bucket.get("min_start_ns")
            cur_max = bucket.get("max_end_ns")
            bucket["min_start_ns"] = ev.start_ns if cur_min is None else min(int(cur_min), ev.start_ns)
            bucket["max_end_ns"] = ev.end_ns if cur_max is None else max(int(cur_max), ev.end_ns)

        for (device_id, stream_id), bucket in buckets.items():
            stats = _stream_ranking_stats(
                db_idx=db_idx,
                db_path=db_path,
                device_id=device_id,
                stream_id=stream_id,
                bucket=bucket,
            )
            selections.append(
                StreamSelection(
                    global_rank=0,
                    db_idx=db_idx,
                    db_path=db_path,
                    device_id=device_id,
                    stream_id=stream_id,
                    events=[],
                    stats=stats,
                )
            )

    selections.sort(
        key=lambda s: (
            float(s.stats["total_dur_us"]),
            float(s.stats["busy_time_us"]),
            int(s.stats["event_count"]),
        ),
        reverse=True,
    )
    ranked: List[StreamSelection] = []
    rows: List[Dict[str, object]] = []
    for rank, sel in enumerate(selections, start=1):
        stats = dict(sel.stats)
        stats["global_rank"] = rank
        ranked_sel = StreamSelection(
            global_rank=rank,
            db_idx=sel.db_idx,
            db_path=sel.db_path,
            device_id=sel.device_id,
            stream_id=sel.stream_id,
            events=[],
            stats=stats,
        )
        ranked.append(ranked_sel)
        rows.append(stats)
    return ranked, rows


def _load_cuda_events(db_path: Path) -> Tuple[List[StreamEvent], Dict[int, Dict[str, object]]]:
    events: List[StreamEvent] = []
    stream_stats: Dict[int, Dict[str, object]] = {}
    with sqlite3.connect(f"file:{db_path.resolve()}?immutable=1", uri=True) as conn:
        strings = _load_string_ids(conn)
        if _table_exists(conn, CUDA_GRAPH_TRACE_TABLE):
            events.extend(_load_graph_trace_events(conn))
        if _table_exists(conn, CUDA_KERNEL_TABLE):
            events.extend(_load_kernel_events(conn, strings))
        if _table_exists(conn, CUDA_MEMCPY_TABLE):
            events.extend(_load_memcpy_events(conn))
        if _table_exists(conn, CUDA_MEMSET_TABLE):
            events.extend(_load_memset_events(conn))
        if _table_exists(conn, CUDA_SYNC_TABLE):
            events.extend(_load_sync_events(conn))

    events = [ev for ev in events if ev.end_ns > ev.start_ns]
    events.sort(key=lambda ev: (ev.start_ns, ev.end_ns, ev.device_id, ev.stream_id))
    for ev in events:
        _add_stream_stat(stream_stats, ev)
    return events, stream_stats


def _load_kernel_events(conn: sqlite3.Connection, strings: Dict[int, str]) -> List[StreamEvent]:
    events: List[StreamEvent] = []
    rows = conn.execute(
        """
        SELECT start, end, deviceId, streamId, correlationId, demangledName, shortName,
               gridX, gridY, gridZ, blockX, blockY, blockZ, registersPerThread,
               staticSharedMemory, dynamicSharedMemory
        FROM CUPTI_ACTIVITY_KIND_KERNEL
        WHERE start IS NOT NULL AND end IS NOT NULL AND end > start
        ORDER BY start, end, correlationId
        """
    )
    for row_idx, row in enumerate(rows, start=1):
        (
            start_ns,
            end_ns,
            device_id,
            stream_id,
            correlation_id,
            demangled_name_id,
            short_name_id,
            grid_x,
            grid_y,
            grid_z,
            block_x,
            block_y,
            block_z,
            registers,
            static_smem,
            dynamic_smem,
        ) = row
        short_name = strings.get(_as_int(short_name_id, -1))
        demangled_name = strings.get(_as_int(demangled_name_id, -1))
        raw_name = _choose_kernel_label(short_name=short_name, demangled_name=demangled_name)
        if not raw_name:
            raw_name = f"cuda_kernel_{row_idx}"
        category = "comm" if _is_collective_kernel(raw_name) else "exec"
        label = _canonical_label(_lift_cuda_kernel_label(raw_name), category=category)
        task_id = _as_int(correlation_id, row_idx)
        source_key = (
            f"correlationId={task_id};start={start_ns};"
            f"grid={_as_int(grid_x, 0)}x{_as_int(grid_y, 0)}x{_as_int(grid_z, 0)};"
            f"block={_as_int(block_x, 0)}x{_as_int(block_y, 0)}x{_as_int(block_z, 0)};"
            f"registers={_as_int(registers, 0)};"
            f"static_smem={_as_int(static_smem, 0)};dynamic_smem={_as_int(dynamic_smem, 0)};"
            f"raw_label={raw_name};short_label={short_name or ''};demangled_label={demangled_name or ''}"
        )
        events.append(
            StreamEvent(
                start_ns=_as_int(start_ns, 0),
                end_ns=_as_int(end_ns, 0),
                device_id=_as_int(device_id, -1),
                stream_id=_as_int(stream_id, -1),
                task_id=task_id,
                global_task_id=task_id,
                connection_id=_as_int(correlation_id, -1),
                task_type="CUDA_COLLECTIVE_KERNEL" if category == "comm" else "CUDA_KERNEL",
                label=label,
                category=category,
                source_table=CUDA_KERNEL_TABLE,
                source_key=source_key,
            )
        )
    return events


def _load_graph_trace_events(conn: sqlite3.Connection) -> List[StreamEvent]:
    events: List[StreamEvent] = []
    rows = conn.execute(
        """
        SELECT start, end, deviceId, streamId, correlationId, graphId, graphExecId, contextId
        FROM CUPTI_ACTIVITY_KIND_GRAPH_TRACE
        WHERE start IS NOT NULL AND end IS NOT NULL AND end > start
        ORDER BY start, end, graphId, graphExecId
        """
    )
    for row_idx, row in enumerate(rows, start=1):
        start_ns, end_ns, device_id, stream_id, correlation_id, graph_id, graph_exec_id, context_id = row
        # Use a TraceLoom-local negative id so graph replay anchors do not collide
        # with CUDA kernel/runtime correlation ids. Keep the raw correlation id in
        # connection_id and source_key for evidence joins.
        task_id = -2_000_000_000 - row_idx
        source_key = (
            f"row={row_idx};start={start_ns};correlationId={_as_int(correlation_id, -1)};"
            f"graphId={_as_int(graph_id, -1)};graphExecId={_as_int(graph_exec_id, -1)};"
            f"contextId={_as_int(context_id, -1)}"
        )
        events.append(
            StreamEvent(
                start_ns=_as_int(start_ns, 0),
                end_ns=_as_int(end_ns, 0),
                device_id=_as_int(device_id, -1),
                stream_id=_as_int(stream_id, -1),
                task_id=task_id,
                global_task_id=task_id,
                connection_id=_as_int(correlation_id, -1),
                task_type="CUDA_GRAPH_TRACE",
                label="CudaGraphReplay",
                category="exec",
                source_table=CUDA_GRAPH_TRACE_TABLE,
                source_key=source_key,
            )
        )
    return events


def _load_memcpy_events(conn: sqlite3.Connection) -> List[StreamEvent]:
    events: List[StreamEvent] = []
    copy_kind = _load_enum_labels(conn, "ENUM_CUDA_MEMCPY_OPER")
    rows = conn.execute(
        """
        SELECT start, end, deviceId, streamId, correlationId, bytes, copyKind,
               srcDeviceId, dstDeviceId, srcKind, dstKind
        FROM CUPTI_ACTIVITY_KIND_MEMCPY
        WHERE start IS NOT NULL AND end IS NOT NULL AND end > start
        ORDER BY start, end, correlationId
        """
    )
    for row_idx, row in enumerate(rows, start=1):
        start_ns, end_ns, device_id, stream_id, correlation_id, bytes_, kind, src_dev, dst_dev, src_kind, dst_kind = row
        kind_i = _as_int(kind, -1)
        kind_label = copy_kind.get(kind_i, f"kind={kind_i}")
        task_id = _as_int(correlation_id, row_idx)
        label = _canonical_label(f"cuda_memcpy {kind_label} bytes={_as_int(bytes_, 0)}", category="comm")
        events.append(
            StreamEvent(
                start_ns=_as_int(start_ns, 0),
                end_ns=_as_int(end_ns, 0),
                device_id=_as_int(device_id, -1),
                stream_id=_as_int(stream_id, -1),
                task_id=task_id,
                global_task_id=task_id,
                connection_id=_as_int(correlation_id, -1),
                task_type=f"CUDA_MEMCPY_{kind_i}",
                label=label,
                category="comm",
                source_table=CUDA_MEMCPY_TABLE,
                source_key=(
                    f"correlationId={task_id};start={start_ns};bytes={_as_int(bytes_, 0)};"
                    f"copyKind={kind_i};srcDeviceId={_as_int(src_dev, -1)};dstDeviceId={_as_int(dst_dev, -1)};"
                    f"srcKind={_as_int(src_kind, -1)};dstKind={_as_int(dst_kind, -1)}"
                ),
            )
        )
    return events


def _load_memset_events(conn: sqlite3.Connection) -> List[StreamEvent]:
    events: List[StreamEvent] = []
    rows = conn.execute(
        """
        SELECT start, end, deviceId, streamId, correlationId, value, bytes
        FROM CUPTI_ACTIVITY_KIND_MEMSET
        WHERE start IS NOT NULL AND end IS NOT NULL AND end > start
        ORDER BY start, end, correlationId
        """
    )
    for row_idx, row in enumerate(rows, start=1):
        start_ns, end_ns, device_id, stream_id, correlation_id, value, bytes_ = row
        task_id = _as_int(correlation_id, row_idx)
        label = _canonical_label(f"cuda_memset value={_as_int(value, 0)} bytes={_as_int(bytes_, 0)}", category="comm")
        events.append(
            StreamEvent(
                start_ns=_as_int(start_ns, 0),
                end_ns=_as_int(end_ns, 0),
                device_id=_as_int(device_id, -1),
                stream_id=_as_int(stream_id, -1),
                task_id=task_id,
                global_task_id=task_id,
                connection_id=_as_int(correlation_id, -1),
                task_type="CUDA_MEMSET",
                label=label,
                category="comm",
                source_table=CUDA_MEMSET_TABLE,
                source_key=f"correlationId={task_id};start={start_ns};value={_as_int(value, 0)};bytes={_as_int(bytes_, 0)}",
            )
        )
    return events


def _load_sync_events(conn: sqlite3.Connection) -> List[StreamEvent]:
    events: List[StreamEvent] = []
    sync_types = _load_enum_labels(conn, "ENUM_CUPTI_SYNC_TYPE")
    rows = conn.execute(
        """
        SELECT start, end, deviceId, streamId, correlationId, syncType, eventId, eventSyncId
        FROM CUPTI_ACTIVITY_KIND_SYNCHRONIZATION
        WHERE start IS NOT NULL AND end IS NOT NULL AND end > start
        ORDER BY start, end, correlationId
        """
    )
    for row_idx, row in enumerate(rows, start=1):
        start_ns, end_ns, device_id, stream_id, correlation_id, sync_type, event_id, event_sync_id = row
        sync_i = _as_int(sync_type, -1)
        sync_label = sync_types.get(sync_i, f"sync_type={sync_i}")
        task_id = _as_int(correlation_id, row_idx)
        events.append(
            StreamEvent(
                start_ns=_as_int(start_ns, 0),
                end_ns=_as_int(end_ns, 0),
                device_id=_as_int(device_id, -1),
                stream_id=_as_int(stream_id, -1),
                task_id=task_id,
                global_task_id=task_id,
                connection_id=_as_int(correlation_id, -1),
                task_type=f"CUDA_SYNC_{sync_i}",
                label=_canonical_label(f"cuda_sync {sync_label}", category="wait"),
                category="wait",
                source_table=CUDA_SYNC_TABLE,
                source_key=(
                    f"correlationId={task_id};start={start_ns};syncType={sync_i};"
                    f"eventId={_as_int(event_id, -1)};eventSyncId={_as_int(event_sync_id, -1)}"
                ),
            )
        )
    return events


def _choose_kernel_label(*, short_name: str | None, demangled_name: str | None) -> str:
    short = short_name or ""
    demangled = demangled_name or ""
    generic_short_names = {"kernel", "kernel1", "kernel2", "kernel3"}
    if short.lower() in generic_short_names and demangled:
        return demangled
    return short or demangled


def _lift_cuda_kernel_label(raw_name: str) -> str:
    low = raw_name.lower()
    if _is_collective_kernel(raw_name):
        return raw_name
    if any(term in low for term in ("flash_attn", "flashattention", "flash_fwd", "_flash_fwd")):
        return "CudaFlashAttention"
    if "flash" in low and "attention" in low:
        return "CudaFlashAttention"
    if "paged_attention" in low or "attention" in low:
        return "CudaAttention"
    if any(term in low for term in ("sgemm", "dgemm", "hgemm", "gemm", "cutlass", "matmul")):
        return "CudaMatMul"
    if "rmsnorm" in low or "rms_norm" in low:
        return "CudaRmsNorm"
    if "layer_norm" in low or "layernorm" in low:
        return "CudaLayerNorm"
    if "rotary" in low or "rope" in low:
        return "CudaRope"
    if "silu" in low or "swiglu" in low or "gelu" in low or "act_and_mul" in low:
        return "CudaActivation"
    if "reshape_and_cache" in low or "slot_mapping" in low or "kv_cache" in low:
        return "CudaKVCache"
    if "topk" in low or "topp" in low or "sampling" in low or "sampler" in low:
        return "CudaSampling"
    if "softmax" in low:
        return "CudaSoftmax"
    if "splitkreduce" in low or "reduce_kernel" in low:
        return "CudaReduce"
    if "distribution_" in low or "random" in low or "curand" in low:
        return "CudaAux:Random"
    if "vectorized_elementwise_kernel" in low or "elementwise" in low:
        return "CudaAux:Pointwise"
    if "memset" in low or "fill" in low or "zero" in low:
        return "CudaAux:Fill"
    if "copy" in low or "catarraybatchedcopy" in low or "tensorcopy" in low:
        return "CudaAux:Copy"
    if "index" in low or "gather" in low or "scatter" in low:
        return "CudaAux:Index"
    return raw_name


def _is_collective_kernel(raw_name: str) -> bool:
    low = raw_name.lower()
    return "nccl" in low or any(
        term in low
        for term in (
            "allreduce",
            "all_reduce",
            "allgather",
            "all_gather",
            "reducescatter",
            "reduce_scatter",
            "alltoall",
            "all_to_all",
            "broadcast",
        )
    )


def _load_string_ids(conn: sqlite3.Connection) -> Dict[int, str]:
    out: Dict[int, str] = {}
    if not _table_exists(conn, "StringIds"):
        return out
    for sid, value in conn.execute("SELECT id, value FROM StringIds"):
        if sid is None:
            continue
        out[int(sid)] = str(value or "")
    return out


def _load_enum_labels(conn: sqlite3.Connection, table: str) -> Dict[int, str]:
    out: Dict[int, str] = {}
    if not _table_exists(conn, table):
        return out
    for id_, label in conn.execute(f'SELECT id, label FROM "{table}"'):
        if id_ is None:
            continue
        out[int(id_)] = str(label or "")
    return out


def _add_stream_stat(stream_stats: Dict[int, Dict[str, object]], ev: StreamEvent) -> None:
    bucket = stream_stats.setdefault(
        ev.stream_id,
        {
            "stream_id": ev.stream_id,
            "event_count": 0,
            "exec_count": 0,
            "exec_us": 0.0,
            "comm_us": 0.0,
            "wait_us": 0.0,
        },
    )
    bucket["event_count"] = int(bucket["event_count"]) + 1
    dur_us = ev.dur_ns / 1000.0
    if ev.category == "exec":
        bucket["exec_count"] = int(bucket["exec_count"]) + 1
        bucket["exec_us"] = float(bucket["exec_us"]) + dur_us
    elif ev.category in {"comm", "data_move"}:
        bucket["comm_us"] = float(bucket["comm_us"]) + dur_us
    elif ev.category == "wait":
        bucket["wait_us"] = float(bucket["wait_us"]) + dur_us


def _new_rank_bucket() -> Dict[str, object]:
    return {
        "event_count": 0,
        "total_ns": 0,
        "wait_ns": 0,
        "comm_ns": 0,
        "exec_ns": 0,
        "other_ns": 0,
        "min_start_ns": None,
        "max_end_ns": None,
    }


def _stream_ranking_stats(
    *,
    db_idx: int,
    db_path: Path,
    device_id: int,
    stream_id: int,
    bucket: Dict[str, object],
) -> Dict[str, object]:
    total_ns = int(bucket["total_ns"])
    wait_ns = int(bucket["wait_ns"])
    comm_ns = int(bucket["comm_ns"])
    exec_ns = int(bucket["exec_ns"])
    other_ns = int(bucket["other_ns"])
    event_count = int(bucket["event_count"])
    min_start_ns = bucket.get("min_start_ns")
    max_end_ns = bucket.get("max_end_ns")
    span_ns = (
        max(0, int(max_end_ns) - int(min_start_ns))
        if min_start_ns is not None and max_end_ns is not None
        else 0
    )
    covered_ns = min(total_ns, span_ns) if span_ns > 0 else total_ns
    idle_gap_ns = max(0, span_ns - covered_ns)
    total_denom = total_ns if total_ns > 0 else 1
    span_denom = span_ns if span_ns > 0 else 1
    span_ms = span_ns / 1_000_000.0
    return {
        "global_rank": 0,
        "db_idx": db_idx,
        "db": str(db_path),
        "device_id": device_id,
        "stream_id": stream_id,
        "global_stream_key": f"db{db_idx:02d}:cuda{device_id}:stream{stream_id}",
        "event_count": event_count,
        "total_dur_us": round(total_ns / 1000.0, 3),
        "busy_time_us": round(covered_ns / 1000.0, 3),
        "span_us": round(span_ns / 1000.0, 3),
        "idle_gap_us": round(idle_gap_ns / 1000.0, 3),
        "event_density_per_ms": round(event_count / span_ms, 6) if span_ms > 0 else 0.0,
        "wait_us": round(wait_ns / 1000.0, 3),
        "comm_us": round(comm_ns / 1000.0, 3),
        "exec_us": round(exec_ns / 1000.0, 3),
        "other_us": round(other_ns / 1000.0, 3),
        "wait_ratio_task": wait_ns / total_denom,
        "comm_ratio_task": comm_ns / total_denom,
        "exec_ratio_task": exec_ns / total_denom,
        "other_ratio_task": other_ns / total_denom,
        "busy_ratio_span": covered_ns / span_denom,
        "idle_ratio_span": idle_gap_ns / span_denom,
    }


def _table_exists(conn: sqlite3.Connection, name: str) -> bool:
    row = conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1",
        (name,),
    ).fetchone()
    return row is not None


def _table_row_count(conn: sqlite3.Connection, name: str) -> int:
    try:
        row = conn.execute(f'SELECT COUNT(*) FROM "{name}"').fetchone()
    except sqlite3.Error:
        return 0
    return int(row[0] if row and row[0] is not None else 0)


def _as_int(value: object, default: int) -> int:
    if value is None or value == "":
        return default
    try:
        return int(value)
    except (TypeError, ValueError):
        return default
