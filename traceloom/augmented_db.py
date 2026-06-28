from __future__ import annotations

import json
import re
import shutil
import sqlite3
from pathlib import Path
from typing import Dict, List, Sequence, Tuple

from .collective_tag import ensure_collective_link_schema


Row = Dict[str, object]


def prepare_augmented_db(*, source_db: Path, out_dir: Path, db_idx: int) -> Path:
    """Create a sidecar copy of an msprof DB and initialize TraceLoom tables."""
    target = out_dir / f"db{db_idx:02d}.traceloom_augmented.db"
    if target.exists():
        target.unlink()
    shutil.copy2(source_db, target)
    with sqlite3.connect(str(target)) as conn:
        _initialize_schema(conn)
        _replace_metadata(
            conn,
            {
                "traceloom_schema_version": "augmented_db_v1",
                "source_db": str(source_db.resolve()),
                "db_idx": db_idx,
            },
        )
    return target


def append_device_analysis(
    *,
    augmented_db: Path,
    db_idx: int,
    device_id: int,
    global_rank: int,
    stem: str,
    view_name: str,
    step_rows: Sequence[Row],
    anchor_step_rows: Sequence[Row],
    aux_slot_rows: Sequence[Row],
    node_metric_rows: Sequence[Row],
    node_anchor_link_rows: Sequence[Row],
    loop_cost_rows: Sequence[Row],
    tree_payload: Row,
    cuda_graph_event_rows: Sequence[Row] = (),
    cuda_graph_envelope_rows: Sequence[Row] = (),
) -> None:
    with sqlite3.connect(str(augmented_db)) as conn:
        conn.execute("PRAGMA foreign_keys=OFF")
        _initialize_schema(conn)
        _replace_metadata(
            conn,
            {
                f"device_{device_id}_global_rank": global_rank,
                f"device_{device_id}_stem": stem,
                f"device_{device_id}_view_name": view_name,
            },
        )
        _delete_device_rows(conn, db_idx=db_idx, device_id=device_id, view_name=view_name)
        _insert_events(conn, db_idx=db_idx, device_id=device_id, rows=step_rows)
        _insert_anchors(conn, db_idx=db_idx, device_id=device_id, rows=anchor_step_rows)
        _insert_cuda_graph_replays(
            conn,
            db_idx=db_idx,
            device_id=device_id,
            rows=cuda_graph_event_rows,
        )
        _insert_cuda_graph_envelopes(
            conn,
            db_idx=db_idx,
            device_id=device_id,
            rows=cuda_graph_envelope_rows,
        )
        _insert_aux_links(
            conn,
            db_idx=db_idx,
            device_id=device_id,
            step_rows=step_rows,
            aux_slot_rows=aux_slot_rows,
        )
        _insert_nodes(
            conn,
            db_idx=db_idx,
            device_id=device_id,
            view_name=view_name,
            rows=node_metric_rows,
        )
        _validate_node_id_namespace(
            tree_payload=tree_payload,
            node_rows=node_metric_rows,
            node_anchor_rows=node_anchor_link_rows,
            loop_rows=loop_cost_rows,
        )
        _insert_edges(
            conn,
            db_idx=db_idx,
            device_id=device_id,
            view_name=view_name,
            tree_payload=tree_payload,
            node_rows=node_metric_rows,
        )
        _insert_node_anchor_links(
            conn,
            db_idx=db_idx,
            device_id=device_id,
            view_name=view_name,
            rows=node_anchor_link_rows,
            node_rows=node_metric_rows,
        )
        _insert_semantic_tree(
            conn,
            db_idx=db_idx,
            device_id=device_id,
            view_name=view_name,
            stem=stem,
            tree_payload=tree_payload,
            node_rows=node_metric_rows,
        )
        _insert_loop_nodes(
            conn,
            db_idx=db_idx,
            device_id=device_id,
            view_name=view_name,
            rows=loop_cost_rows,
        )
        _insert_anchor_primary_nodes(conn, db_idx=db_idx, device_id=device_id, view_name=view_name)
        conn.commit()


def _validate_node_id_namespace(
    *,
    tree_payload: Row,
    node_rows: Sequence[Row],
    node_anchor_rows: Sequence[Row],
    loop_rows: Sequence[Row],
) -> None:
    """Reject reports whose visible tree ids do not match augmented-DB keys."""

    node_ids = [str(row.get("node_id", "")).strip() for row in node_rows]
    node_ids = [node_id for node_id in node_ids if node_id]
    node_id_set = set(node_ids)
    if len(node_ids) != len(node_id_set):
        raise ValueError("duplicate node_id values in node metrics")

    tree_ids: List[str] = []

    def visit(node: object) -> None:
        if not isinstance(node, dict):
            return
        node_id = str(node.get("node_id", "")).strip()
        if node_id:
            tree_ids.append(node_id)
            if node_id not in node_id_set:
                raise ValueError(
                    f"tree payload node_id {node_id!r} is not present in node metrics; "
                    "visible node ids must use the augmented-DB local_node_id namespace"
                )
        node_type = str(node.get("type", ""))
        if node_type == "Seq":
            for item in node.get("items", []):
                if isinstance(item, dict):
                    visit(item.get("node"))
        elif node_type == "Repeat":
            visit(node.get("body"))
        overlays = node.get("overlays", [])
        if isinstance(overlays, list):
            for item in overlays:
                if isinstance(item, dict):
                    visit(item.get("node"))

    visit(tree_payload.get("root"))
    missing_tree_ids = sorted(node_id_set - set(tree_ids))
    if missing_tree_ids:
        preview = ", ".join(missing_tree_ids[:8])
        raise ValueError(f"node metrics contain ids missing from tree payload: {preview}")

    for source_name, rows in (("node-anchor links", node_anchor_rows), ("loop costs", loop_rows)):
        unknown = sorted(
            {
                node_id
                for row in rows
                for node_id in [str(row.get("node_id", "")).strip()]
                if node_id and node_id not in node_id_set
            }
        )
        if unknown:
            preview = ", ".join(unknown[:8])
            raise ValueError(f"{source_name} reference unknown local_node_id values: {preview}")


