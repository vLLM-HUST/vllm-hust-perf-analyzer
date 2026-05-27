from __future__ import annotations

import argparse
import bisect
import copy
import csv
import json
import math
import sqlite3
import time
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple

from .io.discover import discover_msprof_dbs
from .loop_tree import (
    MacroDef,
    _build_tree_v2,
    _symbol_name,
)
from .msprof_reader import (
    StreamEvent,
    _canonical_label,
    _load_device_events,
    _load_string_ids,
    _normalize_task_key,
    _rank_streams_global,
)


@dataclass(frozen=True)
class ComputePreludeConfig:
    top_devices_global: int = 0
    max_main_events_per_device: int = 5_000
    max_macro_defs: int = 32
    collective_episode_gap_us: float = 5_000.0
    min_main_event_us: float = 0.0
    top_prelude_labels: int = 8
    readable_macro_mode: str = "inline"
    kernel_role_file: Path | None = None
    summary_top_loops: int = 12
    device_ids: Tuple[int, ...] | None = None


LOOP_PROMOTION_METHODS = [
    "pair_grammar_macro_discovery",
    "adjacent_identical_macro_runs",
]


@dataclass(frozen=True)
class DeviceSelection:
    global_rank: int
    db_idx: int
    db_path: Path
    device_id: int
    main_event_count: int
    exec_us: float
    data_move_us: float
    total_main_us: float


@dataclass(frozen=True)
class MainEvent:
    event: StreamEvent
    role: str
    symbol: str
    source_global_task_ids: Tuple[int, ...] = ()
    source_stream_ids: Tuple[int, ...] = ()


@dataclass(frozen=True)
class KernelRoleOverride:
    role: str
    symbol: str = ""
    label: str = ""
    task_type: str = ""
    family: str = ""
    contains: str = ""


@dataclass(frozen=True)
class GrammarToken:
    name: str
    start_ns: int
    end_ns: int


def _write_csv(path: Path, rows: Sequence[Dict[str, object]]) -> None:
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fields: List[str] = []
    seen: set[str] = set()
    for row in rows:
        for key in row.keys():
            if key not in seen:
                seen.add(key)
                fields.append(key)
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def _mean(values: Sequence[float]) -> float:
    return sum(values) / len(values) if values else 0.0


def _q95(values: Sequence[float]) -> float:
    if not values:
        return 0.0
    xs = sorted(values)
    idx = min(len(xs) - 1, int(math.ceil(0.95 * len(xs))) - 1)
    return xs[idx]


def _task_key(ev: StreamEvent) -> str:
    return _normalize_task_key(ev.task_type)


def _is_memcpy_like(ev: StreamEvent) -> bool:
    key = _task_key(ev)
    label = ev.label.lower()
    return "MEMCPY" in key or "memcpy" in label or "tensorcopy" in label


COLLECTIVE_FAMILIES = {"allreduce", "allgather", "alltoall", "reducescatter", "broadcast"}


def _is_device_allreduce_kernel_label(text: str) -> bool:
    low = text.lower()
    return (
        "aiv_all_reduce" in low
        or "aiv_allreduce" in low
        or "aic_all_reduce" in low
        or "aic_allreduce" in low
        or "all_reduce_bfloat16" in low
        or "allreduce_bfloat16" in low
    )


def _is_hccl_sync_kernel_label(text: str) -> bool:
    low = text.lower()
    return (
        "hccl_aiv_sync" in low
        or "hccl_aic_sync" in low
        or "aiv_sync" in low
        or "aic_sync" in low
    )


def _is_alltoall_label(text: str) -> bool:
    return (
        "alltoall" in text
        or "all_to_all" in text
        or "all-to-all" in text
        or "all2all" in text
        or "all#all" in text
        or "a2a" in text
        or "a#a" in text
    )


def _is_collective_like(ev: StreamEvent) -> bool:
    low = ev.label.lower()
    return any(
        name in low
        for name in (
            "allreduce",
            "all_reduce",
            "allgather",
            "all_gather",
            "reducescatter",
            "reduce_scatter",
            "broadcast",
        )
    ) or _is_alltoall_label(low) or _is_device_allreduce_kernel_label(low)


def _label_family(label: str, category: str) -> str:
    label = _canonical_label(label, category=category)
    low = label.lower()
    if "allreduce" in low or "all_reduce" in low or _is_device_allreduce_kernel_label(low):
        return "allreduce"
    if "allgather" in low or "all_gather" in low:
        return "allgather"
    if _is_alltoall_label(low):
        return "alltoall"
    if "reducescatter" in low or "reduce_scatter" in low:
        return "reducescatter"
    if "broadcast" in low:
        return "broadcast"
    if "matmul" in low:
        return "matmul"
    if "rmsnorm" in low or "layernorm" in low:
        return "norm"
    if "pagedattention" in low or "attention" in low:
        return "attention"
    if "swiglu" in low:
        return "swiglu"
    if "rope" in low or "rotary" in low:
        return "rope"
    if "copy" in low or "memcpy" in low or "tensor move" in low:
        return "data_move"
    if "event wait" in low:
        return "event_wait"
    if "event record" in low:
        return "event_record"
    return label[:64].strip().lower().replace(" ", "_")


def _main_event_key(ev: StreamEvent, role: str) -> Tuple[str, str, str]:
    family = _label_family(ev.label, ev.category)
    if role in {"collective", "data_move"}:
        return (role, family, _task_key(ev))
    return (role, ev.label, _task_key(ev))


def _stream_role_stats(events_by_stream: Dict[Tuple[int, int], List[StreamEvent]]) -> Dict[int, Dict[str, object]]:
    stats: Dict[int, Dict[str, object]] = {}
    for (_device_id, stream_id), events in events_by_stream.items():
        bucket = stats.setdefault(
            stream_id,
            {
                "stream_id": stream_id,
                "event_count": 0,
                "exec_count": 0,
                "exec_us": 0.0,
                "comm_us": 0.0,
                "wait_us": 0.0,
            },
        )
        for ev in events:
            bucket["event_count"] = int(bucket["event_count"]) + 1
            dur_us = ev.dur_ns / 1000.0
            if ev.category == "exec":
                bucket["exec_count"] = int(bucket["exec_count"]) + 1
                bucket["exec_us"] = float(bucket["exec_us"]) + dur_us
            elif ev.category == "comm":
                bucket["comm_us"] = float(bucket["comm_us"]) + dur_us
            elif ev.category == "wait":
                bucket["wait_us"] = float(bucket["wait_us"]) + dur_us
    return stats


def _is_main_event(ev: StreamEvent, stream_stats: Dict[int, Dict[str, object]], cfg: ComputePreludeConfig) -> bool:
    if ev.dur_ns / 1000.0 < cfg.min_main_event_us:
        return False
    if ev.category == "exec":
        return True
    if ev.category != "comm":
        return False
    if _is_collective_like(ev):
        return True
    if not _is_memcpy_like(ev):
        return False
    if _is_collective_like(ev):
        return False
    stats = stream_stats.get(ev.stream_id, {})
    return int(stats.get("exec_count", 0)) > 0


def _main_role(ev: StreamEvent) -> str:
    if ev.category == "exec" and _is_device_allreduce_kernel_label(ev.label):
        return "collective"
    if ev.category == "exec":
        return "compute"
    if ev.category == "comm" and _is_collective_like(ev):
        return "collective"
    return "data_move"


def _source_global_task_ids(item: MainEvent) -> Tuple[int, ...]:
    if item.source_global_task_ids:
        return item.source_global_task_ids
    return (item.event.global_task_id,)


def _collective_episode_key(ev: StreamEvent) -> Tuple[str, str]:
    return (_label_family(ev.label, ev.category), ev.label)


def _coalesce_collective_episodes(
    device_events: Sequence[StreamEvent],
    *,
    gap_us: float,
) -> List[Tuple[StreamEvent, Tuple[int, ...], Tuple[int, ...], int]]:
    candidates = [ev for ev in device_events if ev.category == "comm" and _is_collective_like(ev)]
    if not candidates:
        return []

    gap_ns = max(0, int(gap_us * 1000.0))
    active: Dict[Tuple[str, str], List[StreamEvent]] = {}
    episodes: List[List[StreamEvent]] = []

    def finish(key: Tuple[str, str]) -> None:
        group = active.pop(key, [])
        episodes.append(group)

    for ev in candidates:
        key = _collective_episode_key(ev)
        group = active.get(key)
        if group and ev.start_ns - max(item.end_ns for item in group) > gap_ns:
            finish(key)
            group = None
        if group is None:
            active[key] = [ev]
        else:
            group.append(ev)

    for key in list(active):
        finish(key)

    out: List[Tuple[StreamEvent, Tuple[int, ...], Tuple[int, ...], int]] = []
    for group in episodes:
        first_comm = min(group, key=lambda ev: (ev.start_ns, ev.end_ns, ev.stream_id))
        start_ns = min(ev.start_ns for ev in group)
        end_ns = max(ev.end_ns for ev in group)
        stream_dur: Dict[int, int] = {}
        for ev in group:
            stream_dur[ev.stream_id] = stream_dur.get(ev.stream_id, 0) + ev.dur_ns
        primary_stream = max(stream_dur.items(), key=lambda kv: (kv[1], -kv[0]))[0]
        source_ids = tuple(sorted({ev.global_task_id for ev in group if ev.global_task_id >= 0}))
        source_streams = tuple(sorted({ev.stream_id for ev in group}))
        synthetic = StreamEvent(
            start_ns=start_ns,
            end_ns=end_ns,
            device_id=first_comm.device_id,
            stream_id=primary_stream,
            task_id=-1,
            global_task_id=min(source_ids) if source_ids else first_comm.global_task_id,
            connection_id=first_comm.connection_id,
            task_type="COLLECTIVE_EPISODE",
            label=first_comm.label,
            category="comm",
        )
        out.append((synthetic, source_ids, source_streams, len(group)))

    out.sort(key=lambda item: (item[0].start_ns, item[0].end_ns, item[0].stream_id))
    return out


def _is_transparent_main_event(item: MainEvent) -> bool:
    key = _task_key(item.event)
    family = _label_family(item.event.label, item.event.category)
    return key in {"MODEL_MAINTAINCE", "MODEL_MAINTENANCE"} or family in {
        "model_maintaince",
        "model_maintenance",
    }


def _normalize_kernel_role(role: str) -> str:
    low = role.strip().lower().replace("-", "_")
    if low in {"main", "primary", "compute", "reliable", "anchor"}:
        return "anchor"
    if low in {"helper", "semi_noise", "semi_noise_aux", "auxiliary", "aux"}:
        return "aux"
    if low in {"noise", "control", "ignore", "transparent"}:
        return "transparent"
    return ""


def _load_kernel_role_overrides(path: Path | None) -> List[KernelRoleOverride]:
    if path is None:
        return []
    overrides: List[KernelRoleOverride] = []
    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            role = _normalize_kernel_role(str(row.get("semantic_role") or row.get("role") or ""))
            if not role:
                continue
            overrides.append(
                KernelRoleOverride(
                    role=role,
                    symbol=str(row.get("symbol") or "").strip(),
                    label=str(row.get("label") or "").strip(),
                    task_type=_normalize_task_key(str(row.get("task_type") or "")),
                    family=str(row.get("family") or "").strip().lower(),
                    contains=str(row.get("contains") or row.get("label_contains") or "").strip().lower(),
                )
            )
    return overrides


def _match_kernel_role_override(
    item: MainEvent,
    override: KernelRoleOverride,
) -> bool:
    ev = item.event
    label = ev.label.strip()
    task_type = _task_key(ev)
    family = _label_family(ev.label, ev.category)
    low_blob = f"{label} {ev.task_type} {family}".lower()
    if override.symbol and override.symbol != item.symbol:
        return False
    if override.label and override.label != label:
        return False
    if override.task_type and override.task_type != task_type:
        return False
    if override.family and override.family != family:
        return False
    if override.contains and override.contains not in low_blob:
        return False
    return any((override.symbol, override.label, override.task_type, override.family, override.contains))


def _default_aux_reason(item: MainEvent) -> str:
    ev = item.event
    task_key = _task_key(ev)
    if task_key in {"AI_CORE", "MODEL_EXECUTE"}:
        return f"aux_control_task:{task_key.lower()}"
    if item.role == "data_move":
        return "aux_data_move"
    low = f"{ev.label} {ev.task_type} {_label_family(ev.label, ev.category)}".lower()
    if _is_hccl_sync_kernel_label(low):
        return "aux_hccl_sync_kernel"
    aux_keywords = (
        "memcpy",
        "tensorcopy",
        "copy",
        "fill",
        "zeroslike",
        "zero",
        "oneslike",
        "one_",
        "cast",
        "slice",
        "tile",
        "gather",
        "scatter",
        "reshape",
        "transpose",
        "expand",
        "broadcastto",
        "arange",
        "range",
        "realdiv",
        "div",
        "pow",
        "reciprocal",
        "cos",
        "sin",
        "concat",
        "cat",
        "quant",
        "add",
        "sub",
        "greaterequal",
        "less",
        "logical",
        "bitwise",
        "index",
        "argmax",
    )
    for keyword in aux_keywords:
        if keyword in low:
            return f"aux_keyword:{keyword}"
    return ""


def _default_anchor_compute_reason(item: MainEvent) -> str:
    if item.role != "compute":
        return ""
    ev = item.event
    family = _label_family(ev.label, ev.category)
    if family in {"matmul", "norm", "attention", "swiglu", "rope"}:
        return f"anchor_family:{family}"
    low = f"{ev.label} {ev.task_type} {family}".lower()
    anchor_keywords = (
        "matmul",
        "batchmatmul",
        "gemm",
        "conv",
        "flashattention",
        "fusedinferattention",
        "pagedattention",
        "attention",
        "rmsnorm",
        "layernorm",
        "swiglu",
        "siluandmul",
        "moe",
        "ffn",
        "rotary",
        "rope",
    )
    for keyword in anchor_keywords:
        if keyword in low:
            return f"anchor_keyword:{keyword}"
    return ""


def _classify_kernel_role(
    item: MainEvent,
    overrides: Sequence[KernelRoleOverride],
) -> Tuple[str, str]:
    for override in overrides:
        if _match_kernel_role_override(item, override):
            return override.role, "override"
    if item.role == "collective":
        return "anchor", "anchor_collective"
    if _is_transparent_main_event(item):
        return "transparent", "transparent_model_maintenance"
    anchor_reason = _default_anchor_compute_reason(item)
    if anchor_reason:
        return "anchor", anchor_reason
    aux_reason = _default_aux_reason(item)
    if aux_reason:
        return "aux", aux_reason
    if item.role == "compute":
        return "aux", "aux_default_compute_non_anchor"
    return "aux", "aux_default_non_anchor"


def _apply_kernel_roles(
    *,
    main_events: Sequence[MainEvent],
    step_rows: List[Dict[str, object]],
    symbol_rows: List[Dict[str, object]],
    overrides: Sequence[KernelRoleOverride],
) -> Tuple[List[str], List[str], List[Dict[str, object]]]:
    semantic_roles: List[str] = []
    semantic_reasons: List[str] = []
    role_by_symbol: Dict[str, Counter[str]] = {}
    reason_by_symbol: Dict[str, Counter[str]] = {}
    for idx, item in enumerate(main_events):
        role, reason = _classify_kernel_role(item, overrides)
        semantic_roles.append(role)
        semantic_reasons.append(reason)

    for idx, (item, role, reason) in enumerate(zip(main_events, semantic_roles, semantic_reasons)):
        role_by_symbol.setdefault(item.symbol, Counter())[role] += 1
        reason_by_symbol.setdefault(item.symbol, Counter())[reason] += 1
        if idx < len(step_rows):
            step_rows[idx]["semantic_role"] = role
            step_rows[idx]["semantic_role_reason"] = reason

    for row in symbol_rows:
        symbol = str(row.get("symbol", ""))
        role_counts = role_by_symbol.get(symbol, Counter())
        reason_counts = reason_by_symbol.get(symbol, Counter())
        role = role_counts.most_common(1)[0][0] if role_counts else ""
        reason = reason_counts.most_common(1)[0][0] if reason_counts else ""
        row["semantic_role"] = role
        row["semantic_role_reason"] = reason
        row["semantic_role_counts"] = " ".join(f"{k}:{v}" for k, v in sorted(role_counts.items()))

    role_rows: List[Dict[str, object]] = []
    for row in sorted(symbol_rows, key=lambda r: (str(r.get("semantic_role", "")), str(r.get("symbol", "")))):
        role_rows.append(
            {
                "symbol": row.get("symbol", ""),
                "semantic_role": row.get("semantic_role", ""),
                "semantic_role_reason": row.get("semantic_role_reason", ""),
                "role": row.get("role", ""),
                "category": row.get("category", ""),
                "task_type": row.get("task_type", ""),
                "label": row.get("label", ""),
                "family": row.get("family", ""),
                "window_count": row.get("window_count", 0),
                "total_us": row.get("total_us", 0.0),
                "streams": row.get("streams", ""),
            }
        )
    return semantic_roles, semantic_reasons, role_rows


def _collect_device_events(events_by_stream: Dict[Tuple[int, int], List[StreamEvent]], device_id: int) -> List[StreamEvent]:
    events: List[StreamEvent] = []
    for (dev, _stream), stream_events in events_by_stream.items():
        if dev == device_id:
            events.extend(stream_events)
    events.sort(key=lambda e: (e.start_ns, e.end_ns, e.stream_id, e.global_task_id))
    return events


