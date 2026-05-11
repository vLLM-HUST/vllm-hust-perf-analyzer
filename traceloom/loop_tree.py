from __future__ import annotations

import csv
import json
import re
import sqlite3
import textwrap
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple

from .io.discover import discover_msprof_dbs


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
class LoopAnalyzerConfig:
    top_streams_global: int = 12
    top_streams_per_db: int = 3
    max_events_per_stream: int = 5000
    max_macro_events: int = 2000
    max_macro_defs: int = 32
    max_compression_passes: int = 64
    max_period: int = 12
    min_repeat_count: int = 2


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


@dataclass(frozen=True)
class SeqToken:
    name: str
    start_ns: int
    end_ns: int


@dataclass
class MacroDef:
    name: str
    level: str
    tokens: List[str]
    definition_len: int
    replace_count: int
    gain: int
    first_pos: int
    windows: List[Tuple[int, int]]
    defs_covered: int


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


def _symbol_name(idx: int) -> str:
    alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    base = len(alphabet)
    out = ""
    x = idx
    while True:
        out = alphabet[x % base] + out
        x = x // base - 1
        if x < 0:
            break
    return out


def _rle_tokens(tokens: Sequence[str]) -> List[str]:
    if not tokens:
        return []
    out: List[str] = []
    cur = tokens[0]
    cnt = 1
    for t in tokens[1:]:
        if t == cur:
            cnt += 1
            continue
        out.append(f"{cur}^{cnt}" if cnt > 1 else cur)
        cur = t
        cnt = 1
    out.append(f"{cur}^{cnt}" if cnt > 1 else cur)
    return out


def _wrap_expression(expr: str, width: int = 120) -> str:
    return textwrap.fill(
        expr.strip(),
        width=width,
        break_long_words=False,
        break_on_hyphens=False,
    )


def _one_line_preview(text: str, limit: int = 240) -> str:
    return re.sub(r"\s+", " ", text).strip()[:limit]


def _is_subseq(needle: Tuple[str, ...], hay: Tuple[str, ...]) -> bool:
    if len(needle) > len(hay):
        return False
    for i in range(0, len(hay) - len(needle) + 1):
        if hay[i : i + len(needle)] == needle:
            return True
    return False


def _find_non_overlap_starts(seq: Sequence[str], pattern: Sequence[str]) -> List[int]:
    if not pattern or len(pattern) > len(seq):
        return []
    starts: List[int] = []
    i = 0
    n = len(seq)
    m = len(pattern)
    while i <= n - m:
        if tuple(seq[i : i + m]) == tuple(pattern):
            starts.append(i)
            i += m
        else:
            i += 1
    return starts


def _non_overlap_starts_from_positions(positions: Sequence[int], pattern_len: int) -> List[int]:
    starts: List[int] = []
    next_allowed = 0
    for pos in positions:
        if pos < next_allowed:
            continue
        starts.append(pos)
        next_allowed = pos + pattern_len
    return starts


def _select_best_candidate(
    seq: Sequence[str],
    *,
    min_len: int,
    max_len: int,
    min_count: int,
) -> Tuple[Tuple[str, ...], List[int], int] | None:
    n = len(seq)
    if n < min_len:
        return None
    counts: Dict[Tuple[str, ...], int] = {}
    first_pos: Dict[Tuple[str, ...], int] = {}
    positions: Dict[Tuple[str, ...], List[int]] = {}
    upper = min(max_len, n)
    for l in range(min_len, upper + 1):
        for i in range(0, n - l + 1):
            pat = tuple(seq[i : i + l])
            counts[pat] = counts.get(pat, 0) + 1
            positions.setdefault(pat, []).append(i)
            if pat not in first_pos:
                first_pos[pat] = i

    best: Tuple[Tuple[str, ...], List[int], int] | None = None
    best_key: Tuple[int, int, int, int] | None = None
    for pat, c in counts.items():
        if c < min_count:
            continue
        if len(set(pat)) < 2:
            continue

        starts = _non_overlap_starts_from_positions(positions.get(pat, []), len(pat))
        k = len(starts)
        if k < min_count:
            continue
        gain = k * (len(pat) - 1) - (len(pat) + 1)
        if gain <= 0:
            continue

        key = (len(pat), gain, k, -first_pos.get(pat, 0))
        if best_key is None or key > best_key:
            best_key = key
            best = (pat, starts, gain)
    return best


