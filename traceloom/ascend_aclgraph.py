from __future__ import annotations

import csv
import hashlib
import json
import math
import re
import sqlite3
from collections import Counter
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Any, Sequence

from .msprof_reader import canonical_device_label


GRAPH_TASK_KEYS = {
    "MODEL_EXECUTE",
    "MODEL_MAINTAINCE",
    "MODEL_MAINTENANCE",
    "NOTIFY_WAIT",
    "NOTIFY_RECORD",
}

GRAPH_BODY_EXCLUDED_KEYS = {
    "AI_CORE",
    "CAPTURE_RECORD",
    "CAPTURE_WAIT",
    "EVENT_RECORD",
    "MEM_WRITE_VALUE",
    "MEMCPY",
    "MEMCPY_ASYNC",
    "MODEL_EXECUTE",
    "MODEL_MAINTAINCE",
    "MODEL_MAINTENANCE",
    "NOTIFY",
    "NOTIFY_RECORD",
    "NOTIFY_WAIT",
    "SDMA",
    "WRITE_VALUE",
}

GRAPH_BODY_ANCHOR_FAMILIES = {
    "attention",
    "cast",
    "compare",
    "concat",
    "div",
    "elemwise",
    "fill",
    "index",
    "matmul",
    "norm",
    "pow",
    "quant",
    "range",
    "rope",
    "shape",
    "swiglu",
    "trig",
}