def _load_communication_op_events(
    db_path: Path,
    device_id: int,
) -> List[Tuple[StreamEvent, Tuple[int, ...], Tuple[int, ...], int]]:
    out: List[Tuple[StreamEvent, Tuple[int, ...], Tuple[int, ...], int]] = []
    with sqlite3.connect(str(db_path)) as conn:
        if not conn.execute(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='COMMUNICATION_OP'"
        ).fetchone():
            return out

        sid_to_value = _load_string_ids(conn)
        rows = list(
            conn.execute(
                "SELECT opName, startNs, endNs, connectionId, opId "
                "FROM COMMUNICATION_OP "
                "WHERE deviceId = ? AND startNs IS NOT NULL AND endNs IS NOT NULL AND endNs > startNs "
                "ORDER BY startNs, endNs, connectionId",
                (device_id,),
            )
        )
        for op_name_id, start_raw, end_raw, connection_raw, op_id_raw in rows:
            start_ns = int(start_raw if start_raw is not None else 0)
            end_ns = int(end_raw if end_raw is not None else 0)
            connection_id = int(connection_raw if connection_raw is not None else -1)
            op_id = int(op_id_raw if op_id_raw is not None else -1)
            label_raw = sid_to_value.get(int(op_name_id), str(op_name_id))
            label = _canonical_label(label_raw, category="comm")
            family = _label_family(label, "comm")
            if family not in COLLECTIVE_FAMILIES:
                continue

            source_ids: set[int] = set()
            source_streams: set[int] = set()
            stream_dur: Dict[int, int] = {}
            source_event_count = 0
            child_rows = conn.execute(
                "SELECT streamId, globalTaskId, startNs, endNs "
                "FROM TASK "
                "WHERE deviceId = ? AND connectionId = ? "
                "AND startNs IS NOT NULL AND endNs IS NOT NULL "
                "AND startNs <= ? AND endNs >= ?",
                (device_id, connection_id, end_ns, start_ns),
            )
            for stream_raw, gid_raw, child_start_raw, child_end_raw in child_rows:
                source_event_count += 1
                stream_id = int(stream_raw if stream_raw is not None else -1)
                gid = int(gid_raw if gid_raw is not None else -1)
                child_start = int(child_start_raw if child_start_raw is not None else 0)
                child_end = int(child_end_raw if child_end_raw is not None else child_start)
                if gid >= 0:
                    source_ids.add(gid)
                if stream_id >= 0:
                    source_streams.add(stream_id)
                    stream_dur[stream_id] = stream_dur.get(stream_id, 0) + max(0, child_end - child_start)

            primary_stream = -1
            if stream_dur:
                primary_stream = max(stream_dur.items(), key=lambda kv: (kv[1], -kv[0]))[0]
            elif source_streams:
                primary_stream = min(source_streams)

            synthetic = StreamEvent(
                start_ns=start_ns,
                end_ns=end_ns,
                device_id=device_id,
                stream_id=primary_stream,
                task_id=op_id,
                global_task_id=-1,
                connection_id=connection_id,
                task_type="COMMUNICATION_OP",
                label=label,
                category="comm",
            )
            out.append((synthetic, tuple(sorted(source_ids)), tuple(sorted(source_streams)), source_event_count))
    return out


def _build_main_events(
    *,
    device_events: Sequence[StreamEvent],
    communication_op_events: Sequence[Tuple[StreamEvent, Tuple[int, ...], Tuple[int, ...], int]],
    stream_stats: Dict[int, Dict[str, object]],
    cfg: ComputePreludeConfig,
) -> Tuple[List[MainEvent], List[Dict[str, object]]]:
    symbol_by_key: Dict[Tuple[str, str, str], str] = {}
    main_events: List[MainEvent] = []

    collective_events = (
        list(communication_op_events)
        if communication_op_events
        else _coalesce_collective_episodes(device_events, gap_us=cfg.collective_episode_gap_us)
    )
    for ev, source_ids, source_streams, _source_event_count in collective_events:
        key = _main_event_key(ev, "collective")
        symbol = symbol_by_key.get(key)
        if symbol is None:
            symbol = _symbol_name(len(symbol_by_key))
            symbol_by_key[key] = symbol
        main_events.append(
            MainEvent(
                event=ev,
                role="collective",
                symbol=symbol,
                source_global_task_ids=source_ids,
                source_stream_ids=source_streams,
            )
        )

    for ev in device_events:
        if not _is_main_event(ev, stream_stats, cfg):
            continue
        role = _main_role(ev)
        if role == "collective" and ev.category == "comm":
            continue
        key = _main_event_key(ev, role)
        symbol = symbol_by_key.get(key)
        if symbol is None:
            symbol = _symbol_name(len(symbol_by_key))
            symbol_by_key[key] = symbol
        main_events.append(MainEvent(event=ev, role=role, symbol=symbol))

    main_events.sort(key=lambda item: (item.event.start_ns, item.event.end_ns, item.event.stream_id))
    if cfg.max_main_events_per_device > 0:
        main_events = main_events[: cfg.max_main_events_per_device]

    symbol_rows_by_symbol: Dict[str, Dict[str, object]] = {}
    for item in main_events:
        ev = item.event
        row = symbol_rows_by_symbol.setdefault(
            item.symbol,
            {
                "symbol": item.symbol,
                "role": item.role,
                "category": ev.category,
                "task_type": ev.task_type,
                "label": ev.label,
                "family": _label_family(ev.label, ev.category),
                "window_count": 0,
                "total_us": 0.0,
                "prelude_total_us_avg": 0.0,
                "prelude_comm_us_avg": 0.0,
                "prelude_wait_us_avg": 0.0,
                "source_event_count": 0,
            },
        )
        row["window_count"] = int(row["window_count"]) + 1
        row["total_us"] = round(float(row["total_us"]) + ev.dur_ns / 1000.0, 3)
        row["source_event_count"] = int(row["source_event_count"]) + len(_source_global_task_ids(item))
    return main_events, list(symbol_rows_by_symbol.values())


def _top_counts(counter: Dict[str, float], limit: int) -> str:
    items = sorted(counter.items(), key=lambda kv: kv[1], reverse=True)[:limit]
    return " ".join(f"{name}:{round(value, 3)}" for name, value in items)


def _union_duration_us(intervals: Sequence[Tuple[int, int]]) -> float:
    if not intervals:
        return 0.0
    merged: List[Tuple[int, int]] = []
    for start, end in sorted(intervals):
        if end <= start:
            continue
        if not merged or start > merged[-1][1]:
            merged.append((start, end))
        else:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
    return round(sum(end - start for start, end in merged) / 1000.0, 3)


def _prelude_stats(
    *,
    events: Sequence[StreamEvent],
    starts: Sequence[int],
    start_ns: int,
    end_ns: int,
    main_global_task_ids: set[int],
    top_label_limit: int,
) -> Dict[str, object]:
    gap_us = max(0, end_ns - start_ns) / 1000.0
    if end_ns <= start_ns:
        return {
            "prelude_start_ns": start_ns,
            "prelude_end_ns": end_ns,
            "prelude_gap_us": 0.0,
            "prelude_active_union_us": 0.0,
            "prelude_idle_us": 0.0,
            "prelude_wait_us": 0.0,
            "prelude_comm_us": 0.0,
            "prelude_exec_aux_us": 0.0,
            "prelude_memcpy_us": 0.0,
            "prelude_event_count": 0,
            "prelude_stream_count": 0,
            "prelude_top_streams": "",
            "prelude_top_labels": "",
            "prelude_collective_hint": "",
        }

    lower = bisect.bisect_left(starts, start_ns)
    upper = bisect.bisect_left(starts, end_ns)
    return _prelude_stats_from_range(
        events=events,
        lower=lower,
        upper=upper,
        start_ns=start_ns,
        end_ns=end_ns,
        main_global_task_ids=main_global_task_ids,
        top_label_limit=top_label_limit,
    )


def _prelude_stats_from_range(
    *,
    events: Sequence[StreamEvent],
    lower: int,
    upper: int,
    start_ns: int,
    end_ns: int,
    main_global_task_ids: set[int],
    top_label_limit: int,
) -> Dict[str, object]:
    gap_us = max(0, end_ns - start_ns) / 1000.0
    if end_ns <= start_ns:
        return {
            "prelude_start_ns": start_ns,
            "prelude_end_ns": end_ns,
            "prelude_gap_us": 0.0,
            "prelude_active_union_us": 0.0,
            "prelude_idle_us": 0.0,
            "prelude_wait_us": 0.0,
            "prelude_comm_us": 0.0,
            "prelude_exec_aux_us": 0.0,
            "prelude_memcpy_us": 0.0,
            "prelude_event_count": 0,
            "prelude_stream_count": 0,
            "prelude_top_streams": "",
            "prelude_top_labels": "",
            "prelude_collective_hint": "",
        }

    active_intervals: List[Tuple[int, int]] = []
    stream_us: Dict[str, float] = {}
    label_us: Dict[str, float] = {}
    collective_us: Dict[str, float] = {}
    wait_us = 0.0
    comm_us = 0.0
    exec_aux_us = 0.0
    memcpy_us = 0.0
    event_count = 0

    # First version attributes events that start inside the prelude gap. Long
    # events that began before the previous main event are intentionally left to
    # the later overlap model.
    for i in range(lower, upper):
        ev = events[i]
        if ev.global_task_id in main_global_task_ids:
            continue
        overlap_start = max(start_ns, ev.start_ns)
        overlap_end = min(end_ns, ev.end_ns)
        if overlap_end <= overlap_start:
            continue
        dur_us = (overlap_end - overlap_start) / 1000.0
        active_intervals.append((overlap_start, overlap_end))
        stream_key = f"s{ev.stream_id}:{ev.category}"
        stream_us[stream_key] = stream_us.get(stream_key, 0.0) + dur_us
        family_key = f"{ev.category}:{_label_family(ev.label, ev.category)}"
        label_us[family_key] = label_us.get(family_key, 0.0) + dur_us
        event_count += 1

        if ev.category == "wait":
            wait_us += dur_us
        elif ev.category == "comm":
            comm_us += dur_us
        elif ev.category == "exec":
            exec_aux_us += dur_us
        if _is_memcpy_like(ev):
            memcpy_us += dur_us
        if _is_collective_like(ev):
            family = _label_family(ev.label, ev.category)
            collective_us[family] = collective_us.get(family, 0.0) + dur_us

    active_union_us = _union_duration_us(active_intervals)
    idle_us = max(0.0, gap_us - active_union_us)
    return {
        "prelude_start_ns": start_ns,
        "prelude_end_ns": end_ns,
        "prelude_gap_us": round(gap_us, 3),
        "prelude_active_union_us": active_union_us,
        "prelude_idle_us": round(idle_us, 3),
        "prelude_wait_us": round(wait_us, 3),
        "prelude_comm_us": round(comm_us, 3),
        "prelude_exec_aux_us": round(exec_aux_us, 3),
        "prelude_memcpy_us": round(memcpy_us, 3),
        "prelude_event_count": event_count,
        "prelude_stream_count": len({k.split(":", 1)[0] for k in stream_us}),
        "prelude_top_streams": _top_counts(stream_us, 6),
        "prelude_top_labels": _top_counts(label_us, top_label_limit),
        "prelude_collective_hint": _top_counts(collective_us, 3),
    }


def _build_steps(
    *,
    db_idx: int,
    db_path: Path,
    device_id: int,
    device_events: Sequence[StreamEvent],
    main_events: Sequence[MainEvent],
    cfg: ComputePreludeConfig,
) -> List[Dict[str, object]]:
    main_ids = {
        task_id
        for item in main_events
        for task_id in _source_global_task_ids(item)
        if task_id >= 0
    }
    rows: List[Dict[str, object]] = []
    lower = 0
    upper = 0

    for idx, item in enumerate(main_events):
        ev = item.event
        if idx == 0:
            prelude_start = ev.start_ns
            prev_end = ev.start_ns
        else:
            prev_end = main_events[idx - 1].event.end_ns
            prelude_start = min(prev_end, ev.start_ns)
        prelude_end = ev.start_ns
        while lower < len(device_events) and device_events[lower].start_ns < prelude_start:
            lower += 1
        if upper < lower:
            upper = lower
        while upper < len(device_events) and device_events[upper].start_ns < prelude_end:
            upper += 1
        stats = _prelude_stats_from_range(
            events=device_events,
            lower=lower,
            upper=upper,
            start_ns=prelude_start,
            end_ns=prelude_end,
            main_global_task_ids=main_ids,
            top_label_limit=cfg.top_prelude_labels,
        )
        prev_overlap_us = max(0, prev_end - ev.start_ns) / 1000.0
        gap_us = float(stats["prelude_gap_us"])
        rows.append(
            {
                "step_idx": idx + 1,
                "db_idx": db_idx,
                "db": str(db_path),
                "device_id": device_id,
                "symbol": item.symbol,
                "role": item.role,
                "stream_id": ev.stream_id,
                "task_type": ev.task_type,
                "label": ev.label,
                "family": _label_family(ev.label, ev.category),
                "source_event_count": len(_source_global_task_ids(item)),
                "source_streams": " ".join(str(s) for s in item.source_stream_ids),
                "start_ns": ev.start_ns,
                "end_ns": ev.end_ns,
                "dur_us": round(ev.dur_ns / 1000.0, 3),
                "prev_compute_overlap_us": round(prev_overlap_us, 3),
                "prelude_idle_ratio": round(float(stats["prelude_idle_us"]) / gap_us, 6) if gap_us else 0.0,
                "prelude_comm_ratio": round(float(stats["prelude_comm_us"]) / gap_us, 6) if gap_us else 0.0,
                "prelude_wait_ratio": round(float(stats["prelude_wait_us"]) / gap_us, 6) if gap_us else 0.0,
                **stats,
            }
        )
    return rows


def _augment_symbol_rows(symbol_rows: List[Dict[str, object]], step_rows: Sequence[Dict[str, object]]) -> List[Dict[str, object]]:
    by_symbol: Dict[str, List[Dict[str, object]]] = {}
    for row in step_rows:
        by_symbol.setdefault(str(row["symbol"]), []).append(row)

    out: List[Dict[str, object]] = []
    for row in symbol_rows:
        rows = by_symbol.get(str(row["symbol"]), [])
        row = dict(row)
        row["prelude_total_us_avg"] = round(_mean([float(r["prelude_gap_us"]) for r in rows]), 3)
        row["prelude_total_us_p95"] = round(_q95([float(r["prelude_gap_us"]) for r in rows]), 3)
        row["prelude_comm_us_avg"] = round(_mean([float(r["prelude_comm_us"]) for r in rows]), 3)
        row["prelude_wait_us_avg"] = round(_mean([float(r["prelude_wait_us"]) for r in rows]), 3)
        row["prelude_idle_us_avg"] = round(_mean([float(r["prelude_idle_us"]) for r in rows]), 3)
        streams: set[int] = set()
        for r in rows:
            raw_streams = str(r.get("source_streams", "")).strip()
            if raw_streams:
                for part in raw_streams.split():
                    streams.add(int(part))
            else:
                streams.add(int(r["stream_id"]))
        row["streams"] = " ".join(str(s) for s in sorted(streams)[:8])
        out.append(row)
    return out


def _macro_rows(defs: Sequence[MacroDef]) -> List[Dict[str, object]]:
    rows: List[Dict[str, object]] = []
    for d in defs:
        rows.append(
            {
                "name": d.name,
                "level": d.level,
                "definition": " ".join(d.tokens),
                "definition_len": d.definition_len,
                "replace_count": d.replace_count,
                "gain": d.gain,
                "first_pos": d.first_pos,
                "defs_covered": d.defs_covered,
            }
        )
    return rows


def _fold_adjacent_macro_loops(
    seq_tokens: Sequence[GrammarToken],
    defs: List[MacroDef],
    *,
    macro_id: int,
    max_macro_defs: int,
    min_repeat_count: int = 2,
) -> Tuple[List[GrammarToken], int]:
    """Fold adjacent equal bare macro refs into opaque loop macro refs.

    The loop ref is represented as a normal macro definition whose RHS is
    `[M] * k`, but the newly-created loop macro is excluded from later run
    folding. It remains a single symbol for subsequent pair-gain matching.
    """

    out = list(seq_tokens)
    macro_levels: Dict[str, str] = {d.name: d.level for d in defs}

    def has_budget() -> bool:
        return max_macro_defs <= 0 or len(defs) < max_macro_defs

    while has_budget() and len(out) >= min_repeat_count:
        candidates: Dict[Tuple[str, int], Dict[str, int]] = {}
        i = 0
        while i < len(out):
            name = out[i].name
            if name not in macro_levels or macro_levels.get(name) == "LP":
                i += 1
                continue
            j = i + 1
            while j < len(out) and out[j].name == name:
                j += 1
            run_len = j - i
            if run_len >= min_repeat_count:
                key = (name, run_len)
                row = candidates.setdefault(key, {"occurrences": 0, "first_pos": i})
                row["occurrences"] += 1
                row["first_pos"] = min(row["first_pos"], i)
            i = j

        if not candidates:
            break

        (source_name, run_len), best = max(
            candidates.items(),
            key=lambda kv: (
                kv[1]["occurrences"] * (kv[0][1] - 1),
                kv[0][1],
                kv[1]["occurrences"],
                -kv[1]["first_pos"],
                kv[0][0],
            ),
        )
        loop_gain = best["occurrences"] * (run_len - 1)
        if loop_gain <= 1:
            break

        loop_name = f"M{macro_id}"
        new_tokens: List[GrammarToken] = []
        windows: List[Tuple[int, int]] = []
        first_selected = -1
        replace_count = 0
        i = 0
        while i < len(out):
            name = out[i].name
            j = i + 1
            while j < len(out) and out[j].name == name:
                j += 1
            if name == source_name and j - i == run_len:
                start_ns = out[i].start_ns
                end_ns = out[j - 1].end_ns
                new_tokens.append(GrammarToken(name=loop_name, start_ns=start_ns, end_ns=end_ns))
                windows.append((start_ns, end_ns))
                if first_selected < 0:
                    first_selected = i
                replace_count += 1
            else:
                new_tokens.extend(out[i:j])
            i = j

        if replace_count <= 0:
            break

        defs.append(
            MacroDef(
                name=loop_name,
                level="LP",
                tokens=[source_name] * run_len,
                definition_len=run_len,
                replace_count=replace_count,
                gain=loop_gain,
                first_pos=first_selected,
                windows=windows,
                defs_covered=1,
            )
        )
        macro_levels[loop_name] = "LP"
        out = new_tokens
        macro_id += 1

    return out, macro_id


