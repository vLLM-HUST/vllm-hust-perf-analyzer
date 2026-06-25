from __future__ import annotations

import hashlib
import re
import sqlite3
import time
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple


LOCAL_LINK_TABLE = "traceloom_collective_global_link"
GLOBAL_DB_NAME = "global_collectives.db"


@dataclass(frozen=True)
class LoopNode:
    db_name: str
    db_idx: int
    device_id: int
    member_id: str
    node_id: str
    local_node_id: str
    repeat_count: int
    occurrence_count: int
    anchor_count: int
    anchors_per_occurrence: int
    first_anchor_idx: int
    level: int
    path: str
    collective_pattern: str
    signature: str
    signature_ordinal: int = 0
    pair_id: str = ""


@dataclass(frozen=True)
class CollectiveLink:
    candidate_collective_key: str
    db_name: str
    db_idx: int
    device_id: int
    member_id: str
    pair_id: str
    local_node_id: str
    occurrence_idx: int
    idx_in_occurrence: int
    op_type: str
    anchor_id: str
    event_id: str
    source_table: str
    source_key: str
    connection_id: str
    op_id: str
    start_ns: int
    end_ns: int
    dur_us: float
    validation_status: str
    confidence: float


@dataclass(frozen=True)
class GlobalSummary:
    candidate_collective_key: str
    pair_id: str
    occurrence_idx: int
    op_type: str
    idx_in_occurrence: int
    member_count: int
    expected_world_size: int
    start_skew_us: float
    duration_skew_us: float
    connection_ids: str
    op_ids: str
    members: str
    missing_members: str
    validation_status: str
    confidence: float


def run_collective_tag(
    *,
    analysis_dir: Path,
    run_name: str | None = None,
    expected_world_size: int | None = None,
) -> Dict[str, object]:
    started = time.time()
    resolved_dir = _resolve_analysis_dir(analysis_dir)
    db_paths = sorted(resolved_dir.glob("db*.traceloom_augmented.db"))
    if not db_paths:
        raise FileNotFoundError(f"no db*.traceloom_augmented.db files under {resolved_dir}")

    run_label = _sanitize_run_name(run_name or _default_run_name(resolved_dir))
    all_loops: List[LoopNode] = []
    expected_members: set[str] = set()

    for db_path in db_paths:
        with sqlite3.connect(str(db_path)) as conn:
            conn.row_factory = sqlite3.Row
            _require_augmented_views(conn, db_path)
            members = _load_member_ids(conn, db_path.name)
            expected_members.update(members)
            loops = _load_loop_nodes(conn, db_path.name)
            all_loops.extend(loops)

    loops_with_pairs = _assign_loop_pairs(all_loops)
    pair_by_node = {(loop.db_name, loop.node_id): loop.pair_id for loop in loops_with_pairs}

    links_by_db: Dict[Path, List[CollectiveLink]] = {}
    all_links: List[CollectiveLink] = []
    for db_path in db_paths:
        with sqlite3.connect(str(db_path)) as conn:
            conn.row_factory = sqlite3.Row
            links = _build_db_links(
                conn=conn,
                db_name=db_path.name,
                run_name=run_label,
                pair_by_node=pair_by_node,
            )
        links_by_db[db_path] = links
        all_links.extend(links)

    world_size = expected_world_size or max(1, len(expected_members))
    summaries = _summarize_links(
        links=all_links,
        expected_members=sorted(expected_members),
        expected_world_size=world_size,
    )
    status_by_key = {
        row.candidate_collective_key: (row.validation_status, row.confidence)
        for row in summaries
    }
    links_by_db = {
        db_path: [_with_validation(link, status_by_key) for link in links]
        for db_path, links in links_by_db.items()
    }
    all_links = [link for links in links_by_db.values() for link in links]

    for db_path, links in links_by_db.items():
        with sqlite3.connect(str(db_path)) as conn:
            _write_local_links(conn, links)
            conn.commit()

    global_db_path = resolved_dir / GLOBAL_DB_NAME
    _write_global_db(
        global_db_path=global_db_path,
        links=all_links,
        summaries=summaries,
    )
    summary_path = resolved_dir / "collective_summary.md"
    summary_path.write_text(
        _build_summary_markdown(
            analysis_dir=resolved_dir,
            run_name=run_label,
            db_paths=db_paths,
            loops=loops_with_pairs,
            links=all_links,
            summaries=summaries,
            expected_world_size=world_size,
        ),
        encoding="utf-8",
    )

    return {
        "version": "collective_tag_v1",
        "analysis_dir": str(resolved_dir),
        "run_name": run_label,
        "db_count": len(db_paths),
        "loop_pair_count": len({loop.pair_id for loop in loops_with_pairs}),
        "local_link_count": len(all_links),
        "candidate_collective_count": len(summaries),
        "expected_world_size": world_size,
        "global_db_file": str(global_db_path),
        "summary_file": str(summary_path),
        "elapsed_sec": round(time.time() - started, 3),
    }


