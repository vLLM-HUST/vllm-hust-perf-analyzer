from __future__ import annotations

import csv
import os
import re
import sqlite3
from pathlib import Path
from typing import Dict, List, Tuple

from .msprof_reader import StreamEvent, StreamSelection, _canonical_label


HYGON_GLUE_ATTRIBUTION_FAMILIES = {
    "elementwise_helper",
    "index_gather",
    "index_or_range",
    "init_or_fill",
    "layout_copy",
    "mask_or_select",
    "random_or_init",
    "reduction_helper",
    "scan_helper",
    "softmax",
    "startup_or_autotune",
}


def is_hygon_profile(db_path: Path) -> bool:
    try:
        with sqlite3.connect(f"file:{db_path.resolve()}?immutable=1", uri=True) as conn:
            tables = _table_names(conn)
    except sqlite3.Error:
        return False
    return any(name.startswith(("HIPOPS_", "HIPCOPY_", "HIP_")) for name in tables) or "ROCPROF_KERNEL" in tables


def load_hygon_device_events(db_path: Path, device_id: int) -> Tuple[List[StreamEvent], Dict[int, Dict[str, object]]]:
    events, stream_stats = _load_hygon_events(db_path)
    selected = [ev for ev in events if ev.device_id == device_id]
    selected.sort(key=lambda ev: (ev.start_ns, ev.end_ns, ev.stream_id, ev.global_task_id))
    selected_stats: Dict[int, Dict[str, object]] = {}
    for ev in selected:
        _add_stream_stat(selected_stats, ev)
    return selected, selected_stats


def rank_hygon_streams_global(db_paths: List[Path]) -> Tuple[List[StreamSelection], List[Dict[str, object]]]:
    selections: List[StreamSelection] = []
    for db_idx, db_path in enumerate(db_paths, start=1):
        events, _stream_stats = _load_hygon_events(db_path)
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


def _load_hygon_events(db_path: Path) -> Tuple[List[StreamEvent], Dict[int, Dict[str, object]]]:
    events: List[StreamEvent] = []
    stream_stats: Dict[int, Dict[str, object]] = {}
    attribution_map = _load_attribution_map()
    with sqlite3.connect(f"file:{db_path.resolve()}?immutable=1", uri=True) as conn:
        tables = _table_names(conn)
        if "ROCPROF_KERNEL" in tables:
            events.extend(_load_rocprof_kernel_events(conn))
        str_map = _load_hipprof_strings(conn)
        api_name_map = _load_hip_api_names(conn, db_path)
        default_device = _default_device_id(conn, tables)
        for table in tables:
            if table.startswith("HIPOPS_"):
                events.extend(_load_hipops_events(conn, table, str_map, attribution_map))
            elif table.startswith("HIPCOPY_"):
                events.extend(_load_hipcopy_events(conn, table))
            elif table.startswith("HIP_"):
                events.extend(_load_hip_api_events(conn, table, api_name_map, default_device))
    events = [ev for ev in events if ev.end_ns > ev.start_ns]
    events.sort(key=lambda ev: (ev.start_ns, ev.end_ns, ev.device_id, ev.stream_id))
    for ev in events:
        _add_stream_stat(stream_stats, ev)
    return events, stream_stats


def _load_rocprof_kernel_events(conn: sqlite3.Connection) -> List[StreamEvent]:
    events: List[StreamEvent] = []
    rows = conn.execute(
        """
        SELECT Dispatch_ID, GPU_ID, Queue_ID, Kernel_Name, Start_Timestamp, End_Timestamp,
               GRD, WGR, LDS, Arch_VGPR, SGPR, Wave_Size
        FROM ROCPROF_KERNEL
        WHERE Start_Timestamp IS NOT NULL AND End_Timestamp IS NOT NULL
          AND End_Timestamp > Start_Timestamp
        ORDER BY Start_Timestamp, End_Timestamp, Dispatch_ID
        """
    )
    for dispatch_id, gpu_id, queue_id, kernel_name, start_ns, end_ns, grd, wgr, lds, vgpr, sgpr, wave in rows:
        label_raw = (
            f"[grd={grd},wgr={wgr},lds={lds},vgpr={vgpr},sgpr={sgpr},wave={wave}] "
            f"{kernel_name or 'ROCPROF_KERNEL'}"
        )
        label = _canonical_label(label_raw, category="exec")
        task_id = _as_int(dispatch_id, -1)
        events.append(
            StreamEvent(
                start_ns=_as_int(start_ns, 0),
                end_ns=_as_int(end_ns, 0),
                device_id=_as_int(gpu_id, -1),
                stream_id=_as_int(queue_id, -1),
                task_id=task_id,
                global_task_id=task_id,
                connection_id=-1,
                task_type="ROCPROF_KERNEL",
                label=label,
                category="exec",
                source_table="ROCPROF_KERNEL",
                source_key=f"Dispatch_ID={task_id}",
            )
        )
    return events


