from __future__ import annotations

import re
import sqlite3
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Sequence, Tuple


COMM_TASK_TYPES = {
    "SDMA",
    "RDMA",
    "LOCAL",
    "MEMCPY_ASYNC",
    "MEMCPY",
    "WRITE_VALUE",
    "MEM_WRITE_VALUE",
    "EVENT_RECORD",
    "NOTIFY_RECORD",
    "CAPTURE_RECORD",
}

EXEC_HINTS = (
    "AI_CORE",
    "AI_VECTOR_CORE",
    "AIVEC",
    "AICORE",
    "KERNEL",
    "MODEL_EXECUTE",
    "MODEL_MAINTAINCE",
    "MODEL_MAINTENANCE",
    "MIX_AIV",
    "MIX_AIC",
)


@dataclass(frozen=True)
class StreamEvent:
    start_ns: int
    end_ns: int
    device_id: int
    stream_id: int
    task_id: int
    global_task_id: int
    connection_id: int
    task_type: str
    label: str
    category: str

    @property
    def dur_ns(self) -> int:
        return max(self.end_ns - self.start_ns, 0)


@dataclass(frozen=True)
class StreamSelection:
    global_rank: int
    db_idx: int
    db_path: Path
    device_id: int
    stream_id: int
    events: List[StreamEvent]
    stats: Dict[str, object]


def _normalize_task_type(name: str) -> str:
    return re.sub(r"\s+", " ", (name or "").strip()).upper().replace("_", " ")


def _normalize_task_key(name: str) -> str:
    s = (name or "").strip().upper()
    s = re.sub(r"[^A-Z0-9]+", "_", s)
    s = re.sub(r"_+", "_", s).strip("_")
    return s


def _canonical_label(label: str, *, category: str) -> str:
    s = (label or "").strip()
    if not s:
        return "UNKNOWN"
    # Keep operator variants like MatMulV2/MatMulV3 distinguishable for exec.
    # For non-exec control/comm labels, normalize numbers more aggressively.
    if category == "exec":
        s = re.sub(r"\b\d{6,}\b", "#", s)
    else:
        s = re.sub(r"\d+", "#", s)
    s = re.sub(r"\s+", " ", s)
    if len(s) > 96:
        s = s[:93] + "..."
    return s


def _classify_task(task_type: str) -> str:
    k = _normalize_task_key(task_type)
    if "WAIT" in k:
        return "wait"
    if k in COMM_TASK_TYPES:
        return "comm"
    if "NOTIFY" in k and "WAIT" not in k:
        return "comm"
    if any(h in k for h in EXEC_HINTS):
        return "exec"
    return "other"


def _load_string_ids(conn: sqlite3.Connection) -> Dict[int, str]:
    out: Dict[int, str] = {}
    for sid, value in conn.execute("SELECT id, value FROM STRING_IDS"):
        if sid is None:
            continue
        out[int(sid)] = str(value or "")
    return out


def _load_global_task_names(
    conn: sqlite3.Connection,
) -> Tuple[Dict[int, str], Dict[int, str], Dict[int, str]]:
    compute: Dict[int, str] = {}
    compute_optype: Dict[int, str] = {}
    if conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name='COMPUTE_TASK_INFO'"
    ).fetchone():
        for gid, name_id, op_type_id in conn.execute(
            "SELECT globalTaskId, name, opType FROM COMPUTE_TASK_INFO"
        ):
            if gid is None or name_id is None:
                pass
            else:
                compute[int(gid)] = str(name_id)
            if gid is not None and op_type_id is not None:
                compute_optype[int(gid)] = str(op_type_id)

    comm: Dict[int, str] = {}
    if conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name='COMMUNICATION_TASK_INFO'"
    ).fetchone():
        for gid, name_id in conn.execute(
            "SELECT globalTaskId, MIN(name) FROM COMMUNICATION_TASK_INFO GROUP BY globalTaskId"
        ):
            if gid is None or name_id is None:
                continue
            comm[int(gid)] = str(name_id)
    return compute, compute_optype, comm