def _discover_pair_grammar_macros(
    symbol_seq: Sequence[str],
    atom_windows: Sequence[Tuple[int, int]],
    *,
    max_macro_defs: int,
) -> Tuple[List[str], List[MacroDef], List[MacroDef]]:
    if len(symbol_seq) < 2:
        return list(symbol_seq), [], []

    seq_tokens = [
        GrammarToken(name=s, start_ns=atom_windows[i][0], end_ns=atom_windows[i][1])
        for i, s in enumerate(symbol_seq)
    ]
    defs: List[MacroDef] = []
    macro_id = 1

    while (max_macro_defs <= 0 or len(defs) < max_macro_defs) and len(seq_tokens) >= 2:
        counts: Counter[Tuple[str, str]] = Counter(
            (seq_tokens[i].name, seq_tokens[i + 1].name)
            for i in range(len(seq_tokens) - 1)
            if seq_tokens[i].name != seq_tokens[i + 1].name
        )
        if not counts:
            break

        first_pos: Dict[Tuple[str, str], int] = {}
        for i in range(len(seq_tokens) - 1):
            pair = (seq_tokens[i].name, seq_tokens[i + 1].name)
            if pair[0] == pair[1]:
                continue
            first_pos.setdefault(pair, i)

        pair, count = max(
            counts.items(),
            key=lambda kv: (kv[1], -first_pos.get(kv[0], 10**9), kv[0]),
        )
        gain = count - 3
        if gain <= 0:
            break

        macro_name = f"M{macro_id}"
        new_tokens: List[GrammarToken] = []
        windows: List[Tuple[int, int]] = []
        first_selected = -1
        replace_count = 0
        i = 0
        while i < len(seq_tokens):
            if i + 1 >= len(seq_tokens) or (seq_tokens[i].name, seq_tokens[i + 1].name) != pair:
                new_tokens.append(seq_tokens[i])
                i += 1
                continue

            start_ns = seq_tokens[i].start_ns
            end_ns = seq_tokens[i + 1].end_ns
            new_tokens.append(GrammarToken(name=macro_name, start_ns=start_ns, end_ns=end_ns))
            windows.append((start_ns, end_ns))
            if first_selected < 0:
                first_selected = i
            replace_count += 1
            i += 2

        if replace_count < 4:
            break

        defs.append(
            MacroDef(
                name=macro_name,
                level="RP",
                tokens=[pair[0], pair[1]],
                definition_len=2,
                replace_count=replace_count,
                gain=replace_count - 3,
                first_pos=first_selected,
                windows=windows,
                defs_covered=0,
            )
        )
        seq_tokens = new_tokens
        macro_id += 1
        seq_tokens, macro_id = _fold_adjacent_macro_loops(
            seq_tokens,
            defs,
            macro_id=macro_id,
            max_macro_defs=max_macro_defs,
        )

    final_tokens = [t.name for t in seq_tokens]
    return final_tokens, defs, []


def _expand_macro_tokens(
    name: str,
    macro_def_tokens: Dict[str, List[str]],
    cache: Dict[str, List[str]],
) -> List[str]:
    cached = cache.get(name)
    if cached is not None:
        return cached
    tokens = macro_def_tokens.get(name, [])
    out: List[str] = []
    for tok in tokens:
        if tok in macro_def_tokens:
            out.extend(_expand_macro_tokens(tok, macro_def_tokens, cache))
        else:
            out.append(tok)
    cache[name] = out
    return out


def _expand_expr_tokens(
    tokens: Sequence[str],
    macro_def_tokens: Dict[str, List[str]],
) -> List[str]:
    cache: Dict[str, List[str]] = {}
    out: List[str] = []
    for tok in tokens:
        if tok in macro_def_tokens:
            out.extend(_expand_macro_tokens(tok, macro_def_tokens, cache))
        else:
            out.append(tok)
    return out


def _macro_edge_rows(macro_defs: Sequence[MacroDef]) -> List[Dict[str, object]]:
    macro_names = {d.name for d in macro_defs}
    rows: List[Dict[str, object]] = []
    for d in macro_defs:
        counts = Counter(d.tokens)
        for child, count in sorted(counts.items()):
            rows.append(
                {
                    "parent": d.name,
                    "parent_level": d.level,
                    "child": child,
                    "child_type": "macro" if child in macro_names else "atom",
                    "edge_count": count,
                }
            )
    return rows


def _prefix_sums(step_rows: Sequence[Dict[str, object]], field: str) -> List[float]:
    out = [0.0]
    total = 0.0
    for row in step_rows:
        total += float(row.get(field, 0.0))
        out.append(total)
    return out


def _range_sum(prefix: Sequence[float], left: int, right: int) -> float:
    left = max(0, min(left, len(prefix) - 1))
    right = max(left, min(right, len(prefix) - 1))
    return prefix[right] - prefix[left]


def _macro_metric_rows(
    *,
    macro_defs: Sequence[MacroDef],
    step_rows: Sequence[Dict[str, object]],
) -> List[Dict[str, object]]:
    macro_def_tokens = {d.name: list(d.tokens) for d in macro_defs}
    macro_names = set(macro_def_tokens)
    parent_counts: Dict[str, int] = {}
    child_macro_counts: Dict[str, int] = {}
    child_atom_counts: Dict[str, int] = {}
    for d in macro_defs:
        child_macro_counts[d.name] = sum(1 for tok in d.tokens if tok in macro_names)
        child_atom_counts[d.name] = sum(1 for tok in d.tokens if tok not in macro_names)
        for tok in d.tokens:
            if tok in macro_names:
                parent_counts[tok] = parent_counts.get(tok, 0) + 1

    starts = [int(r["start_ns"]) for r in step_rows]
    dur_prefix = _prefix_sums(step_rows, "dur_us")
    prelude_gap_prefix = _prefix_sums(step_rows, "prelude_gap_us")
    prelude_comm_prefix = _prefix_sums(step_rows, "prelude_comm_us")
    prelude_wait_prefix = _prefix_sums(step_rows, "prelude_wait_us")
    prelude_idle_prefix = _prefix_sums(step_rows, "prelude_idle_us")

    expand_cache: Dict[str, List[str]] = {}
    rows: List[Dict[str, object]] = []
    for d in macro_defs:
        inclusive_step_count = 0
        inclusive_dur_us = 0.0
        inclusive_prelude_gap_us = 0.0
        inclusive_prelude_comm_us = 0.0
        inclusive_prelude_wait_us = 0.0
        inclusive_prelude_idle_us = 0.0

        for start_ns, end_ns in d.windows:
            left = bisect.bisect_left(starts, start_ns)
            right = bisect.bisect_right(starts, end_ns)
            inclusive_step_count += max(0, right - left)
            inclusive_dur_us += _range_sum(dur_prefix, left, right)
            inclusive_prelude_gap_us += _range_sum(prelude_gap_prefix, left, right)
            inclusive_prelude_comm_us += _range_sum(prelude_comm_prefix, left, right)
            inclusive_prelude_wait_us += _range_sum(prelude_wait_prefix, left, right)
            inclusive_prelude_idle_us += _range_sum(prelude_idle_prefix, left, right)

        occurrence_count = len(d.windows)
        expanded = _expand_macro_tokens(d.name, macro_def_tokens, expand_cache)
        rows.append(
            {
                "name": d.name,
                "level": d.level,
                "definition": " ".join(d.tokens),
                "definition_len": d.definition_len,
                "expanded_len": len(expanded),
                "replace_count": d.replace_count,
                "occurrence_count": occurrence_count,
                "gain": d.gain,
                "parent_count": parent_counts.get(d.name, 0),
                "child_macro_count": child_macro_counts.get(d.name, 0),
                "child_atom_count": child_atom_counts.get(d.name, 0),
                "inclusive_step_count": inclusive_step_count,
                "inclusive_dur_us": round(inclusive_dur_us, 3),
                "inclusive_prelude_gap_us": round(inclusive_prelude_gap_us, 3),
                "inclusive_prelude_comm_us": round(inclusive_prelude_comm_us, 3),
                "inclusive_prelude_wait_us": round(inclusive_prelude_wait_us, 3),
                "inclusive_prelude_idle_us": round(inclusive_prelude_idle_us, 3),
                "avg_dur_us_per_occurrence": round(inclusive_dur_us / occurrence_count, 3)
                if occurrence_count
                else 0.0,
                "avg_prelude_comm_us_per_occurrence": round(inclusive_prelude_comm_us / occurrence_count, 3)
                if occurrence_count
                else 0.0,
            }
        )
    return rows


def _aux_kind_from_row(row: Dict[str, object]) -> str:
    family = str(row.get("family", "")).lower()
    label = str(row.get("label", "")).lower()
    task_type_raw = str(row.get("task_type", ""))
    task_type = task_type_raw.lower()
    task_key = _normalize_task_key(task_type_raw)
    blob = f"{family} {label} {task_type}"
    if task_key == "AI_CORE":
        return "ai_core"
    if task_key == "MODEL_EXECUTE":
        return "model_execute"
    if "memcpy" in blob:
        return "memcpy"
    if "copy" in blob:
        return "copy"
    if "fill" in blob:
        return "fill"
    if "zero" in blob or "oneslike" in blob or "one_" in blob:
        return "set_value"
    if "cast" in blob:
        return "cast"
    if any(k in blob for k in ("slice", "tile", "gather", "scatter", "reshape", "transpose", "expand")):
        return "shape"
    return family or "other"


def _build_anchor_aux_slots(
    *,
    main_events: Sequence[MainEvent],
    semantic_roles: Sequence[str],
    step_rows: Sequence[Dict[str, object]],
) -> Tuple[List[Dict[str, object]], List[Dict[str, object]]]:
    slots: List[Dict[str, object]] = []
    last_anchor_pos = -1
    anchor_idx = 0
    for pos, role in enumerate(semantic_roles):
        if role != "anchor":
            continue
        anchor_idx += 1
        aux_positions = [
            i
            for i in range(last_anchor_pos + 1, pos)
            if i < len(semantic_roles) and semantic_roles[i] == "aux"
        ]
        aux_rows = [step_rows[i] for i in aux_positions if i < len(step_rows)]
        kind_counts = Counter(_aux_kind_from_row(row) for row in aux_rows)
        family_counts = Counter(str(row.get("family", "")) for row in aux_rows)
        symbol_seq = [str(row.get("symbol", "")) for row in aux_rows]
        label_seq = [str(row.get("label", "")) for row in aux_rows]
        anchor_row = step_rows[pos]
        prev_anchor = main_events[last_anchor_pos] if last_anchor_pos >= 0 else None
        slots.append(
            {
                "anchor_idx": anchor_idx,
                "step_idx": anchor_row.get("step_idx", pos + 1),
                "anchor_symbol": anchor_row.get("symbol", ""),
                "anchor_label": anchor_row.get("label", ""),
                "anchor_family": anchor_row.get("family", ""),
                "anchor_stream_id": anchor_row.get("stream_id", ""),
                "anchor_start_ns": anchor_row.get("start_ns", 0),
                "anchor_dur_us": anchor_row.get("dur_us", 0.0),
                "prev_anchor_symbol": prev_anchor.symbol if prev_anchor is not None else "",
                "prev_anchor_label": prev_anchor.event.label if prev_anchor is not None else "",
                "aux_start_step_idx": step_rows[aux_positions[0]].get("step_idx", "") if aux_positions else "",
                "aux_end_step_idx": step_rows[aux_positions[-1]].get("step_idx", "") if aux_positions else "",
                "aux_event_count": len(aux_rows),
                "aux_compute_count": sum(1 for row in aux_rows if row.get("role") == "compute"),
                "aux_data_move_count": sum(1 for row in aux_rows if row.get("role") == "data_move"),
                "aux_dur_us": round(sum(float(row.get("dur_us", 0.0)) for row in aux_rows), 3),
                "aux_memcpy_count": kind_counts.get("memcpy", 0),
                "aux_ai_core_count": kind_counts.get("ai_core", 0),
                "aux_model_execute_count": kind_counts.get("model_execute", 0),
                "aux_copy_count": kind_counts.get("copy", 0),
                "aux_fill_count": kind_counts.get("fill", 0),
                "aux_cast_count": kind_counts.get("cast", 0),
                "aux_set_value_count": kind_counts.get("set_value", 0),
                "aux_shape_count": kind_counts.get("shape", 0),
                "aux_top_families": " ".join(f"{k}:{v}" for k, v in family_counts.most_common(8)),
                "aux_symbol_seq": " ".join(symbol_seq[:64]),
                "aux_label_seq": " | ".join(label_seq[:16]),
            }
        )
        last_anchor_pos = pos

    by_symbol: Dict[str, List[Dict[str, object]]] = {}
    for row in slots:
        by_symbol.setdefault(str(row["anchor_symbol"]), []).append(row)

    symbol_aux_rows: List[Dict[str, object]] = []
    for symbol, rows in sorted(by_symbol.items()):
        occurrence_count = len(rows)
        aux_counts = [float(r["aux_event_count"]) for r in rows]
        aux_durs = [float(r["aux_dur_us"]) for r in rows]
        symbol_aux_rows.append(
            {
                "anchor_symbol": symbol,
                "anchor_label": rows[0].get("anchor_label", ""),
                "anchor_family": rows[0].get("anchor_family", ""),
                "occurrence_count": occurrence_count,
                "aux_event_count_total": int(sum(aux_counts)),
                "aux_event_count_avg": round(_mean(aux_counts), 3),
                "aux_event_count_p95": round(_q95(aux_counts), 3),
                "aux_dur_us_total": round(sum(aux_durs), 3),
                "aux_dur_us_avg": round(_mean(aux_durs), 3),
                "aux_dur_us_p95": round(_q95(aux_durs), 3),
                "memcpy_count_avg": round(_mean([float(r["aux_memcpy_count"]) for r in rows]), 3),
                "ai_core_count_avg": round(_mean([float(r["aux_ai_core_count"]) for r in rows]), 3),
                "model_execute_count_avg": round(_mean([float(r["aux_model_execute_count"]) for r in rows]), 3),
                "copy_count_avg": round(_mean([float(r["aux_copy_count"]) for r in rows]), 3),
                "fill_count_avg": round(_mean([float(r["aux_fill_count"]) for r in rows]), 3),
                "cast_count_avg": round(_mean([float(r["aux_cast_count"]) for r in rows]), 3),
                "set_value_count_avg": round(_mean([float(r["aux_set_value_count"]) for r in rows]), 3),
                "shape_count_avg": round(_mean([float(r["aux_shape_count"]) for r in rows]), 3),
            }
        )
    return slots, symbol_aux_rows


def _anchor_macro_aux_metric_rows(
    *,
    macro_defs: Sequence[MacroDef],
    anchor_step_rows: Sequence[Dict[str, object]],
    aux_slot_rows: Sequence[Dict[str, object]],
) -> List[Dict[str, object]]:
    starts = [int(r["start_ns"]) for r in anchor_step_rows]
    field_names = (
        "aux_event_count",
        "aux_compute_count",
        "aux_data_move_count",
        "aux_dur_us",
        "aux_memcpy_count",
        "aux_ai_core_count",
        "aux_model_execute_count",
        "aux_copy_count",
        "aux_fill_count",
        "aux_cast_count",
        "aux_set_value_count",
        "aux_shape_count",
    )
    prefixes = {field: _prefix_sums(aux_slot_rows, field) for field in field_names}
    rows: List[Dict[str, object]] = []
    for d in macro_defs:
        totals = {field: 0.0 for field in field_names}
        inclusive_anchor_count = 0
        for start_ns, end_ns in d.windows:
            left = bisect.bisect_left(starts, start_ns)
            right = bisect.bisect_right(starts, end_ns)
            inclusive_anchor_count += max(0, right - left)
            for field in field_names:
                totals[field] += _range_sum(prefixes[field], left, right)
        occurrence_count = len(d.windows)
        rows.append(
            {
                "name": d.name,
                "level": d.level,
                "definition": " ".join(d.tokens),
                "definition_len": d.definition_len,
                "replace_count": d.replace_count,
                "gain": d.gain,
                "occurrence_count": occurrence_count,
                "inclusive_anchor_count": inclusive_anchor_count,
                "aux_event_count": int(totals["aux_event_count"]),
                "aux_compute_count": int(totals["aux_compute_count"]),
                "aux_data_move_count": int(totals["aux_data_move_count"]),
                "aux_dur_us": round(totals["aux_dur_us"], 3),
                "aux_memcpy_count": int(totals["aux_memcpy_count"]),
                "aux_ai_core_count": int(totals["aux_ai_core_count"]),
                "aux_model_execute_count": int(totals["aux_model_execute_count"]),
                "aux_copy_count": int(totals["aux_copy_count"]),
                "aux_fill_count": int(totals["aux_fill_count"]),
                "aux_cast_count": int(totals["aux_cast_count"]),
                "aux_set_value_count": int(totals["aux_set_value_count"]),
                "aux_shape_count": int(totals["aux_shape_count"]),
                "aux_event_count_per_occurrence": round(totals["aux_event_count"] / occurrence_count, 3)
                if occurrence_count
                else 0.0,
                "aux_dur_us_per_occurrence": round(totals["aux_dur_us"] / occurrence_count, 3)
                if occurrence_count
                else 0.0,
            }
        )
    return rows


def _parse_duration_hint(text: str) -> Dict[str, float]:
    out: Dict[str, float] = {}
    for part in text.split():
        if ":" not in part:
            continue
        name, value = part.rsplit(":", 1)
        try:
            out[name] = out.get(name, 0.0) + float(value)
        except ValueError:
            continue
    return out


def _node_atom_symbols(
    node: Dict[str, object],
    *,
    macro_def_tokens: Dict[str, List[str]],
    cache: Dict[str, List[str]],
) -> List[str]:
    node_type = str(node.get("type", ""))
    if node_type == "Atom":
        symbol = str(node.get("symbol", ""))
        return [symbol] if symbol else []
    if node_type == "MacroRef":
        name = str(node.get("name", ""))
        if name in cache:
            return cache[name]
        out: List[str] = []
        for tok in macro_def_tokens.get(name, []):
            if tok in macro_def_tokens:
                out.extend(_node_atom_symbols({"type": "MacroRef", "name": tok}, macro_def_tokens=macro_def_tokens, cache=cache))
            else:
                out.append(tok)
        cache[name] = out
        return out
    if node_type == "Repeat":
        count = int(node.get("count", 1))
        body = node.get("body", {})
        if not isinstance(body, dict):
            return []
        return _node_atom_symbols(body, macro_def_tokens=macro_def_tokens, cache=cache) * max(0, count)
    if node_type == "Seq":
        out: List[str] = []
        items = node.get("items", [])
        if isinstance(items, list):
            for item in items:
                if not isinstance(item, dict):
                    continue
                child = item.get("node", {})
                if isinstance(child, dict):
                    out.extend(_node_atom_symbols(child, macro_def_tokens=macro_def_tokens, cache=cache))
        return out
    return []


