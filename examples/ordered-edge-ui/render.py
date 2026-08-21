#!/usr/bin/env python3
"""Render an interactive ordered-edge experiment from a TraceLoom database."""

from __future__ import annotations

import argparse
import json
import sqlite3
from collections import defaultdict
from pathlib import Path
from typing import Any


ROLE_COLORS = (
    "#74c0fc",
    "#b197fc",
    "#63e6be",
    "#ffd43b",
    "#ff8787",
    "#91a7ff",
    "#69db7c",
    "#ffa94d",
    "#e599f7",
    "#66d9e8",
    "#f783ac",
    "#a9e34b",
    "#faa2c1",
    "#4dabf7",
    "#c0eb75",
    "#ffc078",
)


def open_readonly(path: Path) -> sqlite3.Connection:
    db = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    db.row_factory = sqlite3.Row
    return db


def query_one(db: sqlite3.Connection, sql: str, params: tuple[Any, ...] = ()) -> sqlite3.Row:
    row = db.execute(sql, params).fetchone()
    if row is None:
        raise RuntimeError("query returned no row")
    return row


def choose_position(db: sqlite3.Connection, requested: str | None) -> sqlite3.Row:
    if requested:
        return query_one(
            db,
            """
            SELECT p.*,
                   (SELECT count(*) FROM traceloom_position_refinement r
                    WHERE r.parent_position_id = p.position_id) AS role_count
            FROM traceloom_v_position p
            WHERE p.position_id = ?
            """,
            (requested,),
        )

    return query_one(
        db,
        """
        SELECT p.*,
               (SELECT count(*) FROM traceloom_position_refinement r
                WHERE r.parent_position_id = p.position_id) AS role_count
        FROM traceloom_v_position p
        WHERE p.node_type = 'Repeat'
          AND p.repeat_count BETWEEN 4 AND 64
          AND p.occurrence_count > 1
          AND (SELECT count(*) FROM traceloom_position_refinement r
               WHERE r.parent_position_id = p.position_id) BETWEEN 4 AND 24
        ORDER BY
          p.repeat_count * p.occurrence_count *
            (SELECT count(*) FROM traceloom_position_refinement r
             WHERE r.parent_position_id = p.position_id) DESC,
          p.total_us DESC,
          p.position_id
        LIMIT 1
        """,
    )


