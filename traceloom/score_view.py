from __future__ import annotations

import argparse
import csv
import html
import json
from pathlib import Path
from typing import Any


def _read_metrics(path: Path) -> dict[str, dict[str, Any]]:
    rows: dict[str, dict[str, Any]] = {}
    numeric_fields = {
        "depth",
        "occurrence_count",
        "anchor_count",
        "anchors_per_occurrence",
        "compute_us",
        "comm_us",
        "idle_us",
        "total_us",
        "comm_pct",
        "idle_pct",
        "self_us",
        "self_exec_us",
        "self_comm_us",
        "aux_events",
        "aux_us",
    }
    with path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            parsed: dict[str, Any] = dict(row)
            for field in numeric_fields:
                value = parsed.get(field, "")
                if value == "":
                    parsed[field] = 0.0
                else:
                    parsed[field] = float(value)
            rows[str(parsed["node_id"])] = parsed
    return rows


def _infer_metrics_path(tree_path: Path) -> Path:
    name = tree_path.name
    candidates = []
    if name.endswith(".anchor.tree.json"):
        candidates.append(tree_path.with_name(name.replace(".anchor.tree.json", ".anchor.node_metrics.csv")))
    if name.endswith(".tree.json"):
        candidates.append(tree_path.with_name(name.replace(".tree.json", ".node_metrics.csv")))
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise FileNotFoundError(
        "Could not infer node metrics path. Pass --metrics explicitly. "
        f"Tried: {', '.join(str(p) for p in candidates)}"
    )


def _node_label(node: dict[str, Any], metrics: dict[str, Any] | None) -> str:
    if metrics and metrics.get("label"):
        return str(metrics["label"])
    if node.get("type") == "Seq":
        return f"Seq[{len(node.get('items', []))}]"
    if node.get("type") == "Repeat":
        return f"Repeat x{node.get('count', node.get('repeat', ''))}"
    return str(node.get("op_label") or node.get("label") or node.get("type") or "")


def _node_kind(node: dict[str, Any], metrics: dict[str, Any] | None) -> str:
    if metrics and metrics.get("kind"):
        return str(metrics["kind"])
    node_type = str(node.get("type") or "").lower()
    if node_type == "atom":
        return str(node.get("category") or "atom")
    return node_type


def _family(label: str, kind: str, category: str, symbol: str) -> str:
    text = f"{label} {kind} {category} {symbol}".lower()
    if "repeat" in kind or "repeat" in text:
        return "repeat"
    if "seq" in kind:
        return "seq"
    if "allreduce" in text:
        return "allreduce"
    if "allgather" in text:
        return "allgather"
    if (
        "alltoall" in text
        or "all_to_all" in text
        or "all-to-all" in text
        or "all2all" in text
        or "all#all" in text
        or "a2a" in text
        or "a#a" in text
    ):
        return "alltoall"
    if "reducescatter" in text:
        return "reducescatter"
    if "broadcast" in text:
        return "broadcast"
    if "comm" in category:
        return "comm"
    if "matmul" in text or symbol in {"AN", "AR", "BB"}:
        return "matmul"
    if "attention" in text:
        return "attention"
    if "rmsnorm" in text or "norm" in text:
        return "norm"
    if "swiglu" in text:
        return "swiglu"
    if "rope" in text or "rotary" in text or "qkv" in text:
        return "qkv_rope"
    if "copy" in text or "memcpy" in text:
        return "copy"
    return "other"


def _children_for_score(node: dict[str, Any]) -> list[str]:
    node_type = node.get("type")
    if node_type == "Seq":
        return [
            item.get("node", {}).get("node_id")
            for item in node.get("items", [])
            if item.get("node", {}).get("node_id")
        ]
    if node_type == "Repeat":
        body = node.get("body", {})
        if body.get("type") == "Seq":
            return [
                item.get("node", {}).get("node_id")
                for item in body.get("items", [])
                if item.get("node", {}).get("node_id")
            ]
        body_id = body.get("node_id")
        return [body_id] if body_id else []
    return []