def _load_comm_connection_ids(conn: sqlite3.Connection) -> set[int]:
    out: set[int] = set()
    if not conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name='COMMUNICATION_OP'"
    ).fetchone():
        return out
    for (cid,) in conn.execute("SELECT DISTINCT connectionId FROM COMMUNICATION_OP"):
        if cid is None:
            continue
        out.add(int(cid))
    return out


def _resolve_label(
    *,
    global_task_id: int,
    task_type: str,
    category: str,
    sid_to_value: Dict[int, str],
    compute_name_ids: Dict[int, str],
    compute_optype_ids: Dict[int, str],
    comm_name_ids: Dict[int, str],
) -> str:
    label_raw = ""
    compute_name_id = compute_name_ids.get(global_task_id)
    if compute_name_id is not None:
        try:
            label_raw = sid_to_value.get(int(compute_name_id), "")
        except ValueError:
            label_raw = ""
    if not label_raw:
        compute_op_type_id = compute_optype_ids.get(global_task_id)
        if compute_op_type_id is not None:
            try:
                label_raw = sid_to_value.get(int(compute_op_type_id), "")
            except ValueError:
                label_raw = ""
    if not label_raw:
        comm_name_id = comm_name_ids.get(global_task_id)
        if comm_name_id is not None:
            try:
                label_raw = sid_to_value.get(int(comm_name_id), "")
            except ValueError:
                label_raw = ""
    if not label_raw:
        label_raw = task_type
    return _canonical_label(label_raw, category=category)


def _task_row_to_stream_event(
    *,
    start_ns: int,
    end_ns: int,
    device_id: int,
    stream_id: int,
    task_id: int,
    global_task_id: int,
    connection_id: int,
    task_type_id: int,
    sid_to_value: Dict[int, str],
    compute_name_ids: Dict[int, str],
    compute_optype_ids: Dict[int, str],
    comm_name_ids: Dict[int, str],
    comm_connection_ids: set[int],
) -> StreamEvent | None:
    task_type = sid_to_value.get(task_type_id, str(task_type_id))
    task_type_norm = _normalize_task_type(task_type)
    task_key = _normalize_task_key(task_type_norm)
    if task_key == "CAPTURE_WAIT":
        return None

    category = _classify_task(task_type_norm)
    if category == "exec" and connection_id in comm_connection_ids:
        category = "comm"
    if category not in {"wait", "comm", "exec"}:
        return None

    label = _resolve_label(
        global_task_id=global_task_id,
        task_type=task_type_norm,
        category=category,
        sid_to_value=sid_to_value,
        compute_name_ids=compute_name_ids,
        compute_optype_ids=compute_optype_ids,
        comm_name_ids=comm_name_ids,
    )
    return StreamEvent(
        start_ns=start_ns,
        end_ns=end_ns,
        device_id=device_id,
        stream_id=stream_id,
        task_id=task_id,
        global_task_id=global_task_id,
        connection_id=connection_id,
        task_type=task_type_norm,
        label=label,
        category=category,
    )


