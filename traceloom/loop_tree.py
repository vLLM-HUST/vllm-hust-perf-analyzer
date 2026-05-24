from __future__ import annotations

import csv
import json
import re
import textwrap
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple


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
