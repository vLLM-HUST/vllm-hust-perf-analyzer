#!/usr/bin/env python3
"""Verify the checked-in Ascend interleaved-structure paper artifact."""

from __future__ import annotations

import argparse
import hashlib
import json
import sqlite3
import subprocess
import tempfile
from pathlib import Path
from typing import Any


SCOPED_UNITS = ("G1", "U1", "G2", "U2", "G3", "U3", "G4")
UNIT_COLUMNS = (
    "unit_order",
    "unit_id",
    "family_id",
    "kind",
    "run_count",
    "body_fingerprint",
    "anchor_count",
    "start_ns",
    "end_ns",
    "span_us",
    "compute_us",
    "comm_us",
    "idle_us",
    "total_us",
    "aux_events",
    "aux_us",
    "evidence_status",
    "boundary_policy",
    "shape_signature",
)
ANCHOR_COLUMNS = (
    "symbol",
    "role",
    "label",
    "family",
    "start_ns",
    "end_ns",
    "dur_us",
)
EVENT_COLUMNS = (
    "source_table",
    "source_key",
    "stream_id",
    "start_ns",
    "end_ns",
    "dur_us",
    "category",
    "role",
    "semantic_role",
    "semantic_role_reason",
    "symbol",
    "label",
    "raw_label",
    "op_type",
    "compute_task_type",
    "family",
    "task_type",
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def rows_by_ids(
    db: sqlite3.Connection,
    table: str,
    id_column: str,
    identifiers: list[str],
    columns: tuple[str, ...],
) -> dict[str, tuple[Any, ...]]:
    result: dict[str, tuple[Any, ...]] = {}
    selected = ", ".join((id_column, *columns))
    for offset in range(0, len(identifiers), 500):
        chunk = identifiers[offset : offset + 500]
        placeholders = ",".join("?" for _ in chunk)
        for row in db.execute(
            f"SELECT {selected} FROM {table} "
            f"WHERE {id_column} IN ({placeholders})",
            chunk,
        ):
            result[str(row[0])] = tuple(row[1:])
    return result


def stable_units(db: sqlite3.Connection) -> list[dict[str, Any]]:
    placeholders = ",".join("?" for _ in SCOPED_UNITS)
    selected = ", ".join(UNIT_COLUMNS)
    rows = db.execute(
        f"SELECT {selected} FROM traceloom_structural_unit "
        f"WHERE unit_id IN ({placeholders}) ORDER BY unit_order",
        SCOPED_UNITS,
    ).fetchall()
    return [dict(zip(UNIT_COLUMNS, row)) for row in rows]


def membership_hashes(db: sqlite3.Connection) -> dict[str, str]:
    placeholders = ",".join("?" for _ in SCOPED_UNITS)
    memberships = db.execute(
        "SELECT unit_id, anchor_id, anchor_order, membership_role "
        "FROM traceloom_structural_unit_anchor "
        f"WHERE unit_id IN ({placeholders}) ORDER BY unit_id, anchor_order",
        SCOPED_UNITS,
    ).fetchall()
    anchor_ids = [str(row[1]) for row in memberships]
    anchors = rows_by_ids(
        db, "traceloom_anchor", "anchor_id", anchor_ids, ("event_id", *ANCHOR_COLUMNS)
    )
    event_ids = sorted(
        {str(row[0]) for row in anchors.values() if row[0] not in (None, "")}
    )
    events = rows_by_ids(
        db, "traceloom_event", "event_id", event_ids, EVENT_COLUMNS
    )

    sources: dict[str, list[tuple[Any, ...]]] = {event_id: [] for event_id in event_ids}
    for offset in range(0, len(event_ids), 500):
        chunk = event_ids[offset : offset + 500]
        placeholders = ",".join("?" for _ in chunk)
        for row in db.execute(
            "SELECT event_id, source_ordinal, source_table, source_key, "
            "source_role, raw_json FROM traceloom_event_source "
            f"WHERE event_id IN ({placeholders}) "
            "ORDER BY event_id, source_ordinal",
            chunk,
        ):
            sources[str(row[0])].append(tuple(row[1:]))

    members: dict[str, list[dict[str, Any]]] = {
        unit_id: [] for unit_id in SCOPED_UNITS
    }
    for unit_id, anchor_id, anchor_order, membership_role in memberships:
        anchor = anchors[str(anchor_id)]
        event_id = str(anchor[0]) if anchor[0] not in (None, "") else ""
        members[str(unit_id)].append(
            {
                "anchor_order": anchor_order,
                "membership_role": membership_role,
                "anchor": anchor[1:],
                "event": events.get(event_id),
                "sources": sources.get(event_id, []),
            }
        )

    return {
        unit_id: hashlib.sha256(
            json.dumps(
                members[unit_id],
                ensure_ascii=False,
                separators=(",", ":"),
            ).encode("utf-8")
        ).hexdigest()
        for unit_id in SCOPED_UNITS
    }


def execute_sql_script(db: sqlite3.Connection, script: str) -> list[tuple[Any, ...]]:
    statement = ""
    rows: list[tuple[Any, ...]] = []
    for line in script.splitlines(keepends=True):
        statement += line
        if sqlite3.complete_statement(statement):
            cursor = db.execute(statement)
            if cursor.description:
                rows = cursor.fetchall()
            statement = ""
    if statement.strip():
        raise AssertionError("incomplete SQL audit script")
    return rows


def verify_source_artifact(profile_root: Path, manifest: dict[str, Any]) -> Path:
    database = next(profile_root.glob("msprof_*.db"))
    stream_info = profile_root / "host" / "sqlite" / "stream_info.db"
    assert database.stat().st_size < 10 * 1024 * 1024
    assert sha256_file(database) == manifest["output_sha256"]
    assert sha256_file(stream_info) == manifest["stream_info"]["output_sha256"]

    with sqlite3.connect(database) as db:
        assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
        assert db.execute("SELECT count(*) FROM HOST_INFO").fetchone()[0] == 0
        assert db.execute("SELECT count(*) FROM TASK_PMU_INFO").fetchone()[0] == 0
        suspicious = db.execute(
            "SELECT value FROM STRING_IDS "
            "WHERE (value LIKE '%/%' AND value <> 'N/A') "
            "OR value LIKE '%\\%' OR value LIKE '%@%'"
        ).fetchall()
        assert not suspicious, f"path or identity-like strings retained: {suspicious[:3]}"
    with sqlite3.connect(stream_info) as db:
        assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
        assert db.execute("SELECT count(*) FROM CaptureStreamInfo").fetchone()[0] == 10
    return database


def verify_sidecar(
    sidecar: Path,
    source: Path,
    expected: dict[str, Any],
    audit_sql: str,
) -> None:
    with sqlite3.connect(sidecar) as db:
        actual_units = stable_units(db)
        assert actual_units == expected["units"]
        assert membership_hashes(db) == expected["membership_sha256"]
        region_counts = dict(
            db.execute(
                "SELECT status, count(*) "
                "FROM traceloom_aclgraph_reconstruction_region GROUP BY status"
            )
        )
        assert region_counts == {"recognized_complete_pattern": 4}
        audit_rows = execute_sql_script(db, audit_sql)
        assert audit_rows and audit_rows[0][-1] == "PASS"

        db.execute("ATTACH DATABASE ? AS source", (str(source),))
        allowed = {"TASK", "COMMUNICATION_OP", "ACLGRAPH_REPLAY_UNIT"}
        observed = {
            str(row[0])
            for row in db.execute(
                "SELECT DISTINCT source_table FROM traceloom_event_source"
            )
        }
        assert observed <= allowed
        for table in ("TASK", "COMMUNICATION_OP"):
            orphan_count = db.execute(
                "SELECT count(*) FROM traceloom_event_source e "
                f"WHERE e.source_table='{table}' AND NOT EXISTS ("
                f"SELECT 1 FROM source.{table} s "
                "WHERE s.rowid=CAST(e.source_key AS INTEGER))"
            ).fetchone()[0]
            assert orphan_count == 0, f"{table} has {orphan_count} orphan source links"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--traceloom", type=Path, required=True)
    parser.add_argument(
        "--reference-root",
        type=Path,
        help="optional directory containing stock-r0.db and fused-r0.db full sidecars",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo = Path(__file__).resolve().parents[3]
    artifact = repo / "examples" / "paper_artifacts" / "ascend_interleaved"
    expected = json.loads((artifact / "expected.json").read_text(encoding="utf-8"))
    audit_sql = (
        repo / "docs" / "report-sql" / "structural-composition-audit.sql"
    ).read_text(encoding="utf-8")

    total_bytes = 0
    with tempfile.TemporaryDirectory(prefix="traceloom-ascend-artifact-") as temp:
        temp_root = Path(temp)
        for kind in ("stock", "fused"):
            profile = artifact / kind / "PROF_REDUCED"
            manifest = json.loads(
                (artifact / kind / "reduction-manifest.json").read_text(
                    encoding="utf-8"
                )
            )
            source = verify_source_artifact(profile, manifest)
            total_bytes += source.stat().st_size
            output = temp_root / kind
            output.mkdir()
            subprocess.run(
                [
                    str(args.traceloom.resolve()),
                    str(source),
                    "--threads",
                    "2",
                    "--loop-tree-out",
                    str(output / "loop_tree_v2.md"),
                    "--compat-db-out",
                    str(output / "sidecar.db"),
                    "--out",
                    str(output / "result.json"),
                ],
                check=True,
                stdout=subprocess.DEVNULL,
            )
            verify_sidecar(
                output / "sidecar.db", source, expected["profiles"][kind], audit_sql
            )

            if args.reference_root:
                reference_path = args.reference_root / f"{kind}-r0.db"
                with sqlite3.connect(reference_path) as reference:
                    assert stable_units(reference) == expected["profiles"][kind]["units"]
                    assert membership_hashes(reference) == expected["profiles"][kind][
                        "membership_sha256"
                    ]
            print(f"{kind}: PASS")

    assert total_bytes < 20 * 1024 * 1024
    print(f"combined main databases: {total_bytes} bytes; PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