def _load_stream_events(
    db_path: Path,
    stream_filter: set[Tuple[int, int]] | None = None,
) -> Dict[Tuple[int, int], List[StreamEvent]]:
    out: Dict[Tuple[int, int], List[StreamEvent]] = {}
    with sqlite3.connect(str(db_path)) as conn:
        sid_to_value = _load_string_ids(conn)
        compute_name_ids, compute_optype_ids, comm_name_ids = _load_global_task_names(conn)
        comm_connection_ids = _load_comm_connection_ids(conn)

        query = (
            "SELECT startNs, endNs, deviceId, streamId, taskId, globalTaskId, connectionId, taskType "
            "FROM TASK ORDER BY deviceId, streamId, startNs, endNs, globalTaskId"
        )
        for row in conn.execute(query):
            device_id = int(row[2] if row[2] is not None else -1)
            stream_id = int(row[3] if row[3] is not None else -1)
            key = (device_id, stream_id)
            if stream_filter is not None and key not in stream_filter:
                continue
            ev = _task_row_to_stream_event(
                start_ns=int(row[0] if row[0] is not None else 0),
                end_ns=int(row[1] if row[1] is not None else 0),
                device_id=device_id,
                stream_id=stream_id,
                task_id=int(row[4] if row[4] is not None else -1),
                global_task_id=int(row[5] if row[5] is not None else -1),
                connection_id=int(row[6] if row[6] is not None else -1),
                task_type_id=int(row[7] if row[7] is not None else -1),
                sid_to_value=sid_to_value,
                compute_name_ids=compute_name_ids,
                compute_optype_ids=compute_optype_ids,
                comm_name_ids=comm_name_ids,
                comm_connection_ids=comm_connection_ids,
            )
            if ev is None:
                continue
            out.setdefault(key, []).append(ev)
    return out


def _load_device_events(db_path: Path, device_id: int) -> Tuple[List[StreamEvent], Dict[int, Dict[str, object]]]:
    events: List[StreamEvent] = []
    stream_stats: Dict[int, Dict[str, object]] = {}

    with sqlite3.connect(str(db_path)) as conn:
        sid_to_value = _load_string_ids(conn)
        compute_name_ids, compute_optype_ids, comm_name_ids = _load_global_task_names(conn)
        comm_connection_ids = _load_comm_connection_ids(conn)

        query = (
            "SELECT startNs, endNs, streamId, taskId, globalTaskId, connectionId, taskType "
            "FROM TASK "
            "WHERE deviceId = ? AND startNs IS NOT NULL AND endNs IS NOT NULL AND endNs > startNs "
            "ORDER BY startNs, endNs, streamId, globalTaskId"
        )
        for row in conn.execute(query, (device_id,)):
            stream_id = int(row[2] if row[2] is not None else -1)
            ev = _task_row_to_stream_event(
                start_ns=int(row[0] if row[0] is not None else 0),
                end_ns=int(row[1] if row[1] is not None else 0),
                device_id=device_id,
                stream_id=stream_id,
                task_id=int(row[3] if row[3] is not None else -1),
                global_task_id=int(row[4] if row[4] is not None else -1),
                connection_id=int(row[5] if row[5] is not None else -1),
                task_type_id=int(row[6] if row[6] is not None else -1),
                sid_to_value=sid_to_value,
                compute_name_ids=compute_name_ids,
                compute_optype_ids=compute_optype_ids,
                comm_name_ids=comm_name_ids,
                comm_connection_ids=comm_connection_ids,
            )
            if ev is None:
                continue
            events.append(ev)
            _add_stream_stat(stream_stats, ev)

    return events, stream_stats


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
    elif ev.category == "comm":
        bucket["comm_us"] = float(bucket["comm_us"]) + dur_us
    elif ev.category == "wait":
        bucket["wait_us"] = float(bucket["wait_us"]) + dur_us


def _stream_total_dur(events: Sequence[StreamEvent]) -> int:
    return sum(e.dur_ns for e in events)


def _new_stream_rank_bucket() -> Dict[str, object]:
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