def _build_payload(tree: dict[str, Any], metrics: dict[str, dict[str, Any]]) -> dict[str, Any]:
    nodes: dict[str, dict[str, Any]] = {}

    def visit(node: dict[str, Any]) -> None:
        node_id = node.get("node_id")
        if not node_id:
            return
        row = metrics.get(str(node_id), {})
        kind = _node_kind(node, row)
        category = str(row.get("category") or node.get("category") or "")
        symbol = str(row.get("symbol") or node.get("symbol") or "")
        label = _node_label(node, row)
        family = _family(label, kind, category, symbol)
        repeat = str(row.get("repeat") or (f"x{node.get('count')}" if node.get("type") == "Repeat" else ""))
        nodes[str(node_id)] = {
            "id": str(node_id),
            "type": str(node.get("type") or row.get("type") or ""),
            "kind": kind,
            "symbol": symbol,
            "label": label,
            "category": category,
            "repeat": repeat,
            "path": str(row.get("path") or node.get("tree_path") or ""),
            "depth": int(float(row.get("depth", node.get("tree_depth", 0) or 0))),
            "family": family,
            "children": _children_for_score(node),
            "metrics": {
                "occurrence_count": row.get("occurrence_count", 0.0),
                "anchor_count": row.get("anchor_count", 0.0),
                "compute_us": row.get("compute_us", 0.0),
                "comm_us": row.get("comm_us", 0.0),
                "idle_us": row.get("idle_us", 0.0),
                "total_us": row.get("total_us", 0.0),
                "comm_pct": row.get("comm_pct", 0.0),
                "idle_pct": row.get("idle_pct", 0.0),
                "self_us": row.get("self_us", 0.0),
                "self_exec_us": row.get("self_exec_us", 0.0),
                "self_comm_us": row.get("self_comm_us", 0.0),
                "aux_events": row.get("aux_events", 0.0),
                "aux_us": row.get("aux_us", 0.0),
            },
        }

        if node.get("type") == "Seq":
            for item in node.get("items", []):
                visit(item.get("node", {}))
        elif node.get("type") == "Repeat":
            visit(node.get("body", {}))

    root = tree["root"]
    root.setdefault("node_id", "N001")
    root.setdefault("tree_path", "root")
    root.setdefault("tree_depth", 0)
    visit(root)

    return {
        "schema": "execution_score_view_v1",
        "title": f"Execution Score View: device {tree.get('device_id', '')}",
        "db": str(tree.get("db", "")),
        "device_id": tree.get("device_id"),
        "root_id": str(root.get("node_id", "N001")),
        "nodes": nodes,
    }


def _json_for_html(payload: dict[str, Any]) -> str:
    return json.dumps(payload, ensure_ascii=False).replace("</", "<\\/")


