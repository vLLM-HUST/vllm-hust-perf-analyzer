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
    capture_slot_rows: list[dict[str, object]]
    capture_dictionary_rows: list[dict[str, object]]
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
        capture_slot_rows = _load_capture_slot_rows(conn, strings, db_idx=db_idx, device_id=device_id)
        capture_dictionary_rows = _build_capture_dictionary_rows(capture_slot_rows, db_idx=db_idx, device_id=device_id)
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
        capture_dictionary_rows=capture_dictionary_rows,
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
        "capture_slot_count": len(capture_slot_rows),
        "capture_group_size": _common_int(capture_slot_rows, "capture_group_size"),
        "capture_group_count": _common_int(capture_slot_rows, "capture_group_count"),
        "capture_dictionary_kind_count": len(capture_dictionary_rows),
        "capture_dictionary": capture_dictionary_rows,
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
        capture_slot_rows=capture_slot_rows,
        capture_dictionary_rows=capture_dictionary_rows,
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
    capture_slot_rows = _merge_rows(analyses, "capture_slot_rows")
    capture_dictionary_rows = _merge_rows(analyses, "capture_dictionary_rows")
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
                "aclgraph_capture_slots_file": str(out_dir / "aclgraph_capture_slots.csv"),
                "aclgraph_capture_dictionary_file": str(out_dir / "aclgraph_capture_dictionary.csv"),
            }
        )
        _write_csv(out_dir / "aclgraph_events.csv", replay_rows)
        _write_csv(out_dir / "aclgraph_envelope_events.csv", envelope_rows)
        _write_csv(out_dir / "aclgraph_semantic_tasks.csv", semantic_rows)
        _write_csv(out_dir / "aclgraph_model_streams.csv", model_stream_rows)
        _write_csv(out_dir / "aclgraph_top_ops.csv", top_op_rows)
        _write_csv(out_dir / "aclgraph_capture_slots.csv", capture_slot_rows)
        _write_csv(out_dir / "aclgraph_capture_dictionary.csv", capture_dictionary_rows)
    return {**summary, **files}


def aclgraph_analysis_to_semantic_fixture(
    analysis: AclGraphAnalysis,
    *,
    fixture_id: str = "aclgraph_python_assets",
    description: str = "Python-produced ACLGraph semantic assets.",
) -> dict[str, object]:
    """Export current Python ACLGraph rows into the native semantic fixture schema.

    This is a compatibility/golden-asset bridge. It does not change the current
    report path, and it intentionally keeps unmatched replay subslots as
    diagnostics instead of inventing primary H/L/T anchors for them.
    """

    capture_slots = _semantic_fixture_capture_slots(analysis.capture_slot_rows)
    capture_dictionary = _semantic_fixture_capture_dictionary(
        analysis.capture_dictionary_rows,
        capture_slots=capture_slots,
    )
    (
        replay_activities,
        replay_unit_boundaries,
        replay_units,
        replay_tilings,
        replay_subslots,
        hlt_anchor_seeds,
    ) = _semantic_fixture_replay_assets(analysis.replay_rows)

    diagnostic_codes: dict[str, int] = {}
    if any(
        int(row.get("unique_match_signature_count", 0) or 0) > 1
        for row in capture_dictionary
    ):
        diagnostic_codes["capture_dictionary_variation"] = sum(
            1
            for row in capture_dictionary
            if int(row.get("unique_match_signature_count", 0) or 0) > 1
        )
    partial_tilings = sum(
        1
        for row in replay_tilings
        if int(row.get("unmatched_count", 0) or 0) > 0
    )
    if partial_tilings:
        diagnostic_codes["replay_tiling_partial_coverage"] = partial_tilings

    flat_hlt_sequence = " ".join(str(row["symbol"]) for row in hlt_anchor_seeds)
    capture_group_sizes = {
        int(row.get("capture_group_size", 0) or 0)
        for row in capture_slots
        if int(row.get("capture_group_size", 0) or 0) > 0
    }
    capture_group_counts = {
        int(row.get("capture_group_idx", 0) or 0)
        for row in capture_slots
        if int(row.get("capture_group_idx", 0) or 0) > 0
    }

    return {
        "fixture_id": fixture_id,
        "schema_version": "aclgraph-fixture-v1",
        "description": description,
        "assets": {
            "capture_slots": capture_slots,
            "capture_dictionary": capture_dictionary,
            "replay_activities": replay_activities,
            "replay_unit_boundaries": replay_unit_boundaries,
            "replay_units": replay_units,
            "replay_tilings": replay_tilings,
            "replay_subslots": replay_subslots,
            "hlt_anchor_seeds": hlt_anchor_seeds,
        },
        "golden": {
            "capture_slot_count": len(capture_slots),
            "capture_group_count": (
                max(capture_group_counts) if capture_group_counts else 0
            ),
            "capture_group_size": (
                next(iter(capture_group_sizes))
                if len(capture_group_sizes) == 1
                else 0
            ),
            "capture_dictionary_count": len(capture_dictionary),
            "dictionary_sequence": " ".join(
                str(row["slot_symbol"]) for row in capture_dictionary
            ),
            "replay_activity_count": len(replay_activities),
            "replay_unit_count": len(replay_units),
            "hlt_anchor_count": len(hlt_anchor_seeds),
            "flat_hlt_sequence": flat_hlt_sequence,
            "normal_flat_hlt_sequence": flat_hlt_sequence,
            "launch_anchor_count": 0,
            "launch_boundary_used_as_anchor": False,
            "unique_replay_unit_ids_in_anchors": len(
                {str(row["replay_unit_id"]) for row in hlt_anchor_seeds}
            ),
            "diagnostic_codes": diagnostic_codes,
        },
    }