def _replace_pattern_tokens(
    seq_tokens: Sequence[SeqToken],
    pattern: Sequence[str],
    starts: Sequence[int],
    macro_name: str,
) -> Tuple[List[SeqToken], List[Tuple[int, int]]]:
    m = len(pattern)
    start_set = set(starts)
    out: List[SeqToken] = []
    windows: List[Tuple[int, int]] = []
    i = 0
    n = len(seq_tokens)
    while i < n:
        if i in start_set and i + m <= n:
            seg = seq_tokens[i : i + m]
            s = seg[0].start_ns
            e = seg[-1].end_ns
            out.append(SeqToken(name=macro_name, start_ns=s, end_ns=e))
            windows.append((s, e))
            i += m
            continue
        out.append(seq_tokens[i])
        i += 1
    return out, windows

def _mine_meta_patterns(
    symbol_seq: Sequence[str],
    *,
    min_len: int = 3,
    max_len: int = 8,
    min_count: int = 3,
    topn: int = 20,
) -> List[Dict[str, object]]:
    n = len(symbol_seq)
    if n < min_len:
        return []
    counts: Dict[Tuple[str, ...], int] = {}
    first_pos: Dict[Tuple[str, ...], int] = {}
    upper = min(max_len, n)
    for l in range(min_len, upper + 1):
        for i in range(0, n - l + 1):
            pat = tuple(symbol_seq[i : i + l])
            counts[pat] = counts.get(pat, 0) + 1
            if pat not in first_pos:
                first_pos[pat] = i

    rows: List[Dict[str, object]] = []
    for pat, c in counts.items():
        if c < min_count:
            continue
        score = c * (len(pat) - 1)
        if score <= 0:
            continue
        rows.append(
            {
                "pattern_tokens": pat,
                "pattern_len": len(pat),
                "count": c,
                "score": score,
                "first_pos": first_pos.get(pat, -1),
            }
        )
    rows.sort(
        key=lambda r: (
            int(r["score"]),
            int(r["pattern_len"]),
            int(r["count"]),
            -int(r["first_pos"]),
        ),
        reverse=True,
    )

    selected: List[Dict[str, object]] = []
    for r in rows:
        pat = r["pattern_tokens"]  # type: ignore[assignment]
        conflict = False
        for s in selected:
            sp = s["pattern_tokens"]  # type: ignore[assignment]
            if _is_subseq(pat, sp) or _is_subseq(sp, pat):
                conflict = True
                break
        if conflict:
            continue
        selected.append(r)
        if len(selected) >= topn:
            break

    out: List[Dict[str, object]] = []
    for i, r in enumerate(selected, start=1):
        pat = list(r["pattern_tokens"])  # type: ignore[arg-type]
        out.append(
            {
                "rank": i,
                "pattern": " ".join(_rle_tokens(pat)),
                "pattern_len": int(r["pattern_len"]),
                "count": int(r["count"]),
                "score": int(r["score"]),
                "first_pos": int(r["first_pos"]),
            }
        )
    return out


def _build_readable_markdown(
    *,
    db_path: Path,
    device_id: int,
    stream_id: int,
    original_events: int,
    used_events: int,
    truncated: bool,
    compressed_nodes: int,
    compression_ratio_used: float,
    compression_ratio_original: float,
    expression_pretty: str,
    macro_expression: str,
    macro_defs: Sequence[Dict[str, object]],
    symbol_rows: Sequence[Dict[str, object]],
    meta_rows: Sequence[Dict[str, object]],
) -> str:
    lines: List[str] = []
    lines.append("# Loop Analyzer Readable Report")
    lines.append("")
    lines.append(f"- db: `{db_path}`")
    lines.append(f"- device_id: `{device_id}`")
    lines.append(f"- stream_id: `{stream_id}`")
    lines.append(f"- original_events: `{original_events}`")
    lines.append(f"- used_events: `{used_events}`")
    lines.append(f"- truncated: `{int(truncated)}`")
    lines.append(f"- compressed_nodes: `{compressed_nodes}`")
    lines.append(f"- compression_ratio_used: `{compression_ratio_used:.6f}`")
    lines.append(f"- compression_ratio_original: `{compression_ratio_original:.6f}`")
    lines.append("")
    lines.append("## Expression")
    lines.append("")
    lines.append("```")
    lines.append(expression_pretty)
    lines.append("```")
    lines.append("")
    lines.append("## Macro Expression")
    lines.append("")
    lines.append("```")
    lines.append(macro_expression)
    lines.append("```")
    lines.append("")
    lines.append("## Macros")
    lines.append("")
    if macro_defs:
        lines.append("| name | level | definition | len | replace_count | gain | defs_covered |")
        lines.append("| --- | --- | --- | ---: | ---: | ---: | ---: |")
        for r in macro_defs:
            lines.append(
                f"| {r.get('name','')} | {r.get('level','')} | {r.get('definition','')} | {int(r.get('definition_len',0))} | {int(r.get('replace_count',0))} | {int(r.get('gain',0))} | {int(r.get('defs_covered',0))} |"
            )
    else:
        lines.append("No macro selected (all candidates have non-positive net gain).")
    lines.append("")
    lines.append("## Symbols")
    lines.append("")
    lines.append("| symbol | category | window_count | label |")
    lines.append("| --- | --- | ---: | --- |")
    for r in sorted(symbol_rows, key=lambda x: int(x.get("window_count", 0)), reverse=True):
        lines.append(
            f"| {r.get('symbol','')} | {r.get('category','')} | {int(r.get('window_count',0))} | {r.get('label','')} |"
        )
    lines.append("")
    lines.append("## Meta Patterns")
    lines.append("")
    if meta_rows:
        lines.append("| rank | pattern | len | count | score |")
        lines.append("| ---: | --- | ---: | ---: | ---: |")
        for r in meta_rows:
            lines.append(
                f"| {int(r.get('rank',0))} | {r.get('pattern','')} | {int(r.get('pattern_len',0))} | {int(r.get('count',0))} | {int(r.get('score',0))} |"
            )
    else:
        lines.append("No frequent meta pattern found with current threshold.")
    lines.append("")
    return "\n".join(lines)