def _node_label(node: Dict[str, object]) -> str:
    node_type = str(node.get("type", ""))
    if node_type == "Atom":
        return f"{node.get('symbol', '')} {node.get('op_label', '')}".strip()
    if node_type == "MacroRef":
        return f"MacroRef {node.get('name', '')}"
    if node_type == "Repeat":
        body = node.get("body", {})
        body_label = _node_label(body) if isinstance(body, dict) else ""
        return f"Repeat x{int(node.get('count', 1))} {body_label}".strip()
    if node_type == "Seq":
        items = node.get("items", [])
        return f"Seq[{len(items) if isinstance(items, list) else 0}]"
    return node_type


def _summarize_anchor_span(
    *,
    step_rows: Sequence[Dict[str, object]],
    aux_slot_rows: Sequence[Dict[str, object]],
    start_idx: int,
    end_idx: int,
) -> Dict[str, object]:
    rows = list(step_rows[start_idx:end_idx])
    aux_rows = list(aux_slot_rows[start_idx:end_idx])
    collective_hint: Dict[str, float] = {}
    prelude_top: Dict[str, float] = {}
    for row in rows:
        for name, value in _parse_duration_hint(str(row.get("prelude_collective_hint", ""))).items():
            collective_hint[name] = collective_hint.get(name, 0.0) + value
        for name, value in _parse_duration_hint(str(row.get("prelude_top_labels", ""))).items():
            prelude_top[name] = prelude_top.get(name, 0.0) + value
    return {
        "anchor_start_idx": start_idx + 1 if rows else "",
        "anchor_end_idx": end_idx if rows else "",
        "anchor_count": len(rows),
        "compute_count": sum(1 for row in rows if row.get("role") == "compute"),
        "collective_count": sum(1 for row in rows if row.get("role") == "collective"),
        "data_move_count": sum(1 for row in rows if row.get("role") == "data_move"),
        "dur_us": round(sum(float(row.get("dur_us", 0.0)) for row in rows), 3),
        "compute_us": round(sum(float(row.get("dur_us", 0.0)) for row in rows if row.get("role") == "compute"), 3),
        "collective_us": round(sum(float(row.get("dur_us", 0.0)) for row in rows if row.get("role") == "collective"), 3),
        "data_move_us": round(sum(float(row.get("dur_us", 0.0)) for row in rows if row.get("role") == "data_move"), 3),
        "prelude_gap_us": round(sum(float(row.get("prelude_gap_us", 0.0)) for row in rows), 3),
        "prelude_comm_us": round(sum(float(row.get("prelude_comm_us", 0.0)) for row in rows), 3),
        "prelude_wait_us": round(sum(float(row.get("prelude_wait_us", 0.0)) for row in rows), 3),
        "prelude_idle_us": round(sum(float(row.get("prelude_idle_us", 0.0)) for row in rows), 3),
        "prelude_collective_hint": _top_counts(collective_hint, 3),
        "prelude_top_labels": _top_counts(prelude_top, 5),
        "aux_event_count": int(sum(float(row.get("aux_event_count", 0.0)) for row in aux_rows)),
        "aux_dur_us": round(sum(float(row.get("aux_dur_us", 0.0)) for row in aux_rows), 3),
        "aux_ai_core_count": int(sum(float(row.get("aux_ai_core_count", 0.0)) for row in aux_rows)),
        "aux_model_execute_count": int(sum(float(row.get("aux_model_execute_count", 0.0)) for row in aux_rows)),
        "aux_memcpy_count": int(sum(float(row.get("aux_memcpy_count", 0.0)) for row in aux_rows)),
        "aux_fill_count": int(sum(float(row.get("aux_fill_count", 0.0)) for row in aux_rows)),
        "aux_cast_count": int(sum(float(row.get("aux_cast_count", 0.0)) for row in aux_rows)),
    }


def _node_display_label(node: Dict[str, object]) -> str:
    node_type = str(node.get("type", ""))
    if node_type == "Atom":
        return str(node.get("op_label", "") or node.get("symbol", ""))
    if node_type == "Repeat":
        return f"Repeat x{int(node.get('count', 1))}"
    if node_type == "MacroRef":
        return f"MacroRef {node.get('name', '')}"
    if node_type == "Seq":
        items = node.get("items", [])
        return f"Seq[{len(items) if isinstance(items, list) else 0}]"
    return node_type or "node"


def _node_cost_kind(node: Dict[str, object]) -> str:
    node_type = str(node.get("type", ""))
    if node_type == "Atom":
        category = str(node.get("category", ""))
        if category == "comm":
            return "comm"
        if category == "exec":
            return "exec"
        return category or "atom"
    if node_type == "Repeat":
        return "repeat"
    if node_type == "MacroRef":
        return "macro"
    if node_type == "Seq":
        return "seq"
    return node_type.lower() or "node"


def _node_repeat_label(node: Dict[str, object]) -> str:
    if str(node.get("type", "")) == "Repeat":
        return f"x{int(node.get('count', 1))}"
    return ""


def _summarize_node_cost_occurrence(
    *,
    step_rows: Sequence[Dict[str, object]],
    aux_slot_rows: Sequence[Dict[str, object]],
    start_idx: int,
    end_idx: int,
) -> Dict[str, float]:
    rows = list(step_rows[start_idx:end_idx])
    aux_rows = list(aux_slot_rows[start_idx:end_idx])
    self_compute_us = sum(float(row.get("dur_us", 0.0)) for row in rows if row.get("role") == "compute")
    self_comm_us = sum(float(row.get("dur_us", 0.0)) for row in rows if row.get("role") == "collective")
    prelude_exec_aux_us = sum(float(row.get("prelude_exec_aux_us", 0.0)) for row in rows)
    prelude_comm_us = sum(float(row.get("prelude_comm_us", 0.0)) for row in rows)
    prelude_idle_us = sum(float(row.get("prelude_idle_us", 0.0)) for row in rows)
    compute_us = self_compute_us + prelude_exec_aux_us
    comm_us = self_comm_us + prelude_comm_us
    idle_us = prelude_idle_us
    total_us = compute_us + comm_us + idle_us
    return {
        "anchor_count": float(len(rows)),
        "compute_us": compute_us,
        "comm_us": comm_us,
        "idle_us": idle_us,
        "total_us": total_us,
        "self_anchor_us": self_compute_us + self_comm_us,
        "self_exec_us": self_compute_us,
        "self_comm_us": self_comm_us,
        "prelude_exec_aux_us": prelude_exec_aux_us,
        "prelude_comm_us": prelude_comm_us,
        "prelude_idle_us": prelude_idle_us,
        "aux_event_count": float(sum(float(row.get("aux_event_count", 0.0)) for row in aux_rows)),
        "aux_dur_us": sum(float(row.get("aux_dur_us", 0.0)) for row in aux_rows),
    }


def _augment_tree_node_cost_metrics(
    tree_payload: Dict[str, object],
    *,
    step_rows: Sequence[Dict[str, object]],
    aux_slot_rows: Sequence[Dict[str, object]],
    macro_def_tokens: Dict[str, List[str]],
) -> Tuple[List[Dict[str, object]], List[Dict[str, object]]]:
    root = tree_payload.get("root", {})
    if not isinstance(root, dict):
        return [], []

    node_records: Dict[str, Dict[str, object]] = {}
    occurrence_ranges: Dict[str, List[Tuple[int, int]]] = {}
    occurrence_contexts: Dict[str, List[str]] = {}
    counter = 0

    def ensure_node(node: Dict[str, object], *, depth: int, path: str) -> str:
        nonlocal counter
        node_id = str(node.get("node_id", "")).strip()
        if not node_id:
            counter += 1
            node_id = f"N{counter:03d}"
            node["node_id"] = node_id
        node_type = str(node.get("type", ""))
        structural_alias = node_type == "Seq" and path.endswith(".body")
        display_depth = max(0, depth - path.split(".").count("body"))
        node["tree_path"] = path
        node["tree_depth"] = depth
        if node_id not in node_records:
            node_records[node_id] = {
                "node_id": node_id,
                "path": path,
                "depth": depth,
                "display_depth": display_depth,
                "structural_alias": structural_alias,
                "kind": _node_cost_kind(node),
                "type": node_type,
                "symbol": str(node.get("symbol", "")),
                "label": _node_display_label(node),
                "category": str(node.get("category", "")),
                "repeat": _node_repeat_label(node),
            }
        return node_id

    def record_occurrence(node_id: str, start_idx: int, end_idx: int, context: str) -> None:
        occurrence_ranges.setdefault(node_id, []).append((start_idx, end_idx))
        occurrence_contexts.setdefault(node_id, []).append(context)

    def visit(node: Dict[str, object], cursor: int, *, depth: int, path: str, context: str) -> int:
        node_id = ensure_node(node, depth=depth, path=path)
        node_type = str(node.get("type", ""))
        start_idx = cursor

        if node_type == "Seq":
            items = node.get("items", [])
            if isinstance(items, list):
                for idx, item in enumerate(items, start=1):
                    if not isinstance(item, dict):
                        continue
                    child = item.get("node", {})
                    if isinstance(child, dict):
                        cursor = visit(
                            child,
                            cursor,
                            depth=depth + 1,
                            path=f"{path}.{idx}" if path else str(idx),
                            context=context,
                        )
            record_occurrence(node_id, start_idx, cursor, context)
            return cursor

        if node_type == "Repeat":
            repeat_count = max(0, int(node.get("count", 1)))
            body = node.get("body", {})
            if isinstance(body, dict):
                for repeat_idx in range(1, repeat_count + 1):
                    repeat_context = f"{context}/{node_id}#{repeat_idx}" if context else f"{node_id}#{repeat_idx}"
                    cursor = visit(
                        body,
                        cursor,
                        depth=depth + 1,
                        path=f"{path}.body",
                        context=repeat_context,
                    )
            record_occurrence(node_id, start_idx, cursor, context)
            return cursor

        if node_type == "Atom":
            cursor = min(len(step_rows), cursor + 1)
            record_occurrence(node_id, start_idx, cursor, context)
            return cursor

        if node_type == "MacroRef":
            symbols = _node_atom_symbols(node, macro_def_tokens=macro_def_tokens, cache={})
            cursor = min(len(step_rows), cursor + len(symbols))
            record_occurrence(node_id, start_idx, cursor, context)
            return cursor

        record_occurrence(node_id, start_idx, cursor, context)
        return cursor

    consumed = visit(root, 0, depth=0, path="root", context="")
    metric_rows: List[Dict[str, object]] = []
    link_rows: List[Dict[str, object]] = []
    for node_id, record in node_records.items():
        if record.get("structural_alias"):
            continue
        ranges = occurrence_ranges.get(node_id, [])
        contexts = occurrence_contexts.get(node_id, [])
        occurrence_count = len(ranges)
        totals: Dict[str, float] = {
            "anchor_count": 0.0,
            "compute_us": 0.0,
            "comm_us": 0.0,
            "idle_us": 0.0,
            "total_us": 0.0,
            "self_anchor_us": 0.0,
            "self_exec_us": 0.0,
            "self_comm_us": 0.0,
            "prelude_exec_aux_us": 0.0,
            "prelude_comm_us": 0.0,
            "prelude_idle_us": 0.0,
            "aux_event_count": 0.0,
            "aux_dur_us": 0.0,
        }
        first_anchor_idx = ""
        last_anchor_idx = ""
        for occ_idx, (start_idx, end_idx) in enumerate(ranges, start=1):
            summary = _summarize_node_cost_occurrence(
                step_rows=step_rows,
                aux_slot_rows=aux_slot_rows,
                start_idx=start_idx,
                end_idx=end_idx,
            )
            for key in totals:
                totals[key] += float(summary.get(key, 0.0))
            if end_idx > start_idx:
                if first_anchor_idx == "":
                    first_anchor_idx = start_idx + 1
                last_anchor_idx = end_idx
            link_rows.append(
                {
                    "node_id": node_id,
                    "path": record["path"],
                    "kind": record["kind"],
                    "symbol": record["symbol"],
                    "label": record["label"],
                    "occurrence_idx": occ_idx,
                    "repeat_context": contexts[occ_idx - 1] if occ_idx - 1 < len(contexts) else "",
                    "anchor_start_idx": start_idx + 1 if end_idx > start_idx else "",
                    "anchor_end_idx": end_idx if end_idx > start_idx else "",
                    "anchor_count": max(0, end_idx - start_idx),
                }
            )
        denom = float(occurrence_count or 1)
        compute_us = totals["compute_us"]
        comm_us = totals["comm_us"]
        idle_us = totals["idle_us"]
        total_us = totals["total_us"]
        row = dict(record)
        row.update(
            {
                "occurrence_count": occurrence_count,
                "anchor_count": int(totals["anchor_count"]),
                "anchors_per_occurrence": round(totals["anchor_count"] / denom, 3),
                "first_anchor_idx": first_anchor_idx,
                "last_anchor_idx": last_anchor_idx,
                "compute_us": round(compute_us, 3),
                "comm_us": round(comm_us, 3),
                "idle_us": round(idle_us, 3),
                "total_us": round(total_us, 3),
                "avg_compute_us": round(compute_us / denom, 3),
                "avg_comm_us": round(comm_us / denom, 3),
                "avg_idle_us": round(idle_us / denom, 3),
                "avg_total_us": round(total_us / denom, 3),
                "comm_pct": round(comm_us / total_us, 6) if total_us else 0.0,
                "idle_pct": round(idle_us / total_us, 6) if total_us else 0.0,
                "self_us": round(totals["self_anchor_us"], 3),
                "self_exec_us": round(totals["self_exec_us"], 3),
                "self_comm_us": round(totals["self_comm_us"], 3),
                "avg_self_us": round(totals["self_anchor_us"] / denom, 3),
                "avg_self_exec_us": round(totals["self_exec_us"] / denom, 3),
                "avg_self_comm_us": round(totals["self_comm_us"] / denom, 3),
                "aux_events": round(totals["aux_event_count"], 3),
                "aux_us": round(totals["aux_dur_us"], 3),
                "avg_aux_events": round(totals["aux_event_count"] / denom, 3),
                "avg_aux_us": round(totals["aux_dur_us"] / denom, 3),
            }
        )
        metric_rows.append(row)

    tree_payload["node_cost_metrics"] = metric_rows
    tree_payload["node_anchor_link_count"] = len(link_rows)
    tree_payload["node_cost_metrics_unconsumed_anchors"] = max(0, len(step_rows) - consumed)
    return metric_rows, link_rows


def _augment_root_item_metrics(
    tree_payload: Dict[str, object],
    *,
    step_rows: Sequence[Dict[str, object]],
    aux_slot_rows: Sequence[Dict[str, object]],
    macro_def_tokens: Dict[str, List[str]],
) -> List[Dict[str, object]]:
    root = tree_payload.get("root", {})
    if not isinstance(root, dict):
        return []
    items = root.get("items", [])
    if not isinstance(items, list):
        return []

    cursor = 0
    cache: Dict[str, List[str]] = {}
    rows: List[Dict[str, object]] = []
    for idx, item in enumerate(items, start=1):
        if not isinstance(item, dict):
            continue
        node = item.get("node", {})
        if not isinstance(node, dict):
            continue
        symbols = _node_atom_symbols(node, macro_def_tokens=macro_def_tokens, cache=cache)
        start_idx = cursor
        end_idx = min(len(step_rows), cursor + len(symbols))
        metrics = _summarize_anchor_span(
            step_rows=step_rows,
            aux_slot_rows=aux_slot_rows,
            start_idx=start_idx,
            end_idx=end_idx,
        )
        expected = [str(row.get("symbol", "")) for row in step_rows[start_idx:end_idx]]
        mismatch = len(symbols) != len(expected) or symbols[: len(expected)] != expected
        metrics["symbol_mismatch"] = bool(mismatch)
        node["span_metrics"] = metrics
        row = {"idx": idx, "node": _node_label(node), **metrics}
        rows.append(row)
        cursor = end_idx
    tree_payload["root_item_metrics"] = rows
    tree_payload["root_item_metrics_unconsumed_anchors"] = max(0, len(step_rows) - cursor)
    return rows


def _detect_macro_loop_chains(
    macro_def_tokens: Dict[str, List[str]],
    *,
    min_repeat_count: int = 4,
) -> List[Dict[str, object]]:
    """Detect grammar chains that are equivalent to base + X*N or X*N + base."""
    chains: List[Dict[str, object]] = []
    consumed: set[str] = set()
    for name, tokens in macro_def_tokens.items():
        if name in consumed or len(tokens) != 2:
            continue

        candidates: List[Dict[str, object]] = []
        for direction in ("append", "prepend"):
            if direction == "append":
                base, repeated = tokens
                if base not in macro_def_tokens:
                    continue
            else:
                repeated, base = tokens
                if base not in macro_def_tokens:
                    continue

            chain = [name]
            cur = name
            while True:
                if direction == "append":
                    next_names = [
                        candidate
                        for candidate, candidate_tokens in macro_def_tokens.items()
                        if len(candidate_tokens) == 2
                        and candidate_tokens[0] == cur
                        and candidate_tokens[1] == repeated
                    ]
                else:
                    next_names = [
                        candidate
                        for candidate, candidate_tokens in macro_def_tokens.items()
                        if len(candidate_tokens) == 2
                        and candidate_tokens[0] == repeated
                        and candidate_tokens[1] == cur
                    ]
                if len(next_names) != 1:
                    break
                cur = next_names[0]
                if cur in consumed:
                    break
                chain.append(cur)
            if len(chain) < min_repeat_count:
                continue
            loop_form = (
                f"{base} ({repeated}) x{len(chain)}"
                if direction == "append"
                else f"({repeated}) x{len(chain)} {base}"
            )
            candidates.append(
                {
                    "chain_start": chain[0],
                    "chain_end": chain[-1],
                    "base": base,
                    "repeated": repeated,
                    "repeat_count": len(chain),
                    "chain": " ".join(chain),
                    "direction": direction,
                    "loop_form": loop_form,
                }
            )
        if not candidates:
            continue
        best = max(candidates, key=lambda row: int(row["repeat_count"]))
        chain = str(best["chain"]).split()
        consumed.update(chain)
        chains.append(best)
    chains.sort(key=lambda row: int(row["repeat_count"]), reverse=True)
    return chains