def _initialize_schema(conn: sqlite3.Connection) -> None:
    conn.executescript(
        """
        CREATE TABLE IF NOT EXISTS traceloom_metadata (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS traceloom_event (
            event_id TEXT PRIMARY KEY,
            db_idx INTEGER NOT NULL,
            device_id INTEGER NOT NULL,
            step_idx INTEGER NOT NULL,
            source_table TEXT NOT NULL,
            source_key TEXT NOT NULL,
            stream_id INTEGER,
            start_ns INTEGER,
            end_ns INTEGER,
            dur_us REAL,
            category TEXT,
            role TEXT,
            semantic_role TEXT,
            semantic_role_reason TEXT,
            symbol TEXT,
            label TEXT,
            raw_label TEXT,
            op_type TEXT,
            compute_task_type TEXT,
            family TEXT,
            task_type TEXT,
            raw_json TEXT
        );

        CREATE TABLE IF NOT EXISTS traceloom_event_source (
            event_id TEXT NOT NULL,
            source_ordinal INTEGER NOT NULL,
            db_idx INTEGER NOT NULL,
            device_id INTEGER NOT NULL,
            source_table TEXT NOT NULL,
            source_key TEXT NOT NULL,
            source_role TEXT,
            raw_json TEXT,
            PRIMARY KEY(event_id, source_ordinal)
        );

        CREATE TABLE IF NOT EXISTS traceloom_anchor (
            anchor_id TEXT PRIMARY KEY,
            db_idx INTEGER NOT NULL,
            device_id INTEGER NOT NULL,
            anchor_idx INTEGER NOT NULL,
            event_id TEXT NOT NULL,
            step_idx INTEGER NOT NULL,
            symbol TEXT,
            role TEXT,
            label TEXT,
            family TEXT,
            start_ns INTEGER,
            end_ns INTEGER,
            dur_us REAL,
            UNIQUE(db_idx, device_id, anchor_idx)
        );

        CREATE TABLE IF NOT EXISTS traceloom_anchor_aux_slot (
            anchor_id TEXT PRIMARY KEY,
            db_idx INTEGER NOT NULL,
            device_id INTEGER NOT NULL,
            anchor_idx INTEGER NOT NULL,
            anchor_step_idx INTEGER NOT NULL,
            aux_start_step_idx INTEGER,
            aux_end_step_idx INTEGER,
            aux_event_count INTEGER,
            aux_dur_us REAL,
            raw_json TEXT
        );

        CREATE TABLE IF NOT EXISTS traceloom_aux_link (
            anchor_id TEXT NOT NULL,
            aux_event_id TEXT NOT NULL,
            db_idx INTEGER NOT NULL,
            device_id INTEGER NOT NULL,
            aux_order INTEGER NOT NULL,
            aux_step_idx INTEGER NOT NULL,
            link_type TEXT NOT NULL,
            reason TEXT,
            aux_kind TEXT,
            aux_dur_us REAL,
            raw_json TEXT,
            PRIMARY KEY(anchor_id, aux_event_id)
        );

        CREATE TABLE IF NOT EXISTS traceloom_cuda_graph_replay (
            graph_event_id TEXT PRIMARY KEY,
            db_idx INTEGER NOT NULL,
            device_id INTEGER NOT NULL,
            graph_provider TEXT DEFAULT 'cuda',
            graph_kind TEXT DEFAULT 'cuda_graph_replay',
            graph_event_idx INTEGER NOT NULL,
            event_id TEXT NOT NULL,
            step_idx INTEGER NOT NULL,
            stream_id INTEGER,
            correlation_id TEXT,
            graph_id TEXT,
            graph_exec_id TEXT,
            context_id TEXT,
            start_ns INTEGER,
            end_ns INTEGER,
            dur_us REAL,
            enclosed_event_count INTEGER,
            enclosed_event_us REAL,
            enclosed_kernel_count INTEGER,
            enclosed_kernel_us REAL,
            raw_json TEXT,
            UNIQUE(db_idx, device_id, step_idx)
        );

        CREATE TABLE IF NOT EXISTS traceloom_cuda_graph_envelope (
            envelope_id TEXT PRIMARY KEY,
            db_idx INTEGER NOT NULL,
            device_id INTEGER NOT NULL,
            graph_provider TEXT DEFAULT 'cuda',
            graph_kind TEXT DEFAULT 'cuda_graph_replay',
            envelope_idx INTEGER NOT NULL,
            graph_event_id TEXT NOT NULL,
            child_event_id TEXT NOT NULL,
            graph_step_idx INTEGER NOT NULL,
            child_step_idx INTEGER NOT NULL,
            relation TEXT NOT NULL,
            stream_relation TEXT,
            graph_id TEXT,
            graph_exec_id TEXT,
            graph_correlation_id TEXT,
            graph_start_ns INTEGER,
            graph_end_ns INTEGER,
            child_start_ns INTEGER,
            child_end_ns INTEGER,
            start_offset_us REAL,
            end_offset_us REAL,
            child_dur_us REAL,
            raw_json TEXT,
            UNIQUE(db_idx, device_id, graph_step_idx, child_step_idx)
        );

        CREATE TABLE IF NOT EXISTS traceloom_viz_node (
            node_id TEXT PRIMARY KEY,
            db_idx INTEGER NOT NULL,
            device_id INTEGER NOT NULL,
            view_name TEXT NOT NULL,
            local_node_id TEXT NOT NULL,
            path TEXT,
            node_type TEXT,
            kind TEXT,
            symbol TEXT,
            label TEXT,
            category TEXT,
            depth INTEGER,
            level INTEGER,
            repeat_label TEXT,
            repeat_count INTEGER,
            occurrence_count INTEGER,
            anchor_count INTEGER,
            anchors_per_occurrence REAL,
            first_anchor_idx INTEGER,
            last_anchor_idx INTEGER,
            compute_us REAL,
            comm_us REAL,
            idle_us REAL,
            total_us REAL,
            avg_compute_us REAL,
            avg_comm_us REAL,
            avg_idle_us REAL,
            avg_total_us REAL,
            self_us REAL,
            aux_events REAL,
            aux_us REAL,
            raw_json TEXT,
            UNIQUE(db_idx, device_id, view_name, local_node_id)
        );

        CREATE TABLE IF NOT EXISTS traceloom_viz_edge (
            parent_node_id TEXT NOT NULL,
            child_node_id TEXT NOT NULL,
            db_idx INTEGER NOT NULL,
            device_id INTEGER NOT NULL,
            view_name TEXT NOT NULL,
            edge_order INTEGER NOT NULL,
            edge_kind TEXT,
            raw_json TEXT,
            PRIMARY KEY(parent_node_id, child_node_id, edge_order)
        );

        CREATE TABLE IF NOT EXISTS traceloom_viz_node_anchor (
            node_id TEXT NOT NULL,
            anchor_id TEXT NOT NULL,
            db_idx INTEGER NOT NULL,
            device_id INTEGER NOT NULL,
            view_name TEXT NOT NULL,
            occurrence_idx INTEGER NOT NULL,
            anchor_order INTEGER NOT NULL,
            coverage_kind TEXT NOT NULL,
            repeat_context TEXT,
            PRIMARY KEY(node_id, anchor_id, occurrence_idx)
        );

        CREATE TABLE IF NOT EXISTS traceloom_anchor_primary_node (
            anchor_id TEXT PRIMARY KEY,
            node_id TEXT NOT NULL,
            db_idx INTEGER NOT NULL,
            device_id INTEGER NOT NULL,
            view_name TEXT NOT NULL,
            reason TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS traceloom_loop_node (
            node_id TEXT PRIMARY KEY,
            db_idx INTEGER NOT NULL,
            device_id INTEGER NOT NULL,
            view_name TEXT NOT NULL,
            loop_rank INTEGER,
            repeat_label TEXT,
            repeat_count INTEGER,
            occurrence_count INTEGER,
            anchor_count INTEGER,
            total_us REAL,
            avg_total_us REAL,
            compute_us REAL,
            comm_us REAL,
            idle_us REAL,
            loop_total_pct REAL,
            raw_json TEXT
        );

        CREATE TABLE IF NOT EXISTS traceloom_semantic_tree (
            tree_id TEXT PRIMARY KEY,
            db_idx INTEGER NOT NULL,
            device_id INTEGER NOT NULL,
            view_name TEXT NOT NULL,
            tree_kind TEXT NOT NULL,
            stem TEXT,
            root_node_id TEXT,
            schema_version TEXT,
            semantic_projection TEXT,
            macro_discovery TEXT,
            readable_macro_mode TEXT,
            auxiliary_attribution TEXT,
            raw_json TEXT,
            UNIQUE(db_idx, device_id, view_name, tree_kind)
        );

        CREATE TABLE IF NOT EXISTS traceloom_semantic_node (
            node_id TEXT PRIMARY KEY,
            tree_id TEXT NOT NULL,
            db_idx INTEGER NOT NULL,
            device_id INTEGER NOT NULL,
            view_name TEXT NOT NULL,
            tree_kind TEXT NOT NULL,
            local_node_id TEXT NOT NULL,
            parent_node_id TEXT,
            parent_local_node_id TEXT,
            preorder_idx INTEGER NOT NULL,
            sibling_order INTEGER NOT NULL,
            path TEXT,
            depth INTEGER,
            display_depth INTEGER,
            loop_depth INTEGER,
            node_type TEXT,
            semantic_kind TEXT,
            symbol TEXT,
            label TEXT,
            category TEXT,
            repeat_count INTEGER,
            occurrence_count INTEGER,
            anchor_count INTEGER,
            first_anchor_idx INTEGER,
            last_anchor_idx INTEGER,
            start_ns INTEGER,
            end_ns INTEGER,
            compute_us REAL,
            comm_us REAL,
            idle_us REAL,
            total_us REAL,
            avg_compute_us REAL,
            avg_comm_us REAL,
            avg_idle_us REAL,
            avg_total_us REAL,
            self_us REAL,
            aux_event_count REAL,
            aux_us REAL,
            hidden_aux_event_count REAL,
            hidden_aux_us REAL,
            raw_json TEXT,
            UNIQUE(db_idx, device_id, view_name, tree_kind, local_node_id)
        );

        CREATE TABLE IF NOT EXISTS traceloom_semantic_edge (
            parent_node_id TEXT NOT NULL,
            child_node_id TEXT NOT NULL,
            tree_id TEXT NOT NULL,
            db_idx INTEGER NOT NULL,
            device_id INTEGER NOT NULL,
            view_name TEXT NOT NULL,
            tree_kind TEXT NOT NULL,
            edge_order INTEGER NOT NULL,
            edge_kind TEXT,
            raw_json TEXT,
            PRIMARY KEY(parent_node_id, child_node_id, tree_id, edge_order)
        );

        CREATE INDEX IF NOT EXISTS idx_traceloom_event_device_step
            ON traceloom_event(db_idx, device_id, step_idx);
        CREATE INDEX IF NOT EXISTS idx_traceloom_event_source_lookup
            ON traceloom_event_source(source_table, source_key);
        CREATE INDEX IF NOT EXISTS idx_traceloom_anchor_device_idx
            ON traceloom_anchor(db_idx, device_id, anchor_idx);
        CREATE INDEX IF NOT EXISTS idx_traceloom_aux_anchor
            ON traceloom_aux_link(anchor_id);
        CREATE INDEX IF NOT EXISTS idx_traceloom_cuda_graph_replay_exec
            ON traceloom_cuda_graph_replay(db_idx, device_id, graph_exec_id);
        CREATE INDEX IF NOT EXISTS idx_traceloom_cuda_graph_envelope_graph
            ON traceloom_cuda_graph_envelope(graph_event_id);
        CREATE INDEX IF NOT EXISTS idx_traceloom_cuda_graph_envelope_child
            ON traceloom_cuda_graph_envelope(child_event_id);
        CREATE INDEX IF NOT EXISTS idx_traceloom_node_anchor_node
            ON traceloom_viz_node_anchor(node_id);
        CREATE INDEX IF NOT EXISTS idx_traceloom_node_anchor_anchor
            ON traceloom_viz_node_anchor(anchor_id);
        CREATE INDEX IF NOT EXISTS idx_traceloom_semantic_node_tree_order
            ON traceloom_semantic_node(tree_id, preorder_idx);
        CREATE INDEX IF NOT EXISTS idx_traceloom_semantic_node_parent
            ON traceloom_semantic_node(parent_node_id);
        CREATE INDEX IF NOT EXISTS idx_traceloom_semantic_edge_tree
            ON traceloom_semantic_edge(tree_id, edge_order);

        CREATE VIEW IF NOT EXISTS traceloom_v_node_anchor_cost AS
            SELECT
                na.node_id,
                na.anchor_id,
                na.occurrence_idx,
                na.anchor_order,
                e.dur_us AS anchor_dur_us,
                e.role AS anchor_role,
                e.symbol AS anchor_symbol,
                e.label AS anchor_label
            FROM traceloom_viz_node_anchor na
            JOIN traceloom_anchor a ON a.anchor_id = na.anchor_id
            JOIN traceloom_event e ON e.event_id = a.event_id;

        CREATE VIEW IF NOT EXISTS traceloom_v_node_aux_cost AS
            SELECT
                na.node_id,
                al.anchor_id,
                al.aux_event_id,
                al.aux_order,
                e.dur_us AS aux_dur_us,
                e.role AS aux_role,
                e.symbol AS aux_symbol,
                e.label AS aux_label
            FROM traceloom_viz_node_anchor na
            JOIN traceloom_aux_link al ON al.anchor_id = na.anchor_id
            JOIN traceloom_event e ON e.event_id = al.aux_event_id;

        CREATE VIEW IF NOT EXISTS traceloom_v_cuda_graph_replay AS
            SELECT
                g.*,
                e.symbol,
                e.label,
                e.task_type,
                e.semantic_role,
                e.semantic_role_reason,
                a.anchor_idx
            FROM traceloom_cuda_graph_replay g
            JOIN traceloom_event e ON e.event_id = g.event_id
            LEFT JOIN traceloom_anchor a ON a.event_id = e.event_id;

        CREATE VIEW IF NOT EXISTS traceloom_v_cuda_graph_envelope AS
            SELECT
                ge.*,
                graph_anchor.anchor_idx AS graph_anchor_idx,
                graph.label AS graph_label,
                graph.stream_id AS graph_stream_id,
                child.label AS child_label,
                child.task_type AS child_task_type,
                child.source_table AS child_source_table,
                child.stream_id AS child_stream_id,
                child.symbol AS child_symbol,
                child.semantic_role AS child_semantic_role
            FROM traceloom_cuda_graph_envelope ge
            JOIN traceloom_event graph ON graph.event_id = ge.graph_event_id
            LEFT JOIN traceloom_anchor graph_anchor ON graph_anchor.event_id = graph.event_id
            JOIN traceloom_event child ON child.event_id = ge.child_event_id;

        CREATE VIEW IF NOT EXISTS traceloom_v_node_cost AS
            SELECT
                n.*,
                COALESCE(anchor_cost.anchor_dur_us, 0.0) AS sql_anchor_us,
                COALESCE(aux_cost.aux_dur_us, 0.0) AS sql_aux_us
            FROM traceloom_viz_node n
            LEFT JOIN (
                SELECT node_id, SUM(anchor_dur_us) AS anchor_dur_us
                FROM traceloom_v_node_anchor_cost
                GROUP BY node_id
            ) anchor_cost ON anchor_cost.node_id = n.node_id
            LEFT JOIN (
                SELECT node_id, SUM(aux_dur_us) AS aux_dur_us
                FROM traceloom_v_node_aux_cost
                GROUP BY node_id
            ) aux_cost ON aux_cost.node_id = n.node_id;

        CREATE VIEW IF NOT EXISTS traceloom_v_node_children AS
            SELECT
                e.parent_node_id,
                e.child_node_id,
                e.edge_order,
                child.*
            FROM traceloom_viz_edge e
            JOIN traceloom_viz_node child ON child.node_id = e.child_node_id;

        CREATE VIEW IF NOT EXISTS traceloom_tree_node_anchor AS
            SELECT
                na.node_id,
                n.local_node_id,
                na.anchor_id,
                na.db_idx,
                na.device_id,
                na.view_name,
                na.occurrence_idx,
                na.anchor_order,
                na.coverage_kind,
                na.repeat_context
            FROM traceloom_viz_node_anchor na
            JOIN traceloom_viz_node n ON n.node_id = na.node_id;

        CREATE VIEW IF NOT EXISTS traceloom_tree_node_occurrence AS
            WITH anchor_span AS (
                SELECT
                    na.node_id,
                    na.db_idx,
                    na.device_id,
                    na.view_name,
                    na.occurrence_idx,
                    MIN(a.anchor_idx) AS anchor_start_idx,
                    MAX(a.anchor_idx) AS anchor_end_idx,
                    COUNT(*) AS anchor_count,
                    MIN(a.start_ns) AS start_ns,
                    MAX(a.end_ns) AS end_ns,
                    SUM(CASE WHEN e.role = 'compute' THEN e.dur_us ELSE 0.0 END) AS compute_us,
                    SUM(CASE WHEN e.role = 'collective' THEN e.dur_us ELSE 0.0 END) AS comm_us,
                    SUM(e.dur_us) AS anchor_us,
                    MIN(na.repeat_context) AS repeat_context
                FROM traceloom_viz_node_anchor na
                JOIN traceloom_anchor a ON a.anchor_id = na.anchor_id
                JOIN traceloom_event e ON e.event_id = a.event_id
                GROUP BY na.node_id, na.db_idx, na.device_id, na.view_name, na.occurrence_idx
            ),
            aux_span AS (
                SELECT
                    na.node_id,
                    na.db_idx,
                    na.device_id,
                    na.view_name,
                    na.occurrence_idx,
                    COUNT(al.aux_event_id) AS aux_events,
                    SUM(COALESCE(aux.dur_us, 0.0)) AS aux_us
                FROM traceloom_viz_node_anchor na
                JOIN traceloom_aux_link al ON al.anchor_id = na.anchor_id
                JOIN traceloom_event aux ON aux.event_id = al.aux_event_id
                GROUP BY na.node_id, na.db_idx, na.device_id, na.view_name, na.occurrence_idx
            )
            SELECT
                a.node_id,
                n.local_node_id,
                a.db_idx,
                a.device_id,
                a.view_name,
                a.occurrence_idx,
                a.repeat_context,
                a.anchor_start_idx,
                a.anchor_end_idx,
                a.anchor_count,
                a.start_ns,
                a.end_ns,
                ROUND(COALESCE(a.compute_us, 0.0), 3) AS compute_us,
                ROUND(COALESCE(a.comm_us, 0.0), 3) AS comm_us,
                ROUND(COALESCE(n.idle_us, 0.0) / CASE WHEN COALESCE(n.occurrence_count, 0) = 0 THEN 1 ELSE n.occurrence_count END, 3) AS idle_us,
                ROUND(
                    COALESCE(a.compute_us, 0.0)
                    + COALESCE(a.comm_us, 0.0)
                    + COALESCE(n.idle_us, 0.0) / CASE WHEN COALESCE(n.occurrence_count, 0) = 0 THEN 1 ELSE n.occurrence_count END,
                    3
                ) AS total_us,
                COALESCE(aux.aux_events, 0) AS aux_events,
                ROUND(COALESCE(aux.aux_us, 0.0), 3) AS aux_us
            FROM anchor_span a
            JOIN traceloom_viz_node n ON n.node_id = a.node_id
            LEFT JOIN aux_span aux
                ON aux.node_id = a.node_id
               AND aux.occurrence_idx = a.occurrence_idx;

        CREATE VIEW IF NOT EXISTS traceloom_v_tree_node AS
            WITH RECURSIVE tree AS (
                SELECT
                    n.node_id,
                    CAST(NULL AS TEXT) AS parent_node_id,
                    n.db_idx,
                    n.device_id,
                    n.view_name,
                    n.local_node_id,
                    CAST(SUBSTR(n.local_node_id, 2) AS INTEGER) AS display_order,
                    n.path,
                    n.depth AS tree_depth,
                    n.level AS depth,
                    CASE WHEN n.kind = 'repeat' THEN 1 ELSE 0 END AS loop_depth,
                    n.node_type,
                    n.kind,
                    n.symbol,
                    n.label,
                    n.category,
                    n.repeat_label,
                    n.repeat_count,
                    n.occurrence_count,
                    n.anchor_count,
                    n.anchors_per_occurrence,
                    n.anchors_per_occurrence AS avg_anchor,
                    n.first_anchor_idx,
                    n.last_anchor_idx,
                    n.compute_us,
                    n.comm_us,
                    n.idle_us,
                    n.total_us,
                    n.avg_compute_us,
                    n.avg_comm_us,
                    n.avg_idle_us,
                    n.avg_total_us,
                    n.self_us,
                    ROUND(COALESCE(n.self_us, 0.0) / CASE WHEN COALESCE(n.occurrence_count, 0) = 0 THEN 1 ELSE n.occurrence_count END, 3) AS avg_self_us,
                    n.aux_events,
                    n.aux_us,
                    ROUND(COALESCE(n.aux_us, 0.0) / CASE WHEN COALESCE(n.occurrence_count, 0) = 0 THEN 1 ELSE n.occurrence_count END, 3) AS avg_aux_us,
                    ROUND(CASE WHEN COALESCE(n.total_us, 0.0) = 0.0 THEN 0.0 ELSE COALESCE(n.comm_us, 0.0) / n.total_us END, 6) AS comm_pct,
                    ROUND(CASE WHEN COALESCE(n.total_us, 0.0) = 0.0 THEN 0.0 ELSE COALESCE(n.idle_us, 0.0) / n.total_us END, 6) AS idle_pct
                FROM traceloom_viz_node n
                WHERE NOT EXISTS (
                    SELECT 1
                    FROM traceloom_viz_edge e
                    WHERE e.child_node_id = n.node_id
                )
                UNION ALL
                SELECT
                    child.node_id,
                    e.parent_node_id,
                    child.db_idx,
                    child.device_id,
                    child.view_name,
                    child.local_node_id,
                    CAST(SUBSTR(child.local_node_id, 2) AS INTEGER) AS display_order,
                    child.path,
                    child.depth AS tree_depth,
                    child.level AS depth,
                    tree.loop_depth + CASE WHEN child.kind = 'repeat' THEN 1 ELSE 0 END AS loop_depth,
                    child.node_type,
                    child.kind,
                    child.symbol,
                    child.label,
                    child.category,
                    child.repeat_label,
                    child.repeat_count,
                    child.occurrence_count,
                    child.anchor_count,
                    child.anchors_per_occurrence,
                    child.anchors_per_occurrence AS avg_anchor,
                    child.first_anchor_idx,
                    child.last_anchor_idx,
                    child.compute_us,
                    child.comm_us,
                    child.idle_us,
                    child.total_us,
                    child.avg_compute_us,
                    child.avg_comm_us,
                    child.avg_idle_us,
                    child.avg_total_us,
                    child.self_us,
                    ROUND(COALESCE(child.self_us, 0.0) / CASE WHEN COALESCE(child.occurrence_count, 0) = 0 THEN 1 ELSE child.occurrence_count END, 3) AS avg_self_us,
                    child.aux_events,
                    child.aux_us,
                    ROUND(COALESCE(child.aux_us, 0.0) / CASE WHEN COALESCE(child.occurrence_count, 0) = 0 THEN 1 ELSE child.occurrence_count END, 3) AS avg_aux_us,
                    ROUND(CASE WHEN COALESCE(child.total_us, 0.0) = 0.0 THEN 0.0 ELSE COALESCE(child.comm_us, 0.0) / child.total_us END, 6) AS comm_pct,
                    ROUND(CASE WHEN COALESCE(child.total_us, 0.0) = 0.0 THEN 0.0 ELSE COALESCE(child.idle_us, 0.0) / child.total_us END, 6) AS idle_pct
                FROM tree
                JOIN traceloom_viz_edge e ON e.parent_node_id = tree.node_id
                JOIN traceloom_viz_node child ON child.node_id = e.child_node_id
            )
            SELECT * FROM tree;

        CREATE VIEW IF NOT EXISTS traceloom_v_semantic_tree_node AS
            SELECT
                n.*,
                parent.local_node_id AS parent_local_id,
                parent.label AS parent_label,
                CASE
                    WHEN COALESCE(n.total_us, 0.0) = 0.0 THEN 0.0
                    ELSE ROUND(COALESCE(n.comm_us, 0.0) / n.total_us, 6)
                END AS comm_pct,
                CASE
                    WHEN COALESCE(n.total_us, 0.0) = 0.0 THEN 0.0
                    ELSE ROUND(COALESCE(n.idle_us, 0.0) / n.total_us, 6)
                END AS idle_pct
            FROM traceloom_semantic_node n
            LEFT JOIN traceloom_semantic_node parent ON parent.node_id = n.parent_node_id;

        CREATE VIEW IF NOT EXISTS traceloom_v_semantic_tree_readable AS
            SELECT
                n.tree_id,
                n.db_idx,
                n.device_id,
                n.view_name,
                n.tree_kind,
                n.preorder_idx,
                n.local_node_id,
                n.parent_local_node_id,
                n.path,
                n.display_depth,
                printf('%*s', COALESCE(n.display_depth, 0) * 2, '') ||
                '- [' || COALESCE(NULLIF(n.path, ''), 'root') || '] ' ||
                n.local_node_id || ' ' ||
                CASE
                    WHEN n.node_type = 'Repeat' THEN
                        'Repeat x' || COALESCE(n.repeat_count, 1)
                    WHEN n.node_type = 'Seq' THEN
                        'Seq'
                    ELSE
                        COALESCE(NULLIF(n.node_type, ''), 'Node')
                END ||
                ' | ' || COALESCE(NULLIF(n.label, ''), NULLIF(n.symbol, ''), n.semantic_kind, '') ||
                ' | anchors=' || COALESCE(n.anchor_count, 0) ||
                ' total_us=' || printf('%.3f', COALESCE(n.total_us, 0.0)) ||
                CASE
                    WHEN COALESCE(n.hidden_aux_event_count, 0.0) > 0.0 THEN
                        ' hidden_aux=' || printf('%.0f', n.hidden_aux_event_count) ||
                        ' hidden_aux_us=' || printf('%.3f', COALESCE(n.hidden_aux_us, 0.0))
                    ELSE ''
                END AS line
            FROM traceloom_semantic_node n;
        """
    )
    ensure_collective_link_schema(conn)