def _stream_ranking_stats_from_bucket(
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
        "global_stream_key": f"db{db_idx:02d}:dev{device_id}:stream{stream_id}",
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


def _load_stream_ranking_stats(db_path: Path, *, db_idx: int) -> List[StreamSelection]:
    buckets: Dict[Tuple[int, int], Dict[str, object]] = {}
    with sqlite3.connect(str(db_path)) as conn:
        sid_to_value = _load_string_ids(conn)
        query = (
            "SELECT deviceId, streamId, taskType, COUNT(*), "
            "SUM(CASE WHEN endNs > startNs THEN endNs - startNs ELSE 0 END), "
            "MIN(startNs), MAX(endNs) "
            "FROM TASK "
            "WHERE startNs IS NOT NULL AND endNs IS NOT NULL AND endNs > startNs "
            "GROUP BY deviceId, streamId, taskType"
        )
        for row in conn.execute(query):
            device_id = int(row[0] if row[0] is not None else -1)
            stream_id = int(row[1] if row[1] is not None else -1)
            task_type_id = int(row[2] if row[2] is not None else -1)
            event_count = int(row[3] if row[3] is not None else 0)
            total_ns = int(row[4] if row[4] is not None else 0)
            start_ns = int(row[5] if row[5] is not None else 0)
            end_ns = int(row[6] if row[6] is not None else 0)

            task_type = sid_to_value.get(task_type_id, str(task_type_id))
            task_type_norm = _normalize_task_type(task_type)
            task_key = _normalize_task_key(task_type_norm)
            if task_key == "CAPTURE_WAIT":
                continue

            category = _classify_task(task_type_norm)
            if category not in {"wait", "comm", "exec"}:
                continue

            key = (device_id, stream_id)
            bucket = buckets.setdefault(key, _new_stream_rank_bucket())
            bucket["event_count"] = int(bucket["event_count"]) + event_count
            bucket["total_ns"] = int(bucket["total_ns"]) + total_ns
            bucket[f"{category}_ns"] = int(bucket[f"{category}_ns"]) + total_ns
            cur_min = bucket.get("min_start_ns")
            cur_max = bucket.get("max_end_ns")
            bucket["min_start_ns"] = start_ns if cur_min is None else min(int(cur_min), start_ns)
            bucket["max_end_ns"] = end_ns if cur_max is None else max(int(cur_max), end_ns)

    out: List[StreamSelection] = []
    for (device_id, stream_id), bucket in buckets.items():
        stats = _stream_ranking_stats_from_bucket(
            db_idx=db_idx,
            db_path=db_path,
            device_id=device_id,
            stream_id=stream_id,
            bucket=bucket,
        )
        out.append(
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
    return out


def _rank_streams_global(db_paths: Sequence[Path]) -> Tuple[List[StreamSelection], List[Dict[str, object]]]:
    selections: List[StreamSelection] = []
    for db_idx, db_path in enumerate(db_paths, start=1):
        selections.extend(_load_stream_ranking_stats(db_path, db_idx=db_idx))

    selections.sort(
        key=lambda s: (
            float(s.stats["total_dur_us"]),
            float(s.stats["busy_time_us"]),
            int(s.stats["event_count"]),
        ),
        reverse=True,
    )

    ranked: List[StreamSelection] = []
    ranking_rows: List[Dict[str, object]] = []
    for rank, sel in enumerate(selections, start=1):
        stats = dict(sel.stats)
        stats["global_rank"] = rank
        ranked_sel = StreamSelection(
            global_rank=rank,
            db_idx=sel.db_idx,
            db_path=sel.db_path,
            device_id=sel.device_id,
            stream_id=sel.stream_id,
            events=sel.events,
            stats=stats,
        )
        ranked.append(ranked_sel)
        ranking_rows.append(stats)
    return ranked, ranking_rows


def _select_streams_for_analysis(
    ranked_streams: Sequence[StreamSelection],
    *,
    top_streams_global: int,
    top_streams_per_db: int,
) -> List[StreamSelection]:
    if top_streams_global > 0:
        return list(ranked_streams[:top_streams_global])

    selected: List[StreamSelection] = []
    per_db_count: Dict[int, int] = {}
    for sel in ranked_streams:
        used = per_db_count.get(sel.db_idx, 0)
        if used >= top_streams_per_db:
            continue
        selected.append(sel)
        per_db_count[sel.db_idx] = used + 1
    return selected