def _unfold_macro_loop_chains(
    macro_def_tokens: Dict[str, List[str]],
    macro_rows: Sequence[Dict[str, object]],
) -> Tuple[Dict[str, List[str]], List[Dict[str, object]], List[Dict[str, object]]]:
    """Rewrite chain-shaped macro definitions into repeat-friendly token runs.

    The root expression stays compressed. Only macro definitions are rewritten,
    so _build_tree_v2 can discover Repeat nodes inside the macro IR before the
    readable output recursively inlines MacroRef nodes.
    """
    chains = _detect_macro_loop_chains(macro_def_tokens)
    if not chains:
        return {name: list(tokens) for name, tokens in macro_def_tokens.items()}, [], [dict(row) for row in macro_rows]

    unfolded = {name: list(tokens) for name, tokens in macro_def_tokens.items()}
    chain_by_macro: Dict[str, Dict[str, object]] = {}
    for row in chains:
        chain_names = str(row.get("chain", "")).split()
        base = str(row.get("base", ""))
        repeated = str(row.get("repeated", ""))
        direction = str(row.get("direction", "append"))
        for offset, name in enumerate(chain_names, start=1):
            if direction == "prepend":
                unfolded[name] = [repeated] * offset + [base]
            else:
                unfolded[name] = [base] + [repeated] * offset
            chain_by_macro[name] = row
        row["action"] = "unfolded_definition"
        repeat_count = int(row.get("repeat_count", 0))
        if direction == "prepend":
            row["chain_end_definition"] = " ".join([repeated] * repeat_count + [base])
        else:
            row["chain_end_definition"] = " ".join([base] + [repeated] * repeat_count)

    updated_rows: List[Dict[str, object]] = []
    for row in macro_rows:
        new_row = dict(row)
        name = str(new_row.get("name", ""))
        if name in chain_by_macro and name in unfolded:
            new_row["definition"] = " ".join(unfolded[name])
            new_row["definition_len"] = len(unfolded[name])
            new_row["view_transform"] = "loop_chain_unfolded"
        updated_rows.append(new_row)
    return unfolded, chains, updated_rows


def _tag_macro_loop_chain_rows(rows: Sequence[Dict[str, object]], source: str, action: str) -> List[Dict[str, object]]:
    out: List[Dict[str, object]] = []
    for row in rows:
        new_row = dict(row)
        new_row.setdefault("source", source)
        new_row.setdefault("action", action)
        out.append(new_row)
    return out


def _macro_ref_counts(
    *,
    final_expr_tokens: Sequence[str],
    macro_defs: Sequence[MacroDef],
) -> Tuple[Dict[str, int], Dict[str, int]]:
    macro_names = {d.name for d in macro_defs}
    root_counts = Counter(tok for tok in final_expr_tokens if tok in macro_names)
    parent_counts: Dict[str, int] = {}
    for d in macro_defs:
        for tok in d.tokens:
            if tok in macro_names:
                parent_counts[tok] = parent_counts.get(tok, 0) + 1
    return dict(root_counts), parent_counts


def _macro_view_keep_decision(
    *,
    root_ref_count: int,
    parent_count: int,
    expanded_len: int,
    occurrence_count: int,
    gain: int,
) -> Tuple[bool, str]:
    if root_ref_count == 0 and parent_count <= 1:
        return False, "private_chain_helper"
    if expanded_len <= 3:
        return False, "tiny"
    if gain <= 4 and root_ref_count <= 1 and parent_count <= 1:
        return False, "low_gain_private"
    if parent_count >= 2 and expanded_len >= 4:
        return True, "shared"
    if root_ref_count >= 2 and expanded_len >= 4:
        return True, "root_reused"
    if root_ref_count >= 1 and expanded_len >= 6:
        return True, "root_long"
    if expanded_len >= 8 and occurrence_count >= 4:
        return True, "long_frequent"
    return False, "private_helper"


def _build_macro_readable_view(
    *,
    macro_defs: Sequence[MacroDef],
    final_expr_tokens: Sequence[str],
    mode: str,
) -> Tuple[List[str], List[Dict[str, object]], Dict[str, List[str]], List[Dict[str, object]]]:
    macro_def_tokens = {d.name: list(d.tokens) for d in macro_defs}
    if mode == "inline":
        view_rows: List[Dict[str, object]] = []
        expand_cache: Dict[str, List[str]] = {}
        for d in macro_defs:
            expanded = _expand_macro_tokens(d.name, macro_def_tokens, expand_cache)
            view_rows.append(
                {
                    "name": d.name,
                    "action": "inline",
                    "reason": "readable_macro_mode_inline",
                    "root_ref_count": 0,
                    "parent_count": 0,
                    "expanded_len": len(expanded),
                    "occurrence_count": len(d.windows),
                    "gain": d.gain,
                    "raw_definition": " ".join(d.tokens),
                    "simplified_definition": " ".join(expanded),
                    "simplified_len": len(expanded),
                }
            )
        return _expand_expr_tokens(final_expr_tokens, macro_def_tokens), [], {}, view_rows

    root_counts, parent_counts = _macro_ref_counts(
        final_expr_tokens=final_expr_tokens,
        macro_defs=macro_defs,
    )
    expand_cache: Dict[str, List[str]] = {}

    keep: Dict[str, bool] = {}
    reasons: Dict[str, str] = {}
    expanded_lens: Dict[str, int] = {}
    for d in macro_defs:
        expanded = _expand_macro_tokens(d.name, macro_def_tokens, expand_cache)
        expanded_lens[d.name] = len(expanded)
        should_keep, reason = _macro_view_keep_decision(
            root_ref_count=root_counts.get(d.name, 0),
            parent_count=parent_counts.get(d.name, 0),
            expanded_len=len(expanded),
            occurrence_count=len(d.windows),
            gain=d.gain,
        )
        keep[d.name] = should_keep
        reasons[d.name] = reason

    macro_names = set(macro_def_tokens)

    def simplify_macro(name: str, cache: Dict[str, List[str]]) -> List[str]:
        cached = cache.get(name)
        if cached is not None:
            return cached
        out: List[str] = []
        for tok in macro_def_tokens.get(name, []):
            if tok in macro_def_tokens and not keep.get(tok, False):
                out.extend(simplify_macro(tok, cache))
            else:
                out.append(tok)
        cache[name] = out
        return out

    def build_visible_tokens(cache: Dict[str, List[str]]) -> List[str]:
        out: List[str] = []
        for tok in final_expr_tokens:
            if tok in macro_def_tokens and not keep.get(tok, False):
                out.extend(simplify_macro(tok, cache))
            else:
                out.append(tok)
        return out

    def visible_container_counts(cache: Dict[str, List[str]]) -> Dict[str, int]:
        counts: Dict[str, int] = {}
        for tok in set(build_visible_tokens(cache)):
            if tok in macro_names:
                counts[tok] = counts.get(tok, 0) + 1
        for d in macro_defs:
            if not keep.get(d.name, False):
                continue
            for tok in set(simplify_macro(d.name, cache)):
                if tok in macro_names and tok != d.name:
                    counts[tok] = counts.get(tok, 0) + 1
        return counts

    while True:
        pass_cache: Dict[str, List[str]] = {}
        visible_counts = visible_container_counts(pass_cache)
        changed = False
        for d in macro_defs:
            if not keep.get(d.name, False):
                continue
            if root_counts.get(d.name, 0) <= 1 and visible_counts.get(d.name, 0) <= 1:
                keep[d.name] = False
                reasons[d.name] = "single_visible_ref"
                changed = True
        if not changed:
            break

    simplify_cache: Dict[str, List[str]] = {}

    def simplify_visible_macro(name: str) -> List[str]:
        cached = simplify_cache.get(name)
        if cached is not None:
            return cached
        out: List[str] = []
        for tok in macro_def_tokens.get(name, []):
            if tok in macro_def_tokens and not keep.get(tok, False):
                out.extend(simplify_visible_macro(tok))
            else:
                out.append(tok)
        simplify_cache[name] = out
        return out

    visible_final_tokens: List[str] = []
    for tok in final_expr_tokens:
        if tok in macro_def_tokens and not keep.get(tok, False):
            visible_final_tokens.extend(simplify_visible_macro(tok))
        else:
            visible_final_tokens.append(tok)

    visible_macro_rows: List[Dict[str, object]] = []
    visible_macro_def_tokens: Dict[str, List[str]] = {}
    view_rows: List[Dict[str, object]] = []
    for d in macro_defs:
        simplified = simplify_visible_macro(d.name)
        action = "keep" if keep.get(d.name, False) else "inline"
        view_rows.append(
            {
                "name": d.name,
                "action": action,
                "reason": reasons.get(d.name, ""),
                "root_ref_count": root_counts.get(d.name, 0),
                "parent_count": parent_counts.get(d.name, 0),
                "expanded_len": expanded_lens.get(d.name, 0),
                "occurrence_count": len(d.windows),
                "gain": d.gain,
                "raw_definition": " ".join(d.tokens),
                "simplified_definition": " ".join(simplified),
                "simplified_len": len(simplified),
            }
        )
        if action != "keep":
            continue
        visible_macro_def_tokens[d.name] = simplified
        visible_macro_rows.append(
            {
                "name": d.name,
                "level": "VIEW",
                "definition": " ".join(simplified),
                "definition_len": len(simplified),
                "replace_count": d.replace_count,
                "gain": d.gain,
                "first_pos": d.first_pos,
                "defs_covered": d.defs_covered,
            }
        )

    return visible_final_tokens, visible_macro_rows, visible_macro_def_tokens, view_rows


def _render_compute_readable(
    tree_readable: str,
    *,
    selection: DeviceSelection,
    step_rows: Sequence[Dict[str, object]],
) -> str:
    lines = [
        "- stream_scope: `device_compute_sequence`" if line == "- stream_id: `-1`" else line
        for line in tree_readable.splitlines()
    ]
    lines.append("")
    lines.append("## Compute Prelude Summary")
    lines.append("")
    lines.append(f"- main_events: `{len(step_rows)}`")
    transparent_count = sum(
        1
        for row in step_rows
        if _normalize_task_key(str(row.get("task_type", ""))) in {"MODEL_MAINTAINCE", "MODEL_MAINTENANCE"}
        or str(row.get("family", "")) in {"model_maintaince", "model_maintenance"}
    )
    lines.append(f"- projected_main_events: `{len(step_rows) - transparent_count}`")
    lines.append(f"- transparent_main_events: `{transparent_count}`")
    lines.append(f"- exec_us: `{round(selection.exec_us, 3)}`")
    lines.append(f"- data_move_us: `{round(selection.data_move_us, 3)}`")
    lines.append(f"- collective_anchor_us: `{round(sum(float(r.get('dur_us', 0.0)) for r in step_rows if r.get('role') == 'collective'), 3)}`")
    return "\n".join(lines) + "\n"


def _render_inline_ast_lines(
    node: Dict[str, object],
    *,
    out: List[str],
    indent: str = "",
    prefix: str = "",
) -> None:
    t = str(node.get("type", ""))
    node_id = str(node.get("node_id", "")).strip()
    node_prefix = f"{node_id} " if node_id else ""
    if t == "Seq":
        out.append(f"{indent}{prefix}{node_prefix}Seq")
        items = node.get("items", [])
        if isinstance(items, list):
            for idx, it in enumerate(items, start=1):
                if not isinstance(it, dict):
                    continue
                child = it.get("node", {})
                if isinstance(child, dict):
                    _render_inline_ast_lines(
                        child,
                        out=out,
                        indent=indent + "  ",
                        prefix=f"[{idx}] ",
                    )
        return

    if t == "Repeat":
        out.append(f"{indent}{prefix}{node_prefix}Repeat x{int(node.get('count', 1))}")
        body = node.get("body", {})
        if isinstance(body, dict):
            if str(body.get("type", "")) == "Seq":
                items = body.get("items", [])
                if isinstance(items, list):
                    for idx, it in enumerate(items, start=1):
                        if not isinstance(it, dict):
                            continue
                        child = it.get("node", {})
                        if isinstance(child, dict):
                            _render_inline_ast_lines(
                                child,
                                out=out,
                                indent=indent + "  ",
                                prefix=f"[{idx}] ",
                            )
            else:
                _render_inline_ast_lines(body, out=out, indent=indent + "  ")
        return

    if t == "Atom":
        out.append(
            f"{indent}{prefix}{node_prefix}Atom {node.get('symbol','')} | {node.get('op_label','')} | {node.get('category','')}"
        )
        return

    if t == "MacroRef":
        out.append(f"{indent}{prefix}{node_prefix}MacroRef {node.get('name', '')}")
        return

    out.append(f"{indent}{prefix}{node_prefix}{t}")


def _renumber_seq_items(items: Sequence[Dict[str, object]]) -> List[Dict[str, object]]:
    out: List[Dict[str, object]] = []
    for idx, item in enumerate(items, start=1):
        row = dict(item)
        row["ord"] = idx
        out.append(row)
    return out


def _inline_macro_refs_in_ast(
    node: Dict[str, object],
    *,
    macro_table: Dict[str, Dict[str, object]],
    stack: Tuple[str, ...] = (),
) -> Dict[str, object]:
    def expand_child_to_items(child: Dict[str, object], stack: Tuple[str, ...]) -> List[Dict[str, object]]:
        if str(child.get("type", "")) != "MacroRef":
            return [{"ord": 0, "node": _inline_macro_refs_in_ast(child, macro_table=macro_table, stack=stack)}]

        name = str(child.get("name", ""))
        if not name or name in stack:
            return [{"ord": 0, "node": copy.deepcopy(child)}]
        macro = macro_table.get(name, {})
        macro_tree = macro.get("tree", {})
        if not isinstance(macro_tree, dict):
            return [{"ord": 0, "node": copy.deepcopy(child)}]
        expanded = _inline_macro_refs_in_ast(macro_tree, macro_table=macro_table, stack=stack + (name,))
        if str(expanded.get("type", "")) == "Seq":
            items = expanded.get("items", [])
            if isinstance(items, list):
                return [copy.deepcopy(item) for item in items if isinstance(item, dict)]
        return [{"ord": 0, "node": expanded}]

    t = str(node.get("type", ""))
    if t == "Seq":
        items_out: List[Dict[str, object]] = []
        items = node.get("items", [])
        if isinstance(items, list):
            for item in items:
                if not isinstance(item, dict):
                    continue
                child = item.get("node", {})
                if isinstance(child, dict):
                    items_out.extend(expand_child_to_items(child, stack))
        out = dict(node)
        out["items"] = _renumber_seq_items(items_out)
        return out

    if t == "Repeat":
        out = dict(node)
        body = node.get("body", {})
        if isinstance(body, dict):
            out["body"] = _inline_macro_refs_in_ast(body, macro_table=macro_table, stack=stack)
        return out

    return copy.deepcopy(node)


def _inline_tree_payload_macro_refs(payload: Dict[str, object]) -> Dict[str, object]:
    out = copy.deepcopy(payload)
    macro_table_raw = out.get("macro_table", {})
    macro_table = (
        {
            str(name): value
            for name, value in macro_table_raw.items()
            if isinstance(name, str) and isinstance(value, dict)
        }
        if isinstance(macro_table_raw, dict)
        else {}
    )
    root = out.get("root", {})
    if isinstance(root, dict):
        out["root"] = _inline_macro_refs_in_ast(root, macro_table=macro_table)
    out["macro_defs"] = []
    out["macro_table"] = {}
    out["root_macro_ref_counts"] = {}
    out["macro_refs_inlined_for_readable"] = True
    return out


def _render_tree_payload_readable(payload: Dict[str, object]) -> str:
    lines: List[str] = []
    lines.append("# Loop Tree (v2)")
    lines.append("")
    lines.append(f"- db: `{payload.get('db', '')}`")
    lines.append(f"- device_id: `{payload.get('device_id', '')}`")
    lines.append(f"- stream_id: `{payload.get('stream_id', '')}`")
    lines.append("")
    lines.append("## Root")
    lines.append("")
    lines.append("```")
    root = payload.get("root", {})
    if isinstance(root, dict):
        _render_inline_ast_lines(root, out=lines)
    lines.append("```")
    lines.append("")
    lines.append("## Macro Subtrees")
    lines.append("")
    lines.append("No macro definitions in readable view; macro refs were inlined.")
    lines.append("")
    return "\n".join(lines)


def _render_root_item_metrics(rows: Sequence[Dict[str, object]]) -> List[str]:
    lines: List[str] = []
    lines.append("## Augmented Root Timeline")
    lines.append("")
    lines.append(
        "| idx | node | anchors | compute_us | collective_us | aux_events | aux_us | idle_us | wait_us | comm_us | collective_hint |"
    )
    lines.append("| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |")
    for row in rows[:120]:
        lines.append(
            (
                f"| {row.get('idx','')} | {row.get('node','')} | {row.get('anchor_count',0)} "
                f"| {row.get('compute_us',0.0)} | {row.get('collective_us',0.0)} "
                f"| {row.get('aux_event_count',0)} | {row.get('aux_dur_us',0.0)} "
                f"| {row.get('prelude_idle_us',0.0)} | {row.get('prelude_wait_us',0.0)} "
                f"| {row.get('prelude_comm_us',0.0)} | {row.get('prelude_collective_hint','')} |"
            )
        )
    if len(rows) > 120:
        lines.append(f"| ... | {len(rows) - 120} more root items |  |  |  |  |  |  |  |  |  |")
    return lines