GRAPH_BODY_ANCHOR_KEYWORDS = (
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

GRAPH_BODY_AUX_KEYWORDS = (
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

ZERO_PRELUDE = {
    "prev_compute_overlap_us": 0.0,
    "prelude_start_ns": 0,
    "prelude_end_ns": 0,
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
    "prelude_idle_ratio": 0.0,
    "prelude_comm_ratio": 0.0,
    "prelude_wait_ratio": 0.0,
}


@dataclass(frozen=True)
class AclGraphAnalysis:
    step_rows: list[dict[str, object]]
    replay_rows: list[dict[str, object]]
    envelope_rows: list[dict[str, object]]
    semantic_task_rows: list[dict[str, object]]
    model_stream_rows: list[dict[str, object]]
    top_op_rows: list[dict[str, object]]
    summary: dict[str, object]


def analyze_aclgraph_for_device(
    *,
    db_path: Path,
    db_idx: int,
    global_rank: int,
    device_id: int,
    visible_step_rows: Sequence[dict[str, object]],
    first_step_idx: int,
    gap_us: float = 5000.0,
) -> AclGraphAnalysis:
    prof_dir = db_path.parent
    stream_info_path = prof_dir / "host" / "sqlite" / "stream_info.db"
    ascend_task_paths = sorted(prof_dir.glob(f"device_{device_id}/sqlite/ascend_task.db"))
    if not ascend_task_paths:
        ascend_task_paths = sorted(prof_dir.glob("device_*/sqlite/ascend_task.db"))

    capture_streams = _load_capture_streams(stream_info_path, device_id=device_id)
    mapped_stream_ids = {int(row["model_stream_id"]) for row in capture_streams}
    original_stream_ids = {int(row["original_stream_id"]) for row in capture_streams}
    with sqlite3.connect(str(db_path)) as conn:
        conn.row_factory = sqlite3.Row
        if not _table_exists(conn, "TASK") or not _table_exists(conn, "STRING_IDS"):
            return _empty_analysis(db_path, stream_info_path, device_id, db_idx, gap_us)
        strings = _load_string_ids(conn)
        compute = _load_compute_info(conn, strings)
        graph_task_type_ids = _task_type_ids_for_keys(strings, GRAPH_TASK_KEYS)
        task_rows = _load_tasks(
            conn,
            strings,
            device_id=device_id,
            stream_ids=mapped_stream_ids,
            task_type_ids=graph_task_type_ids,
        )
    rows_by_stream = _bucket_rows_by_stream(task_rows)
    graph_model_tasks = [
        row
        for stream_id in sorted(mapped_stream_ids)
        for row in rows_by_stream.get(stream_id, ())
        if row["end_ns"] > row["start_ns"]
    ]
    semantic_tasks = _semantic_task_rows(task_rows, db_idx=db_idx)
    semantic_by_key = _semantic_rows_by_key(semantic_tasks)
    model_stream_rows = _summarize_model_streams(
        rows_by_stream=rows_by_stream,
        mapped_stream_ids=mapped_stream_ids,
        capture_streams=capture_streams,
        compute=compute,
        db_idx=db_idx,
    )
    activity_segments = _segment_tasks(graph_model_tasks, gap_ns=int(gap_us * 1000.0))
    model_execs_by_activity_segment = _model_execute_controls_by_segment(
        activity_segments,
        semantic_by_key=semantic_by_key,
    )
    wave_size = _infer_model_execute_wave_size(
        activity_segments,
        model_execs_by_segment=model_execs_by_activity_segment,
    )
    segments, segment_metadata = _split_segments_into_replay_units(
        activity_segments,
        semantic_by_key=semantic_by_key,
        compute=compute,
    )
    controls_by_segment = _graph_controls_by_segment(segments, semantic_by_key=semantic_by_key)
    step_rows, replay_rows, top_op_rows = _build_replay_rows(
        db_path=db_path,
        db_idx=db_idx,
        global_rank=global_rank,
        device_id=device_id,
        first_step_idx=first_step_idx,
        segments=segments,
        segment_metadata=segment_metadata,
        controls_by_segment=controls_by_segment,
        compute=compute,
    )
    envelope_rows = _build_envelope_rows(
        replay_rows=replay_rows,
        visible_step_rows=visible_step_rows,
    )
    replay_rows = _augment_replay_rows(replay_rows, envelope_rows)
    ascend_task_summary = _load_ascend_task_summary(ascend_task_paths)

    summary = {
        "schema": "aclgraph.msprof.device_timeline.v1",
        "db_idx": db_idx,
        "msprof_db": str(db_path),
        "stream_info_db": str(stream_info_path) if stream_info_path.exists() else "",
        "ascend_task_dbs": [str(path) for path in ascend_task_paths],
        "device_id": device_id,
        "capture_stream_count": len(capture_streams),
        "original_streams": _compact_ints(original_stream_ids),
        "mapped_model_streams": _compact_ints(mapped_stream_ids),
        "active_model_streams": _compact_ints({row["stream_id"] for row in graph_model_tasks}),
        "active_model_stream_count": len({row["stream_id"] for row in graph_model_tasks}),
        "replay_segment_count": len(replay_rows),
        "activity_segment_count": len(activity_segments),
        "replay_activity_count": len(activity_segments),
        "replay_unit_count": len(replay_rows),
        "model_execute_wave_size": wave_size,
        "replay_total_device_us": round(sum(float(row["dur_us"] or 0.0) for row in replay_rows), 3),
        "replay_child_task_count": sum(int(row.get("raw_child_task_count", 0) or 0) for row in replay_rows),
        "semantic_task_count": len(semantic_tasks),
        "semantic_task_count_by_label": dict(Counter(row["task_label"] for row in semantic_tasks)),
        "semantic_timed_task_count_by_label": dict(
            Counter(row["task_label"] for row in semantic_tasks if int(row["end_ns"]) > int(row["start_ns"]))
        ),
        "ascend_task_summary": ascend_task_summary,
        "gap_us": gap_us,
        "quality": _classify_quality(
            capture_stream_count=len(capture_streams),
            active_model_stream_count=len({row["stream_id"] for row in graph_model_tasks}),
            replay_segment_count=len(replay_rows),
            semantic_task_count=len(semantic_tasks),
            replay_child_task_count=sum(int(row.get("raw_child_task_count", 0) or 0) for row in replay_rows),
        ),
    }
    return AclGraphAnalysis(
        step_rows=step_rows,
        replay_rows=replay_rows,
        envelope_rows=envelope_rows,
        semantic_task_rows=semantic_tasks,
        model_stream_rows=model_stream_rows,
        top_op_rows=top_op_rows,
        summary=summary,
    )


def write_aclgraph_outputs(
    *,
    out_dir: Path,
    analyses: Sequence[AclGraphAnalysis],
    write_csv_outputs: bool,
) -> dict[str, object]:
    replay_rows = _merge_rows(analyses, "replay_rows")
    envelope_rows = _merge_rows(analyses, "envelope_rows")
    semantic_rows = _merge_rows(analyses, "semantic_task_rows")
    model_stream_rows = _merge_rows(analyses, "model_stream_rows")
    top_op_rows = _merge_rows(analyses, "top_op_rows")
    summary = _merge_summaries([analysis.summary for analysis in analyses])

    summary_json = out_dir / "aclgraph_summary.json"
    summary_md = out_dir / "aclgraph_summary.md"
    summary_json.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    summary_md.write_text(_summary_markdown(summary, replay_rows), encoding="utf-8")

    files = {
        "aclgraph_summary_file": str(summary_json),
        "aclgraph_summary_markdown_file": str(summary_md),
    }
    if write_csv_outputs:
        files.update(
            {
                "aclgraph_events_file": str(out_dir / "aclgraph_events.csv"),
                "aclgraph_envelope_file": str(out_dir / "aclgraph_envelope_events.csv"),
                "aclgraph_semantic_tasks_file": str(out_dir / "aclgraph_semantic_tasks.csv"),
                "aclgraph_model_streams_file": str(out_dir / "aclgraph_model_streams.csv"),
                "aclgraph_top_ops_file": str(out_dir / "aclgraph_top_ops.csv"),
            }
        )
        _write_csv(out_dir / "aclgraph_events.csv", replay_rows)
        _write_csv(out_dir / "aclgraph_envelope_events.csv", envelope_rows)
        _write_csv(out_dir / "aclgraph_semantic_tasks.csv", semantic_rows)
        _write_csv(out_dir / "aclgraph_model_streams.csv", model_stream_rows)
        _write_csv(out_dir / "aclgraph_top_ops.csv", top_op_rows)
    return {**summary, **files}


def _empty_analysis(db_path: Path, stream_info_path: Path, device_id: int, db_idx: int, gap_us: float) -> AclGraphAnalysis:
    summary = {
        "schema": "aclgraph.msprof.device_timeline.v1",
        "db_idx": db_idx,
        "msprof_db": str(db_path),
        "stream_info_db": str(stream_info_path) if stream_info_path.exists() else "",
        "device_id": device_id,
        "capture_stream_count": 0,
        "active_model_stream_count": 0,
        "replay_segment_count": 0,
        "replay_total_device_us": 0.0,
        "replay_child_task_count": 0,
        "semantic_task_count": 0,
        "semantic_task_count_by_label": {},
        "semantic_timed_task_count_by_label": {},
        "gap_us": gap_us,
        "quality": "insufficient_aclgraph_signal",
    }
    return AclGraphAnalysis([], [], [], [], [], [], summary)


def _build_replay_rows(
    *,
    db_path: Path,
    db_idx: int,
    global_rank: int,
    device_id: int,
    first_step_idx: int,
    segments: Sequence[Sequence[dict[str, Any]]],
    segment_metadata: Sequence[dict[str, object]],
    controls_by_segment: Sequence[Sequence[dict[str, object]]],
    compute: dict[int, dict[str, str]],
) -> tuple[list[dict[str, object]], list[dict[str, object]], list[dict[str, object]]]:
    step_rows: list[dict[str, object]] = []
    replay_rows: list[dict[str, object]] = []
    top_op_rows: list[dict[str, object]] = []
    graph_type_by_hash: dict[str, str] = {}
    graph_template_by_hash: dict[str, str] = {}
    for replay_idx, segment in enumerate(segments, start=1):
        metadata = segment_metadata[replay_idx - 1] if replay_idx - 1 < len(segment_metadata) else {}
        start_ns = min(int(row["start_ns"]) for row in segment)
        end_ns = max(int(row["end_ns"]) for row in segment)
        streams = sorted({int(row["stream_id"]) for row in segment})
        task_type_counter: Counter[str] = Counter()
        op_counter: Counter[str] = Counter()
        stream_dur: Counter[int] = Counter()
        kernel_count = 0
        kernel_us = 0.0
        body_counter: Counter[str] = Counter()
        body_noise_counter: Counter[str] = Counter()
        for row in segment:
            dur_ns = max(0, int(row["end_ns"]) - int(row["start_ns"]))
            stream_dur[int(row["stream_id"])] += dur_ns
            info = compute.get(int(row["global_task_id"]), {})
            op = info.get("op_type") or info.get("op_name") or str(row["task_label"])
            op_counter[op] += 1
            task_type_counter[str(row["task_label"])] += 1
            body_token = _canonical_graph_body_token(row, info)
            if body_token:
                body_counter[body_token] += 1
            else:
                body_noise_counter[_canonical_graph_noise_token(row, info)] += 1
            if info.get("op_type") or "AI_CORE" in str(row["task_label"]).upper():
                kernel_count += 1
                kernel_us += dur_ns / 1000.0
        controls = (
            list(controls_by_segment[replay_idx - 1])
            if replay_idx - 1 < len(controls_by_segment)
            else []
        )
        control_counter = Counter(str(row["task_label"]) for row in controls)
        body_signature = _format_signature(body_counter)
        body_noise_signature = _format_signature(body_noise_counter)
        control_signature = _format_signature(control_counter)
        replay_unit_count, replay_unit_source = _infer_graph_replay_unit_count(body_counter, control_counter)
        template_signature = _format_unit_signature(body_counter, replay_unit_count)
        body_hash = _stable_hash(body_signature)
        template_hash = _stable_hash(template_signature)
        envelope_hash = _stable_hash(f"{body_hash}\n{control_signature}\nstreams={_compact_ints(streams)}")
        graph_type_symbol = graph_type_by_hash.get(body_hash)
        if graph_type_symbol is None:
            graph_type_symbol = f"G{len(graph_type_by_hash) + 1:03d}"
            graph_type_by_hash[body_hash] = graph_type_symbol
        graph_template_symbol = graph_template_by_hash.get(template_hash)
        if graph_template_symbol is None:
            graph_template_symbol = f"T{len(graph_template_by_hash) + 1:03d}"
            graph_template_by_hash[template_hash] = graph_template_symbol
        graph_replay_symbol = f"{graph_template_symbol}x{replay_unit_count}"
        graph_replay_hash = _stable_hash(f"{template_hash}\nunit_count={replay_unit_count}")
        step_idx = first_step_idx + replay_idx - 1
        primary_stream = max(stream_dur.items(), key=lambda item: (item[1], -item[0]))[0] if stream_dur else -1
        source_key = f"provider=aclgraph;replay_idx={replay_idx};streams={_compact_ints(streams)}"
        common = {
            "graph_provider": "aclgraph",
            "graph_kind": "aclgraph_replay_unit",
            "graph_event_idx": replay_idx,
            "graph_activity_idx": metadata.get("graph_activity_idx", replay_idx),
            "graph_activity_indices": metadata.get("graph_activity_indices", metadata.get("graph_activity_idx", replay_idx)),
            "graph_activity_unit_idx": metadata.get("graph_activity_unit_idx", 1),
            "graph_activity_unit_count": metadata.get("graph_activity_unit_count", 1),
            "graph_activity_expected_unit_count": metadata.get("graph_activity_expected_unit_count", 1),
            "graph_activity_unit_source": metadata.get("graph_activity_unit_source", ""),
            "graph_activity_start_ns": metadata.get("graph_activity_start_ns", start_ns),
            "graph_activity_end_ns": metadata.get("graph_activity_end_ns", end_ns),
            "graph_activity_split_source": metadata.get("graph_activity_split_source", "single"),
            "db_idx": db_idx,
            "db": str(db_path),
            "global_rank": global_rank,
            "device_id": device_id,
            "step_idx": step_idx,
            "symbol": "ACLGRAPH",
            "semantic_role": "graph",
            "semantic_role_reason": "aclgraph_replay_unit",
            "label": f"ACLGraph ReplayUnit {replay_idx}",
            "graph_type_symbol": graph_type_symbol,
            "graph_type_label": f"ACLGraphType {graph_type_symbol}",
            "graph_exact_type_symbol": graph_type_symbol,
            "graph_exact_type_label": f"ACLGraphType {graph_type_symbol}",
            "graph_template_symbol": graph_template_symbol,
            "graph_template_label": f"ACLGraphTemplate {graph_template_symbol}",
            "graph_template_hash": template_hash,
            "graph_template_signature": template_signature,
            "graph_template_hash_policy": "anchor_compute_unit_body_v1",
            "graph_replay_symbol": graph_replay_symbol,
            "graph_replay_label": f"ACLGraph {graph_template_symbol} x{replay_unit_count}",
            "graph_replay_hash": graph_replay_hash,
            "graph_replay_unit_count": replay_unit_count,
            "graph_replay_unit_source": replay_unit_source,
            "graph_body_hash": body_hash,
            "graph_envelope_hash": envelope_hash,
            "graph_body_token_count": sum(body_counter.values()),
            "graph_body_signature": body_signature,
            "graph_body_noise_signature": body_noise_signature,
            "graph_body_hash_policy": "anchor_compute_body_v1",
            "graph_control_signature": control_signature,
            "task_type": "ACL_GRAPH_REPLAY_UNIT",
            "source_table": "ACLGRAPH_REPLAY",
            "source_key": source_key,
            "stream_id": primary_stream,
            "correlation_id": "",
            "graph_id": f"aclgraph:{db_idx}:{device_id}:activity:{metadata.get('graph_activity_idx', replay_idx)}",
            "graph_exec_id": f"aclgraph:{db_idx}:{device_id}:unit:{replay_idx}",
            "context_id": "",
            "start_ns": start_ns,
            "end_ns": end_ns,
            "dur_us": round((end_ns - start_ns) / 1000.0, 3),
            "raw_child_task_count": len(segment),
            "raw_child_stream_count": len(streams),
            "raw_child_streams": _compact_ints(streams),
            "raw_control_task_count": len(controls),
            "raw_control_tasks": _format_counter(control_counter, limit=8),
            "raw_task_types": _format_counter(task_type_counter, limit=8),
            "raw_top_ops": _format_counter(op_counter, limit=10),
            "enclosed_event_count": len(segment),
            "enclosed_event_us": round((end_ns - start_ns) / 1000.0, 3),
            "enclosed_kernel_count": kernel_count,
            "enclosed_kernel_us": round(kernel_us, 3),
            "enclosed_top_labels": _format_counter(op_counter, limit=6),
            "aclgraph_events_file": "",
            "anchor_tree_readable_file": "",
        }
        step_rows.append(
            {
                **common,
                "role": "compute",
                "family": "aclgraph",
                "source_event_count": len(segment),
                "source_streams": " ".join(str(stream) for stream in streams),
                **ZERO_PRELUDE,
                "prelude_start_ns": start_ns,
                "prelude_end_ns": start_ns,
            }
        )
        replay_rows.append(common)
        for rank, (op, count) in enumerate(op_counter.most_common(20), start=1):
            top_op_rows.append(
                {
                    "db_idx": db_idx,
                    "device_id": device_id,
                    "replay_idx": replay_idx,
                    "rank": rank,
                    "op": op,
                    "count": count,
                }
            )
    return step_rows, replay_rows, top_op_rows


def _format_signature(counter: Counter[str]) -> str:
    return "\n".join(f"{name}:{count}" for name, count in sorted(counter.items()))


def _format_unit_signature(counter: Counter[str], unit_count: int) -> str:
    unit_count = max(1, int(unit_count))
    return "\n".join(
        f"{name}:{_format_unit_count(count, unit_count)}"
        for name, count in sorted(counter.items())
    )


def _format_unit_count(count: int, unit_count: int) -> str:
    if unit_count <= 1:
        return str(count)
    if count % unit_count == 0:
        return str(count // unit_count)
    return f"{count}/{unit_count}"


def _infer_graph_replay_unit_count(
    body_counter: Counter[str],
    control_counter: Counter[str],
) -> tuple[int, str]:
    # A replay segment can still contain several identical graph replay units.
    # The old exact body hash treated x1/x2/xN repetitions as unrelated graph
    # types. Prefer an explicit per-unit body landmark when present, then fall
    # back to control waves so the exact Gxxx hash remains queryable while the
    # visible graph identity can include replay length.
    for token in ("index|Index", "attention|Attention"):
        count = int(body_counter.get(token, 0))
        if count > 0:
            return count, f"body:{token}"
    notify_wait = int(control_counter.get("NOTIFY_WAIT", 0))
    if notify_wait > 0:
        if notify_wait % 29 == 0:
            return max(1, notify_wait // 29), "control:NOTIFY_WAIT/29"
        return notify_wait, "control:NOTIFY_WAIT"
    positive_counts = [int(value) for value in body_counter.values() if int(value) > 0]
    if positive_counts:
        common = positive_counts[0]
        for value in positive_counts[1:]:
            common = math.gcd(common, value)
        if common > 1:
            return common, "body:gcd"
    return 1, "fallback:single"


def _stable_hash(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def _canonical_graph_body_token(row: dict[str, Any], info: dict[str, str]) -> str:
    raw_op = info.get("op_type") or info.get("op_name") or str(row.get("task_label", ""))
    task = str(row.get("task_label", ""))
    return _canonical_graph_body_token_from_labels(task, str(raw_op), str(info.get("op_type", "")))


@lru_cache(maxsize=65536)
def _canonical_graph_body_token_from_labels(task: str, raw_op: str, op_type: str) -> str:
    # Graph body hashing used to classify every child task independently even
    # though graph replays contain long repetitions of the same task/op labels.
    # Cache the pure label-to-token lowering so the replay scan still visits
    # every child for counts, but expensive canonicalization scales with the
    # number of distinct graph operator labels instead of child task count.
    op = canonical_device_label(raw_op, op_type, category="exec")
    task_key = _normalize_key(task)
    family = _graph_body_family(op)
    low = f"{op} {task} {family}".lower()
    if task_key in GRAPH_BODY_EXCLUDED_KEYS:
        return ""
    if family in GRAPH_BODY_ANCHOR_FAMILIES or any(keyword in low for keyword in GRAPH_BODY_ANCHOR_KEYWORDS):
        # Keep this intentionally free of timestamps, generated ids, and stream ids.
        # Shape/dtype can be added here later when msprof exposes enough metadata.
        return f"{family}|{op or task}"
    if family and family not in {"data_move"}:
        return f"{family}|{op or task}"
    return ""


def _canonical_graph_noise_token(row: dict[str, Any], info: dict[str, str]) -> str:
    raw_op = info.get("op_type") or info.get("op_name") or str(row.get("task_label", ""))
    task = str(row.get("task_label", ""))
    return _canonical_graph_noise_token_from_labels(task, str(raw_op), str(info.get("op_type", "")))


@lru_cache(maxsize=65536)
def _canonical_graph_noise_token_from_labels(task: str, raw_op: str, op_type: str) -> str:
    op = canonical_device_label(raw_op, op_type, category="exec")
    task_key = _normalize_key(task)
    if task_key in GRAPH_BODY_EXCLUDED_KEYS:
        return f"control_or_transfer:{task_key.lower()}"
    family = _graph_body_family(op)
    low = f"{op} {task} {family}".lower()
    for keyword in GRAPH_BODY_AUX_KEYWORDS:
        if keyword in low:
            return f"aux_keyword:{keyword}"
    if family:
        return f"non_anchor_family:{family}"
    return f"non_anchor_task:{task_key.lower() or 'unknown'}"


@lru_cache(maxsize=65536)
def _graph_body_family(label: str) -> str:
    low = (label or "").strip().lower()
    if not low:
        return ""
    if "matmul" in low or "gemm" in low:
        return "matmul"
    if "rmsnorm" in low or "rms_norm" in low or "layernorm" in low or "layer_norm" in low:
        return "norm"
    if "pagedattention" in low or "paged_attention" in low or "attention" in low:
        return "attention"
    if "swiglu" in low:
        return "swiglu"
    if "rope" in low or "rotary" in low:
        return "rope"
    if "cast" in low:
        return "cast"
    if "fill" in low or "zeroslike" in low or "oneslike" in low:
        return "fill"
    if "shape" in low or "reshape" in low or "transpose" in low or "slice" in low or "tile" in low or "broadcastto" in low or "expand" in low:
        return "shape"
    if "index" in low or "gather" in low or "scatter" in low:
        return "index"
    if "range" in low or "arange" in low:
        return "range"
    if "div" in low or "reciprocal" in low:
        return "div"
    if "pow" in low:
        return "pow"
    if "trig" in low or "cos" in low or "sin" in low:
        return "trig"
    if "concat" in low or low == "cat":
        return "concat"
    if "quant" in low:
        return "quant"
    if "compare" in low or "greaterequal" in low or "less" in low or "logical" in low or "bitwise" in low:
        return "compare"
    if "elemwise" in low or low in {"add", "sub", "mul"}:
        return "elemwise"
    if "copy" in low or "memcpy" in low or "tensor move" in low:
        return "data_move"
    return low[:64].strip().replace(" ", "_")


def _build_envelope_rows(
    *,
    replay_rows: Sequence[dict[str, object]],
    visible_step_rows: Sequence[dict[str, object]],
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    envelope_idx = 0
    candidates = [
        row
        for row in visible_step_rows
        if str(row.get("source_table", "")) != "ACLGRAPH_REPLAY"
        and _safe_int(row.get("end_ns")) > _safe_int(row.get("start_ns"))
    ]
    replay_bounds = [
        (_safe_int(replay.get("start_ns")), _safe_int(replay.get("end_ns")))
        for replay in replay_rows
    ]
    # The envelope used to be a direct replay x visible-event scan.  That was
    # acceptable for smoke traces, but a long decode run may contain hundreds of
    # graph replays and many visible events.  Reuse the same interval sweep as
    # semantic control matching, with half-open overlap semantics to preserve the
    # previous boundary behavior for envelope rows.
    candidates_by_replay = _overlap_rows_by_interval(
        replay_bounds,
        candidates,
        touching_overlaps=False,
    )
    for replay_idx, replay in enumerate(replay_rows):
        graph_start = _safe_int(replay.get("start_ns"))
        graph_end = _safe_int(replay.get("end_ns"))
        graph_step_idx = _safe_int(replay.get("step_idx"))
        if graph_end <= graph_start:
            continue
        replay_candidates = candidates_by_replay[replay_idx] if replay_idx < len(candidates_by_replay) else ()
        for child in replay_candidates:
            child_start = _safe_int(child.get("start_ns"))
            child_end = _safe_int(child.get("end_ns"))
            envelope_idx += 1
            contained = child_start >= graph_start and child_end <= graph_end
            rows.append(
                {
                    "graph_provider": "aclgraph",
                    "graph_kind": "aclgraph_replay",
                    "envelope_idx": envelope_idx,
                    "db_idx": replay.get("db_idx", ""),
                    "db": replay.get("db", ""),
                    "global_rank": replay.get("global_rank", ""),
                    "device_id": replay.get("device_id", ""),
                    "graph_step_idx": graph_step_idx,
                    "graph_symbol": replay.get("symbol", ""),
                    "graph_label": replay.get("label", ""),
                    "graph_stream_id": replay.get("stream_id", ""),
                    "graph_correlation_id": "",
                    "graph_id": replay.get("graph_id", ""),
                    "graph_exec_id": replay.get("graph_exec_id", ""),
                    "graph_context_id": "",
                    "graph_start_ns": graph_start,
                    "graph_end_ns": graph_end,
                    "graph_dur_us": replay.get("dur_us", ""),
                    "child_step_idx": child.get("step_idx", ""),
                    "child_symbol": child.get("symbol", ""),
                    "child_label": child.get("label", ""),
                    "child_task_type": child.get("task_type", ""),
                    "child_source_table": child.get("source_table", ""),
                    "child_stream_id": child.get("stream_id", ""),
                    "child_start_ns": child_start,
                    "child_end_ns": child_end,
                    "child_dur_us": child.get("dur_us", ""),
                    "start_offset_us": round((child_start - graph_start) / 1000.0, 3),
                    "end_offset_us": round((child_end - graph_start) / 1000.0, 3),
                    "stream_relation": (
                        "model_stream"
                        if str(child.get("stream_id", "")) in str(replay.get("raw_child_streams", "")).split(",")
                        else "visible_overlap"
                    ),
                    "relation": "time_contained" if contained else "time_overlap",
                    "cuda_graph_envelope_file": "",
                    "cuda_graph_events_file": "",
                    "aclgraph_envelope_file": "",
                    "aclgraph_events_file": "",
                    "anchor_tree_readable_file": "",
                }
            )
    return rows


def _augment_replay_rows(
    replay_rows: Sequence[dict[str, object]],
    envelope_rows: Sequence[dict[str, object]],
) -> list[dict[str, object]]:
    by_graph_step: dict[int, dict[str, Any]] = {}
    for row in envelope_rows:
        key = _safe_int(row.get("graph_step_idx"))
        bucket = by_graph_step.setdefault(
            key,
            {"visible_event_count": 0, "visible_event_us": 0.0, "top_labels": Counter()},
        )
        bucket["visible_event_count"] += 1
        bucket["visible_event_us"] += float(row.get("child_dur_us") or 0.0)
        bucket["top_labels"][str(row.get("child_label", ""))] += 1
    out = []
    for row in replay_rows:
        row = dict(row)
        bucket = by_graph_step.get(_safe_int(row.get("step_idx")), {})
        row["visible_envelope_event_count"] = int(bucket.get("visible_event_count", 0) or 0)
        row["visible_envelope_event_us"] = round(float(bucket.get("visible_event_us", 0.0) or 0.0), 3)
        top_labels = bucket.get("top_labels")
        row["visible_envelope_top_labels"] = (
            _format_counter(top_labels, limit=6) if isinstance(top_labels, Counter) else ""
        )
        out.append(row)
    return out


def _load_string_ids(conn: sqlite3.Connection) -> dict[int, str]:
    return {
        int(row["id"]): str(row["value"] or "")
        for row in conn.execute("SELECT id, value FROM STRING_IDS")
        if row["id"] is not None
    }


def _load_compute_info(conn: sqlite3.Connection, strings: dict[int, str]) -> dict[int, dict[str, str]]:
    out: dict[int, dict[str, str]] = {}
    if not _table_exists(conn, "COMPUTE_TASK_INFO"):
        return out
    for row in conn.execute("SELECT globalTaskId, name, opType, taskType FROM COMPUTE_TASK_INFO"):
        gid = row["globalTaskId"]
        if gid is None:
            continue
        out[int(gid)] = {
            "op_name": strings.get(int(row["name"]), "") if row["name"] is not None else "",
            "op_type": strings.get(int(row["opType"]), "") if row["opType"] is not None else "",
            "compute_task_type": strings.get(int(row["taskType"]), "") if row["taskType"] is not None else "",
        }
    return out


def _task_type_ids_for_keys(strings: dict[int, str], keys: set[str]) -> set[int]:
    return {
        int(task_type_id)
        for task_type_id, value in strings.items()
        if _normalize_key(value) in keys
    }


def _load_tasks(
    conn: sqlite3.Connection,
    strings: dict[int, str],
    *,
    device_id: int,
    stream_ids: set[int] | None = None,
    task_type_ids: set[int] | None = None,
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    filters = ["deviceId = ?"]
    params: list[object] = [device_id]
    scoped_filters: list[str] = []
    if stream_ids:
        placeholders = ",".join("?" for _ in stream_ids)
        scoped_filters.append(f"streamId IN ({placeholders})")
        params.extend(sorted(stream_ids))
    if task_type_ids:
        placeholders = ",".join("?" for _ in task_type_ids)
        scoped_filters.append(f"taskType IN ({placeholders})")
        params.extend(sorted(task_type_ids))
    if scoped_filters:
        # ACLGraph used to load every TASK row for the selected device and then
        # discard rows that were neither on mapped graph streams nor graph
        # semantic controls.  Large device traces can have millions of unrelated
        # rows, so push that same relevance predicate into SQLite: model-stream
        # rows are needed for graph bodies, and GRAPH_TASK_KEYS rows are needed
        # for MODEL_EXECUTE/NOTIFY control semantics.  This keeps raw TASK
        # evidence intact while avoiding Python objects for irrelevant rows.
        filters.append("(" + " OR ".join(scoped_filters) + ")")
    query = """
        SELECT startNs, endNs, deviceId, connectionId, globalTaskId, streamId,
               taskId, taskType, modelId
        FROM TASK
        WHERE {where_clause}
        ORDER BY startNs, endNs, streamId, taskId
    """.format(where_clause=" AND ".join(filters))
    for row in conn.execute(query, params):
        task_type_id = int(row["taskType"]) if row["taskType"] is not None else -1
        task_label = strings.get(task_type_id, str(task_type_id))
        rows.append(
            {
                "start_ns": int(row["startNs"] or 0),
                "end_ns": int(row["endNs"] or 0),
                "device_id": int(row["deviceId"] if row["deviceId"] is not None else -1),
                "connection_id": int(row["connectionId"] if row["connectionId"] is not None else -1),
                "global_task_id": int(row["globalTaskId"] if row["globalTaskId"] is not None else -1),
                "stream_id": int(row["streamId"] if row["streamId"] is not None else -1),
                "task_id": int(row["taskId"] if row["taskId"] is not None else -1),
                "task_type_id": task_type_id,
                "task_label": task_label,
                "task_key": _normalize_key(task_label),
                "model_id": int(row["modelId"] if row["modelId"] is not None else -1),
            }
        )
    return rows


def _bucket_rows_by_stream(task_rows: Sequence[dict[str, Any]]) -> dict[int, list[dict[str, Any]]]:
    """Index TASK rows once so per-stream summaries do not rescan the whole table."""
    out: dict[int, list[dict[str, Any]]] = {}
    for row in task_rows:
        out.setdefault(int(row["stream_id"]), []).append(row)
    return out


def _load_capture_streams(path: Path, *, device_id: int) -> list[dict[str, int]]:
    if not path.exists():
        return []
    with sqlite3.connect(str(path)) as conn:
        conn.row_factory = sqlite3.Row
        if not _table_exists(conn, "CaptureStreamInfo"):
            return []
        rows = []
        for row in conn.execute(
            """
            SELECT device_id, model_id, original_stream_id, model_stream_id
            FROM CaptureStreamInfo
            WHERE device_id = ?
            ORDER BY model_id, model_stream_id
            """,
            (device_id,),
        ):
            rows.append({key: int(row[key]) for key in row.keys()})
        return rows


def _load_ascend_task_summary(paths: Sequence[Path]) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for path in paths:
        if not path.exists():
            continue
        with sqlite3.connect(str(path)) as conn:
            conn.row_factory = sqlite3.Row
            if not _table_exists(conn, "AscendTask"):
                continue
            for row in conn.execute(
                """
                SELECT host_task_type, device_task_type, COUNT(*) AS n,
                       MIN(stream_id) AS min_stream, MAX(stream_id) AS max_stream,
                       SUM(CASE WHEN start_time >= 0 AND duration > 0 THEN duration ELSE 0 END) AS timed_ns,
                       SUM(CASE WHEN start_time >= 0 AND duration > 0 THEN 1 ELSE 0 END) AS timed_count
                FROM AscendTask
                WHERE host_task_type IN ('MODEL_EXECUTE', 'MODEL_MAINTAINCE',
                                         'MODEL_MAINTENANCE', 'NOTIFY_WAIT',
                                         'NOTIFY_RECORD')
                GROUP BY host_task_type, device_task_type
                ORDER BY host_task_type, device_task_type
                """
            ):
                out.append(
                    {
                        "db": str(path),
                        "host_task_type": row["host_task_type"],
                        "device_task_type": row["device_task_type"],
                        "count": int(row["n"] or 0),
                        "timed_count": int(row["timed_count"] or 0),
                        "timed_us": round(float(row["timed_ns"] or 0) / 1000.0, 3),
                        "stream_range": _compact_ints(range(int(row["min_stream"]), int(row["max_stream"]) + 1)),
                    }
                )
    return out


def _semantic_task_rows(task_rows: Sequence[dict[str, Any]], *, db_idx: int) -> list[dict[str, object]]:
    rows = []
    for raw_order, row in enumerate(task_rows, start=1):
        if row["task_key"] not in GRAPH_TASK_KEYS:
            continue
        rows.append(
            {
                "db_idx": db_idx,
                "semantic_idx": len(rows) + 1,
                "device_id": row["device_id"],
                "stream_id": row["stream_id"],
                "task_id": row["task_id"],
                "global_task_id": row["global_task_id"],
                "connection_id": row["connection_id"],
                "task_type_id": row["task_type_id"],
                "task_label": row["task_label"],
                "task_key": row["task_key"],
                "start_ns": row["start_ns"],
                "end_ns": row["end_ns"],
                "duration_us": round(max(0, row["end_ns"] - row["start_ns"]) / 1000.0, 3),
                "raw_task_order": raw_order,
            }
        )
    return rows


def _segment_tasks(tasks: Sequence[dict[str, Any]], *, gap_ns: int) -> list[list[dict[str, Any]]]:
    sorted_tasks = sorted(tasks, key=lambda row: (row["start_ns"], row["end_ns"], row["stream_id"], row["task_id"]))
    if not sorted_tasks:
        return []
    segments: list[list[dict[str, Any]]] = []
    current = [sorted_tasks[0]]
    last_end = int(sorted_tasks[0]["end_ns"])
    for row in sorted_tasks[1:]:
        if int(row["start_ns"]) - last_end > gap_ns:
            segments.append(current)
            current = [row]
        else:
            current.append(row)
        last_end = max(last_end, int(row["end_ns"]))
    segments.append(current)
    return segments


def _semantic_rows_by_key(semantic_tasks: Sequence[dict[str, object]]) -> dict[str, list[dict[str, object]]]:
    out: dict[str, list[dict[str, object]]] = {}
    for row in semantic_tasks:
        key = str(row.get("task_key", "")) or _normalize_key(str(row.get("task_label", "")))
        out.setdefault(key, []).append(row)
    return out


def _segment_bounds(segments: Sequence[Sequence[dict[str, Any]]]) -> list[tuple[int, int]]:
    bounds: list[tuple[int, int]] = []
    for segment in segments:
        if not segment:
            bounds.append((0, 0))
            continue
        bounds.append(
            (
                min(int(row["start_ns"]) for row in segment),
                max(int(row["end_ns"]) for row in segment),
            )
        )
    return bounds


def _overlap_rows_by_interval(
    intervals: Sequence[tuple[int, int]],
    rows: Sequence[dict[str, object]],
    *,
    touching_overlaps: bool,
) -> list[list[dict[str, object]]]:
    """Join timeline intervals with rows using one sweep instead of a nested scan.

    The naive ACLGraph implementation did this work as:
    ``for graph_segment: for semantic_task: check_time_overlap``.  That is easy
    to read, but on long decode traces it becomes tens of millions of overlap
    checks and repeats label normalization in the inner loop.  The sweep below
    preserves the same flat-timeline overlap semantics while walking the sorted
    semantic rows once and keeping only rows that can still overlap the current
    graph interval.
    """
    out: list[list[dict[str, object]]] = [[] for _ in intervals]
    if not intervals or not rows:
        return out

    ordered_intervals = sorted(
        enumerate(intervals),
        key=lambda item: (item[1][0], item[1][1], item[0]),
    )
    ordered_rows = sorted(
        (
            (int(row.get("start_ns", 0)), int(row.get("end_ns", 0)), int(row.get("stream_id", -1)), row)
            for row in rows
        ),
        key=lambda item: (item[0], item[1], item[2]),
    )

    cursor = 0
    active: list[tuple[int, int, int, dict[str, object]]] = []
    for interval_idx, (start_ns, end_ns) in ordered_intervals:
        if touching_overlaps:
            while cursor < len(ordered_rows) and ordered_rows[cursor][0] <= end_ns:
                active.append(ordered_rows[cursor])
                cursor += 1
            active = [item for item in active if item[1] >= start_ns]
            matches = [
                row
                for row_start, row_end, _stream_id, row in active
                if row_start <= end_ns and row_end >= start_ns
            ]
        else:
            while cursor < len(ordered_rows) and ordered_rows[cursor][0] < end_ns:
                active.append(ordered_rows[cursor])
                cursor += 1
            active = [item for item in active if item[1] > start_ns]
            matches = [
                row
                for row_start, row_end, _stream_id, row in active
                if row_start < end_ns and row_end > start_ns
            ]
        matches.sort(
            key=lambda row: (
                int(row.get("start_ns", 0)),
                int(row.get("end_ns", 0)),
                int(row.get("stream_id", -1)),
            )
        )
        out[interval_idx] = matches
    return out


def _graph_controls_by_segment(
    segments: Sequence[Sequence[dict[str, Any]]],
    *,
    semantic_by_key: dict[str, Sequence[dict[str, object]]],
) -> list[list[dict[str, object]]]:
    rows: list[dict[str, object]] = []
    for key in sorted(GRAPH_TASK_KEYS):
        rows.extend(semantic_by_key.get(key, ()))
    return _overlap_rows_by_interval(
        _segment_bounds(segments),
        rows,
        touching_overlaps=True,
    )


def _model_execute_controls_by_segment(
    segments: Sequence[Sequence[dict[str, Any]]],
    *,
    semantic_by_key: dict[str, Sequence[dict[str, object]]],
) -> list[list[dict[str, object]]]:
    return _overlap_rows_by_interval(
        _segment_bounds(segments),
        semantic_by_key.get("MODEL_EXECUTE", ()),
        touching_overlaps=True,
    )


def _infer_model_execute_wave_size(
    segments: Sequence[Sequence[dict[str, Any]]],
    *,
    model_execs_by_segment: Sequence[Sequence[dict[str, object]]],
) -> int:
    counts = [len(rows) for rows in model_execs_by_segment[: len(segments)]]
    counts = [count for count in counts if count > 0]
    if not counts:
        return 0
    wave_size = counts[0]
    for count in counts[1:]:
        wave_size = math.gcd(wave_size, count)
    return wave_size if wave_size > 1 else 0


def _split_segments_by_model_execute_wave(
    segments: Sequence[Sequence[dict[str, Any]]],
    *,
    model_execs_by_segment: Sequence[Sequence[dict[str, object]]],
    wave_size: int,
) -> list[list[dict[str, Any]]]:
    if wave_size <= 1:
        return [list(segment) for segment in segments]
    out: list[list[dict[str, Any]]] = []
    for idx, segment in enumerate(segments):
        out.extend(
            _split_one_segment_by_model_execute_wave(
                segment,
                model_execs=model_execs_by_segment[idx] if idx < len(model_execs_by_segment) else (),
                wave_size=wave_size,
            )
        )
    return out


def _split_one_segment_by_model_execute_wave(
    segment: Sequence[dict[str, Any]],
    *,
    model_execs: Sequence[dict[str, object]],
    wave_size: int,
) -> list[list[dict[str, Any]]]:
    rows = sorted(segment, key=lambda row: (row["start_ns"], row["end_ns"], row["stream_id"], row["task_id"]))
    if not rows or len(model_execs) <= wave_size or len(model_execs) % wave_size != 0:
        return [list(rows)]

    wave_count = len(model_execs) // wave_size
    boundaries: list[int] = [int(rows[0]["start_ns"])]
    for wave_idx in range(1, wave_count):
        boundaries.append(int(model_execs[wave_idx * wave_size]["start_ns"]))
    boundaries.append(max(int(row["end_ns"]) for row in rows) + 1)

    waves: list[list[dict[str, Any]]] = [[] for _ in range(wave_count)]
    cursor = 0
    for row in rows:
        start_ns = int(row["start_ns"])
        while cursor + 1 < wave_count and start_ns >= boundaries[cursor + 1]:
            cursor += 1
        waves[cursor].append(row)

    if any(not wave for wave in waves):
        return [list(rows)]
    return waves


def _split_segments_into_replay_units(
    segments: Sequence[Sequence[dict[str, Any]]],
    *,
    semantic_by_key: dict[str, Sequence[dict[str, object]]],
    compute: dict[int, dict[str, str]],
) -> tuple[list[list[dict[str, Any]]], list[dict[str, object]]]:
    global_units = _split_activities_by_global_body_landmarks(segments, compute=compute)
    if global_units is not None:
        return global_units

    notify_waits_by_segment = _overlap_rows_by_interval(
        _segment_bounds(segments),
        semantic_by_key.get("NOTIFY_WAIT", ()),
        touching_overlaps=True,
    )
    model_execs_by_segment = _overlap_rows_by_interval(
        _segment_bounds(segments),
        semantic_by_key.get("MODEL_EXECUTE", ()),
        touching_overlaps=True,
    )
    out_segments: list[list[dict[str, Any]]] = []
    out_metadata: list[dict[str, object]] = []
    for activity_idx, segment in enumerate(segments, start=1):
        rows = sorted(segment, key=lambda row: (row["start_ns"], row["end_ns"], row["stream_id"], row["task_id"]))
        if not rows:
            continue
        body_counter = _graph_body_counter(rows, compute)
        controls = list(notify_waits_by_segment[activity_idx - 1]) if activity_idx - 1 < len(notify_waits_by_segment) else []
        controls.extend(
            list(model_execs_by_segment[activity_idx - 1])
            if activity_idx - 1 < len(model_execs_by_segment)
            else []
        )
        unit_count, unit_source = _infer_graph_replay_unit_count(
            body_counter,
            Counter(str(row.get("task_label", "")) for row in controls),
        )
        unit_segments, split_source = _split_one_activity_into_replay_units(
            rows,
            unit_count=unit_count,
            notify_waits=notify_waits_by_segment[activity_idx - 1] if activity_idx - 1 < len(notify_waits_by_segment) else (),
            model_execs=model_execs_by_segment[activity_idx - 1] if activity_idx - 1 < len(model_execs_by_segment) else (),
            compute=compute,
        )
        activity_start = min(int(row["start_ns"]) for row in rows)
        activity_end = max(int(row["end_ns"]) for row in rows)
        effective_unit_count = len(unit_segments)
        for unit_idx, unit_segment in enumerate(unit_segments, start=1):
            out_segments.append(unit_segment)
            out_metadata.append(
                {
                    "graph_activity_idx": activity_idx,
                    "graph_activity_unit_idx": unit_idx,
                    "graph_activity_unit_count": effective_unit_count,
                    "graph_activity_expected_unit_count": unit_count,
                    "graph_activity_unit_source": unit_source,
                    "graph_activity_split_source": split_source,
                    "graph_activity_start_ns": activity_start,
                    "graph_activity_end_ns": activity_end,
                }
            )
    return out_segments, out_metadata


def _split_activities_by_global_body_landmarks(
    segments: Sequence[Sequence[dict[str, Any]]],
    *,
    compute: dict[int, dict[str, str]],
) -> tuple[list[list[dict[str, Any]]], list[dict[str, object]]] | None:
    rows: list[dict[str, Any]] = []
    row_activity: dict[int, int] = {}
    activity_bounds: dict[int, tuple[int, int]] = {}
    for activity_idx, segment in enumerate(segments, start=1):
        ordered = sorted(segment, key=lambda row: (row["start_ns"], row["end_ns"], row["stream_id"], row["task_id"]))
        if not ordered:
            continue
        activity_bounds[activity_idx] = (
            min(int(row["start_ns"]) for row in ordered),
            max(int(row["end_ns"]) for row in ordered),
        )
        for row in ordered:
            rows.append(row)
            row_activity[id(row)] = activity_idx
    rows.sort(key=lambda row: (row["start_ns"], row["end_ns"], row["stream_id"], row["task_id"]))
    if len(rows) <= 1:
        return None

    landmarks: list[int] = []
    for row in rows:
        token = _canonical_graph_body_token(row, compute.get(int(row["global_task_id"]), {}))
        if token in {"index|Index", "attention|Attention"}:
            landmarks.append(int(row["start_ns"]))
    if len(landmarks) <= 1:
        return None
    landmarks.sort()
    boundaries = _valid_inner_boundaries(
        (landmarks[idx - 1] + landmarks[idx]) // 2 for idx in range(1, len(landmarks))
    )
    if len(boundaries) != len(landmarks) - 1:
        return None
    units = _split_rows_by_boundaries(rows, boundaries)
    if len(units) != len(landmarks) or any(not unit for unit in units):
        return None

    metadata: list[dict[str, object]] = []
    total_units = len(units)
    for unit_idx, unit in enumerate(units, start=1):
        activity_ids = sorted({row_activity.get(id(row), 0) for row in unit if row_activity.get(id(row), 0)})
        activity_idx = activity_ids[0] if activity_ids else 0
        activity_start, activity_end = activity_bounds.get(
            activity_idx,
            (
                min(int(row["start_ns"]) for row in unit),
                max(int(row["end_ns"]) for row in unit),
            ),
        )
        metadata.append(
            {
                "graph_activity_idx": activity_idx,
                "graph_activity_indices": _compact_ints(activity_ids),
                "graph_activity_unit_idx": unit_idx,
                "graph_activity_unit_count": total_units,
                "graph_activity_expected_unit_count": total_units,
                "graph_activity_unit_source": "body:global_landmark",
                "graph_activity_split_source": "body:global_landmark_midpoint",
                "graph_activity_start_ns": activity_start,
                "graph_activity_end_ns": activity_end,
            }
        )
    return units, metadata


def _graph_body_counter(
    rows: Sequence[dict[str, Any]],
    compute: dict[int, dict[str, str]],
) -> Counter[str]:
    counter: Counter[str] = Counter()
    for row in rows:
        token = _canonical_graph_body_token(row, compute.get(int(row["global_task_id"]), {}))
        if token:
            counter[token] += 1
    return counter


def _split_one_activity_into_replay_units(
    rows: Sequence[dict[str, Any]],
    *,
    unit_count: int,
    notify_waits: Sequence[dict[str, object]],
    model_execs: Sequence[dict[str, object]],
    compute: dict[int, dict[str, str]],
) -> tuple[list[list[dict[str, Any]]], str]:
    ordered_rows = sorted(rows, key=lambda row: (row["start_ns"], row["end_ns"], row["stream_id"], row["task_id"]))
    unit_count = max(1, int(unit_count))
    if unit_count <= 1 or len(ordered_rows) <= 1:
        return [list(ordered_rows)], "single"

    boundaries = _unit_boundaries_from_body_landmarks(ordered_rows, unit_count=unit_count, compute=compute)
    if boundaries:
        split = _split_rows_by_boundaries(ordered_rows, boundaries)
        if len(split) == unit_count:
            return split, "body:landmark_midpoint"

    boundaries = _unit_boundaries_from_controls(notify_waits, unit_count=unit_count)
    if boundaries:
        split = _split_rows_by_boundaries(ordered_rows, boundaries)
        if len(split) == unit_count:
            return split, "control:NOTIFY_WAIT"

    boundaries = _unit_boundaries_from_controls(model_execs, unit_count=unit_count)
    if boundaries:
        split = _split_rows_by_boundaries(ordered_rows, boundaries)
        if len(split) == unit_count:
            return split, "control:MODEL_EXECUTE"

    return [list(ordered_rows)], "unsplit"


def _unit_boundaries_from_controls(
    controls: Sequence[dict[str, object]],
    *,
    unit_count: int,
) -> list[int]:
    ordered = sorted(controls, key=lambda row: (int(row.get("start_ns", 0)), int(row.get("end_ns", 0)), int(row.get("stream_id", -1))))
    if unit_count <= 1 or len(ordered) < unit_count:
        return []
    wave_size = len(ordered) // unit_count
    if wave_size <= 0:
        return []
    boundaries: list[int] = []
    for unit_idx in range(1, unit_count):
        control_idx = unit_idx * wave_size
        if control_idx >= len(ordered):
            return []
        boundaries.append(int(ordered[control_idx].get("start_ns", 0)))
    return _valid_inner_boundaries(boundaries)


def _unit_boundaries_from_body_landmarks(
    rows: Sequence[dict[str, Any]],
    *,
    unit_count: int,
    compute: dict[int, dict[str, str]],
) -> list[int]:
    landmarks: list[int] = []
    for row in rows:
        token = _canonical_graph_body_token(row, compute.get(int(row["global_task_id"]), {}))
        if token in {"index|Index", "attention|Attention"}:
            landmarks.append(int(row["start_ns"]))
    if len(landmarks) != unit_count or unit_count <= 1:
        return []
    landmarks.sort()
    boundaries = [(landmarks[idx - 1] + landmarks[idx]) // 2 for idx in range(1, len(landmarks))]
    return _valid_inner_boundaries(boundaries)


def _split_rows_by_boundaries(
    rows: Sequence[dict[str, Any]],
    boundaries: Sequence[int],
) -> list[list[dict[str, Any]]]:
    out: list[list[dict[str, Any]]] = [[] for _ in range(len(boundaries) + 1)]
    cursor = 0
    for row in rows:
        start_ns = int(row["start_ns"])
        while cursor < len(boundaries) and start_ns >= boundaries[cursor]:
            cursor += 1
        out[cursor].append(row)
    if any(not part for part in out):
        return [list(rows)]
    return out


def _valid_inner_boundaries(boundaries: Sequence[int]) -> list[int]:
    out: list[int] = []
    last = None
    for value in boundaries:
        value = int(value)
        if value <= 0:
            return []
        if last is not None and value <= last:
            return []
        out.append(value)
        last = value
    return out


def _summarize_model_streams(
    *,
    rows_by_stream: dict[int, Sequence[dict[str, Any]]],
    mapped_stream_ids: set[int],
    capture_streams: Sequence[dict[str, int]],
    compute: dict[int, dict[str, str]],
    db_idx: int,
) -> list[dict[str, object]]:
    capture_by_stream = {row["model_stream_id"]: row for row in capture_streams}
    out: list[dict[str, object]] = []
    for stream_id in sorted(mapped_stream_ids):
        # This replaces the original per-stream full-table scan.  On traces with
        # dozens of captured graph streams and millions of TASK rows, the naive
        # version was O(stream_count * task_count); the bucket keeps it O(task_count).
        rows = list(rows_by_stream.get(stream_id, ()))
        valid = [row for row in rows if row["end_ns"] > row["start_ns"]]
        ops = Counter(
            compute.get(row["global_task_id"], {}).get("op_type", "") or row["task_label"]
            for row in valid
        )
        cap = capture_by_stream.get(stream_id, {})
        min_start = min((row["start_ns"] for row in valid), default=0)
        max_end = max((row["end_ns"] for row in valid), default=0)
        out.append(
            {
                "db_idx": db_idx,
                "device_id": cap.get("device_id", ""),
                "model_id": cap.get("model_id", ""),
                "original_stream_id": cap.get("original_stream_id", ""),
                "model_stream_id": stream_id,
                "raw_task_count": len(rows),
                "timed_task_count": len(valid),
                "start_ns": min_start if valid else "",
                "end_ns": max_end if valid else "",
                "duration_us": round((max_end - min_start) / 1000.0, 3) if valid else 0.0,
                "top_ops": _format_counter(ops, limit=6),
            }
        )
    return out


def _merge_rows(analyses: Sequence[AclGraphAnalysis], attr: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for analysis in analyses:
        rows.extend(getattr(analysis, attr))
    return rows


def _merge_summaries(summaries: Sequence[dict[str, object]]) -> dict[str, object]:
    semantic_counts: Counter[str] = Counter()
    timed_counts: Counter[str] = Counter()
    ascend_task_summary: list[dict[str, Any]] = []
    for summary in summaries:
        semantic_counts.update(dict(summary.get("semantic_task_count_by_label", {})))
        timed_counts.update(dict(summary.get("semantic_timed_task_count_by_label", {})))
        ascend_task_summary.extend(list(summary.get("ascend_task_summary", [])))
    replay_segment_count = sum(int(summary.get("replay_segment_count", 0) or 0) for summary in summaries)
    replay_activity_count = sum(int(summary.get("replay_activity_count", summary.get("activity_segment_count", 0)) or 0) for summary in summaries)
    replay_unit_count = sum(int(summary.get("replay_unit_count", summary.get("replay_segment_count", 0)) or 0) for summary in summaries)
    replay_child_task_count = sum(int(summary.get("replay_child_task_count", 0) or 0) for summary in summaries)
    active_model_stream_count = sum(int(summary.get("active_model_stream_count", 0) or 0) for summary in summaries)
    capture_stream_count = sum(int(summary.get("capture_stream_count", 0) or 0) for summary in summaries)
    semantic_task_count = sum(int(summary.get("semantic_task_count", 0) or 0) for summary in summaries)
    return {
        "schema": "aclgraph.msprof.device_timeline.v1",
        "db_count": len(summaries),
        "msprof_dbs": [summary.get("msprof_db", "") for summary in summaries],
        "capture_stream_count": capture_stream_count,
        "active_model_stream_count": active_model_stream_count,
        "replay_segment_count": replay_segment_count,
        "replay_activity_count": replay_activity_count,
        "replay_unit_count": replay_unit_count,
        "replay_total_device_us": round(
            sum(float(summary.get("replay_total_device_us", 0.0) or 0.0) for summary in summaries),
            3,
        ),
        "replay_child_task_count": replay_child_task_count,
        "semantic_task_count": semantic_task_count,
        "semantic_task_count_by_label": dict(semantic_counts),
        "semantic_timed_task_count_by_label": dict(timed_counts),
        "ascend_task_summary": ascend_task_summary,
        "per_db": list(summaries),
        "quality": _classify_quality(
            capture_stream_count=capture_stream_count,
            active_model_stream_count=active_model_stream_count,
            replay_segment_count=replay_segment_count,
            semantic_task_count=semantic_task_count,
            replay_child_task_count=replay_child_task_count,
        ),
    }


def _summary_markdown(summary: dict[str, object], replay_rows: Sequence[dict[str, object]]) -> str:
    lines = [
        "# ACLGraph Device Timeline Summary",
        "",
        f"- quality: `{summary.get('quality', '')}`",
        f"- db_count: `{summary.get('db_count', 0)}`",
        f"- capture_stream_count: `{summary.get('capture_stream_count', 0)}`",
        f"- active_model_stream_count: `{summary.get('active_model_stream_count', 0)}`",
        f"- replay_segment_count: `{summary.get('replay_segment_count', 0)}`",
        f"- replay_activity_count: `{summary.get('replay_activity_count', 0)}`",
        f"- replay_unit_count: `{summary.get('replay_unit_count', 0)}`",
        f"- replay_total_device_us: `{summary.get('replay_total_device_us', 0.0)}`",
        f"- replay_child_task_count: `{summary.get('replay_child_task_count', 0)}`",
        f"- semantic_task_count: `{summary.get('semantic_task_count', 0)}`",
        "",
        "## Semantic Task Counts",
        "",
    ]
    counts = dict(summary.get("semantic_task_count_by_label", {}))
    timed = dict(summary.get("semantic_timed_task_count_by_label", {}))
    if counts:
        for label, count in counts.items():
            lines.append(f"- `{label}`: {count} rows, {timed.get(label, 0)} timed rows")
    else:
        lines.append("No ACLGraph semantic task rows were found.")
    lines.extend(["", "## Replay Segments", ""])
    if replay_rows:
        lines.append("| replay | db | device | duration_us | raw_child_tasks | visible_events | streams | controls | top_ops |")
        lines.append("| ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- |")
        for row in replay_rows:
            lines.append(
                "| "
                f"{row.get('graph_event_idx', '')} | {row.get('db_idx', '')} | {row.get('device_id', '')} | "
                f"{row.get('dur_us', '')} | {row.get('raw_child_task_count', '')} | "
                f"{row.get('visible_envelope_event_count', 0)} | `{row.get('raw_child_streams', '')}` | "
                f"`{row.get('raw_control_tasks', '')}` | `{row.get('raw_top_ops', '')}` |"
            )
    else:
        lines.append("No ACLGraph replay segments were found.")
    return "\n".join(lines) + "\n"


def _write_csv(path: Path, rows: Sequence[dict[str, object]]) -> None:
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fields: list[str] = []
    seen: set[str] = set()
    for row in rows:
        for key in row:
            if key not in seen:
                seen.add(key)
                fields.append(key)
    with path.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def _table_exists(conn: sqlite3.Connection, table: str) -> bool:
    return conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",
        (table,),
    ).fetchone() is not None


@lru_cache(maxsize=8192)
def _normalize_key(value: str) -> str:
    # ACLGraph uses normalized keys for task-label membership tests.  The naive
    # version ran the regex pair for every TASK row and every graph child token;
    # msprof traces repeat a small set of task labels, so memoizing this pure
    # normalization removes millions of duplicate regex calls without changing
    # the key format.
    s = (value or "").strip().upper()
    s = re.sub(r"[^A-Z0-9]+", "_", s)
    return re.sub(r"_+", "_", s).strip("_")


def _compact_ints(values: Any) -> str:
    vals = sorted({int(v) for v in values})
    if not vals:
        return ""
    ranges: list[str] = []
    start = prev = vals[0]
    for value in vals[1:]:
        if value == prev + 1:
            prev = value
            continue
        ranges.append(str(start) if start == prev else f"{start}..{prev}")
        start = prev = value
    ranges.append(str(start) if start == prev else f"{start}..{prev}")
    return ",".join(ranges)


def _format_counter(counter: Counter[str], *, limit: int) -> str:
    return "; ".join(f"{key}:{count}" for key, count in counter.most_common(limit))


def _safe_int(value: object) -> int:
    text = str(value).strip()
    if re.fullmatch(r"[+-]?\d+(?:\.0+)?", text):
        return int(text.split(".", 1)[0])
    try:
        return int(float(text))
    except (TypeError, ValueError):
        return 0


def _classify_quality(
    *,
    capture_stream_count: int,
    active_model_stream_count: int,
    replay_segment_count: int,
    semantic_task_count: int,
    replay_child_task_count: int,
) -> str:
    if (
        capture_stream_count >= 8
        and active_model_stream_count >= 8
        and replay_segment_count >= 2
        and semantic_task_count >= 100
        and replay_child_task_count >= 1000
    ):
        return "good_demo_dataset"
    if replay_segment_count and active_model_stream_count:
        return "small_but_usable"
    return "insufficient_aclgraph_signal"