def _load_hipops_events(
    conn: sqlite3.Connection,
    table: str,
    str_map: Dict[int, str],
    attribution_map: Dict[int, Dict[str, str]],
) -> List[StreamEvent]:
    events: List[StreamEvent] = []
    rows = conn.execute(
        f"""
        SELECT BeginNs, EndNs, dev_id, queue_id, Name, _Index, DurationNs
        FROM "{table}"
        WHERE BeginNs IS NOT NULL AND EndNs IS NOT NULL AND EndNs > BeginNs
        ORDER BY BeginNs, EndNs, _Index
        """
    )
    for start_ns, end_ns, dev_id, queue_id, name, index, _dur_ns in rows:
        name_id = _as_int(name, -1)
        raw_label = str_map.get(name_id, f"hip_kernel_{name_id}")
        lifted = _lift_hygon_kernel_label(raw_label, name_id=name_id, attribution_map=attribution_map)
        task_id = _as_int(index, -1)
        events.append(
            StreamEvent(
                start_ns=_as_int(start_ns, 0),
                end_ns=_as_int(end_ns, 0),
                device_id=_as_int(dev_id, -1),
                stream_id=_as_int(queue_id, -1),
                task_id=task_id,
                global_task_id=task_id,
                connection_id=-1,
                task_type="HIP_KERNEL" if lifted["role"] == "anchor" else "HIP_KERNEL_AUX",
                label=_canonical_label(str(lifted["label"]), category="exec"),
                category="exec",
                source_table=table,
                source_key=(
                    f"_Index={task_id};BeginNs={start_ns};Name={name_id};"
                    f"hygon_role={lifted['role']};hygon_detail={lifted['detail']};raw_label={raw_label}"
                ),
            )
        )
    return events