def format_collective_tag_summary(meta: Dict[str, object]) -> str:
    return "\n".join(
        [
            "TraceLoom collective-tag complete",
            f"- analysis_dir: {meta.get('analysis_dir', '')}",
            f"- run_name: {meta.get('run_name', '')}",
            f"- db_count: {meta.get('db_count', 0)}",
            f"- loop_pair_count: {meta.get('loop_pair_count', 0)}",
            f"- local_link_count: {meta.get('local_link_count', 0)}",
            f"- candidate_collective_count: {meta.get('candidate_collective_count', 0)}",
            f"- global_db: {meta.get('global_db_file', '')}",
            f"- summary: {meta.get('summary_file', '')}",
        ]
    )


def ensure_collective_link_schema(conn: sqlite3.Connection) -> None:
    conn.executescript(
        f"""
        CREATE TABLE IF NOT EXISTS {LOCAL_LINK_TABLE} (
            candidate_collective_key TEXT NOT NULL,
            db_name TEXT NOT NULL,
            db_idx INTEGER NOT NULL,
            device_id INTEGER NOT NULL,
            member_id TEXT NOT NULL,
            pair_id TEXT NOT NULL,
            local_node_id TEXT NOT NULL,
            occurrence_idx INTEGER NOT NULL,
            idx_in_occurrence INTEGER NOT NULL,
            op_type TEXT NOT NULL,
            anchor_id TEXT NOT NULL,
            event_id TEXT NOT NULL,
            source_table TEXT,
            source_key TEXT,
            connection_id TEXT,
            op_id TEXT,
            start_ns INTEGER,
            end_ns INTEGER,
            dur_us REAL,
            validation_status TEXT,
            confidence REAL,
            PRIMARY KEY(db_name, device_id, local_node_id, occurrence_idx, anchor_id)
        );

        CREATE INDEX IF NOT EXISTS idx_traceloom_collective_key
            ON {LOCAL_LINK_TABLE}(candidate_collective_key);
        CREATE INDEX IF NOT EXISTS idx_traceloom_collective_pair
            ON {LOCAL_LINK_TABLE}(pair_id, occurrence_idx, op_type, idx_in_occurrence);
        """
    )


def _resolve_analysis_dir(path: Path) -> Path:
    path = path.resolve()
    if list(path.glob("db*.traceloom_augmented.db")):
        return path
    nested = path / "traceloom"
    if list(nested.glob("db*.traceloom_augmented.db")):
        return nested
    raise FileNotFoundError(
        f"{path} is not a TraceLoom analysis bundle; expected db*.traceloom_augmented.db"
    )


def _require_augmented_views(conn: sqlite3.Connection, db_path: Path) -> None:
    required = {"traceloom_tree_node_anchor", "traceloom_anchor", "traceloom_event", "traceloom_viz_node"}
    found = {
        str(row[0])
        for row in conn.execute(
            "SELECT name FROM sqlite_master WHERE name LIKE 'traceloom_%'"
        )
    }
    missing = sorted(required - found)
    if missing:
        raise ValueError(f"{db_path} is missing TraceLoom views/tables: {', '.join(missing)}")