def _render_node_cost_metrics(rows: Sequence[Dict[str, object]]) -> List[str]:
    lines: List[str] = []
    lines.append("## Node Cost Table")
    lines.append("")
    lines.append("This table is the cost index for the compressed loop tree.")
    lines.append("")
    lines.append("- Each row is one visible tree node after structural compression. A `Repeat` row represents a detected loop, and its following deeper rows describe the loop body structure.")
    lines.append("- All `*_us` columns are measured in microseconds.")
    lines.append("- `depth` is the node's nesting depth in the compressed tree. It can be used to observe loop nesting depth and to map rows back to the tree above.")
    lines.append("- `kind` describes the node shape. `Anchor` rows correspond to concrete compute/collective anchors, `Seq` rows aggregate ordered child structure, and `Repeat` rows aggregate a repeated body.")
    lines.append("- `label` is the representative kernel or structural label for that node.")
    lines.append("- `repeat` is the loop repetition count for repeat nodes. `occ` is the number of times this row occurs in the expanded execution implied by its ancestor loops.")
    lines.append("- `total_us` is the inclusive wall-clock contribution of the node across all occurrences. For a `Repeat`, it should equal the sum of the visible body-child totals.")
    lines.append("- `avg_total_us`, `avg_compute_us`, `avg_comm_us`, `avg_idle_us`, `avg_self_us`, and `avg_aux_us` divide the corresponding totals by `occ`, making loop-body per-iteration costs comparable.")
    lines.append("- `avg_compute_us` counts covered compute-anchor execution, `avg_comm_us` counts prelude collective/communication time before those anchors, and `avg_idle_us` counts uncovered prelude gap time.")
    lines.append("- `avg_self_us` is the node's own anchor cost after subtracting visible child contributions where applicable. It is most useful for spotting whether cost belongs to the node itself or to its nested structure.")
    lines.append("- `comm%` and `idle%` are computed from inclusive totals, not averages, so they describe the node's total cost composition.")
    lines.append("- `aux_events` counts auxiliary events attached to the node's anchor prelude slots; `avg_aux_us` is their average attached duration per occurrence.")
    lines.append("- Read top-level rows for phase-level structure and total cost distribution; read a `Repeat` row together with its immediate children for loop-body cost distribution.")
    lines.append("")
    visible_rows = [row for row in rows if not row.get("structural_alias")]
    lines.append(
        "| node | depth | kind | label | repeat | occ | avg_total_us | total_us | avg_compute_us | avg_comm_us | avg_idle_us | avg_self_us | comm% | idle% | aux_events | avg_aux_us |"
    )
    lines.append(
        "| --- | ---: | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
    )
    for row in visible_rows:
        lines.append(
            (
                f"| {row.get('node_id','')} | {row.get('display_depth', row.get('depth',0))} | {row.get('kind','')} "
                f"| {row.get('label','')} | {row.get('repeat','')} "
                f"| {row.get('occurrence_count',0)} "
                f"| {row.get('avg_total_us',0.0)} | {row.get('total_us',0.0)} "
                f"| {row.get('avg_compute_us',0.0)} | {row.get('avg_comm_us',0.0)} "
                f"| {row.get('avg_idle_us',0.0)} | {row.get('avg_self_us',0.0)} "
                f"| {round(float(row.get('comm_pct',0.0)) * 100.0, 2)} "
                f"| {round(float(row.get('idle_pct',0.0)) * 100.0, 2)} "
                f"| {row.get('aux_events',0.0)} | {row.get('avg_aux_us',0.0)} |"
            )
        )
    return lines


def _loop_cost_rows(
    *,
    selection: DeviceSelection,
    node_metric_rows: Sequence[Dict[str, object]],
    anchor_tree_readable_file: str,
) -> List[Dict[str, object]]:
    loops = [
        row
        for row in node_metric_rows
        if not row.get("structural_alias")
        and (
            str(row.get("kind", "")) == "repeat"
            or str(row.get("repeat", "")).strip()
        )
    ]
    total_loop_us = sum(float(row.get("total_us", 0.0)) for row in loops)
    out: List[Dict[str, object]] = []
    for rank, row in enumerate(
        sorted(
            loops,
            key=lambda r: (
                float(r.get("total_us", 0.0)),
                float(r.get("avg_total_us", 0.0)),
                int(r.get("anchor_count", 0)),
            ),
            reverse=True,
        ),
        start=1,
    ):
        total_us = float(row.get("total_us", 0.0))
        avg_total_us = float(row.get("avg_total_us", 0.0))
        out.append(
            {
                "loop_rank": rank,
                "global_rank": selection.global_rank,
                "db_idx": selection.db_idx,
                "db": str(selection.db_path),
                "device_id": selection.device_id,
                "node_id": row.get("node_id", ""),
                "path": row.get("path", ""),
                "depth": row.get("display_depth", row.get("depth", 0)),
                "label": row.get("label", ""),
                "repeat": row.get("repeat", ""),
                "occurrence_count": row.get("occurrence_count", 0),
                "anchor_count": row.get("anchor_count", 0),
                "anchors_per_occurrence": row.get("anchors_per_occurrence", 0.0),
                "total_us": round(total_us, 3),
                "avg_total_us": round(avg_total_us, 3),
                "compute_us": row.get("compute_us", 0.0),
                "comm_us": row.get("comm_us", 0.0),
                "idle_us": row.get("idle_us", 0.0),
                "avg_compute_us": row.get("avg_compute_us", 0.0),
                "avg_comm_us": row.get("avg_comm_us", 0.0),
                "avg_idle_us": row.get("avg_idle_us", 0.0),
                "comm_pct": row.get("comm_pct", 0.0),
                "idle_pct": row.get("idle_pct", 0.0),
                "aux_events": row.get("aux_events", 0.0),
                "avg_aux_us": row.get("avg_aux_us", 0.0),
                "loop_total_pct": round(total_us / total_loop_us, 6) if total_loop_us else 0.0,
                "anchor_tree_readable_file": anchor_tree_readable_file,
            }
        )
    return out


def _render_loop_cost_summary(rows: Sequence[Dict[str, object]], *, limit: int) -> List[str]:
    lines: List[str] = []
    lines.append("## Detected Loop Costs")
    lines.append("")
    if not rows:
        lines.append("No repeat nodes were detected in the compressed anchor tree.")
        return lines
    lines.append(
        "| rank | node | depth | repeat | occ | anchors/occ | avg_total_us | total_us | avg_compute_us | avg_comm_us | avg_idle_us | comm% | idle% | label |"
    )
    lines.append(
        "| ---: | --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |"
    )
    for row in rows[:limit]:
        lines.append(
            (
                f"| {row.get('loop_rank','')} | {row.get('node_id','')} | {row.get('depth',0)} "
                f"| {row.get('repeat','')} | {row.get('occurrence_count',0)} "
                f"| {row.get('anchors_per_occurrence',0.0)} "
                f"| {row.get('avg_total_us',0.0)} | {row.get('total_us',0.0)} "
                f"| {row.get('avg_compute_us',0.0)} | {row.get('avg_comm_us',0.0)} "
                f"| {row.get('avg_idle_us',0.0)} "
                f"| {round(float(row.get('comm_pct',0.0)) * 100.0, 2)} "
                f"| {round(float(row.get('idle_pct',0.0)) * 100.0, 2)} "
                f"| {row.get('label','')} |"
            )
        )
    if len(rows) > limit:
        lines.append(f"| ... | {len(rows) - limit} more loops |  |  |  |  |  |  |  |  |  |  |  |  |")
    return lines


def _render_macro_loop_chains(rows: Sequence[Dict[str, object]]) -> List[str]:
    lines: List[str] = []
    lines.append("## Macro Loop Chains")
    lines.append("")
    if not rows:
        lines.append("Post-hoc macro DAG chain promotion is disabled; loops come from online loop refs and tree repeats.")
        return lines
    lines.append("| source | chain_end | direction | repeat | loop_form | action | chain_start |")
    lines.append("| --- | --- | --- | ---: | --- | --- | --- |")
    for row in rows[:40]:
        lines.append(
            (
                f"| {row.get('source','')} | {row.get('chain_end','')} | {row.get('direction','')} "
                f"| {row.get('repeat_count',0)} "
                f"| {row.get('loop_form','')} | {row.get('action','')} | {row.get('chain_start','')} |"
            )
        )
    return lines


def _render_anchor_readable(
    tree_readable: str,
    *,
    selection: DeviceSelection,
    anchor_step_rows: Sequence[Dict[str, object]],
    kernel_role_rows: Sequence[Dict[str, object]],
    aux_slot_rows: Sequence[Dict[str, object]],
    aux_symbol_rows: Sequence[Dict[str, object]],
    aux_macro_rows: Sequence[Dict[str, object]],
    node_metric_rows: Sequence[Dict[str, object]],
    root_item_metric_rows: Sequence[Dict[str, object]],
    macro_loop_chain_rows: Sequence[Dict[str, object]],
    loop_cost_rows: Sequence[Dict[str, object]],
    loop_summary_limit: int,
) -> str:
    text = _render_compute_readable(
        tree_readable,
        selection=selection,
        step_rows=anchor_step_rows,
    )
    role_counts = Counter(str(row.get("semantic_role", "")) for row in kernel_role_rows)
    lines = text.splitlines()
    lines.append("")
    lines.extend(_render_node_cost_metrics(node_metric_rows))
    lines.append("")
    lines.extend(_render_loop_cost_summary(loop_cost_rows, limit=loop_summary_limit))
    lines.append("")
    lines.append("## Anchor View Summary")
    lines.append("")
    lines.append("- view: `hybrid_anchor_sequence`")
    lines.append(f"- anchor_events: `{len(anchor_step_rows)}`")
    lines.append(f"- aux_slots: `{len(aux_slot_rows)}`")
    lines.append(
        "- symbol_roles: `"
        + " ".join(f"{role}:{count}" for role, count in sorted(role_counts.items()) if role)
        + "`"
    )
    return "\n".join(lines) + "\n"


def _rank_devices(db_paths: Sequence[Path], cfg: ComputePreludeConfig) -> List[DeviceSelection]:
    ranked_streams, _ranking_rows = _rank_streams_global(db_paths)
    buckets: Dict[Tuple[int, int], Dict[str, object]] = {}
    for stream in ranked_streams:
        if cfg.device_ids is not None and stream.device_id not in cfg.device_ids:
            continue
        key = (stream.db_idx, stream.device_id)
        bucket = buckets.setdefault(
            key,
            {
                "db_idx": stream.db_idx,
                "db_path": stream.db_path,
                "device_id": stream.device_id,
                "main_event_count": 0,
                "exec_us": 0.0,
                "data_move_us": 0.0,
            },
        )
        bucket["main_event_count"] = int(bucket["main_event_count"]) + int(stream.stats.get("event_count", 0))
        bucket["exec_us"] = float(bucket["exec_us"]) + float(stream.stats.get("exec_us", 0.0))

    selections: List[DeviceSelection] = []
    for bucket in buckets.values():
        exec_us = float(bucket["exec_us"])
        if exec_us <= 0:
            continue
        selections.append(
            DeviceSelection(
                global_rank=0,
                db_idx=int(bucket["db_idx"]),
                db_path=Path(bucket["db_path"]),
                device_id=int(bucket["device_id"]),
                main_event_count=int(bucket["main_event_count"]),
                exec_us=exec_us,
                data_move_us=float(bucket["data_move_us"]),
                total_main_us=exec_us + float(bucket["data_move_us"]),
            )
        )

    selections.sort(key=lambda s: (s.total_main_us, s.main_event_count), reverse=True)
    ranked: List[DeviceSelection] = []
    for idx, selection in enumerate(selections, start=1):
        ranked.append(
            DeviceSelection(
                global_rank=idx,
                db_idx=selection.db_idx,
                db_path=selection.db_path,
                device_id=selection.device_id,
                main_event_count=selection.main_event_count,
                exec_us=selection.exec_us,
                data_move_us=selection.data_move_us,
                total_main_us=selection.total_main_us,
            )
        )
    return ranked[: cfg.top_devices_global] if cfg.top_devices_global > 0 else ranked


def _device_summary_row(selection: DeviceSelection, step_rows: Sequence[Dict[str, object]]) -> Dict[str, object]:
    used_exec_us = sum(float(r["dur_us"]) for r in step_rows if r.get("role") == "compute")
    used_data_move_us = sum(float(r["dur_us"]) for r in step_rows if r.get("role") == "data_move")
    used_collective_us = sum(float(r["dur_us"]) for r in step_rows if r.get("role") == "collective")
    transparent_rows = [
        r
        for r in step_rows
        if _normalize_task_key(str(r.get("task_type", ""))) in {"MODEL_MAINTAINCE", "MODEL_MAINTENANCE"}
        or str(r.get("family", "")) in {"model_maintaince", "model_maintenance"}
    ]
    return {
        "global_rank": selection.global_rank,
        "db_idx": selection.db_idx,
        "db": str(selection.db_path),
        "device_id": selection.device_id,
        "main_event_count": selection.main_event_count,
        "used_main_event_count": len(step_rows),
        "transparent_main_event_count": len(transparent_rows),
        "projected_main_event_count": len(step_rows) - len(transparent_rows),
        "transparent_main_event_us": round(sum(float(r["dur_us"]) for r in transparent_rows), 3),
        "exec_us": round(selection.exec_us, 3),
        "data_move_us": round(selection.data_move_us, 3),
        "total_main_us": round(selection.total_main_us, 3),
        "used_exec_us": round(used_exec_us, 3),
        "used_data_move_us": round(used_data_move_us, 3),
        "used_collective_us": round(used_collective_us, 3),
        "used_total_main_us": round(used_exec_us + used_data_move_us + used_collective_us, 3),
        "prelude_gap_us": round(sum(float(r["prelude_gap_us"]) for r in step_rows), 3),
        "prelude_comm_us": round(sum(float(r["prelude_comm_us"]) for r in step_rows), 3),
        "prelude_wait_us": round(sum(float(r["prelude_wait_us"]) for r in step_rows), 3),
        "prelude_idle_us": round(sum(float(r["prelude_idle_us"]) for r in step_rows), 3),
    }


def _relpath_or_self(path_text: str, out_dir: Path) -> str:
    try:
        return str(Path(path_text).resolve().relative_to(out_dir.resolve()))
    except (OSError, ValueError):
        return path_text


def _build_run_summary_markdown(
    *,
    summary_rows: Sequence[Dict[str, object]],
    loop_cost_rows: Sequence[Dict[str, object]],
    out_dir: Path,
    top_loops: int,
) -> str:
    lines: List[str] = []
    lines.append("# TraceLoom Summary")
    lines.append("")
    lines.append(f"- out_dir: `{out_dir}`")
    lines.append(f"- analyzed_devices: `{len(summary_rows)}`")
    lines.append("")
    lines.append("## Devices")
    lines.append("")
    if summary_rows:
        lines.append(
            "| rank | db | device | anchors | loops_file | tree | used_total_us | prelude_gap_us | prelude_comm_us | prelude_idle_us |"
        )
        lines.append("| ---: | --- | ---: | ---: | --- | --- | ---: | ---: | ---: | ---: |")
        for row in summary_rows:
            lines.append(
                (
                    f"| {row.get('global_rank','')} | db{int(row.get('db_idx',0)):02d} | {row.get('device_id','')} "
                    f"| {row.get('anchor_event_count',0)} | {row.get('anchor_loop_costs_file','')} "
                    f"| {row.get('anchor_tree_readable_file','')} | {row.get('used_total_main_us',0.0)} "
                    f"| {row.get('prelude_gap_us',0.0)} | {row.get('prelude_comm_us',0.0)} "
                    f"| {row.get('prelude_idle_us',0.0)} |"
                )
            )
    else:
        lines.append("No devices with executable task timelines were selected.")
    lines.append("")
    lines.append("## Top Loop Costs")
    lines.append("")
    if loop_cost_rows:
        lines.append(
            "| rank | device | node | repeat | occ | anchors/occ | avg_total_us | total_us | avg_compute_us | avg_comm_us | avg_idle_us | tree |"
        )
        lines.append("| ---: | ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |")
        for idx, row in enumerate(
            sorted(loop_cost_rows, key=lambda r: float(r.get("total_us", 0.0)), reverse=True)[:top_loops],
            start=1,
        ):
            lines.append(
                (
                    f"| {idx} | {row.get('device_id','')} | {row.get('node_id','')} "
                    f"| {row.get('repeat','')} | {row.get('occurrence_count',0)} "
                    f"| {row.get('anchors_per_occurrence',0.0)} | {row.get('avg_total_us',0.0)} "
                    f"| {row.get('total_us',0.0)} | {row.get('avg_compute_us',0.0)} "
                    f"| {row.get('avg_comm_us',0.0)} | {row.get('avg_idle_us',0.0)} "
                    f"| {row.get('anchor_tree_readable_file','')} |"
                )
            )
    else:
        lines.append("No repeat nodes were detected in the selected anchor timelines.")
    lines.append("")
    lines.append("## Main Files")
    lines.append("")
    lines.append("- `device_summary.csv`")
    lines.append("- `compute_anchor_loop_costs.csv`")
    lines.append("- `compute_anchor_node_metrics.csv`")
    lines.append("- `compute_anchor_aux_slots.csv`")
    return "\n".join(lines) + "\n"


def _format_console_summary(meta: Dict[str, object]) -> str:
    out_dir = Path(str(meta.get("summary_file", ""))).parent
    lines = [
        "TraceLoom analysis complete",
        f"out_dir: {out_dir}",
        f"devices: {meta.get('device_count', 0)} / dbs: {meta.get('db_count', 0)}",
        f"summary: {_relpath_or_self(str(meta.get('run_summary_file', '')), out_dir)}",
        f"loop_costs: {_relpath_or_self(str(meta.get('anchor_loop_costs_file', '')), out_dir)}",
        f"node_metrics: {_relpath_or_self(str(meta.get('anchor_node_metrics_file', '')), out_dir)}",
    ]
    return "\n".join(lines)