def _lift_hygon_kernel_label(
    raw_label: str,
    *,
    name_id: int | None = None,
    attribution_map: Dict[int, Dict[str, str]] | None = None,
) -> Dict[str, str]:
    """Project low-level Hygon HIP kernel symbols to TraceLoom semantic labels.

    Hygon hipprof records GPU kernel symbols rather than framework-level task
    names. The labels here are intentionally coarse: they become the symbol key
    used by grammar discovery, while the raw kernel name remains in source_key
    for drill-down.
    """

    label = raw_label or ""
    low = label.lower()
    if name_id is not None and attribution_map:
        attributed = attribution_map.get(name_id)
        if attributed is not None:
            return attributed

    if "cijk_b_postgsu" in low:
        return _lifted_aux("GemmEpilogue", "TensilePostGSU")
    if "cijk_alik_bljk_bbh" in low:
        match = re.search(r"_mt(\d+x\d+x\d+)", low)
        return _lifted_anchor("MatMul", f"TensileGEMM[{match.group(1)}]" if match else "TensileGEMM")

    if "flash_fwd_kernel" in low:
        return _lifted_anchor("FlashAttention", "flash_fwd")
    if "kernel_unified_attention" in low:
        return _lifted_anchor("Attention", "unified_attention")
    if "reshape_and_cache_kernel_flash" in low:
        return _lifted_anchor("KVCacheUpdate", "reshape_and_cache_flash")
    if "rotary_kernel" in low or "_triton_mrope_forward" in low:
        return _lifted_anchor("Rope", "rotary/mrope")

    if (
        "layer_norm_fwd_kernel" in low
        or "l2norm_fwd_kernel2" in low
        or "vectorized_layer_norm_kernel" in low
    ):
        return _lifted_anchor("Norm", "layer/l2 norm")
    if "triton_red_fused__to_copy_add_mean_mul_pow_rsqrt_0" in low:
        return _lifted_anchor("RmsNorm", "triton fused rmsnorm")

    if "act_and_mul_kernel" in low or "gelucudakernelimpl" in low:
        return _lifted_anchor("Activation", "silu/gelu gate")

    if "_causal_conv1d_fwd_kernel" in low or "_causal_conv1d_update_kernel" in low:
        return _lifted_anchor("MambaConv", "causal_conv1d")
    if "chunk_scaled_dot_kkt_fwd_kernel" in low:
        return _lifted_anchor("MambaChunk", "scaled_dot_kkt")
    if "chunk_gated_delta_rule_fwd_kernel" in low or "fused_recurrent_gated_delta_rule" in low:
        return _lifted_anchor("MambaDeltaRule", "gated_delta_rule")
    if "chunk_fwd_kernel_o" in low:
        return _lifted_anchor("MambaChunkOut", "chunk_fwd_o")
    if "chunk_local_cumsum_scalar_kernel" in low:
        return _lifted_anchor("MambaScan", "local_cumsum")
    if "merge_16x16_to_64x64_inverse_kernel" in low:
        return _lifted_anchor("MambaLayout", "merge_inverse")
    if "recompute_w_u_fwd_kernel" in low:
        return _lifted_anchor("MambaRecompute", "recompute_w_u")
    if "reduce_segments" in low:
        return _lifted_anchor("MambaSegmentReduce", "reduce_segments")
    if "fused_gdn_gating_kernel" in low:
        return _lifted_anchor("MambaGate", "fused_gdn_gating")

    aux_terms = [
        ("KVCacheInit", ("_zero_kv_blocks_kernel",)),
        ("Fill", ("fillfunctor", "fill_reverse_indices")),
        ("Copy", ("direct_copy_kernel", "copy_kernel_cuda", "catarraybatchedcopy", "tensorcopy")),
        ("Index", ("index_elementwise_kernel", "indexselectsmallindex", "vectorized_gather_kernel", "scatter_gather")),
        ("Mask", ("masked_fill_kernel", "where_kernel_impl", "compare_scalar_kernel", "bitwise_not_kernel")),
        ("Random", ("distribution_elementwise_grid_stride_kernel", "distribution_nullary_kernel")),
        (
            "ScanHelper",
            (
                "rocprim::detail::single_scan_kernel",
                "rocprim::detail::lookback_scan_kernel",
                "rocprim::detail::init_lookback_scan_state_kernel",
            ),
        ),
        (
            "Pointwise",
            (
                "binaryfunctor",
                "aunaryfunctor",
                "bunaryfunctor",
                "cudafunctor",
                "divfunctor",
                "mulfunctor",
                "pow_tensor_tensor_kernel",
                "reciprocal_kernel_cuda",
                "sigmoid_kernel_cuda",
            ),
        ),
        ("Range", ("arange_cuda_out", "linspace_cuda_out", "launch_clamp_scalar")),
        ("ReductionHelper", ("argmaxops", "sum_functor", "reduceop")),
    ]
    for detail, terms in aux_terms:
        if any(term in low for term in terms):
            return _lifted_aux(detail, detail)

    return _lifted_aux("Unknown", "unclassified")


def _load_attribution_map() -> Dict[int, Dict[str, str]]:
    path = os.environ.get("TRACELOOM_HYGON_ATTRIBUTION_CSV", "").strip()
    if not path:
        return {}
    csv_path = Path(path)
    if not csv_path.exists():
        return {}
    demote_glue = _env_truthy("TRACELOOM_HYGON_GLUE_AUX")
    out: Dict[int, Dict[str, str]] = {}
    with csv_path.open("r", encoding="utf-8", newline="", errors="replace") as f:
        reader = csv.DictReader(f)
        for row in reader:
            kernel_id = _as_int(row.get("kernel_id"), -1)
            if kernel_id < 0:
                continue
            confidence = str(row.get("confidence") or "").strip().lower()
            family = str(row.get("semantic_family") or "").strip()
            compute = str(row.get("compute_pattern") or "").strip()
            matched = str(row.get("matched_pattern") or "").strip()
            family_key = _attribution_key(family)
            label = _op_label_from_attribution(family, compute, matched)
            is_glue = family_key in HYGON_GLUE_ATTRIBUTION_FAMILIES
            if confidence in {"high", "medium"} and not (demote_glue and is_glue):
                out[kernel_id] = _lifted_anchor(
                    label,
                    f"{confidence}:{family}/{compute}",
                )
            else:
                out[kernel_id] = _lifted_aux(
                    label,
                    f"{confidence or 'none'}:{family}/{compute}",
                )
    return out