def write_aclgraph_semantic_fixture(
    *,
    out_path: Path,
    analysis: AclGraphAnalysis,
    fixture_id: str = "aclgraph_python_assets",
    description: str = "Python-produced ACLGraph semantic assets.",
) -> dict[str, object]:
    fixture = aclgraph_analysis_to_semantic_fixture(
        analysis,
        fixture_id=fixture_id,
        description=description,
    )
    out_path.write_text(
        json.dumps(fixture, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return fixture


def _semantic_fixture_capture_slots(
    rows: Sequence[dict[str, object]],
) -> list[dict[str, object]]:
    out: list[dict[str, object]] = []
    for fallback_idx, row in enumerate(rows, start=1):
        slot_idx = _safe_int(row.get("capture_slot_idx")) or fallback_idx
        slot_id = str(row.get("capture_slot_id", "") or f"cs{slot_idx}")
        out.append(
            {
                "capture_slot_id": slot_id,
                "capture_slot_idx": slot_idx,
                "capture_group_idx": _safe_int(row.get("capture_group_idx")),
                "capture_group_size": _safe_int(row.get("capture_group_size")),
                "capture_slot_in_group": _safe_int(
                    row.get("capture_slot_in_group")
                ),
                "slot_kind": str(
                    row.get("capture_dictionary_kind", row.get("slot_kind", ""))
                    or ""
                ),
                "slot_symbol": str(
                    row.get("capture_dictionary_symbol", row.get("slot_symbol", ""))
                    or ""
                ),
                "start_ns": _safe_int(row.get("start_ns")),
                "end_ns": _safe_int(row.get("end_ns")),
                "body_match_signature": str(row.get("body_match_signature", "") or ""),
            }
        )
    return out


def _semantic_fixture_capture_dictionary(
    rows: Sequence[dict[str, object]],
    *,
    capture_slots: Sequence[dict[str, object]],
) -> list[dict[str, object]]:
    slot_ids_by_kind: dict[str, list[str]] = {}
    for slot in capture_slots:
        kind = str(slot.get("slot_kind", ""))
        slot_id = str(slot.get("capture_slot_id", ""))
        if kind and slot_id:
            slot_ids_by_kind.setdefault(kind, []).append(slot_id)

    out: list[dict[str, object]] = []
    for fallback_idx, row in enumerate(rows, start=1):
        kind = str(
            row.get("capture_dictionary_kind", row.get("slot_kind", "")) or ""
        )
        symbol = str(
            row.get("capture_dictionary_symbol", row.get("slot_symbol", "")) or ""
        )
        out.append(
            {
                "capture_dictionary_id": str(
                    row.get("capture_dictionary_id", "")
                    or f"dict:{symbol or kind}"
                ),
                "dictionary_idx": (
                    _safe_int(row.get("capture_dictionary_idx")) or fallback_idx
                ),
                "slot_kind": kind,
                "slot_symbol": symbol,
                "capture_slot_ids": slot_ids_by_kind.get(kind, []),
                "capture_slot_count": _safe_int(row.get("capture_slot_count")),
                "unique_match_signature_count": _safe_int(
                    row.get("unique_match_signature_count")
                ),
                "variation_summary": str(row.get("variation_summary", "") or ""),
            }
        )
    return out


def _semantic_fixture_replay_assets(
    replay_rows: Sequence[dict[str, object]],
) -> tuple[
    list[dict[str, object]],
    list[dict[str, object]],
    list[dict[str, object]],
    list[dict[str, object]],
    list[dict[str, object]],
    list[dict[str, object]],
]:
    activity_rows_by_id: dict[str, list[dict[str, object]]] = {}
    replay_activities: list[dict[str, object]] = []
    replay_unit_boundaries: list[dict[str, object]] = []
    replay_units: list[dict[str, object]] = []
    replay_tilings: list[dict[str, object]] = []
    replay_subslots: list[dict[str, object]] = []
    hlt_anchor_seeds: list[dict[str, object]] = []
    symbol_by_slot = {"H": "ACLH", "L": "ACLL", "T": "ACLT"}

    for fallback_idx, row in enumerate(replay_rows, start=1):
        if str(row.get("graph_provider", "aclgraph") or "aclgraph") != "aclgraph":
            continue
        replay_idx = _safe_int(row.get("graph_event_idx")) or fallback_idx
        activity_idx = _safe_int(row.get("graph_activity_idx")) or replay_idx
        activity_id = f"act{activity_idx}"
        replay_unit_id = f"ru{replay_idx}"
        boundary_id = f"b{activity_idx}"
        activity_rows_by_id.setdefault(activity_id, []).append(row)

        replay_units.append(
            {
                "replay_unit_id": replay_unit_id,
                "replay_activity_id": activity_id,
                "boundary_set_id": boundary_id,
                "unit_idx_global": replay_idx,
                "unit_idx_in_activity": (
                    _safe_int(row.get("graph_activity_unit_idx")) or 1
                ),
                "unit_count_in_activity": (
                    _safe_int(row.get("graph_activity_unit_count")) or 1
                ),
                "start_ns": _safe_int(row.get("start_ns")),
                "end_ns": _safe_int(row.get("end_ns")),
            }
        )

        replay_tiling_id = f"tile{replay_idx}"
        replay_tilings.append(
            {
                "replay_tiling_id": replay_tiling_id,
                "replay_unit_id": replay_unit_id,
                "policy": str(row.get("replay_tiling_policy", "") or ""),
                "subslot_count": _safe_int(
                    row.get("replay_tiling_subslot_count")
                ),
                "sequence": str(row.get("replay_tiling_sequence", "") or ""),
                "matched_count": _safe_int(
                    row.get("replay_tiling_matched_count")
                ),
                "unmatched_count": _safe_int(
                    row.get("replay_tiling_unmatched_count")
                ),
                "coverage": str(row.get("replay_tiling_coverage", "") or ""),
                "top_mismatches": str(row.get("replay_tiling_top_mismatches", "") or ""),
            }
        )

        subslots = _parse_replay_subslots(row.get("replay_tiling_subslots_json"))
        for subslot in subslots:
            subslot_idx = _safe_int(subslot.get("subslot_idx"))
            slot_symbol = str(subslot.get("symbol", "") or "")
            matched = _safe_int(subslot.get("matched")) > 0
            subslot_id = f"ss{replay_idx}_{subslot_idx}"
            replay_subslots.append(
                {
                    "subslot_id": subslot_id,
                    "replay_tiling_id": replay_tiling_id,
                    "subslot_idx": subslot_idx,
                    "slot_kind": str(subslot.get("kind", "") or ""),
                    "slot_symbol": slot_symbol,
                    "matched": matched,
                    "start_ns": _safe_int(subslot.get("start_ns")),
                    "end_ns": _safe_int(subslot.get("end_ns")),
                    "stream_id": _safe_int(subslot.get("stream_id")),
                    "raw_child_task_count": _safe_int(
                        subslot.get("raw_child_task_count")
                    ),
                    "raw_top_ops": str(subslot.get("raw_top_ops", "") or ""),
                    "body_match_signature": str(
                        subslot.get("body_match_signature", "") or ""
                    ),
                }
            )
            graph_symbol = symbol_by_slot.get(slot_symbol)
            if not graph_symbol or not matched:
                continue
            hlt_anchor_seeds.append(
                {
                    "anchor_seed_id": f"a{replay_idx}_{subslot_idx}",
                    "replay_unit_id": replay_unit_id,
                    "subslot_id": subslot_id,
                    "launch_activity_id": activity_id,
                    "symbol": graph_symbol,
                    "slot_symbol": slot_symbol,
                    "semantic_role": "anchor",
                    "start_ns": _safe_int(subslot.get("start_ns")),
                    "end_ns": _safe_int(subslot.get("end_ns")),
                    "raw_child_task_count": _safe_int(
                        subslot.get("raw_child_task_count")
                    ),
                    "raw_top_ops": str(subslot.get("raw_top_ops", "") or ""),
                    "body_match_signature": str(
                        subslot.get("body_match_signature", "") or ""
                    ),
                }
            )

    for activity_id, rows in sorted(activity_rows_by_id.items()):
        first = rows[0]
        activity_idx = (
            _safe_int(first.get("graph_activity_idx")) or len(replay_activities) + 1
        )
        replay_activities.append(
            {
                "replay_activity_id": activity_id,
                "activity_idx": activity_idx,
                "start_ns": min(_safe_int(row.get("start_ns")) for row in rows),
                "end_ns": max(_safe_int(row.get("end_ns")) for row in rows),
                "stream_ids": sorted(
                    {
                        _safe_int(row.get("stream_id"))
                        for row in rows
                        if _safe_int(row.get("stream_id")) >= 0
                    }
                ),
                "raw_child_task_count": sum(
                    _safe_int(row.get("raw_child_task_count")) for row in rows
                ),
            }
        )
        replay_unit_boundaries.append(
            {
                "boundary_set_id": f"b{activity_idx}",
                "replay_activity_id": activity_id,
                "expected_unit_count": (
                    _safe_int(first.get("graph_activity_expected_unit_count"))
                    or len(rows)
                ),
                "effective_unit_count": (
                    _safe_int(first.get("graph_activity_unit_count")) or len(rows)
                ),
                "unit_source": str(first.get("graph_activity_unit_source", "") or ""),
                "split_source": str(first.get("graph_activity_split_source", "") or "single"),
                "confidence": "high" if len(rows) == 1 else "medium",
                "boundary_ns": [],
            }
        )

    return (
        replay_activities,
        replay_unit_boundaries,
        replay_units,
        replay_tilings,
        replay_subslots,
        hlt_anchor_seeds,
    )


def _parse_replay_subslots(raw: object) -> list[dict[str, object]]:
    text = str(raw or "").strip()
    if not text:
        return []
    try:
        loaded = json.loads(text)
    except json.JSONDecodeError:
        return []
    if not isinstance(loaded, list):
        return []
    return [item for item in loaded if isinstance(item, dict)]


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
    return AclGraphAnalysis([], [], [], [], [], [], [], [], summary)


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
    capture_dictionary_rows: Sequence[dict[str, object]],
) -> tuple[list[dict[str, object]], list[dict[str, object]], list[dict[str, object]]]:
    step_rows: list[dict[str, object]] = []
    replay_rows: list[dict[str, object]] = []
    top_op_rows: list[dict[str, object]] = []
    graph_type_by_hash: dict[str, str] = {}
    graph_template_by_hash: dict[str, str] = {}
    graph_tiling_by_sequence: dict[str, str] = {}
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
        template_vocabulary_signature = _format_template_vocabulary_signature(segment, compute)
        template_sequence_signature = _format_template_sequence_signature(segment, compute)
        template_signature = _format_template_signature(segment, compute) or _format_unit_signature(
            body_counter,
            replay_unit_count,
        )
        replay_tiling = _match_replay_segment_to_capture_dictionary(
            segment,
            compute=compute,
            capture_dictionary_rows=capture_dictionary_rows,
        )
        body_hash = _stable_hash(body_signature)
        template_vocabulary_hash = _stable_hash(template_vocabulary_signature) if template_vocabulary_signature else ""
        template_sequence_hash = _stable_hash(template_sequence_signature) if template_sequence_signature else ""
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
        tiling_sequence = str(replay_tiling.get("sequence", ""))
        if tiling_sequence:
            graph_tiling_symbol = graph_tiling_by_sequence.get(tiling_sequence)
            if graph_tiling_symbol is None:
                graph_tiling_symbol = f"D{len(graph_tiling_by_sequence) + 1:03d}"
                graph_tiling_by_sequence[tiling_sequence] = graph_tiling_symbol
            graph_tiling_label = f"ACLGraph H/L/T {tiling_sequence}"
        else:
            graph_tiling_symbol = ""
            graph_tiling_label = ""
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
            "graph_template_hash_policy": "anchor_compute_body_multiset_v1",
            "graph_template_vocabulary_hash": template_vocabulary_hash,
            "graph_template_vocabulary_signature": template_vocabulary_signature,
            "graph_template_vocabulary_hash_policy": "anchor_compute_unit_vocabulary_v3",
            "graph_template_sequence_hash": template_sequence_hash,
            "graph_template_sequence_signature": template_sequence_signature,
            "graph_template_sequence_hash_policy": "diagnostic_compute_unit_sequence_v1",
            "graph_tiling_symbol": graph_tiling_symbol,
            "graph_tiling_label": graph_tiling_label,
            "graph_tiling_hash": _stable_hash(tiling_sequence) if tiling_sequence else "",
            "graph_tiling_hash_policy": "capture_dictionary_tiling_sequence_v1",
            "replay_tiling_policy": replay_tiling.get("policy", ""),
            "replay_tiling_subslot_count": replay_tiling.get("subslot_count", 0),
            "replay_tiling_sequence": replay_tiling.get("sequence", ""),
            "replay_tiling_coverage": replay_tiling.get("coverage", ""),
            "replay_tiling_matched_count": replay_tiling.get("matched_count", 0),
            "replay_tiling_unmatched_count": replay_tiling.get("unmatched_count", 0),
            "replay_tiling_score": replay_tiling.get("score", 0.0),
            "replay_tiling_top_mismatches": replay_tiling.get("top_mismatches", ""),
            "replay_tiling_subslots_json": replay_tiling.get("subslots_json", ""),
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


def _format_template_signature(
    rows: Sequence[dict[str, Any]],
    compute: dict[int, dict[str, str]],
) -> str:
    body_counter = _graph_body_counter(rows, compute)
    if not body_counter:
        return ""
    return "\n".join(
        [
            "body_multiset_v1:",
            *[f"- {token}:{body_counter[token]}" for token in sorted(body_counter)],
        ]
    )


def _format_template_sequence_signature(
    rows: Sequence[dict[str, Any]],
    compute: dict[int, dict[str, str]],
) -> str:
    ordered_rows = sorted(rows, key=lambda row: (row["start_ns"], row["end_ns"], row["stream_id"], row["task_id"]))
    stream_slots = {
        stream_id: idx
        for idx, stream_id in enumerate(sorted({int(row["stream_id"]) for row in ordered_rows}))
    }
    sequence: list[str] = []
    tokens: set[str] = set()
    for row in ordered_rows:
        token = _canonical_graph_body_token(row, compute.get(int(row["global_task_id"]), {}))
        if token:
            tokens.add(token)
            sequence.append(f"s{stream_slots[int(row['stream_id'])]:02d}:{token}")
    if not sequence:
        return ""
    lines = [
        "body_sequence_v1:",
        f"stream_count: {len(stream_slots)}",
        "body_tokens:",
        *[f"- {token}" for token in _run_length_encode(sequence)],
        "body_vocabulary:",
        *[f"- {token}" for token in sorted(tokens)],
    ]
    return "\n".join(lines)


def _format_template_vocabulary_signature(rows: Sequence[dict[str, Any]], compute: dict[int, dict[str, str]]) -> str:
    ordered_rows = sorted(rows, key=lambda row: (row["start_ns"], row["end_ns"], row["stream_id"], row["task_id"]))
    tokens: set[str] = set()
    for row in ordered_rows:
        token = _canonical_graph_body_token(row, compute.get(int(row["global_task_id"]), {}))
        if token:
            tokens.add(token)
    if not tokens:
        return ""
    return "\n".join(["body_tokens:", *[f"- {token}" for token in sorted(tokens)]])


def _run_length_encode(tokens: Sequence[str]) -> list[str]:
    if not tokens:
        return []
    encoded: list[str] = []
    prev = tokens[0]
    count = 1
    for token in tokens[1:]:
        if token == prev:
            count += 1
            continue
        encoded.append(f"{prev}*{count}" if count > 1 else prev)
        prev = token
        count = 1
    encoded.append(f"{prev}*{count}" if count > 1 else prev)
    return encoded


def _format_unit_signature(counter: Counter[str], unit_count: int) -> str:
    # Fallback only. Normal replay units use the ordered discrete body sequence
    # above so graph units with different internal structure become distinct
    # anchors without using timing or cost features.
    return "\n".join(name for name, count in sorted(counter.items()) if int(count) > 0)


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


def _load_capture_slot_rows(
    conn: sqlite3.Connection,
    strings: dict[int, str],
    *,
    db_idx: int,
    device_id: int,
) -> list[dict[str, object]]:
    if not _table_exists(conn, "CANN_API"):
        return []
    begin_ids = {sid for sid, value in strings.items() if value == "aclmdlRICaptureBegin"}
    end_ids = {sid for sid, value in strings.items() if value == "aclmdlRICaptureEnd"}
    if not begin_ids or not end_ids:
        return []

    begin_placeholders = ",".join("?" for _ in begin_ids)
    end_placeholders = ",".join("?" for _ in end_ids)
    begin_rows = [
        (int(row["startNs"] or 0), int(row["endNs"] or 0))
        for row in conn.execute(
            f"""
            SELECT startNs, endNs
            FROM CANN_API
            WHERE name IN ({begin_placeholders})
            ORDER BY startNs, endNs, connectionId
            """,
            sorted(begin_ids),
        )
    ]
    end_rows = [
        (int(row["startNs"] or 0), int(row["endNs"] or 0))
        for row in conn.execute(
            f"""
            SELECT startNs, endNs
            FROM CANN_API
            WHERE name IN ({end_placeholders})
            ORDER BY startNs, endNs, connectionId
            """,
            sorted(end_ids),
        )
    ]
    pairs = [
        (begin_start, begin_end, end_start, end_end)
        for (begin_start, begin_end), (end_start, end_end) in zip(begin_rows, end_rows)
        if begin_start <= end_end and begin_start > 0 and end_end > 0
    ]
    if not pairs:
        return []

    capture_group_size = _infer_capture_group_size(pairs)
    capture_group_count = math.ceil(len(pairs) / capture_group_size) if capture_group_size else 1
    capture_start = min(pair[0] for pair in pairs)
    capture_end = max(pair[3] for pair in pairs)
    api_rows = [
        {
            "start_ns": int(row["startNs"] or 0),
            "end_ns": int(row["endNs"] or 0),
            "api_name": strings.get(int(row["name"]), "") if row["name"] is not None else "",
        }
        for row in conn.execute(
            """
            SELECT startNs, endNs, name
            FROM CANN_API
            WHERE startNs >= ? AND startNs <= ?
            ORDER BY startNs, endNs, connectionId
            """,
            (capture_start, capture_end),
        )
    ]

    out: list[dict[str, object]] = []
    cursor = 0
    for capture_idx, (begin_start, _begin_end, _end_start, end_end) in enumerate(pairs, start=1):
        api_counter: Counter[str] = Counter()
        token_counter: Counter[str] = Counter()
        sequence: list[str] = []
        while cursor < len(api_rows) and int(api_rows[cursor]["start_ns"]) < begin_start:
            cursor += 1
        scan = cursor
        while scan < len(api_rows) and int(api_rows[scan]["start_ns"]) <= end_end:
            api = api_rows[scan]
            scan += 1
            if int(api["end_ns"]) > end_end:
                continue
            api_name = str(api["api_name"])
            if api_name in {"aclmdlRICaptureBegin", "aclmdlRICaptureEnd", "aclmdlRICaptureGetInfo"}:
                continue
            token = _canonical_capture_api_token(api_name)
            if not token:
                continue
            api_counter[api_name] += 1
            token_counter[token] += 1
            sequence.append(token)

        slot_idx = ((capture_idx - 1) % capture_group_size) + 1 if capture_group_size else capture_idx
        group_idx = ((capture_idx - 1) // capture_group_size) + 1 if capture_group_size else 1
        match_counter = _match_feature_counter(token_counter)
        kind = _infer_capture_slot_kind(
            slot_idx=slot_idx,
            group_size=capture_group_size,
            counter=match_counter,
        )
        signature = _format_signature(token_counter)
        sequence_signature = "\n".join(_run_length_encode(sequence))
        out.append(
            {
                "db_idx": db_idx,
                "device_id": device_id,
                "capture_slot_idx": capture_idx,
                "capture_group_idx": group_idx,
                "capture_group_count": capture_group_count,
                "capture_group_size": capture_group_size,
                "capture_slot_in_group": slot_idx,
                "capture_dictionary_kind": kind,
                "capture_dictionary_symbol": _capture_kind_symbol(kind),
                "start_ns": begin_start,
                "end_ns": end_end,
                "duration_us": round((end_end - begin_start) / 1000.0, 3),
                "body_api_count": sum(api_counter.values()),
                "body_token_count": sum(token_counter.values()),
                "body_signature": signature,
                "body_match_signature": _format_signature(match_counter),
                "body_sequence_signature": sequence_signature,
                "body_hash": _stable_hash(signature) if signature else "",
                "top_apis": _format_counter(api_counter, limit=10),
            }
        )
    return out


def _infer_capture_group_size(pairs: Sequence[tuple[int, int, int, int]]) -> int:
    if len(pairs) <= 1:
        return len(pairs)
    gaps = [
        max(0, int(pairs[idx][0]) - int(pairs[idx - 1][3]))
        for idx in range(1, len(pairs))
    ]
    positive = sorted(gap for gap in gaps if gap > 0)
    if positive:
        median_gap = positive[len(positive) // 2]
        threshold = max(median_gap * 10, 10_000_000)
        boundaries = [idx + 1 for idx, gap in enumerate(gaps) if gap >= threshold]
        if boundaries:
            sizes: list[int] = []
            prev = 0
            for boundary in boundaries:
                sizes.append(boundary - prev)
                prev = boundary
            sizes.append(len(pairs) - prev)
            if sizes and len(set(sizes)) == 1 and sizes[0] > 0:
                return sizes[0]
    return len(pairs)


def _canonical_capture_api_token(api_name: str) -> str:
    low = (api_name or "").strip().lower()
    if not low:
        return ""
    if "workspace" in low or low in {"aclmdlricapturebegin", "aclmdlricaptureend", "aclmdlricapturegetinfo"}:
        return ""
    if not (low.startswith("aclnn") or "tiling" in low):
        return ""
    if low in {"aclnnmm", "aclnnaddmm"} or "matmul" in low or "gemm" in low:
        return "matmul|MatMul"
    if "addrmsnormbias" in low or "rmsnorm" in low or "layernorm" in low:
        return "norm|RmsNorm"
    if "swiglu" in low:
        return "swiglu|SwiGlu"
    if "rotary" in low or "rope" in low:
        return "rope|Rope"
    if "embedding" in low:
        return "index|Embedding"
    if "gather" in low or "scatter" in low or "index" in low:
        return "index|Index"
    if "cast" in low:
        return "cast|Cast"
    if any(keyword in low for keyword in ("slice", "reshape", "transpose", "tile", "broadcast", "expand")):
        return "shape|Shape"
    if "attention" in low:
        return "attention|Attention"
    if "fill" in low or "inplacecopy" in low or "copy" in low:
        return ""
    label = canonical_device_label(api_name, "", category="exec")
    family = _graph_body_family(label)
    if family in {"matmul", "norm", "swiglu", "rope", "index", "attention", "cast", "shape"}:
        return f"{family}|{label}"
    return ""


def _infer_capture_slot_kind(*, slot_idx: int, group_size: int, counter: Counter[str]) -> str:
    if group_size > 2:
        if slot_idx == 1:
            return "head"
        if slot_idx == group_size:
            return "tail"
        return "layer"
    families = {token.split("|", 1)[0] for token in counter}
    matmul_count = sum(count for token, count in counter.items() if token.startswith("matmul|"))
    if "index" in families:
        return "head"
    if "rope" not in families and matmul_count >= 2:
        return "tail"
    return "layer"


def _capture_kind_symbol(kind: str) -> str:
    return {"head": "H", "layer": "L", "tail": "T"}.get(kind, "?")


def _build_capture_dictionary_rows(
    capture_slot_rows: Sequence[dict[str, object]],
    *,
    db_idx: int,
    device_id: int,
) -> list[dict[str, object]]:
    by_kind: dict[str, list[dict[str, object]]] = {}
    for row in capture_slot_rows:
        kind = str(row.get("capture_dictionary_kind", ""))
        if kind:
            by_kind.setdefault(kind, []).append(row)
    rows: list[dict[str, object]] = []
    for dictionary_idx, kind in enumerate(["head", "layer", "tail"], start=1):
        slots = by_kind.get(kind, [])
        if not slots:
            continue
        signature_counts = Counter(str(row.get("body_match_signature", "")) for row in slots)
        representative_signature, representative_count = signature_counts.most_common(1)[0]
        token_counter = _parse_signature(representative_signature)
        full_signature_counts = Counter(str(row.get("body_signature", "")) for row in slots)
        representative_full_signature, _full_count = full_signature_counts.most_common(1)[0]
        slot_indices = [int(row.get("capture_slot_in_group", 0) or 0) for row in slots]
        rows.append(
            {
                "db_idx": db_idx,
                "device_id": device_id,
                "capture_dictionary_idx": dictionary_idx,
                "capture_dictionary_kind": kind,
                "capture_dictionary_symbol": _capture_kind_symbol(kind),
                "capture_slot_count": len(slots),
                "capture_slot_indices": _compact_ints(slot_indices),
                "capture_group_count": _common_int(slots, "capture_group_count"),
                "body_token_count_min": min(int(row.get("body_token_count", 0) or 0) for row in slots),
                "body_token_count_max": max(int(row.get("body_token_count", 0) or 0) for row in slots),
                "unique_match_signature_count": len(signature_counts),
                "representative_match_count": representative_count,
                "capture_match_signature": representative_signature,
                "capture_signature": representative_full_signature,
                "capture_template_hash": _stable_hash(representative_signature),
                "capture_match_tokens": _format_signature(token_counter),
            }
        )
    return rows


def _match_replay_segment_to_capture_dictionary(
    segment: Sequence[dict[str, Any]],
    *,
    compute: dict[int, dict[str, str]],
    capture_dictionary_rows: Sequence[dict[str, object]],
) -> dict[str, object]:
    dictionary = [
        (
            str(row.get("capture_dictionary_kind", "")),
            str(row.get("capture_dictionary_symbol", "")),
            _parse_signature(str(row.get("capture_match_signature", ""))),
        )
        for row in capture_dictionary_rows
        if str(row.get("capture_dictionary_kind", "")) and str(row.get("capture_match_signature", ""))
    ]
    if not dictionary:
        return {
            "policy": "",
            "subslot_count": 0,
            "sequence": "",
            "coverage": "",
            "matched_count": 0,
            "unmatched_count": 0,
            "score": 0.0,
            "top_mismatches": "",
            "subslots_json": "",
        }

    subslots = _replay_stream_subslots(segment, compute=compute)
    symbols: list[str] = []
    matched_count = 0
    score_total = 0.0
    mismatches: Counter[str] = Counter()
    subslot_details: list[dict[str, object]] = []
    for subslot_idx, subslot in enumerate(subslots, start=1):
        counter = subslot["counter"]
        match_counter = _match_feature_counter(counter)
        best_kind = ""
        best_symbol = "?"
        best_score = float("inf")
        best_norm = 0
        for kind, symbol, template_counter in dictionary:
            score = _counter_distance(match_counter, template_counter)
            norm = sum(max(int(match_counter.get(token, 0)), int(template_counter.get(token, 0))) for token in set(match_counter) | set(template_counter))
            if score < best_score:
                best_kind = kind
                best_symbol = symbol
                best_score = score
                best_norm = norm
        threshold = max(4.0, best_norm * 0.6)
        score_total += best_score if math.isfinite(best_score) else 0.0
        if best_score <= threshold:
            matched_count += 1
            symbols.append(best_symbol)
            matched = True
        else:
            symbols.append(best_symbol)
            mismatches[best_kind or "unknown"] += 1
            matched = False
        subslot_details.append(
            {
                "subslot_idx": subslot_idx,
                "stream_id": subslot["stream_id"],
                "start_ns": subslot["start_ns"],
                "end_ns": subslot["end_ns"],
                "duration_us": round((int(subslot["end_ns"]) - int(subslot["start_ns"])) / 1000.0, 3),
                "symbol": best_symbol,
                "kind": best_kind,
                "matched": int(matched),
                "score": round(best_score, 3) if math.isfinite(best_score) else "",
                "threshold": round(threshold, 3),
                "raw_child_task_count": subslot["raw_child_task_count"],
                "raw_top_ops": subslot["raw_top_ops"],
                "body_match_signature": _format_signature(match_counter),
            }
        )
    subslot_count = len(subslots)
    unmatched_count = subslot_count - matched_count
    sequence = ",".join(_run_length_encode(symbols))
    return {
        "policy": "capture_dictionary_stream_subslot_nearest_v2",
        "subslot_count": subslot_count,
        "sequence": sequence,
        "coverage": f"{matched_count}/{subslot_count}" if subslot_count else "",
        "matched_count": matched_count,
        "unmatched_count": unmatched_count,
        "score": round(score_total, 3),
        "top_mismatches": _format_counter(mismatches, limit=5),
        "subslots_json": json.dumps(subslot_details, separators=(",", ":")),
    }


def _replay_stream_subslots(
    segment: Sequence[dict[str, Any]],
    *,
    compute: dict[int, dict[str, str]],
) -> list[dict[str, object]]:
    by_stream: dict[int, list[dict[str, Any]]] = {}
    for row in segment:
        token = _canonical_graph_body_token(row, compute.get(int(row["global_task_id"]), {}))
        if not token:
            continue
        by_stream.setdefault(int(row["stream_id"]), []).append(row)
    subslots: list[dict[str, object]] = []
    for stream_id, rows in by_stream.items():
        counter = _graph_body_counter(rows, compute)
        if not counter:
            continue
        first_start = min(int(row["start_ns"]) for row in rows)
        last_end = max(int(row["end_ns"]) for row in rows)
        first_task = min(int(row["task_id"]) for row in rows)
        op_counter = Counter(
            (
                compute.get(int(row["global_task_id"]), {}).get("op_type")
                or compute.get(int(row["global_task_id"]), {}).get("op_name")
                or str(row["task_label"])
            )
            for row in rows
        )
        subslots.append(
            {
                "stream_id": stream_id,
                "counter": counter,
                "start_ns": first_start,
                "end_ns": last_end,
                "first_task_id": first_task,
                "raw_child_task_count": len(rows),
                "raw_top_ops": _format_counter(op_counter, limit=10),
            }
        )
    subslots.sort(key=lambda item: (int(item["start_ns"]), int(item["first_task_id"]), int(item["stream_id"])))
    return subslots


def _match_feature_counter(counter: Counter[str]) -> Counter[str]:
    out: Counter[str] = Counter()
    for token, count in counter.items():
        family = token.split("|", 1)[0]
        if family not in {"matmul", "norm", "swiglu", "rope", "index", "attention", "cast", "shape"}:
            continue
        out[token] += int(count)
    return out


def _parse_signature(signature: str) -> Counter[str]:
    counter: Counter[str] = Counter()
    for line in str(signature or "").splitlines():
        text = line.strip()
        if not text or ":" not in text:
            continue
        key, value = text.rsplit(":", 1)
        try:
            count = int(value.strip())
        except ValueError:
            continue
        if count:
            counter[key.strip()] += count
    return counter


def _counter_distance(left: Counter[str], right: Counter[str]) -> float:
    score = 0.0
    for token in set(left) | set(right):
        score += abs(int(left.get(token, 0)) - int(right.get(token, 0)))
    return score


def _common_int(rows: Sequence[dict[str, object]], key: str) -> int:
    values = {int(row.get(key, 0) or 0) for row in rows if int(row.get(key, 0) or 0) > 0}
    return values.pop() if len(values) == 1 else 0


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
    capture_dictionary: list[dict[str, object]] = []
    for summary in summaries:
        semantic_counts.update(dict(summary.get("semantic_task_count_by_label", {})))
        timed_counts.update(dict(summary.get("semantic_timed_task_count_by_label", {})))
        ascend_task_summary.extend(list(summary.get("ascend_task_summary", [])))
        capture_dictionary.extend(list(summary.get("capture_dictionary", [])))
    replay_segment_count = sum(int(summary.get("replay_segment_count", 0) or 0) for summary in summaries)
    replay_activity_count = sum(int(summary.get("replay_activity_count", summary.get("activity_segment_count", 0)) or 0) for summary in summaries)
    replay_unit_count = sum(int(summary.get("replay_unit_count", summary.get("replay_segment_count", 0)) or 0) for summary in summaries)
    replay_child_task_count = sum(int(summary.get("replay_child_task_count", 0) or 0) for summary in summaries)
    active_model_stream_count = sum(int(summary.get("active_model_stream_count", 0) or 0) for summary in summaries)
    capture_stream_count = sum(int(summary.get("capture_stream_count", 0) or 0) for summary in summaries)
    capture_slot_count = sum(int(summary.get("capture_slot_count", 0) or 0) for summary in summaries)
    semantic_task_count = sum(int(summary.get("semantic_task_count", 0) or 0) for summary in summaries)
    return {
        "schema": "aclgraph.msprof.device_timeline.v1",
        "db_count": len(summaries),
        "msprof_dbs": [summary.get("msprof_db", "") for summary in summaries],
        "capture_stream_count": capture_stream_count,
        "capture_slot_count": capture_slot_count,
        "capture_dictionary_kind_count": len(capture_dictionary),
        "capture_dictionary": capture_dictionary,
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
        f"- capture_slot_count: `{summary.get('capture_slot_count', 0)}`",
        f"- capture_dictionary_kind_count: `{summary.get('capture_dictionary_kind_count', 0)}`",
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
    lines.extend(["", "## Capture Dictionary", ""])
    capture_dictionary = list(summary.get("capture_dictionary", []))
    if capture_dictionary:
        lines.append("| kind | symbol | slots | slot_indices | signature_variants | match_signature |")
        lines.append("| --- | --- | ---: | --- | ---: | --- |")
        for row in capture_dictionary:
            match_signature = str(row.get("capture_match_signature", "")).replace("\n", "; ")
            lines.append(
                "| "
                f"{row.get('capture_dictionary_kind', '')} | `{row.get('capture_dictionary_symbol', '')}` | "
                f"{row.get('capture_slot_count', '')} | `{row.get('capture_slot_indices', '')}` | "
                f"{row.get('unique_match_signature_count', '')} | `{match_signature}` |"
            )
    else:
        lines.append("No ACLGraph capture dictionary entries were found.")
    lines.extend(["", "## Replay Segments", ""])
    if replay_rows:
        lines.append("| replay | db | device | duration_us | raw_child_tasks | tiling | coverage | streams | controls | top_ops |")
        lines.append("| ---: | ---: | ---: | ---: | ---: | --- | ---: | --- | --- | --- |")
        for row in replay_rows:
            lines.append(
                "| "
                f"{row.get('graph_event_idx', '')} | {row.get('db_idx', '')} | {row.get('device_id', '')} | "
                f"{row.get('dur_us', '')} | {row.get('raw_child_task_count', '')} | "
                f"`{row.get('replay_tiling_sequence', '')}` | `{row.get('replay_tiling_coverage', '')}` | "
                f"`{row.get('raw_child_streams', '')}` | "
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