def _render_html(payload: dict[str, Any]) -> str:
    title = html.escape(str(payload["title"]))
    payload_json = _json_for_html(payload)
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<style>
:root {{
  --bg: #f7f8fa;
  --panel: #ffffff;
  --ink: #1c2530;
  --muted: #607080;
  --grid: #dfe5eb;
  --compute: #2f8f83;
  --comm: #d65a31;
  --idle: #a8b2bd;
  --self: #314f9f;
  --aux: #8e6bb8;
  --col: 14px;
  font-family: ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
}}
* {{ box-sizing: border-box; }}
body {{ margin: 0; background: var(--bg); color: var(--ink); }}
main {{ padding: 18px; }}
header {{
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 16px;
  margin-bottom: 14px;
}}
h1 {{ font-size: 20px; line-height: 1.2; margin: 0 0 6px; letter-spacing: 0; }}
.subtle {{ color: var(--muted); font-size: 12px; overflow-wrap: anywhere; }}
.toolbar {{ display: flex; gap: 8px; align-items: center; flex-wrap: wrap; justify-content: flex-end; }}
button {{
  border: 1px solid #cbd5df;
  background: #fff;
  color: var(--ink);
  height: 32px;
  padding: 0 11px;
  border-radius: 6px;
  cursor: pointer;
  font: inherit;
}}
button:hover {{ border-color: #8190a0; background: #f3f6f8; }}
button:disabled {{ opacity: .45; cursor: default; }}
.layout {{
  display: grid;
  grid-template-columns: minmax(0, 1fr) 360px;
  gap: 14px;
  align-items: start;
}}
.panel {{
  background: var(--panel);
  border: 1px solid var(--grid);
  border-radius: 8px;
  box-shadow: 0 1px 2px rgba(20, 30, 40, .05);
}}
.score-panel {{ overflow: hidden; }}
.score-head {{
  padding: 12px 14px;
  border-bottom: 1px solid var(--grid);
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}}
.crumbs {{ font-size: 13px; color: var(--muted); overflow-wrap: anywhere; }}
.legend {{ display: flex; flex-wrap: wrap; gap: 10px; font-size: 12px; color: var(--muted); }}
.swatch {{ display: inline-block; width: 10px; height: 10px; border-radius: 2px; margin-right: 4px; vertical-align: -1px; }}
.scroll {{ overflow-x: auto; }}
.score {{
  display: grid;
  grid-template-columns: 118px 1fr;
  min-width: 740px;
}}
.axis {{
  position: sticky;
  left: 0;
  z-index: 3;
  background: #fbfcfd;
  border-right: 1px solid var(--grid);
}}
.axis-cell {{
  height: 52px;
  border-bottom: 1px solid var(--grid);
  padding: 8px 10px;
  color: var(--muted);
  font-size: 12px;
  display: flex;
  align-items: center;
}}
.rows {{
  display: grid;
  grid-template-rows: auto repeat(4, 52px);
  min-width: max-content;
}}
.brackets {{
  display: grid;
  grid-auto-flow: column;
  grid-auto-columns: var(--col);
  grid-auto-rows: 18px;
  min-height: 52px;
  padding-top: 4px;
  border-bottom: 1px solid var(--grid);
  background: #fbfcfd;
}}
.bracket {{
  position: relative;
  height: 16px;
  border-top: 1px solid #586674;
  border-left: 1px solid #586674;
  border-right: 1px solid #586674;
  color: #405060;
  font-size: 10px;
  line-height: 14px;
  text-align: center;
  overflow: hidden;
  white-space: nowrap;
  cursor: pointer;
  background: rgba(255,255,255,.55);
}}
.bracket:hover {{
  background: rgba(88,102,116,.12);
  border-color: #1f2f3d;
}}
.row {{
  display: grid;
  grid-auto-flow: column;
  grid-auto-columns: var(--col);
  border-bottom: 1px solid var(--grid);
}}
.cell {{
  position: relative;
  min-width: var(--col);
  height: 52px;
  border-right: 1px solid #edf1f5;
  padding: 0 2px;
  overflow: hidden;
}}
.tile {{
  position: absolute;
  left: 2px;
  right: 2px;
  top: 8px;
  bottom: 8px;
  border-radius: 2px;
  cursor: pointer;
}}
.tile:hover {{ filter: brightness(.94); }}
.bar-wrap {{ position: absolute; inset: 6px 3px 6px; display: flex; align-items: flex-end; justify-content: center; }}
.bar {{ width: 7px; min-height: 1px; border-radius: 2px 2px 0 0; background: #5877c7; }}
.bar.active {{ background: #2f8f83; }}
.bar.idle {{ background: var(--idle); }}
.bar.self {{ background: var(--self); }}
.stack {{
  position: absolute;
  left: 3px;
  right: 3px;
  bottom: 6px;
  height: 38px;
  border-radius: 2px;
  overflow: hidden;
  display: flex;
  flex-direction: column-reverse;
  background: #eef2f5;
  border: 1px solid #dbe2e8;
}}
.seg.compute {{ background: var(--compute); }}
.seg.comm {{ background: var(--comm); }}
.seg.idle {{ background: var(--idle); }}
.detail {{ padding: 14px; position: sticky; top: 14px; }}
.detail h2 {{ margin: 0 0 8px; font-size: 16px; letter-spacing: 0; }}
.kv {{ display: grid; grid-template-columns: 116px 1fr; gap: 6px 10px; font-size: 12px; }}
.kv div:nth-child(odd) {{ color: var(--muted); }}
.detail-label {{ overflow-wrap: anywhere; }}
.hint {{ margin-top: 12px; padding-top: 12px; border-top: 1px solid var(--grid); color: var(--muted); font-size: 12px; line-height: 1.45; }}
.empty {{ padding: 36px; color: var(--muted); }}
.legend-block {{ margin-top: 12px; padding-top: 12px; border-top: 1px solid var(--grid); }}
.legend-grid {{ display: grid; grid-template-columns: 1fr 1fr; gap: 6px 12px; font-size: 12px; color: var(--muted); }}
.family-repeat {{ background: #6b5b95; }}
.family-seq {{ background: #59656f; }}
.family-allreduce {{ background: #cc503e; }}
.family-allgather {{ background: #d17b2f; }}
.family-alltoall {{ background: #c35b1f; }}
.family-reducescatter {{ background: #c74772; }}
.family-broadcast {{ background: #b15d9a; }}
.family-comm {{ background: #ba4d3c; }}
.family-matmul {{ background: #2d75b8; }}
.family-attention {{ background: #5f8f36; }}
.family-norm {{ background: #2f8f83; }}
.family-swiglu {{ background: #7d6bb8; }}
.family-qkv_rope {{ background: #24889c; }}
.family-copy {{ background: #9a7447; }}
.family-other {{ background: #6e7b85; }}
@media (max-width: 900px) {{
  main {{ padding: 12px; }}
  .layout {{ grid-template-columns: 1fr; }}
  .detail {{ position: static; }}
  header {{ display: block; }}
  .toolbar {{ justify-content: flex-start; margin-top: 10px; }}
}}
</style>
</head>
<body>
<main>
  <header>
    <div>
      <h1>{title}</h1>
      <div class="subtle" id="dbPath"></div>
    </div>
    <div class="toolbar">
      <button id="rootBtn">Root</button>
      <button id="backBtn">Back</button>
    </div>
  </header>
  <div class="layout">
    <section class="panel score-panel">
      <div class="score-head">
        <div class="crumbs" id="crumbs"></div>
        <div class="legend">
          <span><span class="swatch" style="background:var(--compute)"></span>compute</span>
          <span><span class="swatch" style="background:var(--comm)"></span>comm</span>
          <span><span class="swatch" style="background:var(--idle)"></span>idle</span>
          <span><span class="swatch" style="background:var(--self)"></span>self</span>
        </div>
      </div>
      <div class="scroll">
        <div class="score">
          <div class="axis">
            <div class="axis-cell">Repeat spans</div>
            <div class="axis-cell">Kernel type</div>
            <div class="axis-cell">Active avg</div>
            <div class="axis-cell">Idle avg</div>
            <div class="axis-cell">Active mix</div>
            <div class="axis-cell">Self active</div>
          </div>
          <div class="rows" id="rows"></div>
        </div>
      </div>
    </section>
    <aside class="panel detail" id="detail"></aside>
  </div>
</main>
<script id="payload" type="application/json">{payload_json}</script>
<script>
const payload = JSON.parse(document.getElementById("payload").textContent);
const nodes = payload.nodes;
const rootId = payload.root_id;
let viewStack = [rootId];
let selectedId = rootId;

const rowsEl = document.getElementById("rows");
const detailEl = document.getElementById("detail");
const crumbsEl = document.getElementById("crumbs");
const dbPathEl = document.getElementById("dbPath");
const rootBtn = document.getElementById("rootBtn");
const backBtn = document.getElementById("backBtn");

dbPathEl.textContent = payload.db || "";
rootBtn.addEventListener("click", () => {{ viewStack = [rootId]; selectedId = rootId; render(); }});
backBtn.addEventListener("click", () => {{
  if (viewStack.length > 1) viewStack.pop();
  selectedId = viewStack[viewStack.length - 1];
  render();
}});

function currentNode() {{ return nodes[viewStack[viewStack.length - 1]]; }}
function fmtUs(v) {{
  v = Number(v || 0);
  if (v >= 1_000_000) return (v / 1_000_000).toFixed(2) + "s";
  if (v >= 1000) return (v / 1000).toFixed(1) + "ms";
  return v.toFixed(v >= 10 ? 1 : 2) + "us";
}}
function fmtCount(v) {{
  v = Number(v || 0);
  return v >= 1000 ? Math.round(v).toLocaleString() : String(Math.round(v));
}}
function linearHeight(value, maxValue) {{
  value = Math.max(0, Number(value || 0));
  maxValue = Math.max(1, Number(maxValue || 1));
  if (value <= 0) return 0;
  return Math.max(1, 40 * value / maxValue);
}}
function activeUs(metrics) {{
  return Number(metrics.compute_us || 0) + Number(metrics.comm_us || 0);
}}
function metricValue(node, field) {{
  const m = node.metrics || {{}};
  if (field === "active_us") return activeUs(m);
  return Number(m[field] || 0);
}}
function enterNode(node) {{
  selectedId = node.id;
  if ((node.children || []).length > 0) {{
    viewStack.push(node.id);
    render();
  }} else {{
    renderDetail(node);
  }}
}}
function renderCrumbs() {{
  const parts = viewStack.map(id => nodes[id]).filter(Boolean).map(n => `${{n.id}} ${{n.repeat || n.label || n.type}}`);
  crumbsEl.textContent = parts.join(" / ");
  backBtn.disabled = viewStack.length <= 1;
}}
function flattenScore(viewNode) {{
  const columns = [];
  const spans = [];
  function walk(node, level) {{
    if (!node) return;
    const children = (node.children || []).map(id => nodes[id]).filter(Boolean);
    if (node.type === "Repeat" && children.length > 0) {{
      const start = columns.length;
      for (const child of children) walk(child, level + 1);
      const end = columns.length;
      if (end > start) spans.push({{ node, start, end, level }});
      return;
    }}
    if (node.type === "Seq" && children.length > 0) {{
      for (const child of children) walk(child, level);
      return;
    }}
    columns.push(node);
  }}
  for (const id of viewNode.children || []) walk(nodes[id], 0);
  return {{ columns, spans }};
}}
function render() {{
  renderCrumbs();
  const viewNode = currentNode();
  const score = flattenScore(viewNode);
  const visible = score.columns;
  selectedId = viewStack[viewStack.length - 1];
  rowsEl.innerHTML = "";
  if (visible.length === 0) {{
    rowsEl.innerHTML = '<div class="empty">No children to render.</div>';
    renderDetail(currentNode());
    return;
  }}
  const maxActive = Math.max(...visible.map(n => metricValue(n, "active_us")), 1);
  const maxIdle = Math.max(...visible.map(n => metricValue(n, "idle_us")), 1);
  const maxSelf = Math.max(...visible.map(n => metricValue(n, "self_us")), 1);
  rowsEl.appendChild(renderBracketRow(score.spans, visible.length));
  rowsEl.appendChild(renderTypeRow(visible));
  rowsEl.appendChild(renderBarRow(visible, "active_us", maxActive, "active"));
  rowsEl.appendChild(renderBarRow(visible, "idle_us", maxIdle, "idle"));
  rowsEl.appendChild(renderBreakdownRow(visible));
  rowsEl.appendChild(renderBarRow(visible, "self_us", maxSelf, "self"));
  renderDetail(nodes[selectedId] || currentNode());
}}
function renderBracketRow(spans, columnCount) {{
  const row = document.createElement("div");
  row.className = "brackets";
  row.style.gridTemplateColumns = `repeat(${{Math.max(columnCount, 1)}}, var(--col))`;
  const maxLevel = spans.length ? Math.max(...spans.map(s => s.level)) : 0;
  row.style.gridTemplateRows = `repeat(${{maxLevel + 1}}, 18px)`;
  row.style.minHeight = `${{Math.max(52, (maxLevel + 1) * 18 + 4)}}px`;
  for (const span of spans) {{
    const item = document.createElement("div");
    item.className = "bracket";
    item.style.gridColumn = `${{span.start + 1}} / ${{span.end + 1}}`;
    item.style.gridRow = `${{span.level + 1}}`;
    item.textContent = `${{span.node.id}} ${{span.node.repeat || span.node.label}}`;
    item.title = `${{span.node.id}} ${{span.node.label}}`;
    item.addEventListener("mouseenter", () => renderDetail(span.node));
    item.addEventListener("click", () => enterNode(span.node));
    row.appendChild(item);
  }}
  return row;
}}
function baseCell(node) {{
  const cell = document.createElement("div");
  cell.className = "cell";
  cell.title = `${{node.id}} ${{node.label}}`;
  cell.addEventListener("mouseenter", () => renderDetail(node));
  cell.addEventListener("click", () => enterNode(node));
  return cell;
}}
function renderTypeRow(visible) {{
  const row = document.createElement("div");
  row.className = "row";
  for (const node of visible) {{
    const cell = baseCell(node);
    const tile = document.createElement("div");
    tile.className = `tile family-${{node.family || "other"}}`;
    cell.appendChild(tile);
    row.appendChild(cell);
  }}
  return row;
}}
function renderBarRow(visible, field, maxValue, cls) {{
  const row = document.createElement("div");
  row.className = "row";
  for (const node of visible) {{
    const cell = baseCell(node);
    const value = metricValue(node, field);
    const wrap = document.createElement("div");
    wrap.className = "bar-wrap";
    const bar = document.createElement("div");
    bar.className = `bar ${{cls}}`;
    bar.style.height = `${{linearHeight(value, maxValue)}}px`;
    wrap.appendChild(bar);
    cell.appendChild(wrap);
    row.appendChild(cell);
  }}
  return row;
}}
function renderBreakdownRow(visible) {{
  const row = document.createElement("div");
  row.className = "row";
  for (const node of visible) {{
    const cell = baseCell(node);
    const m = node.metrics;
    const total = Math.max(1e-9, Number(m.compute_us || 0) + Number(m.comm_us || 0));
    const stack = document.createElement("div");
    stack.className = "stack";
    for (const [name, value] of [["compute", m.compute_us], ["comm", m.comm_us]]) {{
      const seg = document.createElement("div");
      seg.className = `seg ${{name}}`;
      seg.style.height = `${{100 * Number(value || 0) / total}}%`;
      stack.appendChild(seg);
    }}
    cell.appendChild(stack);
    row.appendChild(cell);
  }}
  return row;
}}
function renderDetail(node) {{
  if (!node) {{
    detailEl.innerHTML = "";
    return;
  }}
  const m = node.metrics || {{}};
  const active = activeUs(m);
  const total = Number(m.total_us || 0);
  const idle = Number(m.idle_us || 0);
  const activePct = total > 0 ? 100 * active / total : 0;
  const idlePct = total > 0 ? 100 * idle / total : 0;
  detailEl.innerHTML = `
    <h2>${{node.id}} <span class="subtle">${{escapeHtml(node.kind || node.type)}}</span></h2>
    <div class="detail-label">${{escapeHtml(node.label || "")}}</div>
    <div class="hint"></div>
    <div class="kv">
      <div>symbol</div><div>${{escapeHtml(node.symbol || "")}}</div>
      <div>family</div><div>${{escapeHtml(node.family || "")}}</div>
      <div>repeat</div><div>${{escapeHtml(node.repeat || "")}}</div>
      <div>occurrences</div><div>${{fmtCount(m.occurrence_count)}}</div>
      <div>anchors</div><div>${{fmtCount(m.anchor_count)}}</div>
      <div>total avg</div><div>${{fmtUs(m.total_us)}}</div>
      <div>active avg</div><div>${{fmtUs(active)}} (${{activePct.toFixed(1)}}%)</div>
      <div>compute avg</div><div>${{fmtUs(m.compute_us)}}</div>
      <div>comm avg</div><div>${{fmtUs(m.comm_us)}}</div>
      <div>idle avg</div><div>${{fmtUs(m.idle_us)}} (${{idlePct.toFixed(1)}}%)</div>
      <div>self active</div><div>${{fmtUs(m.self_us)}}</div>
      <div>aux avg</div><div>${{fmtUs(m.aux_us)}} / ${{fmtCount(m.aux_events)}} events</div>
      <div>path</div><div>${{escapeHtml(node.path || "")}}</div>
    </div>
    <div class="legend-block">
      <h2>Color Legend</h2>
      <div class="legend-grid">
        ${{legendItem("matmul", "MatMul")}}
        ${{legendItem("attention", "Attention")}}
        ${{legendItem("norm", "Norm / RMSNorm")}}
        ${{legendItem("swiglu", "SwiGlu")}}
        ${{legendItem("qkv_rope", "QKV / RoPE")}}
        ${{legendItem("allreduce", "AllReduce")}}
        ${{legendItem("allgather", "AllGather")}}
        ${{legendItem("alltoall", "All-to-All")}}
        ${{legendItem("repeat", "Repeat span")}}
        ${{legendItem("other", "Other")}}
      </div>
    </div>
    <div class="hint">${{(node.children || []).length ? "Click this node in the score to drill into its compressed body." : "Leaf node. Hover other cells to compare costs."}}</div>
  `;
}}
function legendItem(family, label) {{
  return `<div><span class="swatch family-${{family}}"></span>${{escapeHtml(label)}}</div>`;
}}
function escapeHtml(s) {{
  return String(s).replace(/[&<>"']/g, ch => ({{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}}[ch]));
}}
render();
</script>
</body>
</html>
"""


def build_score_view(tree_path: Path, metrics_path: Path, out_path: Path) -> None:
    tree = json.loads(tree_path.read_text(encoding="utf-8"))
    metrics = _read_metrics(metrics_path)
    payload = _build_payload(tree, metrics)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(_render_html(payload), encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Build a static execution score view from TraceLoom output")
    parser.add_argument("tree_json", type=Path, help="Path to *.anchor.tree.json")
    parser.add_argument("--metrics", type=Path, help="Path to matching *.anchor.node_metrics.csv")
    parser.add_argument("--out", type=Path, help="Output HTML path")
    args = parser.parse_args(argv)

    tree_path = args.tree_json
    metrics_path = args.metrics or _infer_metrics_path(tree_path)
    out_path = args.out or tree_path.with_name(tree_path.name.replace(".tree.json", ".score_view.html"))
    build_score_view(tree_path, metrics_path, out_path)
    print(out_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