def _env_truthy(name: str) -> bool:
    value = os.environ.get(name, "").strip().lower()
    return value in {"1", "true", "yes", "on"}


def _attribution_key(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", (value or "").strip().lower()).strip("_")


def _op_label_from_attribution(family: str, compute: str, matched: str) -> str:
    family_key = _attribution_key(family)
    compute_key = _attribution_key(compute)
    matched_key = (matched or "").strip().lower()

    if family_key == "attention":
        return "FlashAttention" if "flash" in matched_key else "Attention"
    if family_key == "matmul_or_mlp_candidate":
        return "MatMul"
    if family_key == "state_space":
        return "StateSpace"
    if family_key == "mlp":
        return "MLP"
    if family_key == "norm":
        return "Norm"
    if family_key == "rope":
        return "Rope"
    if family_key == "kv_cache":
        return "KVCache"
    if family_key == "layout_copy":
        return "LayoutCopy"
    if family_key == "init_or_fill":
        return "InitOrFill"
    if family_key == "elementwise_helper":
        return "Elementwise"
    if family_key == "index_gather":
        return "IndexGather"
    if family_key == "reduction_helper":
        return "Reduction"
    if family_key == "scan_helper":
        return "Scan"
    if family_key == "mask_or_select":
        return "MaskSelect"
    if family_key == "random_or_init":
        return "RandomInit"
    if family_key == "softmax":
        return "Softmax"
    if family_key == "startup_or_autotune":
        return "StartupAutotune"
    if family_key == "index_or_range":
        return "IndexRange"
    if family_key:
        return "".join(part.capitalize() for part in family_key.split("_"))
    if compute_key:
        return "".join(part.capitalize() for part in compute_key.split("_"))
    return "Unknown"


def _lifted_anchor(label: str, detail: str) -> Dict[str, str]:
    return {"role": "anchor", "label": label, "detail": detail}


def _lifted_aux(label: str, detail: str) -> Dict[str, str]:
    return {"role": "aux", "label": f"HygonAux:{label}", "detail": detail}


def _load_hipcopy_events(conn: sqlite3.Connection, table: str) -> List[StreamEvent]:
    events: List[StreamEvent] = []
    rows = conn.execute(
        f"""
        SELECT BeginNs, EndNs, dev_id, queue_id, Kind, _Index, Bytes, MemoryType
        FROM "{table}"
        WHERE BeginNs IS NOT NULL AND EndNs IS NOT NULL AND EndNs > BeginNs
        ORDER BY BeginNs, EndNs, _Index
        """
    )
    for start_ns, end_ns, dev_id, queue_id, kind, index, bytes_, memory_type in rows:
        kind_i = _as_int(kind, -1)
        mem_i = _as_int(memory_type, -1)
        label = f"hip_copy kind={kind_i} memory_type={mem_i} bytes={_as_int(bytes_, 0)}"
        task_id = _as_int(index, -1)
        events.append(
            StreamEvent(
                start_ns=_as_int(start_ns, 0),
                end_ns=_as_int(end_ns, 0),
                device_id=_as_int(dev_id, -1),
                stream_id=_as_int(queue_id, -1),
                task_id=task_id,
                global_task_id=task_id,
                connection_id=-1,
                task_type=f"HIP_COPY_KIND_{kind_i}",
                label=_canonical_label(label, category="comm"),
                category="comm",
                source_table=table,
                source_key=f"_Index={task_id};BeginNs={start_ns}",
            )
        )
    return events


def _load_hip_api_events(
    conn: sqlite3.Connection,
    table: str,
    api_name_map: Dict[int, str],
    default_device: int,
) -> List[StreamEvent]:
    events: List[StreamEvent] = []
    rows = conn.execute(
        f"""
        SELECT BeginNs, EndNs, pid, tid, Name, _Index, State, DurationNs
        FROM "{table}"
        WHERE BeginNs IS NOT NULL AND EndNs IS NOT NULL AND EndNs > BeginNs
        ORDER BY BeginNs, EndNs, _Index
        """
    )
    for start_ns, end_ns, _pid, tid, name, index, state, _duration_ns in rows:
        api_id = _as_int(name, -1)
        api_name = api_name_map.get(api_id, f"hip_api_{api_id}")
        task_id = _as_int(index, -1)
        category = "wait" if _is_wait_api(api_name) else "runtime"
        events.append(
            StreamEvent(
                start_ns=_as_int(start_ns, 0),
                end_ns=_as_int(end_ns, 0),
                device_id=default_device,
                stream_id=-1,
                task_id=task_id,
                global_task_id=-(task_id + 1),
                connection_id=-1,
                task_type=f"HIP_API_{api_id}",
                label=_canonical_label(api_name, category=category),
                category=category,
                source_table=table,
                source_key=f"_Index={task_id};Name={api_id};State={_as_int(state, -1)}",
            )
        )
    return events


def _load_hipprof_strings(conn: sqlite3.Connection) -> Dict[int, str]:
    out: Dict[int, str] = {}
    if "STR_TABLE" not in _table_names(conn):
        return out
    for str_id, str_name in conn.execute("SELECT STR_ID, STR_NAME FROM STR_TABLE"):
        if str_id is None:
            continue
        out[int(str_id)] = str(str_name or "")
    return out


def _load_hip_api_names(conn: sqlite3.Connection, db_path: Path) -> Dict[int, str]:
    mapping: Dict[int, str] = {}
    summary_rows: List[Tuple[int, int, int]] = []
    if "SUMMARY" in _table_names(conn):
        for name, calls, total_ns in conn.execute("SELECT NAME, CALLS, TOTAL_NS FROM SUMMARY WHERE KIND = 1"):
            if name is None or calls is None or total_ns is None:
                continue
            summary_rows.append((int(name), int(calls), int(total_ns)))

    csv_rows = _read_hiptrace_csv(db_path)
    used: set[int] = set()
    for api_id, calls, total_ns in summary_rows:
        for idx, row in enumerate(csv_rows):
            if idx in used:
                continue
            if row["calls"] == calls and row["total_ns"] == total_ns:
                mapping[api_id] = row["name"]
                used.add(idx)
                break
    return mapping


def _read_hiptrace_csv(db_path: Path) -> List[Dict[str, object]]:
    csv_path = db_path.with_suffix(".hiptrace.csv")
    if not csv_path.exists():
        return []
    rows: List[Dict[str, object]] = []
    with csv_path.open("r", encoding="utf-8", newline="", errors="replace") as f:
        reader = csv.DictReader(f)
        for row in reader:
            name = str(row.get("Name") or "")
            if not name or name == "Total":
                continue
            rows.append(
                {
                    "name": name,
                    "calls": _as_int(row.get("Calls"), 0),
                    "total_ns": _as_int(row.get("TotalDurationNs"), 0),
                }
            )
    return rows


def _default_device_id(conn: sqlite3.Connection, tables: List[str]) -> int:
    for table in tables:
        if not table.startswith(("HIPOPS_", "HIPCOPY_")):
            continue
        try:
            row = conn.execute(f'SELECT dev_id FROM "{table}" WHERE dev_id IS NOT NULL LIMIT 1').fetchone()
        except sqlite3.Error:
            continue
        if row is not None:
            return _as_int(row[0], 0)
    return 0


def _table_names(conn: sqlite3.Connection) -> List[str]:
    return [
        str(row[0])
        for row in conn.execute("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")
        if row[0] is not None
    ]


def _is_wait_api(name: str) -> bool:
    low = name.lower()
    return "synchronize" in low or "wait" in low or "eventquery" in low


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


def _as_int(value: object, default: int) -> int:
    if value is None or value == "":
        return default
    try:
        return int(str(value))
    except ValueError:
        return default