def _replace_metadata(conn: sqlite3.Connection, values: Dict[str, object]) -> None:
    conn.executemany(
        "INSERT OR REPLACE INTO traceloom_metadata(key, value) VALUES (?, ?)",
        [(str(k), _json_value(v)) for k, v in values.items()],
    )


def _delete_device_rows(conn: sqlite3.Connection, *, db_idx: int, device_id: int, view_name: str) -> None:
    params = {"db_idx": db_idx, "device_id": device_id, "view_name": view_name}
    for table in (
        "traceloom_semantic_edge",
        "traceloom_semantic_node",
        "traceloom_semantic_tree",
        "traceloom_anchor_primary_node",
        "traceloom_viz_node_anchor",
        "traceloom_viz_edge",
        "traceloom_loop_node",
        "traceloom_viz_node",
    ):
        conn.execute(
            f"DELETE FROM {table} WHERE db_idx = :db_idx AND device_id = :device_id AND view_name = :view_name",
            params,
        )
    for table in (
        "traceloom_cuda_graph_envelope",
        "traceloom_cuda_graph_replay",
        "traceloom_aux_link",
        "traceloom_anchor_aux_slot",
        "traceloom_anchor",
        "traceloom_event_source",
        "traceloom_event",
    ):
        conn.execute(
            f"DELETE FROM {table} WHERE db_idx = :db_idx AND device_id = :device_id",
            {"db_idx": db_idx, "device_id": device_id},
        )