def _load_member_ids(conn: sqlite3.Connection, db_name: str) -> set[str]:
    out: set[str] = set()
    for row in conn.execute("SELECT DISTINCT db_idx, device_id FROM traceloom_viz_node"):
        out.add(_member_id(db_name, _as_int(row[0]), _as_int(row[1])))
    return out


def _load_loop_nodes(conn: sqlite3.Connection, db_name: str) -> List[LoopNode]:
    rows = conn.execute(
        """
        SELECT
            n.node_id,
            n.db_idx,
            n.device_id,
            n.local_node_id,
            COALESCE(n.repeat_count, 0) AS repeat_count,
            COALESCE(n.occurrence_count, 0) AS occurrence_count,
            COALESCE(n.anchor_count, 0) AS anchor_count,
            COALESCE(n.anchors_per_occurrence, 0) AS anchors_per_occurrence,
            COALESCE(n.first_anchor_idx, 0) AS first_anchor_idx,
            COALESCE(n.level, 0) AS level,
            COALESCE(n.path, '') AS path
        FROM traceloom_viz_node n
        WHERE n.kind = 'repeat'
          AND EXISTS (
              SELECT 1
              FROM traceloom_viz_node_anchor na
              JOIN traceloom_anchor a ON a.anchor_id = na.anchor_id
              JOIN traceloom_event e ON e.event_id = a.event_id
              WHERE na.node_id = n.node_id
                AND e.role = 'collective'
          )
        ORDER BY n.db_idx, n.device_id, n.first_anchor_idx, n.local_node_id
        """
    ).fetchall()
    loops: List[LoopNode] = []
    for row in rows:
        pattern = _loop_collective_pattern(conn, str(row["node_id"]))
        repeat_count = _as_int(row["repeat_count"])
        anchor_count = _as_int(row["anchor_count"])
        occurrence_count = _as_int(row["occurrence_count"])
        anchors_per_occurrence = _as_int(row["anchors_per_occurrence"])
        if anchors_per_occurrence <= 0 and occurrence_count > 0:
            anchors_per_occurrence = max(1, int(round(anchor_count / occurrence_count)))
        signature = _loop_signature(
            repeat_count=repeat_count,
            anchors_per_occurrence=anchors_per_occurrence,
            collective_pattern=pattern,
        )
        db_idx = _as_int(row["db_idx"])
        device_id = _as_int(row["device_id"])
        loops.append(
            LoopNode(
                db_name=db_name,
                db_idx=db_idx,
                device_id=device_id,
                member_id=_member_id(db_name, db_idx, device_id),
                node_id=str(row["node_id"]),
                local_node_id=str(row["local_node_id"]),
                repeat_count=repeat_count,
                occurrence_count=occurrence_count,
                anchor_count=anchor_count,
                anchors_per_occurrence=anchors_per_occurrence,
                first_anchor_idx=_as_int(row["first_anchor_idx"]),
                level=_as_int(row["level"]),
                path=str(row["path"]),
                collective_pattern=pattern,
                signature=signature,
            )
        )
    return loops


def _loop_collective_pattern(conn: sqlite3.Connection, node_id: str) -> str:
    occurrence_row = conn.execute(
        "SELECT MIN(occurrence_idx) FROM traceloom_viz_node_anchor WHERE node_id = ?",
        (node_id,),
    ).fetchone()
    occurrence_idx = _as_int(occurrence_row[0] if occurrence_row else 0)
    rows = conn.execute(
        """
        SELECT na.anchor_order, COALESCE(e.family, a.family, e.label, a.label, '') AS family, e.label
        FROM traceloom_viz_node_anchor na
        JOIN traceloom_anchor a ON a.anchor_id = na.anchor_id
        JOIN traceloom_event e ON e.event_id = a.event_id
        WHERE na.node_id = ?
          AND na.occurrence_idx = ?
          AND e.role = 'collective'
        ORDER BY na.anchor_order, e.start_ns, a.anchor_idx
        """,
        (node_id, occurrence_idx),
    ).fetchall()
    return ",".join(f"{_as_int(row[0])}:{_normalize_op_type(str(row[1]), str(row[2]))}" for row in rows)