def _tokens_to_ast_seq(
    tokens: Sequence[str],
    *,
    symbol_meta_map: Dict[str, Dict[str, object]],
    macro_names: set[str],
) -> Dict[str, object]:
    items: List[Dict[str, object]] = []
    for i, name in enumerate(tokens, start=1):
        if name in macro_names:
            items.append({"ord": i, "node": {"type": "MacroRef", "name": name}})
            continue

        meta = symbol_meta_map.get(name, {})
        items.append(
            {
                "ord": i,
                "node": {
                    "type": "Atom",
                    "symbol": name,
                    "op_label": meta.get("label", name),
                    "category": meta.get("category", ""),
                    "task_type": meta.get("task_type", ""),
                    "window_count": int(meta.get("window_count", 0)),
                },
            }
        )
    return {"type": "Seq", "items": items}


def _render_ast_lines(
    node: Dict[str, object],
    *,
    out: List[str],
    indent: str = "",
    prefix: str = "",
) -> None:
    t = str(node.get("type", ""))
    if t == "Seq":
        out.append(f"{indent}{prefix}Seq")
        items = node.get("items", [])
        if isinstance(items, list):
            for idx, it in enumerate(items, start=1):
                if not isinstance(it, dict):
                    continue
                child = it.get("node", {})
                if not isinstance(child, dict):
                    continue
                _render_ast_lines(
                    child,
                    out=out,
                    indent=indent + "  ",
                    prefix=f"[{idx}] ",
                )
        return

    if t == "Repeat":
        out.append(f"{indent}{prefix}Repeat x{int(node.get('count', 1))}")
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
                            _render_ast_lines(
                                child,
                                out=out,
                                indent=indent + "  ",
                                prefix=f"[{idx}] ",
                            )
            else:
                _render_ast_lines(body, out=out, indent=indent + "  ")
        return

    if t == "MacroRef":
        out.append(f"{indent}{prefix}MacroRef {node.get('name', '')}")
        return

    if t == "Atom":
        out.append(
            f"{indent}{prefix}Atom {node.get('symbol','')} | {node.get('op_label','')} | {node.get('category','')}"
        )
        return

    out.append(f"{indent}{prefix}{t}")