def load_model(db: sqlite3.Connection, position_id: str) -> dict[str, Any]:
    position = dict(choose_position(db, position_id))
    roles = [
        dict(row)
        for row in db.execute(
            """
            SELECT r.slot_ordinal AS role_order,
                   r.child_position_id AS role_id,
                   p.label, p.symbol, p.node_type, p.repeat_count,
                   p.semantic_kind, p.category
            FROM traceloom_position_refinement r
            JOIN traceloom_v_position p
              ON p.position_id = r.child_position_id
            WHERE r.parent_position_id = ?
            ORDER BY r.slot_ordinal
            """,
            (position_id,),
        )
    ]
    if not roles:
        raise RuntimeError(f"{position_id} has no child edge roles")

    for index, role in enumerate(roles):
        role["color"] = ROLE_COLORS[index % len(ROLE_COLORS)]

    parent_rows = [
        dict(row)
        for row in db.execute(
            """
            SELECT occurrence_id, occurrence_idx, repeat_iteration,
                   token_start_ordinal, token_end_exclusive,
                   rooted_position_path, occurrence_path
            FROM traceloom_v_position_occurrence
            WHERE position_id = ?
            ORDER BY occurrence_idx
            """,
            (position_id,),
        )
    ]
    if not parent_rows:
        raise RuntimeError(f"{position_id} has no Occurrences")

    member_rows = db.execute(
        """
        SELECT parent.occurrence_id AS parent_occurrence_id,
               parent.occurrence_idx AS parent_occurrence_idx,
               m.slot_ordinal AS stored_role_order,
               m.member_order AS stored_role_rank,
               m.child_position_id AS role_id,
               child_pos.label,
               child_pos.symbol,
               child_pos.node_type,
               m.child_occurrence_id,
               child.occurrence_idx AS child_occurrence_idx,
               child.token_start_ordinal,
               child.token_end_exclusive,
               cost.start_ns,
               cost.end_ns,
               cost.total_us,
               cost.compute_us,
               cost.comm_us,
               cost.idle_us,
               cost.self_us,
               cost.aux_us
        FROM traceloom_v_position_occurrence parent
        JOIN traceloom_v_position_member m
          ON m.parent_occurrence_id = parent.occurrence_id
         AND m.member_kind = 'child_occurrence'
        JOIN traceloom_v_position_occurrence child
          ON child.occurrence_id = m.child_occurrence_id
        JOIN traceloom_v_position child_pos
          ON child_pos.position_id = m.child_position_id
        LEFT JOIN traceloom_tree_node_occurrence cost
          ON cost.node_id = child.position_id
         AND cost.occurrence_idx = child.occurrence_idx
        WHERE parent.position_id = ?
        ORDER BY parent.occurrence_idx,
                 child.token_start_ordinal,
                 child.token_end_exclusive,
                 m.slot_ordinal,
                 m.member_order
        """,
        (position_id,),
    ).fetchall()

    role_by_id = {role["role_id"]: role for role in roles}
    edges_by_parent: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in member_rows:
        edge = dict(row)
        if edge["role_id"] not in role_by_id:
            raise RuntimeError(f"member has unknown role {edge['role_id']}")
        edges_by_parent[edge["parent_occurrence_id"]].append(edge)

    expected_signature: list[str] | None = None
    expected_edge_count: int | None = None
    role_population_count: dict[str, int] = defaultdict(int)
    role_costs: dict[str, list[float]] = defaultdict(list)
    parents: list[dict[str, Any]] = []

    for parent in parent_rows:
        edges = edges_by_parent[parent["occurrence_id"]]
        if not edges:
            raise RuntimeError(f"{parent['occurrence_id']} has no child edges")

        seen_by_role: dict[str, int] = defaultdict(int)
        for concrete_order, edge in enumerate(edges, start=1):
            role_id = edge["role_id"]
            seen_by_role[role_id] += 1
            derived_rank = seen_by_role[role_id]
            if derived_rank != edge["stored_role_rank"]:
                raise RuntimeError(
                    f"{parent['occurrence_id']} {role_id}: concrete-order rank "
                    f"{derived_rank} != stored member_order "
                    f"{edge['stored_role_rank']}"
                )
            if role_by_id[role_id]["role_order"] != edge["stored_role_order"]:
                raise RuntimeError(
                    f"{parent['occurrence_id']} {role_id}: role identity disagrees "
                    "with stored slot"
                )

            edge["concrete_order"] = concrete_order
            edge["derived_role_rank"] = derived_rank
            role_population_count[role_id] += 1
            if edge["total_us"] is not None:
                role_costs[role_id].append(float(edge["total_us"]))

        signature = [edge["role_id"] for edge in edges]
        if expected_signature is None:
            expected_signature = signature
            expected_edge_count = len(edges)
        elif signature != expected_signature:
            raise RuntimeError(
                f"{parent['occurrence_id']} has a different concrete role phrase"
            )

        parent["edges"] = edges
        parents.append(parent)

    role_ids = [role["role_id"] for role in roles]
    repeat_count = int(position["repeat_count"] or 0)
    expected_repeated_signature = role_ids * repeat_count
    phrase_verified = expected_signature == expected_repeated_signature

    for role in roles:
        costs = sorted(role_costs[role["role_id"]])
        role["population_count"] = role_population_count[role["role_id"]]
        role["median_total_us"] = costs[(len(costs) - 1) // 2] if costs else None

    default_candidates = [role for role in roles if role["node_type"] == "Atom"] or roles
    default_role = max(
        default_candidates,
        key=lambda role: (
            -1.0 if role["median_total_us"] is None else role["median_total_us"],
            -role["role_order"],
        ),
    )["role_id"]

    position_summary = {
        "position_id": position["position_id"],
        "label": position["label"],
        "node_type": position["node_type"],
        "repeat_count": repeat_count,
        "occurrence_count": len(parents),
        "display_path": position["display_path"],
        "tree_id": position["tree_id"],
        "db_idx": position["db_idx"],
        "device_id": position["device_id"],
        "role_count": len(roles),
        "edges_per_parent": expected_edge_count,
        "edge_population_count": sum(role_population_count.values()),
        "phrase_verified": phrase_verified,
        "rooted_position_path": parents[0]["rooted_position_path"],
    }
    return {
        "position": position_summary,
        "roles": roles,
        "parents": parents,
        "default_role": default_role,
        "adapter": {
            "concrete_order": "token_start_ordinal, token_end_exclusive",
            "role_identity": "child_position_id under one parent structural context",
            "derived_rank_check": "rank among same-role edges equals stored member_order",
            "canonical_storage": "hierarchical_position_occurrence_v1",
        },
    }


def render_html(model: dict[str, Any], source: Path) -> str:
    payload = json.dumps(model, ensure_ascii=False, separators=(",", ":")).replace(
        "</", "<\\/"
    )
    source_text = str(source)
    template = r'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TraceLoom · ordered edge explorer</title>
<style>
:root {
  color-scheme: dark;
  --bg: #07101d;
  --panel: #0d1929;
  --panel-2: #111f32;
  --line: #283a52;
  --text: #edf4ff;
  --muted: #91a5bf;
  --accent: #63e6be;
  --danger: #ff8787;
  --shadow: 0 18px 60px rgba(0,0,0,.28);
}
* { box-sizing: border-box; }
body {
  margin: 0;
  min-width: 1040px;
  background:
    radial-gradient(circle at 18% -10%, rgba(77,171,247,.15), transparent 34rem),
    radial-gradient(circle at 92% 5%, rgba(99,230,190,.10), transparent 30rem),
    var(--bg);
  color: var(--text);
  font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont,
               "Segoe UI", sans-serif;
}
button, select { font: inherit; }
button { color: inherit; }
.app { width: min(1560px, calc(100vw - 48px)); margin: 0 auto; padding: 34px 0 56px; }
.topline { display: flex; align-items: flex-start; justify-content: space-between; gap: 24px; }
.eyebrow { color: var(--accent); font-size: 12px; font-weight: 750; letter-spacing: .14em; text-transform: uppercase; }
h1 { margin: 8px 0 8px; font-size: 34px; line-height: 1.1; letter-spacing: -.035em; }
.subtitle { margin: 0; color: var(--muted); font-size: 16px; max-width: 850px; line-height: 1.55; }
.status {
  border: 1px solid rgba(99,230,190,.38); background: rgba(99,230,190,.08);
  border-radius: 999px; padding: 9px 14px; color: #9ff3dc; font-size: 12px;
  white-space: nowrap;
}
.warning {
  margin-top: 20px; border: 1px solid #314761; background: rgba(17,31,50,.78);
  border-radius: 12px; padding: 12px 16px; color: #b7c7da; font-size: 13px;
  display: flex; align-items: center; gap: 10px;
}
.warning strong { color: #f7d794; }
.summary-grid { display: grid; grid-template-columns: 1.6fr repeat(4, 1fr); gap: 10px; margin: 18px 0; }
.metric, .path-card {
  background: linear-gradient(180deg, rgba(17,31,50,.92), rgba(12,24,40,.92));
  border: 1px solid var(--line); border-radius: 13px; padding: 14px 16px;
}
.metric-label { color: var(--muted); font-size: 11px; text-transform: uppercase; letter-spacing: .09em; }
.metric-value { margin-top: 5px; font-size: 23px; font-weight: 730; }
.path { margin-top: 7px; color: #d4e3f5; font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 13px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.workspace { display: grid; grid-template-columns: 320px minmax(0, 1fr); gap: 14px; align-items: start; }
.panel {
  background: linear-gradient(180deg, rgba(13,25,41,.96), rgba(9,19,33,.96));
  border: 1px solid var(--line); border-radius: 16px; box-shadow: var(--shadow);
}
.panel-head { padding: 17px 18px 13px; border-bottom: 1px solid rgba(40,58,82,.76); }
.panel-title { font-size: 14px; font-weight: 720; }
.panel-note { color: var(--muted); font-size: 12px; margin-top: 4px; line-height: 1.45; }
.role-list { padding: 10px; max-height: 756px; overflow: auto; }
.role-button {
  width: 100%; display: grid; grid-template-columns: 4px 1fr auto; gap: 10px;
  align-items: center; border: 1px solid transparent; border-radius: 10px;
  background: transparent; padding: 9px 10px 9px 7px; text-align: left; cursor: pointer;
}
.role-button:hover { background: rgba(255,255,255,.035); }
.role-button.active { background: rgba(255,255,255,.065); border-color: #3b506c; }
.role-swatch { height: 32px; border-radius: 4px; }
.role-name { min-width: 0; }
.role-name strong { display: block; font-size: 13px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.role-name span { display: block; margin-top: 2px; color: var(--muted); font-size: 10px; font-family: ui-monospace, monospace; }
.role-cost { color: #b8c8dc; font-size: 11px; text-align: right; }
.main { display: grid; gap: 14px; }
.controls { display: flex; align-items: center; justify-content: space-between; gap: 16px; }
.controls-left, .segmented { display: flex; align-items: center; gap: 8px; }
.control-label { color: var(--muted); font-size: 12px; }
select {
  background: #111f32; color: var(--text); border: 1px solid #38506d;
  border-radius: 8px; padding: 7px 10px;
}
.segmented { background: #091523; border: 1px solid #263b54; padding: 3px; border-radius: 9px; }
.segment { border: 0; background: transparent; color: var(--muted); border-radius: 6px; padding: 6px 10px; cursor: pointer; font-size: 11px; }
.segment.active { background: #1b304b; color: var(--text); }
.stream-wrap { padding: 16px 18px 18px; overflow: auto; max-height: 570px; }
.phrase-row { display: grid; grid-template-columns: 48px minmax(880px,1fr); gap: 10px; align-items: stretch; margin-bottom: 7px; }
.phrase-index { color: #7287a1; font-family: ui-monospace, monospace; font-size: 11px; display: flex; align-items: center; justify-content: flex-end; padding-right: 4px; }
.edge-row { display: grid; gap: 5px; }
.edge {
  position: relative; min-width: 0; height: 50px; border: 1px solid #344a66;
  background: #111f32; border-radius: 7px; cursor: pointer; overflow: hidden;
  padding: 7px 6px; text-align: left; transition: opacity .12s, transform .12s, border-color .12s;
}
.edge:hover { transform: translateY(-1px); border-color: #7e93ad; }
.edge.dim { opacity: .19; }
.edge.equivalent { border-color: var(--role-color); }
.edge.selected { outline: 2px solid #fff; outline-offset: 1px; z-index: 2; }
.edge-fill { position: absolute; inset: auto 0 0; background: var(--role-color); opacity: .28; }
.edge-label { position: relative; display: block; font-size: 10px; font-weight: 700; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.edge-cost { position: relative; display: block; margin-top: 4px; color: #aebfd2; font-size: 9px; font-family: ui-monospace, monospace; }
.legend-line { display: flex; align-items: center; justify-content: space-between; margin: 0 18px 15px 76px; color: var(--muted); font-size: 11px; }
.details-grid { display: grid; grid-template-columns: 1.25fr .75fr; gap: 14px; }
.distribution { padding: 16px 18px 18px; }
.distribution-head { display: flex; justify-content: space-between; gap: 16px; align-items: flex-start; }
.role-heading { display: flex; align-items: center; gap: 9px; }
.dot { width: 10px; height: 10px; border-radius: 50%; background: var(--selected-color); box-shadow: 0 0 14px var(--selected-color); }
.role-heading strong { font-size: 15px; }
.population-caption { color: var(--muted); font-size: 12px; margin-top: 4px; }
.stats { display: flex; gap: 18px; }
.stat { text-align: right; }
.stat b { display: block; font-size: 14px; }
.stat span { color: var(--muted); font-size: 10px; text-transform: uppercase; letter-spacing: .08em; }
.chart { height: 180px; margin-top: 14px; border-radius: 10px; background: #081421; border: 1px solid #21364d; overflow: hidden; }
.chart svg { width: 100%; height: 100%; display: block; }
.axis-label { fill: #7890aa; font-size: 10px; font-family: ui-monospace, monospace; }
.detail { padding: 16px 18px 18px; }
.detail-title { font-size: 15px; font-weight: 720; }
.detail-sub { color: var(--muted); font-size: 11px; margin-top: 4px; font-family: ui-monospace, monospace; overflow-wrap: anywhere; }
.cost-stack { height: 12px; display: flex; margin: 16px 0 8px; background: #1a2a3e; border-radius: 999px; overflow: hidden; }
.cost-stack span { min-width: 1px; }
.lens-note { color: var(--muted); font-size: 10px; margin-bottom: 9px; }
.cost-legend { display: grid; grid-template-columns: repeat(2,1fr); gap: 6px 12px; }
.cost-item { display: flex; justify-content: space-between; gap: 8px; color: var(--muted); font-size: 11px; }
.cost-item b { color: #dce8f6; font-family: ui-monospace, monospace; }
.provenance { margin-top: 15px; padding-top: 13px; border-top: 1px solid #243951; display: grid; gap: 6px; }
.prov-row { display: grid; grid-template-columns: 98px 1fr; gap: 9px; font-size: 10px; }
.prov-row span:first-child { color: var(--muted); }
.prov-row code { color: #c9d9ea; overflow-wrap: anywhere; }
.footer { color: #6f849d; font-size: 11px; margin: 16px 2px 0; display: flex; justify-content: space-between; gap: 20px; }
.empty { color: var(--danger); }
</style>
</head>
<body>
<main class="app">
  <div class="topline">
    <div>
      <div class="eyebrow">TraceLoom model fork · real profile</div>
      <h1>One ordered tree. Aggregate equivalent edges.</h1>
      <p class="subtitle">Concrete execution stays in one order. Selecting an edge role reveals the only population that may be compared without inventing correspondence.</p>
    </div>
    <div class="status" id="supportStatus"></div>
  </div>
  <div class="warning"><strong>Prototype boundary</strong><span>The database still stores HPO slots and member orders. This adapter hides both, recovers concrete edge order, and verifies that per-role rank is derivable before rendering.</span></div>
  <section class="summary-grid" id="summary"></section>
  <div class="workspace">
    <aside class="panel">
      <div class="panel-head">
        <div class="panel-title">Structural edge roles</div>
        <div class="panel-note">A role is contextual identity, not a label match. Click one to select its equivalent-edge population.</div>
      </div>
      <div class="role-list" id="roleList"></div>
    </aside>
    <section class="main">
      <div class="panel">
        <div class="panel-head controls">
          <div>
            <div class="panel-title">Concrete child-edge order</div>
            <div class="panel-note" id="streamNote"></div>
          </div>
          <div class="controls-left">
            <span class="control-label">Parent occurrence</span>
            <select id="parentSelect"></select>
          </div>
        </div>
        <div class="stream-wrap" id="stream"></div>
        <div class="legend-line"><span>Layout wraps at the verified repeated phrase boundary; data remains one linear order.</span><span id="edgeSelectionCaption"></span></div>
      </div>
      <div class="details-grid">
        <div class="panel distribution">
          <div class="distribution-head">
            <div>
              <div class="role-heading"><span class="dot"></span><strong id="distributionTitle"></strong></div>
              <div class="population-caption" id="populationCaption"></div>
            </div>
            <div class="segmented">
              <button class="segment active" data-population="parent">this parent</button>
              <button class="segment" data-population="all">all parents</button>
            </div>
          </div>
          <div class="stats" id="stats"></div>
          <div class="chart" id="chart"></div>
        </div>
        <div class="panel detail" id="detail"></div>
      </div>
    </section>
  </div>
  <div class="footer"><span id="adapterReceipt"></span><span>Source: __SOURCE__</span></div>
</main>
<script id="model" type="application/json">__PAYLOAD__</script>
<script>
const model = JSON.parse(document.getElementById('model').textContent);
const roleById = new Map(model.roles.map(role => [role.role_id, role]));
const state = { parentIndex: 0, roleId: model.default_role, edgeOrder: null, population: 'parent' };
const fmt = value => value == null ? '—' : Number(value).toLocaleString(undefined, {maximumFractionDigits: 3});
const esc = value => String(value ?? '').replace(/[&<>"']/g, ch => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[ch]));
const currentParent = () => model.parents[state.parentIndex];
const currentRole = () => roleById.get(state.roleId);
const normalizeSelection = () => {
  const parent = currentParent();
  const selected = parent.edges.find(edge => edge.concrete_order === state.edgeOrder && edge.role_id === state.roleId);
  if (!selected) {
    const firstEquivalent = parent.edges.find(edge => edge.role_id === state.roleId);
    state.edgeOrder = firstEquivalent ? firstEquivalent.concrete_order : null;
  }
};
const roleEdges = (scope) => {
  const parents = scope === 'all' ? model.parents : [currentParent()];
  return parents.flatMap(parent => parent.edges.filter(edge => edge.role_id === state.roleId));
};
const median = values => {
  if (!values.length) return null;
  const sorted = [...values].sort((a,b) => a-b);
  const middle = Math.floor((sorted.length - 1) / 2);
  return sorted[middle];
};

function renderSummary() {
  const p = model.position;
  document.getElementById('supportStatus').textContent = p.phrase_verified ? '✓ edge-rank derivation verified' : '△ irregular role phrase';
  document.getElementById('summary').innerHTML = `
    <div class="path-card"><div class="metric-label">Selected structural context</div><div class="path">${esc(p.rooted_position_path)}</div></div>
    <div class="metric"><div class="metric-label">Parent occurrences</div><div class="metric-value">${fmt(p.occurrence_count)}</div></div>
    <div class="metric"><div class="metric-label">Edge roles</div><div class="metric-value">${fmt(p.role_count)}</div></div>
    <div class="metric"><div class="metric-label">Edges / parent</div><div class="metric-value">${fmt(p.edges_per_parent)}</div></div>
    <div class="metric"><div class="metric-label">Comparable edges</div><div class="metric-value">${fmt(p.edge_population_count)}</div></div>`;
  document.getElementById('adapterReceipt').textContent = `${model.adapter.canonical_storage} → ordered-edge adapter · ${model.adapter.derived_rank_check}`;
}

function renderRoles() {
  document.getElementById('roleList').innerHTML = model.roles.map(role => `
    <button class="role-button ${role.role_id === state.roleId ? 'active' : ''}" data-role="${esc(role.role_id)}">
      <span class="role-swatch" style="background:${role.color}"></span>
      <span class="role-name"><strong>${esc(role.label)}</strong><span>role ${role.role_order} · ${esc(role.role_id)}</span></span>
      <span class="role-cost">median<br>${fmt(role.median_total_us)} µs</span>
    </button>`).join('');
  document.querySelectorAll('.role-button').forEach(button => button.addEventListener('click', () => {
    state.roleId = button.dataset.role;
    state.edgeOrder = null;
    renderAll();
  }));
}

function renderParentSelect() {
  const select = document.getElementById('parentSelect');
  select.innerHTML = model.parents.map((parent, index) => `<option value="${index}">#${parent.occurrence_idx}${parent.repeat_iteration ? ` · outer rank ${parent.repeat_iteration}` : ''}</option>`).join('');
  select.value = state.parentIndex;
  select.onchange = () => { state.parentIndex = Number(select.value); state.edgeOrder = null; renderAll(); };
}

function renderStream() {
  const parent = currentParent();
  const roleCount = model.roles.length;
  const phraseSize = model.position.phrase_verified ? roleCount : Math.min(roleCount, 16);
  const chunks = [];
  for (let i = 0; i < parent.edges.length; i += phraseSize) chunks.push(parent.edges.slice(i, i + phraseSize));
  const costs = parent.edges.map(edge => Number(edge.total_us || 0));
  const maxCost = Math.max(...costs, 0.001);
  document.getElementById('streamNote').textContent = `${parent.occurrence_id} · ${parent.edges.length} concrete edges · ordered by measured token placement`;
  document.getElementById('stream').innerHTML = chunks.map((chunk, chunkIndex) => `
    <div class="phrase-row">
      <div class="phrase-index">${model.position.phrase_verified ? `×${chunkIndex + 1}` : `${chunk[0].concrete_order}–${chunk.at(-1).concrete_order}`}</div>
      <div class="edge-row" style="grid-template-columns:repeat(${chunk.length},minmax(56px,1fr))">
        ${chunk.map(edge => {
          const role = roleById.get(edge.role_id);
          const equivalent = edge.role_id === state.roleId;
          const selected = state.edgeOrder === edge.concrete_order;
          const fill = Math.max(5, Math.min(100, Number(edge.total_us || 0) / maxCost * 100));
          return `<button class="edge ${equivalent ? 'equivalent' : 'dim'} ${selected ? 'selected' : ''}" data-edge="${edge.concrete_order}" style="--role-color:${role.color}" title="edge ${edge.concrete_order} · ${esc(role.label)} · equivalent rank ${edge.derived_role_rank} · ${fmt(edge.total_us)} µs">
            <span class="edge-fill" style="height:${fill}%"></span><span class="edge-label">${esc(role.label)}</span><span class="edge-cost">${fmt(edge.total_us)} µs</span></button>`;
        }).join('')}
      </div>
    </div>`).join('');
  document.querySelectorAll('.edge').forEach(button => button.addEventListener('click', () => {
    const edge = parent.edges.find(candidate => candidate.concrete_order === Number(button.dataset.edge));
    state.roleId = edge.role_id;
    state.edgeOrder = edge.concrete_order;
    renderAll();
  }));
  const selectedCount = parent.edges.filter(edge => edge.role_id === state.roleId).length;
  document.getElementById('edgeSelectionCaption').textContent = `${selectedCount} of ${parent.edges.length} edges are equivalent to the selected role`;
}

function renderDistribution() {
  const role = currentRole();
  const edges = roleEdges(state.population);
  const values = edges.map(edge => Number(edge.total_us)).filter(Number.isFinite);
  const min = values.length ? Math.min(...values) : null;
  const max = values.length ? Math.max(...values) : null;
  const med = median(values);
  document.documentElement.style.setProperty('--selected-color', role.color);
  document.getElementById('distributionTitle').textContent = role.label;
  document.getElementById('populationCaption').textContent = state.population === 'all'
    ? `${edges.length} equivalent edges across ${model.parents.length} parent occurrences · edge-level view`
    : `${edges.length} equivalent edges inside ${currentParent().occurrence_id} · derived ranks 1…${edges.length}`;
  document.getElementById('stats').innerHTML = `
    <div class="stat"><b>${fmt(min)} µs</b><span>min</span></div>
    <div class="stat"><b>${fmt(med)} µs</b><span>median</span></div>
    <div class="stat"><b>${fmt(max)} µs</b><span>max</span></div>`;
  document.querySelectorAll('.segment').forEach(button => button.classList.toggle('active', button.dataset.population === state.population));

  if (!values.length) { document.getElementById('chart').innerHTML = '<div class="empty">No cost values.</div>'; return; }
  const width = 900, height = 180, padX = 42, padY = 20;
  const low = Math.min(...values), high = Math.max(...values), range = Math.max(high - low, high * .02, .001);
  const pointWidth = (width - padX * 2) / Math.max(values.length - 1, 1);
  const circles = edges.map((edge, index) => {
    const value = Number(edge.total_us);
    const x = padX + index * pointWidth;
    const y = height - padY - ((value - low) / range) * (height - padY * 2);
    const selected = state.population === 'parent' && edge.concrete_order === state.edgeOrder;
    return `<circle cx="${x.toFixed(2)}" cy="${y.toFixed(2)}" r="${selected ? 5 : 2.5}" fill="${role.color}" opacity="${selected ? 1 : .72}"><title>parent ${edge.parent_occurrence_idx}, role rank ${edge.derived_role_rank}: ${fmt(value)} µs</title></circle>`;
  }).join('');
  const medianY = height - padY - ((med - low) / range) * (height - padY * 2);
  document.getElementById('chart').innerHTML = `<svg viewBox="0 0 ${width} ${height}" preserveAspectRatio="none">
    <line x1="${padX}" y1="${medianY}" x2="${width-padX}" y2="${medianY}" stroke="${role.color}" stroke-opacity=".28" stroke-dasharray="5 5"/>
    <text class="axis-label" x="7" y="${padY+4}">${fmt(high)}</text><text class="axis-label" x="7" y="${height-padY+4}">${fmt(low)}</text>
    ${circles}
  </svg>`;
}

function renderDetail() {
  const parent = currentParent();
  const role = currentRole();
  let edge = parent.edges.find(candidate => candidate.concrete_order === state.edgeOrder && candidate.role_id === state.roleId);
  if (!edge) edge = parent.edges.find(candidate => candidate.role_id === state.roleId);
  if (!edge) { document.getElementById('detail').innerHTML = '<div class="empty">Selected role is absent.</div>'; return; }
  state.edgeOrder = edge.concrete_order;
  const total = Math.max(Number(edge.total_us || 0), .001);
  const primaryComponents = [
    ['compute', Number(edge.compute_us || 0), '#74c0fc'],
    ['communication', Number(edge.comm_us || 0), '#ff8787'],
    ['uncovered', Number(edge.idle_us || 0), '#ffd43b'],
  ];
  const auxiliary = Number(edge.aux_us || 0);
  document.getElementById('detail').innerHTML = `
    <div class="detail-title">Concrete edge ${edge.concrete_order} · ${esc(role.label)}</div>
    <div class="detail-sub">${esc(edge.parent_occurrence_id)} → ${esc(edge.child_occurrence_id)}</div>
    <div class="cost-stack">${primaryComponents.map(([name,value,color]) => `<span title="${name}: ${fmt(value)} µs" style="width:${Math.max(0,value/total*100)}%;background:${color}"></span>`).join('')}</div>
    <div class="lens-note">Primary overlap-safe partition: compute + communication + uncovered = total.</div>
    <div class="cost-legend">${primaryComponents.map(([name,value,color]) => `<div class="cost-item"><span style="color:${color}">${name}</span><b>${fmt(value)} µs</b></div>`).join('')}<div class="cost-item"><span>total</span><b>${fmt(edge.total_us)} µs</b></div><div class="cost-item"><span style="color:#b197fc">auxiliary lens</span><b>${fmt(auxiliary)} µs</b></div></div>
    <div class="provenance">
      <div class="prov-row"><span>edge role</span><code>${esc(role.role_id)} · contextual class ${role.role_order}</code></div>
      <div class="prov-row"><span>derived rank</span><code>${edge.derived_role_rank} (stored member_order verified)</code></div>
      <div class="prov-row"><span>concrete order</span><code>${edge.concrete_order}</code></div>
      <div class="prov-row"><span>token extent</span><code>[${edge.token_start_ordinal}, ${edge.token_end_exclusive})</code></div>
      <div class="prov-row"><span>child occurrence</span><code>${esc(edge.child_occurrence_id)}</code></div>
    </div>`;
}

function renderAll() {
  normalizeSelection();
  renderRoles();
  renderParentSelect();
  renderStream();
  renderDistribution();
  renderDetail();
}
document.querySelectorAll('.segment').forEach(button => button.addEventListener('click', () => { state.population = button.dataset.population; renderDistribution(); }));
renderSummary();
renderAll();
</script>
</body>
</html>'''
    return template.replace("__PAYLOAD__", payload).replace("__SOURCE__", source_text)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("analysis_db", type=Path)
    parser.add_argument("output_html", type=Path)
    parser.add_argument("--position-id")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    database = args.analysis_db.resolve()
    if not database.is_file():
        raise SystemExit(f"analysis database does not exist: {database}")
    with open_readonly(database) as db:
        selected = choose_position(db, args.position_id)
        model = load_model(db, selected["position_id"])
    output = args.output_html.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(render_html(model, database), encoding="utf-8")
    position = model["position"]
    print(
        f"wrote {output}\n"
        f"  position: {position['position_id']} {position['label']}\n"
        f"  parents: {position['occurrence_count']}\n"
        f"  roles: {position['role_count']}\n"
        f"  concrete edges per parent: {position['edges_per_parent']}\n"
        f"  phrase verified: {position['phrase_verified']}"
    )


if __name__ == "__main__":
    main()