def _loop_signature(
    *,
    repeat_count: int,
    anchors_per_occurrence: int,
    collective_pattern: str,
) -> str:
    payload = f"repeat={repeat_count};anchors_per_occurrence={anchors_per_occurrence};collectives={collective_pattern}"
    digest = hashlib.sha1(payload.encode("utf-8")).hexdigest()[:10]
    return f"R{repeat_count:04d}:A{anchors_per_occurrence:05d}:{digest}"


def _assign_loop_pairs(loops: Sequence[LoopNode]) -> List[LoopNode]:
    by_signature: Dict[str, List[LoopNode]] = defaultdict(list)
    for loop in loops:
        by_signature[loop.signature].append(loop)

    out: List[LoopNode] = []
    for signature in sorted(by_signature):
        by_member: Dict[str, List[LoopNode]] = defaultdict(list)
        for loop in by_signature[signature]:
            by_member[loop.member_id].append(loop)
        for member_loops in by_member.values():
            member_loops.sort(key=lambda item: (item.first_anchor_idx, item.local_node_id))

        max_ordinal = max((len(items) for items in by_member.values()), default=0)
        for ordinal in range(1, max_ordinal + 1):
            sample = next((items[ordinal - 1] for items in by_member.values() if len(items) >= ordinal), None)
            if sample is None:
                continue
            pair_id = _pair_id(sample, ordinal)
            for items in by_member.values():
                if len(items) >= ordinal:
                    loop = items[ordinal - 1]
                    out.append(
                        LoopNode(
                            **{
                                **loop.__dict__,
                                "signature_ordinal": ordinal,
                                "pair_id": pair_id,
                            }
                        )
                    )
    out.sort(key=lambda item: (item.pair_id, item.db_name, item.device_id, item.first_anchor_idx))
    return out


def _pair_id(loop: LoopNode, ordinal: int) -> str:
    digest = loop.signature.rsplit(":", 1)[-1][:6]
    return f"LP_R{loop.repeat_count:03d}_A{loop.anchors_per_occurrence:04d}_{ordinal:02d}_{digest}"