def _build_tree_v2(
    *,
    db_path: Path,
    device_id: int,
    stream_id: int,
    final_expr_tokens: Sequence[str],
    macro_rows: Sequence[Dict[str, object]],
    macro_def_tokens: Dict[str, List[str]],
    symbol_rows: Sequence[Dict[str, object]],
) -> Tuple[Dict[str, object], str]:
    symbol_meta_map = {str(r.get("symbol", "")): dict(r) for r in symbol_rows}
    macro_names = set(macro_def_tokens.keys())

    # Grammar-only tree construction: pattern discovery has already happened in
    # the macro-discovery phase. This builder only renders the discovered macro
    # IR, and treats LP macro definitions as explicit Repeat nodes.
    root_ast = _tokens_to_ast_seq(
        final_expr_tokens,
        symbol_meta_map=symbol_meta_map,
        macro_names=macro_names,
    )

    macro_defs_ast: List[Dict[str, object]] = []
    for row in macro_rows:
        name = str(row.get("name", ""))
        toks = list(macro_def_tokens.get(name, []))
        if row.get("level") == "LP" and toks:
            run_name = toks[0]
            body_ast = _tokens_to_ast_seq(
                [run_name],
                symbol_meta_map=symbol_meta_map,
                macro_names=macro_names,
            )
            def_ast = {
                "type": "Repeat",
                "count": len(toks),
                "body": body_ast,
            }
        else:
            def_ast = _tokens_to_ast_seq(
                toks,
                symbol_meta_map=symbol_meta_map,
                macro_names=macro_names,
            )
        macro_defs_ast.append(
            {
                "name": name,
                "level": row.get("level", ""),
                "gain": int(row.get("gain", 0)),
                "replace_count": int(row.get("replace_count", 0)),
                "definition": row.get("definition", ""),
                "tree": def_ast,
            }
        )

    def _collect_macro_refs(ast_node: Dict[str, object], out: Dict[str, int]) -> None:
        t = str(ast_node.get("type", ""))
        if t == "MacroRef":
            name = str(ast_node.get("name", ""))
            if name:
                out[name] = out.get(name, 0) + 1
            return
        if t == "Seq":
            items = ast_node.get("items", [])
            if isinstance(items, list):
                for it in items:
                    if isinstance(it, dict):
                        child = it.get("node", {})
                        if isinstance(child, dict):
                            _collect_macro_refs(child, out)
            return
        if t == "Repeat":
            body = ast_node.get("body", {})
            if isinstance(body, dict):
                _collect_macro_refs(body, out)

    root_macro_ref_counts: Dict[str, int] = {}
    _collect_macro_refs(root_ast, root_macro_ref_counts)
    macro_table = {str(m.get("name", "")): m for m in macro_defs_ast if m.get("name")}

    payload = {
        "schema_version": "loop_tree_v2",
        "db": str(db_path),
        "device_id": device_id,
        "stream_id": stream_id,
        "root": root_ast,
        "macro_defs": macro_defs_ast,
        "macro_table": macro_table,
        "root_macro_ref_counts": root_macro_ref_counts,
        "symbol_table": list(symbol_rows),
        "tree_construction": "grammar_only",
        "repeat_discovery": "macro_lp_only",
    }

    lines: List[str] = []
    lines.append("# Loop Tree (v2)")
    lines.append("")
    lines.append(f"- db: `{db_path}`")
    lines.append(f"- device_id: `{device_id}`")
    lines.append(f"- stream_id: `{stream_id}`")
    lines.append("")
    lines.append("## Root")
    lines.append("")
    lines.append("```")
    _render_ast_lines(root_ast, out=lines)
    lines.append("```")
    lines.append("")
    lines.append("## Macro Subtrees")
    lines.append("")
    if macro_defs_ast:
        for m in macro_defs_ast:
            lines.append(
                f"### {m['name']} ({m['level']}, gain={m['gain']}, replace_count={m['replace_count']})"
            )
            lines.append("")
            lines.append("```")
            _render_ast_lines(m["tree"], out=lines)
            lines.append("```")
            lines.append("")
    else:
        lines.append("No macro definitions.")
        lines.append("")
    return payload, "\n".join(lines)


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
            start_ns = int(row[0] if row[0] is not None else 0)
            end_ns = int(row[1] if row[1] is not None else 0)
            device_id = int(row[2] if row[2] is not None else -1)
            stream_id = int(row[3] if row[3] is not None else -1)
            key = (device_id, stream_id)
            if stream_filter is not None and key not in stream_filter:
                continue
            task_id = int(row[4] if row[4] is not None else -1)
            global_task_id = int(row[5] if row[5] is not None else -1)
            connection_id = int(row[6] if row[6] is not None else -1)
            task_type_id = int(row[7] if row[7] is not None else -1)

            task_type = sid_to_value.get(task_type_id, str(task_type_id))
            task_type_norm = _normalize_task_type(task_type)
            task_key = _normalize_task_key(task_type_norm)
            if task_key == "CAPTURE_WAIT":
                continue

            category = _classify_task(task_type_norm)
            if category == "exec" and connection_id in comm_connection_ids:
                category = "comm"
            if category not in {"wait", "comm", "exec"}:
                continue

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
                label_raw = task_type_norm
            label = _canonical_label(label_raw, category=category)

            out.setdefault(key, []).append(
                StreamEvent(
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
            )
    return out


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


def _write_csv(path: Path, rows: Sequence[Dict[str, object]]) -> None:
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fields = list(rows[0].keys())
    with path.open("w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for r in rows:
            w.writerow(r)
