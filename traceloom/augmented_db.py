from __future__ import annotations

import json
import re
import shutil
import sqlite3
from pathlib import Path
from typing import Dict, List, Sequence, Tuple


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
        _insert_loop_nodes(
            conn,
            db_idx=db_idx,
            device_id=device_id,
            view_name=view_name,
            rows=loop_cost_rows,
        )
        _insert_anchor_primary_nodes(conn, db_idx=db_idx, device_id=device_id, view_name=view_name)
        conn.commit()


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

        CREATE INDEX IF NOT EXISTS idx_traceloom_event_device_step
            ON traceloom_event(db_idx, device_id, step_idx);
        CREATE INDEX IF NOT EXISTS idx_traceloom_event_source_lookup
            ON traceloom_event_source(source_table, source_key);
        CREATE INDEX IF NOT EXISTS idx_traceloom_anchor_device_idx
            ON traceloom_anchor(db_idx, device_id, anchor_idx);
        CREATE INDEX IF NOT EXISTS idx_traceloom_aux_anchor
            ON traceloom_aux_link(anchor_id);
        CREATE INDEX IF NOT EXISTS idx_traceloom_node_anchor_node
            ON traceloom_viz_node_anchor(node_id);
        CREATE INDEX IF NOT EXISTS idx_traceloom_node_anchor_anchor
            ON traceloom_viz_node_anchor(anchor_id);

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
        """
    )


def _replace_metadata(conn: sqlite3.Connection, values: Dict[str, object]) -> None:
    conn.executemany(
        "INSERT OR REPLACE INTO traceloom_metadata(key, value) VALUES (?, ?)",
        [(str(k), _json_value(v)) for k, v in values.items()],
    )


def _delete_device_rows(conn: sqlite3.Connection, *, db_idx: int, device_id: int, view_name: str) -> None:
    params = {"db_idx": db_idx, "device_id": device_id, "view_name": view_name}
    for table in (
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
                _event_category(role),
                role,
                str(row.get("semantic_role", "")),
                str(row.get("semantic_role_reason", "")),
                str(row.get("symbol", "")),
                str(row.get("label", "")),
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
            semantic_role_reason, symbol, label, family, task_type, raw_json
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
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


def _source_table(row: Row) -> str:
    role = str(row.get("role", ""))
    if role == "collective":
        return "COMMUNICATION_OP_OR_TASK"
    return "TASK"


def _source_key(row: Row, step_idx: int) -> str:
    stream_id = str(row.get("stream_id", ""))
    start_ns = str(row.get("start_ns", ""))
    return f"step={step_idx};stream={stream_id};start_ns={start_ns}"


def _event_category(role: str) -> str:
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
    try:
        return int(float(str(value)))
    except (TypeError, ValueError):
        return None


def _nullable_float(value: object) -> float | None:
    if value is None or value == "":
        return None
    try:
        return float(str(value))
    except (TypeError, ValueError):
        return None