def _insert_events(conn: sqlite3.Connection, *, db_idx: int, device_id: int, rows: Sequence[Row]) -> None:
    values = []
    source_values = []
    for row in rows:
        step_idx = _as_int(row.get("step_idx"))
        role = str(row.get("role", ""))
        event_id = _event_id(db_idx, device_id, step_idx)
        source_table = _source_table(row)
        source_key = _source_key(row, step_idx)
        values.append(
            (
                event_id,
                db_idx,
                device_id,
                step_idx,
                source_table,
                source_key,
                _nullable_int(row.get("stream_id")),
                _nullable_int(row.get("start_ns")),
                _nullable_int(row.get("end_ns")),
                _nullable_float(row.get("dur_us")),
                _event_category(row, role),
                role,
                str(row.get("semantic_role", "")),
                str(row.get("semantic_role_reason", "")),
                str(row.get("symbol", "")),
                str(row.get("label", "")),
                str(row.get("raw_label", "")),
                str(row.get("op_type", "")),
                str(row.get("compute_task_type", "")),
                str(row.get("family", "")),
                str(row.get("task_type", "")),
                _json_value(row),
            )
        )
        source_values.append(
            (
                event_id,
                0,
                db_idx,
                device_id,
                source_table,
                source_key,
                role,
                _json_value(row),
            )
        )
    conn.executemany(
        """
        INSERT OR REPLACE INTO traceloom_event(
            event_id, db_idx, device_id, step_idx, source_table, source_key,
            stream_id, start_ns, end_ns, dur_us, category, role, semantic_role,
            semantic_role_reason, symbol, label, raw_label, op_type,
            compute_task_type, family, task_type, raw_json
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        values,
    )
    conn.executemany(
        """
        INSERT OR REPLACE INTO traceloom_event_source(
            event_id, source_ordinal, db_idx, device_id, source_table,
            source_key, source_role, raw_json
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        """,
        source_values,
    )


def _insert_anchors(conn: sqlite3.Connection, *, db_idx: int, device_id: int, rows: Sequence[Row]) -> None:
    values = []
    for anchor_idx, row in enumerate(rows, start=1):
        step_idx = _as_int(row.get("step_idx"))
        values.append(
            (
                _anchor_id(db_idx, device_id, anchor_idx),
                db_idx,
                device_id,
                anchor_idx,
                _event_id(db_idx, device_id, step_idx),
                step_idx,
                str(row.get("symbol", "")),
                str(row.get("role", "")),
                str(row.get("label", "")),
                str(row.get("family", "")),
                _nullable_int(row.get("start_ns")),
                _nullable_int(row.get("end_ns")),
                _nullable_float(row.get("dur_us")),
            )
        )
    conn.executemany(
        """
        INSERT OR REPLACE INTO traceloom_anchor(
            anchor_id, db_idx, device_id, anchor_idx, event_id, step_idx,
            symbol, role, label, family, start_ns, end_ns, dur_us
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        values,
    )


def _insert_cuda_graph_replays(
    conn: sqlite3.Connection,
    *,
    db_idx: int,
    device_id: int,
    rows: Sequence[Row],
) -> None:
    values = []
    for row in rows:
        step_idx = _as_int(row.get("step_idx"))
        event_id = _event_id(db_idx, device_id, step_idx)
        values.append(
            (
                event_id,
                db_idx,
                device_id,
                str(row.get("graph_provider", "cuda") or "cuda"),
                str(row.get("graph_kind", "cuda_graph_replay") or "cuda_graph_replay"),
                _as_int(row.get("graph_event_idx")),
                event_id,
                step_idx,
                _nullable_int(row.get("stream_id")),
                str(row.get("correlation_id", "")),
                str(row.get("graph_id", "")),
                str(row.get("graph_exec_id", "")),
                str(row.get("context_id", "")),
                _nullable_int(row.get("start_ns")),
                _nullable_int(row.get("end_ns")),
                _nullable_float(row.get("dur_us")),
                _nullable_int(row.get("enclosed_event_count")),
                _nullable_float(row.get("enclosed_event_us")),
                _nullable_int(row.get("enclosed_kernel_count")),
                _nullable_float(row.get("enclosed_kernel_us")),
                _json_value(row),
            )
        )
    conn.executemany(
        """
        INSERT OR REPLACE INTO traceloom_cuda_graph_replay(
            graph_event_id, db_idx, device_id, graph_provider, graph_kind,
            graph_event_idx, event_id, step_idx, stream_id, correlation_id, graph_id, graph_exec_id,
            context_id, start_ns, end_ns, dur_us, enclosed_event_count,
            enclosed_event_us, enclosed_kernel_count, enclosed_kernel_us,
            raw_json
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        values,
    )


def _insert_cuda_graph_envelopes(
    conn: sqlite3.Connection,
    *,
    db_idx: int,
    device_id: int,
    rows: Sequence[Row],
) -> None:
    values = []
    for row in rows:
        graph_step_idx = _as_int(row.get("graph_step_idx"))
        child_step_idx = _as_int(row.get("child_step_idx"))
        envelope_idx = _as_int(row.get("envelope_idx"))
        values.append(
            (
                f"db{db_idx:02d}:dev{device_id}:cuda_graph_envelope{envelope_idx}",
                db_idx,
                device_id,
                str(row.get("graph_provider", "cuda") or "cuda"),
                str(row.get("graph_kind", "cuda_graph_replay") or "cuda_graph_replay"),
                envelope_idx,
                _event_id(db_idx, device_id, graph_step_idx),
                _event_id(db_idx, device_id, child_step_idx),
                graph_step_idx,
                child_step_idx,
                str(row.get("relation", "")),
                str(row.get("stream_relation", "")),
                str(row.get("graph_id", "")),
                str(row.get("graph_exec_id", "")),
                str(row.get("graph_correlation_id", "")),
                _nullable_int(row.get("graph_start_ns")),
                _nullable_int(row.get("graph_end_ns")),
                _nullable_int(row.get("child_start_ns")),
                _nullable_int(row.get("child_end_ns")),
                _nullable_float(row.get("start_offset_us")),
                _nullable_float(row.get("end_offset_us")),
                _nullable_float(row.get("child_dur_us")),
                _json_value(row),
            )
        )
    conn.executemany(
        """
        INSERT OR REPLACE INTO traceloom_cuda_graph_envelope(
            envelope_id, db_idx, device_id, graph_provider, graph_kind,
            envelope_idx, graph_event_id, child_event_id, graph_step_idx, child_step_idx, relation,
            stream_relation, graph_id, graph_exec_id, graph_correlation_id,
            graph_start_ns, graph_end_ns, child_start_ns, child_end_ns,
            start_offset_us, end_offset_us, child_dur_us, raw_json
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        values,
    )


def _insert_aux_links(
    conn: sqlite3.Connection,
    *,
    db_idx: int,
    device_id: int,
    step_rows: Sequence[Row],
    aux_slot_rows: Sequence[Row],
) -> None:
    step_by_idx = {_as_int(row.get("step_idx")): row for row in step_rows}
    slot_values = []
    link_values = []
    for slot in aux_slot_rows:
        anchor_idx = _as_int(slot.get("anchor_idx"))
        anchor_id = _anchor_id(db_idx, device_id, anchor_idx)
        start = _nullable_int(slot.get("aux_start_step_idx"))
        end = _nullable_int(slot.get("aux_end_step_idx"))
        slot_values.append(
            (
                anchor_id,
                db_idx,
                device_id,
                anchor_idx,
                _as_int(slot.get("step_idx")),
                start,
                end,
                _nullable_int(slot.get("aux_event_count")),
                _nullable_float(slot.get("aux_dur_us")),
                _json_value(slot),
            )
        )
        if start is None or end is None:
            continue
        aux_order = 0
        for step_idx in range(start, end + 1):
            row = step_by_idx.get(step_idx)
            if row is None or str(row.get("semantic_role", "")) != "aux":
                continue
            aux_order += 1
            link_values.append(
                (
                    anchor_id,
                    _event_id(db_idx, device_id, step_idx),
                    db_idx,
                    device_id,
                    aux_order,
                    step_idx,
                    "prelude",
                    str(row.get("semantic_role_reason", "")),
                    _aux_kind_from_event(row),
                    _nullable_float(row.get("dur_us")),
                    _json_value(row),
                )
            )
    conn.executemany(
        """
        INSERT OR REPLACE INTO traceloom_anchor_aux_slot(
            anchor_id, db_idx, device_id, anchor_idx, anchor_step_idx,
            aux_start_step_idx, aux_end_step_idx, aux_event_count, aux_dur_us, raw_json
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        slot_values,
    )
    conn.executemany(
        """
        INSERT OR REPLACE INTO traceloom_aux_link(
            anchor_id, aux_event_id, db_idx, device_id, aux_order, aux_step_idx,
            link_type, reason, aux_kind, aux_dur_us, raw_json
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        link_values,
    )


def _insert_nodes(
    conn: sqlite3.Connection,
    *,
    db_idx: int,
    device_id: int,
    view_name: str,
    rows: Sequence[Row],
) -> None:
    values = []
    for row in rows:
        local_node_id = str(row.get("node_id", ""))
        repeat_label = str(row.get("repeat", ""))
        values.append(
            (
                _node_id(db_idx, device_id, view_name, local_node_id),
                db_idx,
                device_id,
                view_name,
                local_node_id,
                str(row.get("path", "")),
                str(row.get("type", "")),
                str(row.get("kind", "")),
                str(row.get("symbol", "")),
                str(row.get("label", "")),
                str(row.get("category", "")),
                _nullable_int(row.get("depth")),
                _nullable_int(row.get("display_depth")),
                repeat_label,
                _parse_repeat_count(repeat_label) or _parse_repeat_count(str(row.get("label", ""))),
                _nullable_int(row.get("occurrence_count")),
                _nullable_int(row.get("anchor_count")),
                _nullable_float(row.get("anchors_per_occurrence")),
                _nullable_int(row.get("first_anchor_idx")),
                _nullable_int(row.get("last_anchor_idx")),
                _nullable_float(row.get("compute_us")),
                _nullable_float(row.get("comm_us")),
                _nullable_float(row.get("idle_us")),
                _nullable_float(row.get("total_us")),
                _nullable_float(row.get("avg_compute_us")),
                _nullable_float(row.get("avg_comm_us")),
                _nullable_float(row.get("avg_idle_us")),
                _nullable_float(row.get("avg_total_us")),
                _nullable_float(row.get("self_us")),
                _nullable_float(row.get("aux_events")),
                _nullable_float(row.get("aux_us")),
                _json_value(row),
            )
        )
    conn.executemany(
        """
        INSERT OR REPLACE INTO traceloom_viz_node(
            node_id, db_idx, device_id, view_name, local_node_id, path, node_type,
            kind, symbol, label, category, depth, level, repeat_label, repeat_count,
            occurrence_count, anchor_count, anchors_per_occurrence, first_anchor_idx,
            last_anchor_idx, compute_us, comm_us, idle_us, total_us, avg_compute_us,
            avg_comm_us, avg_idle_us, avg_total_us, self_us, aux_events, aux_us, raw_json
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        values,
    )


def _insert_edges(
    conn: sqlite3.Connection,
    *,
    db_idx: int,
    device_id: int,
    view_name: str,
    tree_payload: Row,
    node_rows: Sequence[Row],
) -> None:
    root = tree_payload.get("root", {})
    values: List[Tuple[object, ...]] = []
    visible_node_ids = {str(row.get("node_id", "")) for row in node_rows}

    def visit(node: object, parent_local_id: str | None = None, edge_order: int = 0) -> None:
        if not isinstance(node, dict):
            return
        local_id = str(node.get("node_id", ""))
        node_type = str(node.get("type", ""))
        current_parent_id = parent_local_id
        if parent_local_id and local_id and local_id in visible_node_ids:
            values.append(
                (
                    _node_id(db_idx, device_id, view_name, parent_local_id),
                    _node_id(db_idx, device_id, view_name, local_id),
                    db_idx,
                    device_id,
                    view_name,
                    edge_order,
                    node_type,
                    _json_value({"child": local_id, "parent": parent_local_id, "type": node_type}),
                )
            )
            current_parent_id = local_id
        elif not parent_local_id and local_id in visible_node_ids:
            current_parent_id = local_id
        if node_type == "Seq":
            for item in node.get("items", []):
                if isinstance(item, dict):
                    child = item.get("node")
                    if isinstance(child, dict):
                        visit(child, current_parent_id, _as_int(item.get("ord", 0)))
        elif node_type == "Repeat":
            body = node.get("body")
            if isinstance(body, dict):
                visit(body, current_parent_id, 1)
        overlays = node.get("overlays", [])
        if isinstance(overlays, list):
            for idx, item in enumerate(overlays, start=1):
                if isinstance(item, dict):
                    child = item.get("node")
                    if isinstance(child, dict):
                        visit(child, current_parent_id, 100000 + idx)

    visit(root)
    conn.executemany(
        """
        INSERT OR REPLACE INTO traceloom_viz_edge(
            parent_node_id, child_node_id, db_idx, device_id, view_name,
            edge_order, edge_kind, raw_json
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        """,
        values,
    )


def _insert_node_anchor_links(
    conn: sqlite3.Connection,
    *,
    db_idx: int,
    device_id: int,
    view_name: str,
    rows: Sequence[Row],
    node_rows: Sequence[Row],
) -> None:
    node_info = {str(row.get("node_id", "")): row for row in node_rows}
    values = []
    for row in rows:
        local_node_id = str(row.get("node_id", ""))
        start = _nullable_int(row.get("anchor_start_idx"))
        end = _nullable_int(row.get("anchor_end_idx"))
        if start is None or end is None:
            continue
        info = node_info.get(local_node_id, {})
        coverage_kind = "self" if str(info.get("type", "")) == "Atom" and start == end else "descendant"
        for anchor_idx in range(start, end + 1):
            values.append(
                (
                    _node_id(db_idx, device_id, view_name, local_node_id),
                    _anchor_id(db_idx, device_id, anchor_idx),
                    db_idx,
                    device_id,
                    view_name,
                    _as_int(row.get("occurrence_idx")),
                    anchor_idx - start + 1,
                    coverage_kind,
                    str(row.get("repeat_context", "")),
                )
            )
    conn.executemany(
        """
        INSERT OR REPLACE INTO traceloom_viz_node_anchor(
            node_id, anchor_id, db_idx, device_id, view_name, occurrence_idx,
            anchor_order, coverage_kind, repeat_context
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        values,
    )


def _insert_semantic_tree(
    conn: sqlite3.Connection,
    *,
    db_idx: int,
    device_id: int,
    view_name: str,
    stem: str,
    tree_payload: Row,
    node_rows: Sequence[Row],
) -> None:
    tree_kind = str(tree_payload.get("view", "") or view_name or "semantic_tree")
    tree_id = _tree_id(db_idx, device_id, view_name, tree_kind)
    node_by_raw_id: Dict[str, Row] = {}
    node_by_local_id: Dict[str, Row] = {}
    raw_to_local_id: Dict[str, str] = {}
    for row in node_rows:
        local_id = str(row.get("node_id", "")).strip()
        raw_id = str(row.get("raw_node_id", "") or local_id).strip()
        if not local_id:
            continue
        node_by_local_id[local_id] = row
        node_by_raw_id[raw_id] = row
        raw_to_local_id[raw_id] = local_id
    has_distinct_raw_ids = any(raw != local for raw, local in raw_to_local_id.items())

    root = tree_payload.get("root", {})
    preorder: List[Dict[str, object]] = []
    edge_values: List[Tuple[object, ...]] = []

    def row_for_node(node: Row) -> Row | None:
        local_id = str(node.get("node_id", "")).strip()
        if local_id in node_by_local_id:
            return node_by_local_id[local_id]
        raw_id = str(node.get("raw_node_id", "") or local_id).strip()
        if has_distinct_raw_ids:
            return node_by_raw_id.get(raw_id)
        mapped_local_id = raw_to_local_id.get(raw_id, raw_id)
        return node_by_local_id.get(mapped_local_id)

    def visit(
        node: object,
        *,
        parent_local_id: str | None,
        sibling_order: int,
        depth: int,
        path: str,
    ) -> str | None:
        if not isinstance(node, dict):
            return parent_local_id
        node_type = str(node.get("type", ""))
        row = row_for_node(node)
        local_id = str(row.get("node_id", "")).strip() if row is not None else ""
        current_parent = parent_local_id
        if row is not None and local_id:
            preorder.append(
                {
                    "row": row,
                    "local_id": local_id,
                    "parent_local_id": parent_local_id or "",
                    "sibling_order": sibling_order,
                    "depth": depth,
                    "path": str(row.get("path", "") or path),
                    "node_type": node_type or str(row.get("type", "")),
                }
            )
            if parent_local_id:
                edge_values.append(
                    (
                        _node_id(db_idx, device_id, view_name, parent_local_id),
                        _node_id(db_idx, device_id, view_name, local_id),
                        tree_id,
                        db_idx,
                        device_id,
                        view_name,
                        tree_kind,
                        sibling_order,
                        node_type or str(row.get("type", "")),
                        _json_value({"parent": parent_local_id, "child": local_id, "path": path}),
                    )
                )
            current_parent = local_id
        if node_type == "Seq":
            items = node.get("items", [])
            if isinstance(items, list):
                for idx, item in enumerate(items, start=1):
                    child = item.get("node") if isinstance(item, dict) else None
                    child_path = f"{path}.{idx}" if path else str(idx)
                    visit(
                        child,
                        parent_local_id=current_parent,
                        sibling_order=_as_int(item.get("ord", idx)) if isinstance(item, dict) else idx,
                        depth=depth + 1,
                        path=child_path,
                    )
        elif node_type == "Repeat":
            body = node.get("body")
            visit(
                body,
                parent_local_id=current_parent,
                sibling_order=1,
                depth=depth + 1,
                path=f"{path}.body" if path else "body",
            )
        overlays = node.get("overlays", [])
        if isinstance(overlays, list):
            for idx, item in enumerate(overlays, start=1):
                child = item.get("node") if isinstance(item, dict) else None
                visit(
                    child,
                    parent_local_id=current_parent,
                    sibling_order=100000 + idx,
                    depth=depth + 1,
                    path=f"{path}.graph{idx}" if path else f"graph{idx}",
                )
        return current_parent

    visit(root, parent_local_id=None, sibling_order=1, depth=0, path="root")
    root_local_id = str(preorder[0]["local_id"]) if preorder else ""
    root_node_id = _node_id(db_idx, device_id, view_name, root_local_id) if root_local_id else ""

    conn.execute(
        """
        INSERT OR REPLACE INTO traceloom_semantic_tree(
            tree_id, db_idx, device_id, view_name, tree_kind, stem, root_node_id,
            schema_version, semantic_projection, macro_discovery, readable_macro_mode,
            auxiliary_attribution, raw_json
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            tree_id,
            db_idx,
            device_id,
            view_name,
            tree_kind,
            stem,
            root_node_id,
            str(tree_payload.get("schema_version", "")),
            str(tree_payload.get("semantic_projection", "")),
            str(tree_payload.get("macro_discovery", "")),
            str(tree_payload.get("readable_macro_mode", "")),
            str(tree_payload.get("auxiliary_attribution", "")),
            _json_value(tree_payload),
        ),
    )

    span_by_node = _semantic_anchor_spans(conn, db_idx=db_idx, device_id=device_id, view_name=view_name)
    node_values: List[Tuple[object, ...]] = []
    for idx, entry in enumerate(preorder, start=1):
        row = entry["row"]
        if not isinstance(row, dict):
            continue
        local_id = str(entry["local_id"])
        parent_local_id = str(entry.get("parent_local_id", ""))
        node_id = _node_id(db_idx, device_id, view_name, local_id)
        parent_node_id = _node_id(db_idx, device_id, view_name, parent_local_id) if parent_local_id else None
        span = span_by_node.get(node_id, {})
        repeat_label = str(row.get("repeat", ""))
        repeat_count = _parse_repeat_count(repeat_label) or _parse_repeat_count(str(row.get("label", "")))
        aux_events = _nullable_float(row.get("aux_events"))
        aux_us = _nullable_float(row.get("aux_us"))
        row_start_ns = _nullable_int(row.get("start_ns"))
        row_end_ns = _nullable_int(row.get("end_ns"))
        if str(row.get("kind", "")) == "graph":
            start_ns = row_start_ns
            end_ns = row_end_ns
        else:
            start_ns = _nullable_int(span.get("start_ns")) or row_start_ns
            end_ns = _nullable_int(span.get("end_ns")) or row_end_ns
        node_values.append(
            (
                node_id,
                tree_id,
                db_idx,
                device_id,
                view_name,
                tree_kind,
                local_id,
                parent_node_id,
                parent_local_id or None,
                idx,
                _as_int(entry.get("sibling_order")),
                str(entry.get("path", "")),
                _nullable_int(row.get("depth")),
                _nullable_int(row.get("display_depth")),
                _nullable_int(row.get("loop_depth")),
                str(row.get("type", "") or entry.get("node_type", "")),
                str(row.get("kind", "")),
                str(row.get("symbol", "")),
                str(row.get("label", "")),
                str(row.get("category", "")),
                repeat_count,
                _nullable_int(row.get("occurrence_count")),
                _nullable_int(row.get("anchor_count")),
                _nullable_int(row.get("first_anchor_idx")) or _nullable_int(span.get("first_anchor_idx")),
                _nullable_int(row.get("last_anchor_idx")) or _nullable_int(span.get("last_anchor_idx")),
                start_ns,
                end_ns,
                _nullable_float(row.get("compute_us")),
                _nullable_float(row.get("comm_us")),
                _nullable_float(row.get("idle_us")),
                _nullable_float(row.get("total_us")),
                _nullable_float(row.get("avg_compute_us")),
                _nullable_float(row.get("avg_comm_us")),
                _nullable_float(row.get("avg_idle_us")),
                _nullable_float(row.get("avg_total_us")),
                _nullable_float(row.get("self_us")),
                aux_events,
                aux_us,
                aux_events,
                aux_us,
                _json_value(row),
            )
        )

    conn.executemany(
        """
        INSERT OR REPLACE INTO traceloom_semantic_node(
            node_id, tree_id, db_idx, device_id, view_name, tree_kind, local_node_id,
            parent_node_id, parent_local_node_id, preorder_idx, sibling_order, path,
            depth, display_depth, loop_depth, node_type, semantic_kind, symbol, label,
            category, repeat_count, occurrence_count, anchor_count, first_anchor_idx,
            last_anchor_idx, start_ns, end_ns, compute_us, comm_us, idle_us, total_us,
            avg_compute_us, avg_comm_us, avg_idle_us, avg_total_us, self_us,
            aux_event_count, aux_us, hidden_aux_event_count, hidden_aux_us, raw_json
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        node_values,
    )
    conn.executemany(
        """
        INSERT OR REPLACE INTO traceloom_semantic_edge(
            parent_node_id, child_node_id, tree_id, db_idx, device_id, view_name,
            tree_kind, edge_order, edge_kind, raw_json
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        edge_values,
    )


def _semantic_anchor_spans(
    conn: sqlite3.Connection,
    *,
    db_idx: int,
    device_id: int,
    view_name: str,
) -> Dict[str, Dict[str, object]]:
    rows = conn.execute(
        """
        SELECT
            na.node_id,
            MIN(a.anchor_idx) AS first_anchor_idx,
            MAX(a.anchor_idx) AS last_anchor_idx,
            MIN(a.start_ns) AS start_ns,
            MAX(a.end_ns) AS end_ns
        FROM traceloom_viz_node_anchor na
        JOIN traceloom_anchor a ON a.anchor_id = na.anchor_id
        WHERE na.db_idx = ? AND na.device_id = ? AND na.view_name = ?
        GROUP BY na.node_id
        """,
        (db_idx, device_id, view_name),
    ).fetchall()
    return {
        str(row[0]): {
            "first_anchor_idx": row[1],
            "last_anchor_idx": row[2],
            "start_ns": row[3],
            "end_ns": row[4],
        }
        for row in rows
    }


def _insert_loop_nodes(
    conn: sqlite3.Connection,
    *,
    db_idx: int,
    device_id: int,
    view_name: str,
    rows: Sequence[Row],
) -> None:
    values = []
    for row in rows:
        local_node_id = str(row.get("node_id", ""))
        repeat_label = str(row.get("repeat", ""))
        values.append(
            (
                _node_id(db_idx, device_id, view_name, local_node_id),
                db_idx,
                device_id,
                view_name,
                _nullable_int(row.get("loop_rank")),
                repeat_label,
                _parse_repeat_count(repeat_label),
                _nullable_int(row.get("occurrence_count")),
                _nullable_int(row.get("anchor_count")),
                _nullable_float(row.get("total_us")),
                _nullable_float(row.get("avg_total_us")),
                _nullable_float(row.get("compute_us")),
                _nullable_float(row.get("comm_us")),
                _nullable_float(row.get("idle_us")),
                _nullable_float(row.get("loop_total_pct")),
                _json_value(row),
            )
        )
    conn.executemany(
        """
        INSERT OR REPLACE INTO traceloom_loop_node(
            node_id, db_idx, device_id, view_name, loop_rank, repeat_label,
            repeat_count, occurrence_count, anchor_count, total_us, avg_total_us,
            compute_us, comm_us, idle_us, loop_total_pct, raw_json
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        values,
    )


def _insert_anchor_primary_nodes(conn: sqlite3.Connection, *, db_idx: int, device_id: int, view_name: str) -> None:
    conn.execute(
        """
        INSERT OR REPLACE INTO traceloom_anchor_primary_node(anchor_id, node_id, db_idx, device_id, view_name, reason)
        SELECT
            ranked.anchor_id,
            ranked.node_id,
            ranked.db_idx,
            ranked.device_id,
            ranked.view_name,
            ranked.reason
        FROM (
            SELECT
                na.anchor_id,
                na.node_id,
                na.db_idx,
                na.device_id,
                na.view_name,
                CASE WHEN na.coverage_kind = 'self' THEN 'self_atom' ELSE 'smallest_covering_node' END AS reason,
                ROW_NUMBER() OVER (
                    PARTITION BY na.anchor_id
                    ORDER BY
                        CASE WHEN na.coverage_kind = 'self' THEN 0 ELSE 1 END,
                        COALESCE(n.anchor_count, 9223372036854775807) ASC,
                        COALESCE(n.level, 0) DESC
                ) AS rn
            FROM traceloom_viz_node_anchor na
            JOIN traceloom_viz_node n ON n.node_id = na.node_id
            WHERE na.db_idx = ? AND na.device_id = ? AND na.view_name = ?
        ) ranked
        WHERE ranked.rn = 1
        """,
        (db_idx, device_id, view_name),
    )


def _json_value(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True)


def _event_id(db_idx: int, device_id: int, step_idx: int) -> str:
    return f"db{db_idx:02d}:dev{device_id}:step{step_idx}"


def _anchor_id(db_idx: int, device_id: int, anchor_idx: int) -> str:
    return f"db{db_idx:02d}:dev{device_id}:anchor{anchor_idx}"


def _node_id(db_idx: int, device_id: int, view_name: str, local_node_id: str) -> str:
    return f"db{db_idx:02d}:dev{device_id}:{view_name}:{local_node_id}"


def _tree_id(db_idx: int, device_id: int, view_name: str, tree_kind: str) -> str:
    return f"db{db_idx:02d}:dev{device_id}:{view_name}:{tree_kind}"


def _source_table(row: Row) -> str:
    explicit = str(row.get("source_table", "")).strip()
    if explicit:
        return explicit
    role = str(row.get("role", ""))
    if role == "collective":
        return "COMMUNICATION_OP_OR_TASK"
    return "TASK"


def _source_key(row: Row, step_idx: int) -> str:
    explicit = str(row.get("source_key", "")).strip()
    if explicit:
        return explicit
    stream_id = str(row.get("stream_id", ""))
    start_ns = str(row.get("start_ns", ""))
    return f"step={step_idx};stream={stream_id};start_ns={start_ns}"


def _event_category(row: Row, role: str) -> str:
    explicit = str(row.get("category", "")).strip().lower()
    if explicit == "graph":
        return "graph"
    task_type = str(row.get("task_type", "")).strip().upper()
    source_table = str(row.get("source_table", "")).strip().upper()
    if task_type in {"ACL_GRAPH_REPLAY", "ACL_GRAPH_EXECUTE", "ACL_GRAPH_ACTIVITY_SEGMENT"} or source_table == "ACLGRAPH_REPLAY":
        return "graph"
    if role == "compute":
        return "exec"
    if role in {"collective", "data_move"}:
        return "comm"
    if role == "wait":
        return "wait"
    return role or "other"


def _aux_kind_from_event(row: Row) -> str:
    role = str(row.get("role", ""))
    family = str(row.get("family", "")).lower()
    label = str(row.get("label", "")).lower()
    task_type = str(row.get("task_type", "")).lower()
    blob = f"{role} {family} {label} {task_type}"
    if role == "data_move" or "memcpy" in blob or "copy" in blob:
        return "data_move"
    if role == "collective":
        return "collective"
    if role == "compute":
        return "compute"
    return family or role or "other"


def _parse_repeat_count(value: str) -> int | None:
    match = re.search(r"x(\d+)", value or "")
    return int(match.group(1)) if match else None


def _edge_order(node: Row) -> int:
    return _as_int(node.get("_traceloom_edge_order", 0))


def _as_int(value: object) -> int:
    parsed = _nullable_int(value)
    return int(parsed) if parsed is not None else 0


def _nullable_int(value: object) -> int | None:
    if value is None or value == "":
        return None
    text = str(value).strip()
    if re.fullmatch(r"[+-]?\d+(?:\.0+)?", text):
        return int(text.split(".", 1)[0])
    try:
        return int(float(text))
    except (TypeError, ValueError):
        return None


def _nullable_float(value: object) -> float | None:
    if value is None or value == "":
        return None
    try:
        return float(str(value))
    except (TypeError, ValueError):
        return None