def run_compute_prelude_timeline(
    *,
    run_dir: Path,
    out_dir: Path,
    config: ComputePreludeConfig | None = None,
) -> Dict[str, object]:
    cfg = config or ComputePreludeConfig()
    started = time.time()
    raw_dir = _resolve_msprof_raw_dir(run_dir)
    out_dir = out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    db_paths = discover_msprof_dbs(raw_dir)
    selections = _rank_devices(db_paths, cfg)
    role_overrides = _load_kernel_role_overrides(cfg.kernel_role_file)
    summary_rows: List[Dict[str, object]] = []
    all_step_rows: List[Dict[str, object]] = []
    all_symbol_rows: List[Dict[str, object]] = []
    all_macro_edge_rows: List[Dict[str, object]] = []
    all_macro_metric_rows: List[Dict[str, object]] = []
    all_macro_view_rows: List[Dict[str, object]] = []
    all_kernel_role_rows: List[Dict[str, object]] = []
    all_anchor_aux_slot_rows: List[Dict[str, object]] = []
    all_anchor_aux_symbol_rows: List[Dict[str, object]] = []
    all_anchor_macro_aux_metric_rows: List[Dict[str, object]] = []
    all_anchor_root_item_metric_rows: List[Dict[str, object]] = []
    all_anchor_node_metric_rows: List[Dict[str, object]] = []
    all_anchor_node_link_rows: List[Dict[str, object]] = []
    all_anchor_macro_loop_chain_rows: List[Dict[str, object]] = []
    all_anchor_loop_cost_rows: List[Dict[str, object]] = []

    for selection in selections:
        device_events, stream_stats = _load_device_events(selection.db_path, selection.device_id)
        communication_op_events = _load_communication_op_events(selection.db_path, selection.device_id)
        collective_anchor_source = "communication_op" if communication_op_events else "task_coalesced"
        main_events, symbol_rows = _build_main_events(
            device_events=device_events,
            communication_op_events=communication_op_events,
            stream_stats=stream_stats,
            cfg=cfg,
        )
        step_rows = _build_steps(
            db_idx=selection.db_idx,
            db_path=selection.db_path,
            device_id=selection.device_id,
            device_events=device_events,
            main_events=main_events,
            cfg=cfg,
        )
        symbol_rows = _augment_symbol_rows(symbol_rows, step_rows)
        semantic_roles, _semantic_reasons, kernel_role_rows = _apply_kernel_roles(
            main_events=main_events,
            step_rows=step_rows,
            symbol_rows=symbol_rows,
            overrides=role_overrides,
        )
        projected_main_events = [
            item for item, semantic_role in zip(main_events, semantic_roles) if semantic_role != "transparent"
        ]
        transparent_main_event_count = len(main_events) - len(projected_main_events)
        symbol_seq = [item.symbol for item in projected_main_events]
        atom_windows = [(item.event.start_ns, item.event.end_ns) for item in projected_main_events]
        final_expr_tokens, l1_defs, l2_defs = _discover_pair_grammar_macros(
            symbol_seq,
            atom_windows,
            max_macro_defs=cfg.max_macro_defs,
        )
        macro_defs = l1_defs + l2_defs
        macro_rows = _macro_rows(macro_defs)
        macro_def_tokens = {d.name: list(d.tokens) for d in macro_defs}
        macro_edge_rows = _macro_edge_rows(macro_defs)
        macro_metric_rows = _macro_metric_rows(
            macro_defs=macro_defs,
            step_rows=step_rows,
        )
        (
            view_final_expr_tokens,
            view_macro_rows,
            view_macro_def_tokens,
            macro_view_rows,
        ) = _build_macro_readable_view(
            macro_defs=macro_defs,
            final_expr_tokens=final_expr_tokens,
            mode=cfg.readable_macro_mode,
        )
        macro_loop_chain_rows: List[Dict[str, object]] = []
        inline_tree_macro_def_tokens = macro_def_tokens
        inline_tree_macro_rows = macro_rows

        anchor_events = [item for item, semantic_role in zip(main_events, semantic_roles) if semantic_role == "anchor"]
        anchor_step_rows = [row for row in step_rows if row.get("semantic_role") == "anchor"]
        anchor_symbol_rows = [row for row in symbol_rows if row.get("semantic_role") == "anchor"]
        anchor_aux_slot_rows, anchor_aux_symbol_rows = _build_anchor_aux_slots(
            main_events=main_events,
            semantic_roles=semantic_roles,
            step_rows=step_rows,
        )
        anchor_symbol_seq = [item.symbol for item in anchor_events]
        anchor_atom_windows = [(item.event.start_ns, item.event.end_ns) for item in anchor_events]
        anchor_final_expr_tokens, anchor_l1_defs, anchor_l2_defs = _discover_pair_grammar_macros(
            anchor_symbol_seq,
            anchor_atom_windows,
            max_macro_defs=cfg.max_macro_defs,
        )
        anchor_macro_defs = anchor_l1_defs + anchor_l2_defs
        anchor_macro_rows = _macro_rows(anchor_macro_defs)
        anchor_macro_def_tokens = {d.name: list(d.tokens) for d in anchor_macro_defs}
        anchor_macro_edge_rows = _macro_edge_rows(anchor_macro_defs)
        anchor_macro_metric_rows = _macro_metric_rows(
            macro_defs=anchor_macro_defs,
            step_rows=anchor_step_rows,
        )
        anchor_macro_aux_metric_rows = _anchor_macro_aux_metric_rows(
            macro_defs=anchor_macro_defs,
            anchor_step_rows=anchor_step_rows,
            aux_slot_rows=anchor_aux_slot_rows,
        )
        (
            anchor_view_final_expr_tokens,
            anchor_view_macro_rows,
            anchor_view_macro_def_tokens,
            anchor_macro_view_rows,
        ) = _build_macro_readable_view(
            macro_defs=anchor_macro_defs,
            final_expr_tokens=anchor_final_expr_tokens,
            mode=cfg.readable_macro_mode,
        )
        anchor_inline_tree_macro_def_tokens = anchor_macro_def_tokens
        anchor_inline_tree_macro_rows = anchor_macro_rows
        anchor_macro_loop_chain_rows: List[Dict[str, object]] = []
        raw_tree_payload, raw_tree_readable = _build_tree_v2(
            db_path=selection.db_path,
            device_id=selection.device_id,
            stream_id=-1,
            final_expr_tokens=final_expr_tokens,
            macro_rows=macro_rows,
            macro_def_tokens=macro_def_tokens,
            symbol_rows=symbol_rows,
        )
        tree_expr_tokens = final_expr_tokens if cfg.readable_macro_mode == "inline" else view_final_expr_tokens
        tree_macro_rows = inline_tree_macro_rows if cfg.readable_macro_mode == "inline" else view_macro_rows
        tree_macro_def_tokens = (
            inline_tree_macro_def_tokens if cfg.readable_macro_mode == "inline" else view_macro_def_tokens
        )
        tree_payload, tree_readable = _build_tree_v2(
            db_path=selection.db_path,
            device_id=selection.device_id,
            stream_id=-1,
            final_expr_tokens=tree_expr_tokens,
            macro_rows=tree_macro_rows,
            macro_def_tokens=tree_macro_def_tokens,
            symbol_rows=symbol_rows,
        )
        if cfg.readable_macro_mode == "inline":
            tree_payload = _inline_tree_payload_macro_refs(tree_payload)
            tree_readable = _render_tree_payload_readable(tree_payload)
        anchor_tree_expr_tokens = (
            anchor_final_expr_tokens if cfg.readable_macro_mode == "inline" else anchor_view_final_expr_tokens
        )
        anchor_tree_macro_rows = (
            anchor_inline_tree_macro_rows if cfg.readable_macro_mode == "inline" else anchor_view_macro_rows
        )
        anchor_tree_macro_def_tokens = (
            anchor_inline_tree_macro_def_tokens if cfg.readable_macro_mode == "inline" else anchor_view_macro_def_tokens
        )
        anchor_tree_payload, anchor_tree_readable = _build_tree_v2(
            db_path=selection.db_path,
            device_id=selection.device_id,
            stream_id=-1,
            final_expr_tokens=anchor_tree_expr_tokens,
            macro_rows=anchor_tree_macro_rows,
            macro_def_tokens=anchor_tree_macro_def_tokens,
            symbol_rows=anchor_symbol_rows,
        )
        if cfg.readable_macro_mode == "inline":
            anchor_tree_payload = _inline_tree_payload_macro_refs(anchor_tree_payload)
        anchor_node_metric_rows, anchor_node_link_rows = _augment_tree_node_cost_metrics(
            anchor_tree_payload,
            step_rows=anchor_step_rows,
            aux_slot_rows=anchor_aux_slot_rows,
            macro_def_tokens=anchor_tree_macro_def_tokens,
        )
        if cfg.readable_macro_mode == "inline":
            anchor_tree_readable = _render_tree_payload_readable(anchor_tree_payload)
        anchor_root_item_metric_rows = _augment_root_item_metrics(
            anchor_tree_payload,
            step_rows=anchor_step_rows,
            aux_slot_rows=anchor_aux_slot_rows,
            macro_def_tokens=anchor_tree_macro_def_tokens,
        )

        # The primary readable tree is the semantic anchor timeline: only major
        # compute kernels and coalesced collective episodes participate in loop
        # discovery. Auxiliary events remain in step CSVs and are attached to
        # the following anchor through aux/prelude slots.
        full_tree_payload = tree_payload
        full_tree_readable = tree_readable
        tree_payload = copy.deepcopy(anchor_tree_payload)
        tree_readable = anchor_tree_readable

        full_tree_payload["schema_version"] = "compute_prelude_tree_aux_included_v1"
        full_tree_payload["device_scope"] = True
        full_tree_payload["main_event_count"] = len(main_events)
        full_tree_payload["projected_main_event_count"] = len(projected_main_events)
        full_tree_payload["anchor_event_count"] = len(anchor_events)
        full_tree_payload["aux_event_count"] = sum(1 for role in semantic_roles if role == "aux")
        full_tree_payload["transparent_main_event_count"] = transparent_main_event_count
        full_tree_payload["transparent_task_types"] = ["MODEL_MAINTAINCE", "MODEL_MAINTENANCE"]
        full_tree_payload["macro_discovery"] = "pair_grammar"
        full_tree_payload["readable_macro_mode"] = cfg.readable_macro_mode
        full_tree_payload["loop_promotion_methods"] = LOOP_PROMOTION_METHODS
        full_tree_payload["collective_anchor_source"] = collective_anchor_source
        full_tree_payload["communication_op_event_count"] = len(communication_op_events)
        full_tree_payload["view"] = f"aux_included_readable_{cfg.readable_macro_mode}"

        tree_payload["schema_version"] = "compute_prelude_tree_v1"
        tree_payload["device_scope"] = True
        tree_payload["main_event_count"] = len(main_events)
        tree_payload["projected_main_event_count"] = len(anchor_events)
        tree_payload["anchor_event_count"] = len(anchor_events)
        tree_payload["collective_anchor_event_count"] = sum(1 for item in anchor_events if item.role == "collective")
        tree_payload["aux_event_count"] = sum(1 for role in semantic_roles if role == "aux")
        tree_payload["transparent_main_event_count"] = transparent_main_event_count
        tree_payload["transparent_task_types"] = ["MODEL_MAINTAINCE", "MODEL_MAINTENANCE"]
        tree_payload["macro_discovery"] = "pair_grammar"
        tree_payload["readable_macro_mode"] = cfg.readable_macro_mode
        tree_payload["loop_promotion_methods"] = LOOP_PROMOTION_METHODS
        tree_payload["collective_anchor_source"] = collective_anchor_source
        tree_payload["communication_op_event_count"] = len(communication_op_events)
        tree_payload["semantic_projection"] = "anchor_compute_collective_only"
        tree_payload["auxiliary_attribution"] = "attach_aux_events_to_following_anchor_prelude_slot"
        tree_payload["view"] = f"semantic_anchor_readable_{cfg.readable_macro_mode}"
        raw_tree_payload["schema_version"] = "compute_prelude_tree_raw_v1"
        raw_tree_payload["device_scope"] = True
        raw_tree_payload["main_event_count"] = len(main_events)
        raw_tree_payload["projected_main_event_count"] = len(projected_main_events)
        raw_tree_payload["transparent_main_event_count"] = transparent_main_event_count
        raw_tree_payload["transparent_task_types"] = ["MODEL_MAINTAINCE", "MODEL_MAINTENANCE"]
        raw_tree_payload["macro_discovery"] = "pair_grammar"
        raw_tree_payload["readable_macro_mode"] = "raw"
        raw_tree_payload["loop_promotion_methods"] = LOOP_PROMOTION_METHODS
        raw_tree_payload["collective_anchor_source"] = collective_anchor_source
        raw_tree_payload["communication_op_event_count"] = len(communication_op_events)
        raw_tree_payload["view"] = "raw_pair_grammar"
        anchor_tree_payload["schema_version"] = "compute_anchor_tree_v1"
        anchor_tree_payload["device_scope"] = True
        anchor_tree_payload["main_event_count"] = len(main_events)
        anchor_tree_payload["anchor_event_count"] = len(anchor_events)
        anchor_tree_payload["collective_anchor_event_count"] = sum(1 for item in anchor_events if item.role == "collective")
        anchor_tree_payload["aux_event_count"] = sum(1 for role in semantic_roles if role == "aux")
        anchor_tree_payload["transparent_main_event_count"] = transparent_main_event_count
        anchor_tree_payload["macro_discovery"] = "pair_grammar"
        anchor_tree_payload["readable_macro_mode"] = cfg.readable_macro_mode
        anchor_tree_payload["loop_promotion_methods"] = LOOP_PROMOTION_METHODS
        anchor_tree_payload["collective_anchor_source"] = collective_anchor_source
        anchor_tree_payload["communication_op_event_count"] = len(communication_op_events)
        anchor_tree_payload["view"] = f"hybrid_anchor_readable_{cfg.readable_macro_mode}"

        stem = f"db{selection.db_idx:02d}_rank{selection.global_rank:02d}_dev{selection.device_id}_compute"
        steps_path = out_dir / f"{stem}.steps.csv"
        symbols_path = out_dir / f"{stem}.symbols.csv"
        kernel_roles_path = out_dir / f"{stem}.kernel_roles.csv"
        macros_path = out_dir / f"{stem}.macros.csv"
        macro_edges_path = out_dir / f"{stem}.macro_edges.csv"
        macro_metrics_path = out_dir / f"{stem}.macro_metrics.csv"
        macro_view_path = out_dir / f"{stem}.macro_view.csv"
        full_macros_path = out_dir / f"{stem}.aux_included.macros.csv"
        full_macro_edges_path = out_dir / f"{stem}.aux_included.macro_edges.csv"
        full_macro_metrics_path = out_dir / f"{stem}.aux_included.macro_metrics.csv"
        full_macro_view_path = out_dir / f"{stem}.aux_included.macro_view.csv"
        tree_path = out_dir / f"{stem}.tree.json"
        readable_path = out_dir / f"{stem}.tree.readable.md"
        raw_tree_path = out_dir / f"{stem}.tree.raw.json"
        raw_readable_path = out_dir / f"{stem}.tree.raw.readable.md"
        full_tree_path = out_dir / f"{stem}.tree.aux_included.json"
        full_readable_path = out_dir / f"{stem}.tree.aux_included.readable.md"
        anchor_steps_path = out_dir / f"{stem}.anchor.steps.csv"
        anchor_symbols_path = out_dir / f"{stem}.anchor.symbols.csv"
        anchor_aux_slots_path = out_dir / f"{stem}.anchor.aux_slots.csv"
        anchor_aux_symbols_path = out_dir / f"{stem}.anchor.aux_symbols.csv"
        anchor_macros_path = out_dir / f"{stem}.anchor.macros.csv"
        anchor_macro_edges_path = out_dir / f"{stem}.anchor.macro_edges.csv"
        anchor_macro_metrics_path = out_dir / f"{stem}.anchor.macro_metrics.csv"
        anchor_macro_aux_metrics_path = out_dir / f"{stem}.anchor.macro_aux_metrics.csv"
        anchor_macro_view_path = out_dir / f"{stem}.anchor.macro_view.csv"
        anchor_root_item_metrics_path = out_dir / f"{stem}.anchor.root_item_metrics.csv"
        anchor_node_metrics_path = out_dir / f"{stem}.anchor.node_metrics.csv"
        anchor_node_links_path = out_dir / f"{stem}.anchor.node_anchor_links.csv"
        anchor_loop_costs_path = out_dir / f"{stem}.anchor.loop_costs.csv"
        anchor_macro_loop_chains_path = out_dir / f"{stem}.anchor.macro_loop_chains.csv"
        anchor_tree_path = out_dir / f"{stem}.anchor.tree.json"
        anchor_readable_path = out_dir / f"{stem}.anchor.tree.readable.md"
        anchor_loop_cost_rows = _loop_cost_rows(
            selection=selection,
            node_metric_rows=anchor_node_metric_rows,
            anchor_tree_readable_file=str(anchor_readable_path.relative_to(out_dir)),
        )
        _write_csv(steps_path, step_rows)
        _write_csv(symbols_path, symbol_rows)
        _write_csv(kernel_roles_path, kernel_role_rows)
        _write_csv(macros_path, anchor_macro_rows)
        _write_csv(macro_edges_path, anchor_macro_edge_rows)
        _write_csv(macro_metrics_path, anchor_macro_metric_rows)
        _write_csv(macro_view_path, anchor_macro_view_rows)
        _write_csv(full_macros_path, macro_rows)
        _write_csv(full_macro_edges_path, macro_edge_rows)
        _write_csv(full_macro_metrics_path, macro_metric_rows)
        _write_csv(full_macro_view_path, macro_view_rows)
        _write_csv(anchor_steps_path, anchor_step_rows)
        _write_csv(anchor_symbols_path, anchor_symbol_rows)
        _write_csv(anchor_aux_slots_path, anchor_aux_slot_rows)
        _write_csv(anchor_aux_symbols_path, anchor_aux_symbol_rows)
        _write_csv(anchor_macros_path, anchor_macro_rows)
        _write_csv(anchor_macro_edges_path, anchor_macro_edge_rows)
        _write_csv(anchor_macro_metrics_path, anchor_macro_metric_rows)
        _write_csv(anchor_macro_aux_metrics_path, anchor_macro_aux_metric_rows)
        _write_csv(anchor_macro_view_path, anchor_macro_view_rows)
        _write_csv(anchor_root_item_metrics_path, anchor_root_item_metric_rows)
        _write_csv(anchor_node_metrics_path, anchor_node_metric_rows)
        _write_csv(anchor_node_links_path, anchor_node_link_rows)
        _write_csv(anchor_loop_costs_path, anchor_loop_cost_rows)
        _write_csv(anchor_macro_loop_chains_path, anchor_macro_loop_chain_rows)
        tree_path.write_text(json.dumps(tree_payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        raw_tree_path.write_text(json.dumps(raw_tree_payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        full_tree_path.write_text(json.dumps(full_tree_payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        anchor_tree_path.write_text(
            json.dumps(anchor_tree_payload, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        readable_path.write_text(
            _render_anchor_readable(
                tree_readable,
                selection=selection,
                anchor_step_rows=anchor_step_rows,
                kernel_role_rows=kernel_role_rows,
                aux_slot_rows=anchor_aux_slot_rows,
                aux_symbol_rows=anchor_aux_symbol_rows,
                aux_macro_rows=anchor_macro_aux_metric_rows,
                node_metric_rows=anchor_node_metric_rows,
                root_item_metric_rows=anchor_root_item_metric_rows,
                macro_loop_chain_rows=anchor_macro_loop_chain_rows,
                loop_cost_rows=anchor_loop_cost_rows,
                loop_summary_limit=cfg.summary_top_loops,
            ),
            encoding="utf-8",
        )
        anchor_readable_path.write_text(
            _render_anchor_readable(
                anchor_tree_readable,
                selection=selection,
                anchor_step_rows=anchor_step_rows,
                kernel_role_rows=kernel_role_rows,
                aux_slot_rows=anchor_aux_slot_rows,
                aux_symbol_rows=anchor_aux_symbol_rows,
                aux_macro_rows=anchor_macro_aux_metric_rows,
                node_metric_rows=anchor_node_metric_rows,
                root_item_metric_rows=anchor_root_item_metric_rows,
                macro_loop_chain_rows=anchor_macro_loop_chain_rows,
                loop_cost_rows=anchor_loop_cost_rows,
                loop_summary_limit=cfg.summary_top_loops,
            ),
            encoding="utf-8",
        )
        raw_readable_path.write_text(
            _render_compute_readable(
                raw_tree_readable,
                selection=selection,
                step_rows=step_rows,
            ),
            encoding="utf-8",
        )
        full_readable_path.write_text(
            _render_compute_readable(
                full_tree_readable,
                selection=selection,
                step_rows=step_rows,
            ),
            encoding="utf-8",
        )

        for row in step_rows:
            row = dict(row)
            row["steps_file"] = str(steps_path.relative_to(out_dir))
            row["tree_readable_file"] = str(readable_path.relative_to(out_dir))
            all_step_rows.append(row)
        for row in symbol_rows:
            row = dict(row)
            row["db_idx"] = selection.db_idx
            row["device_id"] = selection.device_id
            row["global_rank"] = selection.global_rank
            row["symbols_file"] = str(symbols_path.relative_to(out_dir))
            row["tree_readable_file"] = str(readable_path.relative_to(out_dir))
            all_symbol_rows.append(row)
        for row in anchor_macro_edge_rows:
            row = dict(row)
            row["db_idx"] = selection.db_idx
            row["device_id"] = selection.device_id
            row["global_rank"] = selection.global_rank
            row["macro_edges_file"] = str(macro_edges_path.relative_to(out_dir))
            row["tree_readable_file"] = str(readable_path.relative_to(out_dir))
            all_macro_edge_rows.append(row)
        for row in anchor_macro_metric_rows:
            row = dict(row)
            row["db_idx"] = selection.db_idx
            row["device_id"] = selection.device_id
            row["global_rank"] = selection.global_rank
            row["macro_metrics_file"] = str(macro_metrics_path.relative_to(out_dir))
            row["macro_view_file"] = str(macro_view_path.relative_to(out_dir))
            row["tree_readable_file"] = str(readable_path.relative_to(out_dir))
            all_macro_metric_rows.append(row)
        for row in anchor_macro_view_rows:
            row = dict(row)
            row["db_idx"] = selection.db_idx
            row["device_id"] = selection.device_id
            row["global_rank"] = selection.global_rank
            row["macro_view_file"] = str(macro_view_path.relative_to(out_dir))
            row["tree_readable_file"] = str(readable_path.relative_to(out_dir))
            all_macro_view_rows.append(row)
        for row in kernel_role_rows:
            row = dict(row)
            row["db_idx"] = selection.db_idx
            row["device_id"] = selection.device_id
            row["global_rank"] = selection.global_rank
            row["kernel_roles_file"] = str(kernel_roles_path.relative_to(out_dir))
            row["anchor_tree_readable_file"] = str(anchor_readable_path.relative_to(out_dir))
            all_kernel_role_rows.append(row)
        for row in anchor_aux_slot_rows:
            row = dict(row)
            row["db_idx"] = selection.db_idx
            row["device_id"] = selection.device_id
            row["global_rank"] = selection.global_rank
            row["anchor_aux_slots_file"] = str(anchor_aux_slots_path.relative_to(out_dir))
            row["anchor_tree_readable_file"] = str(anchor_readable_path.relative_to(out_dir))
            all_anchor_aux_slot_rows.append(row)
        for row in anchor_aux_symbol_rows:
            row = dict(row)
            row["db_idx"] = selection.db_idx
            row["device_id"] = selection.device_id
            row["global_rank"] = selection.global_rank
            row["anchor_aux_symbols_file"] = str(anchor_aux_symbols_path.relative_to(out_dir))
            row["anchor_tree_readable_file"] = str(anchor_readable_path.relative_to(out_dir))
            all_anchor_aux_symbol_rows.append(row)
        for row in anchor_macro_aux_metric_rows:
            row = dict(row)
            row["db_idx"] = selection.db_idx
            row["device_id"] = selection.device_id
            row["global_rank"] = selection.global_rank
            row["anchor_macro_aux_metrics_file"] = str(anchor_macro_aux_metrics_path.relative_to(out_dir))
            row["anchor_tree_readable_file"] = str(anchor_readable_path.relative_to(out_dir))
            all_anchor_macro_aux_metric_rows.append(row)
        for row in anchor_root_item_metric_rows:
            row = dict(row)
            row["db_idx"] = selection.db_idx
            row["device_id"] = selection.device_id
            row["global_rank"] = selection.global_rank
            row["anchor_root_item_metrics_file"] = str(anchor_root_item_metrics_path.relative_to(out_dir))
            row["anchor_tree_readable_file"] = str(anchor_readable_path.relative_to(out_dir))
            all_anchor_root_item_metric_rows.append(row)
        for row in anchor_node_metric_rows:
            row = dict(row)
            row["db_idx"] = selection.db_idx
            row["device_id"] = selection.device_id
            row["global_rank"] = selection.global_rank
            row["anchor_node_metrics_file"] = str(anchor_node_metrics_path.relative_to(out_dir))
            row["anchor_tree_readable_file"] = str(anchor_readable_path.relative_to(out_dir))
            all_anchor_node_metric_rows.append(row)
        for row in anchor_node_link_rows:
            row = dict(row)
            row["db_idx"] = selection.db_idx
            row["device_id"] = selection.device_id
            row["global_rank"] = selection.global_rank
            row["anchor_node_links_file"] = str(anchor_node_links_path.relative_to(out_dir))
            row["anchor_tree_readable_file"] = str(anchor_readable_path.relative_to(out_dir))
            all_anchor_node_link_rows.append(row)
        for row in anchor_loop_cost_rows:
            row = dict(row)
            row["anchor_loop_costs_file"] = str(anchor_loop_costs_path.relative_to(out_dir))
            all_anchor_loop_cost_rows.append(row)
        for row in anchor_macro_loop_chain_rows:
            row = dict(row)
            row["db_idx"] = selection.db_idx
            row["device_id"] = selection.device_id
            row["global_rank"] = selection.global_rank
            row["anchor_macro_loop_chains_file"] = str(anchor_macro_loop_chains_path.relative_to(out_dir))
            row["anchor_tree_readable_file"] = str(anchor_readable_path.relative_to(out_dir))
            all_anchor_macro_loop_chain_rows.append(row)

        summary = _device_summary_row(selection, step_rows)
        summary.update(
            {
                "macro_discovery": "pair_grammar",
                "collective_anchor_source": collective_anchor_source,
                "communication_op_event_count": len(communication_op_events),
                "steps_file": str(steps_path.relative_to(out_dir)),
                "symbols_file": str(symbols_path.relative_to(out_dir)),
                "kernel_roles_file": str(kernel_roles_path.relative_to(out_dir)),
                "macros_file": str(macros_path.relative_to(out_dir)),
                "macro_edges_file": str(macro_edges_path.relative_to(out_dir)),
                "macro_metrics_file": str(macro_metrics_path.relative_to(out_dir)),
                "macro_view_file": str(macro_view_path.relative_to(out_dir)),
                "aux_included_macros_file": str(full_macros_path.relative_to(out_dir)),
                "aux_included_macro_edges_file": str(full_macro_edges_path.relative_to(out_dir)),
                "aux_included_macro_metrics_file": str(full_macro_metrics_path.relative_to(out_dir)),
                "aux_included_macro_view_file": str(full_macro_view_path.relative_to(out_dir)),
                "tree_file": str(tree_path.relative_to(out_dir)),
                "tree_readable_file": str(readable_path.relative_to(out_dir)),
                "raw_tree_file": str(raw_tree_path.relative_to(out_dir)),
                "raw_tree_readable_file": str(raw_readable_path.relative_to(out_dir)),
                "aux_included_tree_file": str(full_tree_path.relative_to(out_dir)),
                "aux_included_tree_readable_file": str(full_readable_path.relative_to(out_dir)),
                "anchor_event_count": len(anchor_events),
                "collective_anchor_event_count": sum(1 for item in anchor_events if item.role == "collective"),
                "aux_event_count": sum(1 for role in semantic_roles if role == "aux"),
                "anchor_steps_file": str(anchor_steps_path.relative_to(out_dir)),
                "anchor_symbols_file": str(anchor_symbols_path.relative_to(out_dir)),
                "anchor_aux_slots_file": str(anchor_aux_slots_path.relative_to(out_dir)),
                "anchor_aux_symbols_file": str(anchor_aux_symbols_path.relative_to(out_dir)),
                "anchor_macros_file": str(anchor_macros_path.relative_to(out_dir)),
                "anchor_macro_edges_file": str(anchor_macro_edges_path.relative_to(out_dir)),
                "anchor_macro_metrics_file": str(anchor_macro_metrics_path.relative_to(out_dir)),
                "anchor_macro_aux_metrics_file": str(anchor_macro_aux_metrics_path.relative_to(out_dir)),
                "anchor_macro_view_file": str(anchor_macro_view_path.relative_to(out_dir)),
                "anchor_root_item_metrics_file": str(anchor_root_item_metrics_path.relative_to(out_dir)),
                "anchor_node_metrics_file": str(anchor_node_metrics_path.relative_to(out_dir)),
                "anchor_node_links_file": str(anchor_node_links_path.relative_to(out_dir)),
                "anchor_loop_costs_file": str(anchor_loop_costs_path.relative_to(out_dir)),
                "anchor_macro_loop_chains_file": str(anchor_macro_loop_chains_path.relative_to(out_dir)),
                "anchor_tree_file": str(anchor_tree_path.relative_to(out_dir)),
                "anchor_tree_readable_file": str(anchor_readable_path.relative_to(out_dir)),
            }
        )
        summary_rows.append(summary)

    _write_csv(out_dir / "compute_prelude_steps.csv", all_step_rows)
    _write_csv(out_dir / "compute_prelude_symbols.csv", all_symbol_rows)
    _write_csv(out_dir / "compute_prelude_macro_edges.csv", all_macro_edge_rows)
    _write_csv(out_dir / "compute_prelude_macro_metrics.csv", all_macro_metric_rows)
    _write_csv(out_dir / "compute_prelude_macro_view.csv", all_macro_view_rows)
    _write_csv(out_dir / "compute_prelude_kernel_roles.csv", all_kernel_role_rows)
    _write_csv(out_dir / "compute_anchor_aux_slots.csv", all_anchor_aux_slot_rows)
    _write_csv(out_dir / "compute_anchor_aux_symbols.csv", all_anchor_aux_symbol_rows)
    _write_csv(out_dir / "compute_anchor_macro_aux_metrics.csv", all_anchor_macro_aux_metric_rows)
    _write_csv(out_dir / "compute_anchor_root_item_metrics.csv", all_anchor_root_item_metric_rows)
    _write_csv(out_dir / "compute_anchor_node_metrics.csv", all_anchor_node_metric_rows)
    _write_csv(out_dir / "compute_anchor_node_anchor_links.csv", all_anchor_node_link_rows)
    _write_csv(out_dir / "compute_anchor_loop_costs.csv", all_anchor_loop_cost_rows)
    _write_csv(out_dir / "compute_anchor_macro_loop_chains.csv", all_anchor_macro_loop_chain_rows)
    _write_csv(out_dir / "device_summary.csv", summary_rows)

    meta = {
        "version": "compute_prelude_timeline_v1",
        "run_dir": str(run_dir.resolve()),
        "msprof_raw_dir": str(raw_dir.resolve()),
        "db_count": len(db_paths),
        "device_count": len(selections),
        "top_devices_global": cfg.top_devices_global,
        "device_ids": list(cfg.device_ids) if cfg.device_ids is not None else None,
        "max_main_events_per_device": cfg.max_main_events_per_device,
        "max_macro_defs": cfg.max_macro_defs,
        "collective_episode_gap_us": cfg.collective_episode_gap_us,
        "collective_anchor_source": "communication_op_if_available",
        "macro_discovery": "pair_grammar",
        "readable_macro_mode": cfg.readable_macro_mode,
        "loop_promotion_methods": LOOP_PROMOTION_METHODS,
        "semantic_projection": "anchor_compute_collective_only",
        "auxiliary_attribution": "attach_aux_events_to_following_anchor_prelude_slot",
        "kernel_role_file": str(cfg.kernel_role_file.resolve()) if cfg.kernel_role_file is not None else "",
        "summary_top_loops": cfg.summary_top_loops,
        "elapsed_sec": round(time.time() - started, 3),
        "summary_file": str(out_dir / "device_summary.csv"),
        "steps_file": str(out_dir / "compute_prelude_steps.csv"),
        "symbols_file": str(out_dir / "compute_prelude_symbols.csv"),
        "macro_edges_file": str(out_dir / "compute_prelude_macro_edges.csv"),
        "macro_metrics_file": str(out_dir / "compute_prelude_macro_metrics.csv"),
        "macro_view_file": str(out_dir / "compute_prelude_macro_view.csv"),
        "kernel_roles_file": str(out_dir / "compute_prelude_kernel_roles.csv"),
        "anchor_aux_slots_file": str(out_dir / "compute_anchor_aux_slots.csv"),
        "anchor_aux_symbols_file": str(out_dir / "compute_anchor_aux_symbols.csv"),
        "anchor_macro_aux_metrics_file": str(out_dir / "compute_anchor_macro_aux_metrics.csv"),
        "anchor_root_item_metrics_file": str(out_dir / "compute_anchor_root_item_metrics.csv"),
        "anchor_node_metrics_file": str(out_dir / "compute_anchor_node_metrics.csv"),
        "anchor_node_links_file": str(out_dir / "compute_anchor_node_anchor_links.csv"),
        "anchor_loop_costs_file": str(out_dir / "compute_anchor_loop_costs.csv"),
        "anchor_macro_loop_chains_file": str(out_dir / "compute_anchor_macro_loop_chains.csv"),
    }
    summary_text = _build_run_summary_markdown(
        summary_rows=summary_rows,
        loop_cost_rows=all_anchor_loop_cost_rows,
        out_dir=out_dir,
        top_loops=cfg.summary_top_loops,
    )
    summary_path = out_dir / "summary.md"
    summary_path.write_text(summary_text, encoding="utf-8")
    meta["run_summary_file"] = str(summary_path)
    (out_dir / "meta.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return meta


def _resolve_msprof_raw_dir(run_dir: Path) -> Path:
    run_dir = run_dir.resolve()
    candidates = [
        run_dir,
        run_dir / "msprof_raw",
        run_dir.parent / "msprof_raw" if run_dir.name == "hprofile_processed" else run_dir,
    ]
    last_error: Exception | None = None
    for candidate in candidates:
        if not candidate.exists():
            continue
        try:
            discover_msprof_dbs(candidate)
        except FileNotFoundError as exc:
            last_error = exc
            continue
        return candidate
    if last_error is not None:
        raise last_error
    raise FileNotFoundError(f"run_dir does not exist or contains no msprof raw data: {run_dir}")


def _parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compute-centered loop tree with prelude communication summaries."
    )
    parser.add_argument("run_dir", type=Path, help="hprofile/msprof run directory containing msprof db files")
    parser.add_argument("--out-dir", type=Path, default=None)
    parser.add_argument("--top-devices-global", type=int, default=ComputePreludeConfig.top_devices_global)
    parser.add_argument(
        "--devices",
        default="",
        help="Comma-separated physical device IDs to analyze, for example 3,4,5,6. Empty means all ranked devices.",
    )
    parser.add_argument(
        "--max-main-events-per-device",
        type=int,
        default=ComputePreludeConfig.max_main_events_per_device,
        help="Maximum main compute/data-move events per device; 0 means no truncation.",
    )
    parser.add_argument(
        "--max-macro-defs",
        type=int,
        default=ComputePreludeConfig.max_macro_defs,
        help="Maximum macro definitions; 0 means keep discovering pair-grammar macros while gain is positive.",
    )
    parser.add_argument(
        "--collective-episode-gap-us",
        type=float,
        default=ComputePreludeConfig.collective_episode_gap_us,
        help=(
            "Fallback only: merge same-label collective TASK fragments by this gap when COMMUNICATION_OP is absent."
        ),
    )
    parser.add_argument("--min-main-event-us", type=float, default=ComputePreludeConfig.min_main_event_us)
    parser.add_argument(
        "--readable-macro-mode",
        choices=("inline", "auto"),
        default=ComputePreludeConfig.readable_macro_mode,
        help=(
            "Macro rendering policy for readable trees. inline expands discovered macros while preserving LP Repeat "
            "nodes; auto keeps selected high-value macros in the readable view."
        ),
    )
    parser.add_argument(
        "--kernel-role-file",
        type=Path,
        default=None,
        help=(
            "Optional CSV overriding kernel semantic roles. Rows may contain semantic_role/role plus "
            "symbol, label, task_type, family, or contains columns. Valid roles: anchor, aux, transparent."
        ),
    )
    parser.add_argument(
        "--summary-top-loops",
        type=int,
        default=ComputePreludeConfig.summary_top_loops,
        help="Number of high-cost repeat nodes to show in summary.md and readable reports.",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Print full meta.json payload to stdout instead of the concise run summary.",
    )
    return parser.parse_args(argv)


def _parse_device_ids(value: str) -> Tuple[int, ...] | None:
    value = value.strip()
    if not value:
        return None
    return tuple(int(part.strip()) for part in value.split(",") if part.strip())


def main(argv: Sequence[str] | None = None) -> int:
    args = _parse_args(argv)
    run_dir = args.run_dir.resolve()
    out_dir = (
        args.out_dir.resolve()
        if args.out_dir is not None
        else run_dir / "hprofile_processed" / "derived" / "compute_prelude_timeline"
    )
    cfg = ComputePreludeConfig(
        top_devices_global=args.top_devices_global,
        max_main_events_per_device=args.max_main_events_per_device,
        max_macro_defs=args.max_macro_defs,
        collective_episode_gap_us=args.collective_episode_gap_us,
        min_main_event_us=args.min_main_event_us,
        readable_macro_mode=args.readable_macro_mode,
        kernel_role_file=args.kernel_role_file,
        summary_top_loops=args.summary_top_loops,
        device_ids=_parse_device_ids(args.devices),
    )
    meta = run_compute_prelude_timeline(run_dir=run_dir, out_dir=out_dir, config=cfg)
    if args.json:
        print(json.dumps(meta, ensure_ascii=False, indent=2))
    else:
        print(_format_console_summary(meta))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