def _build_db_links(
    *,
    conn: sqlite3.Connection,
    db_name: str,
    run_name: str,
    pair_by_node: Dict[Tuple[str, str], str],
) -> List[CollectiveLink]:
    rows = conn.execute(
        """
        WITH ranked AS (
            SELECT
                n.node_id,
                n.local_node_id,
                n.db_idx,
                n.device_id,
                n.anchor_count AS node_anchor_count,
                n.level AS node_level,
                na.occurrence_idx,
                na.anchor_order,
                a.anchor_idx,
                a.anchor_id,
                a.event_id,
                COALESCE(e.family, a.family, '') AS family,
                COALESCE(e.label, a.label, '') AS label,
                e.source_table,
                e.source_key,
                e.start_ns,
                e.end_ns,
                e.dur_us,
                ROW_NUMBER() OVER (
                    PARTITION BY a.anchor_id, na.occurrence_idx
                    ORDER BY
                        COALESCE(n.anchor_count, 9223372036854775807) ASC,
                        COALESCE(n.level, 0) DESC,
                        n.local_node_id ASC
                ) AS rn
            FROM traceloom_viz_node_anchor na
            JOIN traceloom_viz_node n ON n.node_id = na.node_id
            JOIN traceloom_anchor a ON a.anchor_id = na.anchor_id
            JOIN traceloom_event e ON e.event_id = a.event_id
            WHERE n.kind = 'repeat'
              AND e.role = 'collective'
        )
        SELECT *
        FROM ranked
        WHERE rn = 1
        ORDER BY db_idx, device_id, local_node_id, occurrence_idx, anchor_order, start_ns
        """
    ).fetchall()

    grouped: Dict[Tuple[str, int, str, int, str], List[sqlite3.Row]] = defaultdict(list)
    for row in rows:
        pair_id = pair_by_node.get((db_name, str(row["node_id"])))
        if not pair_id:
            continue
        op_type = _normalize_op_type(str(row["family"]), str(row["label"]))
        key = (
            db_name,
            _as_int(row["device_id"]),
            str(row["local_node_id"]),
            _as_int(row["occurrence_idx"]),
            op_type,
        )
        grouped[key].append(row)

    links: List[CollectiveLink] = []
    comm_lookup = _CommunicationLookup(conn)
    for (_db_name, _device_id, _local_node_id, occurrence_idx, op_type), group_rows in sorted(grouped.items()):
        group_rows.sort(key=lambda row: (_as_int(row["anchor_order"]), _as_int(row["start_ns"]), str(row["anchor_id"])))
        for idx, row in enumerate(group_rows, start=1):
            pair_id = pair_by_node[(db_name, str(row["node_id"]))]
            connection_id, op_id = comm_lookup.find(
                device_id=_as_int(row["device_id"]),
                source_key=str(row["source_key"] or ""),
                start_ns=_as_int(row["start_ns"]),
                end_ns=_as_int(row["end_ns"]),
            )
            candidate_key = _candidate_collective_key(
                run_name=run_name,
                pair_id=pair_id,
                occurrence_idx=occurrence_idx,
                op_type=op_type,
                idx_in_occurrence=idx,
            )
            db_idx = _as_int(row["db_idx"])
            device_id = _as_int(row["device_id"])
            links.append(
                CollectiveLink(
                    candidate_collective_key=candidate_key,
                    db_name=db_name,
                    db_idx=db_idx,
                    device_id=device_id,
                    member_id=_member_id(db_name, db_idx, device_id),
                    pair_id=pair_id,
                    local_node_id=str(row["local_node_id"]),
                    occurrence_idx=occurrence_idx,
                    idx_in_occurrence=idx,
                    op_type=op_type,
                    anchor_id=str(row["anchor_id"]),
                    event_id=str(row["event_id"]),
                    source_table=str(row["source_table"] or ""),
                    source_key=str(row["source_key"] or ""),
                    connection_id=connection_id,
                    op_id=op_id,
                    start_ns=_as_int(row["start_ns"]),
                    end_ns=_as_int(row["end_ns"]),
                    dur_us=_as_float(row["dur_us"]),
                    validation_status="candidate",
                    confidence=0.5,
                )
            )
    links.sort(
        key=lambda item: (
            item.candidate_collective_key,
            item.db_name,
            item.device_id,
            item.local_node_id,
            item.anchor_id,
        )
    )
    return links


def _summarize_links(
    *,
    links: Sequence[CollectiveLink],
    expected_members: Sequence[str],
    expected_world_size: int,
) -> List[GlobalSummary]:
    expected_member_set = set(expected_members)
    grouped: Dict[str, List[CollectiveLink]] = defaultdict(list)
    for link in links:
        grouped[link.candidate_collective_key].append(link)

    summaries: List[GlobalSummary] = []
    for candidate_key, group in sorted(grouped.items()):
        group.sort(key=lambda item: (item.db_name, item.device_id, item.start_ns, item.anchor_id))
        members = sorted({item.member_id for item in group})
        missing = sorted(expected_member_set - set(members))
        if expected_world_size > len(expected_member_set):
            missing.extend(f"unknown_member_{idx}" for idx in range(1, expected_world_size - len(expected_member_set) + 1))
        member_count = len(members)
        if member_count >= expected_world_size:
            status = "complete"
            confidence = 0.85
        elif member_count > 1:
            status = "partial"
            confidence = 0.55
        else:
            status = "singleton"
            confidence = 0.35
        starts = [item.start_ns for item in group if item.start_ns]
        durations = [item.dur_us for item in group]
        start_skew_us = (max(starts) - min(starts)) / 1000.0 if len(starts) > 1 else 0.0
        duration_skew_us = max(durations) - min(durations) if len(durations) > 1 else 0.0
        sample = group[0]
        summaries.append(
            GlobalSummary(
                candidate_collective_key=candidate_key,
                pair_id=sample.pair_id,
                occurrence_idx=sample.occurrence_idx,
                op_type=sample.op_type,
                idx_in_occurrence=sample.idx_in_occurrence,
                member_count=member_count,
                expected_world_size=expected_world_size,
                start_skew_us=round(start_skew_us, 3),
                duration_skew_us=round(duration_skew_us, 3),
                connection_ids=_join_unique(item.connection_id for item in group),
                op_ids=_join_unique(item.op_id for item in group),
                members=" ".join(members),
                missing_members=" ".join(missing),
                validation_status=status,
                confidence=confidence,
            )
        )
    return summaries


def _with_validation(
    link: CollectiveLink,
    status_by_key: Dict[str, Tuple[str, float]],
) -> CollectiveLink:
    status, confidence = status_by_key.get(link.candidate_collective_key, ("candidate", 0.5))
    return CollectiveLink(
        **{
            **link.__dict__,
            "validation_status": status,
            "confidence": confidence,
        }
    )


def _write_local_links(conn: sqlite3.Connection, links: Sequence[CollectiveLink]) -> None:
    ensure_collective_link_schema(conn)
    conn.execute(f"DELETE FROM {LOCAL_LINK_TABLE}")
    conn.executemany(
        f"""
        INSERT OR REPLACE INTO {LOCAL_LINK_TABLE}(
            candidate_collective_key, db_name, db_idx, device_id, member_id,
            pair_id, local_node_id, occurrence_idx, idx_in_occurrence, op_type,
            anchor_id, event_id, source_table, source_key, connection_id, op_id,
            start_ns, end_ns, dur_us, validation_status, confidence
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        [
            (
                row.candidate_collective_key,
                row.db_name,
                row.db_idx,
                row.device_id,
                row.member_id,
                row.pair_id,
                row.local_node_id,
                row.occurrence_idx,
                row.idx_in_occurrence,
                row.op_type,
                row.anchor_id,
                row.event_id,
                row.source_table,
                row.source_key,
                row.connection_id,
                row.op_id,
                row.start_ns,
                row.end_ns,
                row.dur_us,
                row.validation_status,
                row.confidence,
            )
            for row in links
        ],
    )


def _write_global_db(
    *,
    global_db_path: Path,
    links: Sequence[CollectiveLink],
    summaries: Sequence[GlobalSummary],
) -> None:
    if global_db_path.exists():
        global_db_path.unlink()
    with sqlite3.connect(str(global_db_path)) as conn:
        conn.executescript(
            """
            CREATE TABLE traceloom_global_collective_summary (
                candidate_collective_key TEXT PRIMARY KEY,
                pair_id TEXT NOT NULL,
                occurrence_idx INTEGER NOT NULL,
                op_type TEXT NOT NULL,
                idx_in_occurrence INTEGER NOT NULL,
                member_count INTEGER NOT NULL,
                expected_world_size INTEGER NOT NULL,
                start_skew_us REAL,
                duration_skew_us REAL,
                connection_ids TEXT,
                op_ids TEXT,
                members TEXT,
                missing_members TEXT,
                validation_status TEXT,
                confidence REAL
            );

            CREATE TABLE traceloom_global_collective_member (
                candidate_collective_key TEXT NOT NULL,
                db_name TEXT NOT NULL,
                db_idx INTEGER NOT NULL,
                device_id INTEGER NOT NULL,
                member_id TEXT NOT NULL,
                pair_id TEXT NOT NULL,
                local_node_id TEXT NOT NULL,
                occurrence_idx INTEGER NOT NULL,
                idx_in_occurrence INTEGER NOT NULL,
                op_type TEXT NOT NULL,
                anchor_id TEXT NOT NULL,
                event_id TEXT NOT NULL,
                source_table TEXT,
                source_key TEXT,
                connection_id TEXT,
                op_id TEXT,
                start_ns INTEGER,
                end_ns INTEGER,
                dur_us REAL,
                validation_status TEXT,
                confidence REAL,
                PRIMARY KEY(candidate_collective_key, member_id, anchor_id)
            );

            CREATE INDEX idx_global_collective_status
                ON traceloom_global_collective_summary(validation_status);
            CREATE INDEX idx_global_collective_member_key
                ON traceloom_global_collective_member(candidate_collective_key);
            """
        )
        conn.executemany(
            """
            INSERT INTO traceloom_global_collective_summary(
                candidate_collective_key, pair_id, occurrence_idx, op_type,
                idx_in_occurrence, member_count, expected_world_size,
                start_skew_us, duration_skew_us, connection_ids, op_ids,
                members, missing_members, validation_status, confidence
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            [
                (
                    row.candidate_collective_key,
                    row.pair_id,
                    row.occurrence_idx,
                    row.op_type,
                    row.idx_in_occurrence,
                    row.member_count,
                    row.expected_world_size,
                    row.start_skew_us,
                    row.duration_skew_us,
                    row.connection_ids,
                    row.op_ids,
                    row.members,
                    row.missing_members,
                    row.validation_status,
                    row.confidence,
                )
                for row in summaries
            ],
        )
        conn.executemany(
            """
            INSERT INTO traceloom_global_collective_member(
                candidate_collective_key, db_name, db_idx, device_id, member_id,
                pair_id, local_node_id, occurrence_idx, idx_in_occurrence, op_type,
                anchor_id, event_id, source_table, source_key, connection_id, op_id,
                start_ns, end_ns, dur_us, validation_status, confidence
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            [
                (
                    row.candidate_collective_key,
                    row.db_name,
                    row.db_idx,
                    row.device_id,
                    row.member_id,
                    row.pair_id,
                    row.local_node_id,
                    row.occurrence_idx,
                    row.idx_in_occurrence,
                    row.op_type,
                    row.anchor_id,
                    row.event_id,
                    row.source_table,
                    row.source_key,
                    row.connection_id,
                    row.op_id,
                    row.start_ns,
                    row.end_ns,
                    row.dur_us,
                    row.validation_status,
                    row.confidence,
                )
                for row in links
            ],
        )
        conn.commit()


def _build_summary_markdown(
    *,
    analysis_dir: Path,
    run_name: str,
    db_paths: Sequence[Path],
    loops: Sequence[LoopNode],
    links: Sequence[CollectiveLink],
    summaries: Sequence[GlobalSummary],
    expected_world_size: int,
) -> str:
    status_counts = Counter(row.validation_status for row in summaries)
    pair_members: Dict[str, List[str]] = defaultdict(list)
    for loop in loops:
        pair_members[loop.pair_id].append(f"{loop.db_name}:dev{loop.device_id}:{loop.local_node_id}")

    lines = [
        "# Collective Candidate Summary",
        "",
        f"- analysis_dir: `{analysis_dir}`",
        f"- run_name: `{run_name}`",
        f"- sidecar_dbs: `{len(db_paths)}`",
        f"- expected_world_size: `{expected_world_size}`",
        f"- loop_pairs: `{len(pair_members)}`",
        f"- local_links: `{len(links)}`",
        f"- candidate_collectives: `{len(summaries)}`",
        "",
        "## Validation Status",
        "",
        "| status | candidate_keys |",
        "| --- | ---: |",
    ]
    for status, count in sorted(status_counts.items()):
        lines.append(f"| {status} | {count} |")

    lines.extend(
        [
            "",
            "## Loop Pairs",
            "",
            "| pair_id | mapped_nodes |",
            "| --- | --- |",
        ]
    )
    for pair_id in sorted(pair_members):
        lines.append(f"| {pair_id} | {' '.join(sorted(pair_members[pair_id]))} |")

    lines.extend(
        [
            "",
            "## Highest Start Skew",
            "",
            "| candidate_collective_key | members | start_skew_us | duration_skew_us | status |",
            "| --- | ---: | ---: | ---: | --- |",
        ]
    )
    for row in sorted(summaries, key=lambda item: item.start_skew_us, reverse=True)[:20]:
        lines.append(
            f"| {row.candidate_collective_key} | {row.member_count}/{row.expected_world_size} "
            f"| {row.start_skew_us} | {row.duration_skew_us} | {row.validation_status} |"
        )
    lines.append("")
    return "\n".join(lines)


class _CommunicationLookup:
    def __init__(self, conn: sqlite3.Connection) -> None:
        self.conn = conn
        self.available = bool(
            conn.execute(
                "SELECT 1 FROM sqlite_master WHERE type='table' AND name='COMMUNICATION_OP'"
            ).fetchone()
        )
        self.cache: Dict[Tuple[int, int, int], Tuple[str, str]] = {}

    def find(self, *, device_id: int, source_key: str, start_ns: int, end_ns: int) -> Tuple[str, str]:
        if not self.available:
            return "", ""
        source_start_ns = _parse_source_start_ns(source_key) or start_ns
        cache_key = (device_id, source_start_ns, end_ns)
        if cache_key in self.cache:
            return self.cache[cache_key]
        row = self.conn.execute(
            """
            SELECT connectionId, opId
            FROM COMMUNICATION_OP
            WHERE deviceId = ?
              AND startNs BETWEEN ? AND ?
            ORDER BY ABS(startNs - ?), ABS(endNs - ?)
            LIMIT 1
            """,
            (device_id, source_start_ns - 100000, source_start_ns + 100000, source_start_ns, end_ns),
        ).fetchone()
        if row is None:
            result = ("", "")
        else:
            result = (_string_or_empty(row[0]), _string_or_empty(row[1]))
        self.cache[cache_key] = result
        return result


def _candidate_collective_key(
    *,
    run_name: str,
    pair_id: str,
    occurrence_idx: int,
    op_type: str,
    idx_in_occurrence: int,
) -> str:
    return f"{run_name}:{pair_id}:occ_{occurrence_idx:06d}:{op_type}:idx_{idx_in_occurrence:04d}"


def _normalize_op_type(family: str, label: str) -> str:
    blob = f"{family} {label}".lower()
    if "allreduce" in blob or "all_reduce" in blob:
        return "allReduce"
    if "allgather" in blob or "all_gather" in blob:
        return "allGather"
    if "alltoall" in blob or "all_to_all" in blob or "all2all" in blob:
        return "allToAll"
    if "reducescatter" in blob or "reduce_scatter" in blob:
        return "reduceScatter"
    if "broadcast" in blob:
        return "broadcast"
    compact = re.sub(r"[^a-zA-Z0-9]+", "_", (family or label or "collective")).strip("_")
    return compact or "collective"


def _default_run_name(analysis_dir: Path) -> str:
    if analysis_dir.name == "traceloom":
        raw_dir = analysis_dir.parent
        if raw_dir.name == "msprof_raw":
            return raw_dir.parent.name
        return raw_dir.name
    return analysis_dir.name


def _sanitize_run_name(value: str) -> str:
    value = re.sub(r"[^A-Za-z0-9_.-]+", "_", value.strip())
    return value.strip("_") or "traceloom_run"


def _member_id(db_name: str, db_idx: int, device_id: int) -> str:
    return f"{db_name}:db{db_idx:02d}:dev{device_id}"


def _join_unique(values: Iterable[str]) -> str:
    return " ".join(sorted({str(value) for value in values if str(value)}))


def _parse_source_start_ns(source_key: str) -> int:
    match = re.search(r"start_ns=(\d+)", source_key)
    return int(match.group(1)) if match else 0


def _as_int(value: object) -> int:
    if value is None or value == "":
        return 0
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(value)
    try:
        return int(str(value))
    except (TypeError, ValueError):
        try:
            return int(float(str(value)))
        except (TypeError, ValueError):
            return 0


def _as_float(value: object) -> float:
    if value is None or value == "":
        return 0.0
    try:
        return float(str(value))
    except (TypeError, ValueError):
        return 0.0


def _string_or_empty(value: object) -> str:
    if value is None:
        return ""
    return str(value)
