#!/usr/bin/env python3
"""Verify the checkout-bundled exact TP2 reconstruction artifact."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sqlite3
import subprocess
import tempfile
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT_ROW = re.compile(r"^ *N\d+ ")
EVIDENCE_FIELDS = {
    "graph_launch_occurrences": (
        "occurrence_id",
        "match_policy",
        "instance_association_policy",
        "task_source_table",
        "host_api_source_table",
        "host_api_source_row_id",
        "model_execute_source_row_id",
        "notify_wait_source_row_id",
        "notify_record_source_row_id",
        "launch_connection_id",
        "graph_connection_id",
        "captured_graph_instance_id",
        "execute_stream_id",
        "model_stream_id",
        "start_ns",
        "end_ns",
    ),
    "graph_launch_bodies": (
        "graph_launch_body_id",
        "graph_launch_occurrence_id",
        "replay_body_template_id",
        "first_normalized_task_source_row_id",
        "last_normalized_task_source_row_id",
        "compute_task_count",
        "communication_task_count",
        "data_move_task_count",
        "normalized_task_count",
        "stream_count",
    ),
    "graph_launch_body_members": (
        "graph_launch_body_id",
        "source_table",
        "source_row_id",
        "lane_ordinal",
        "task_ordinal",
        "kind",
        "operator",
        "task_type",
        "semantic_role",
        "semantic_rule_id",
        "device_id",
        "stream_id",
        "start_ns",
        "end_ns",
        "duration_ns",
    ),
    "replay_composition_regions": (
        "replay_composition_region_id",
        "region_order",
        "first_launch_id",
        "last_launch_id",
        "start_ns",
        "end_ns",
        "observed_launch_count",
        "expected_launch_count",
        "status",
    ),
    "replay_composition_region_members": (
        "replay_composition_region_id",
        "member_order",
        "graph_launch_occurrence_id",
        "expected_slot_order",
    ),
    "replay_units": (
        "replay_unit_id",
        "graph_template_id",
        "replay_composition_region_id",
        "source_table",
        "device_id",
        "stream_id",
        "start_ns",
        "end_ns",
    ),
    "replay_unit_launch_members": (
        "replay_unit_id",
        "member_order",
        "graph_launch_occurrence_id",
        "replay_composition_slot_id",
        "role",
    ),
    "graph_templates": (
        "graph_template_id",
        "source_table",
        "body_sequence_hash",
        "slot_count",
    ),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--traceloom", type=Path, required=True)
    parser.add_argument("--reference-device2", type=Path)
    parser.add_argument("--reference-device3", type=Path)
    args = parser.parse_args()
    if bool(args.reference_device2) != bool(args.reference_device3):
        parser.error("both reference profile directories must be supplied together")
    return args


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_json(value: object) -> str:
    encoded = json.dumps(
        value, ensure_ascii=False, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def root_observation(markdown: str) -> tuple[int, str]:
    root = markdown.partition("## Root")[2]
    fenced = root.partition("```")[2].partition("```")[0].strip() + "\n"
    count = sum(bool(ROOT_ROW.match(line)) for line in fenced.splitlines())
    return count, hashlib.sha256(fenced.encode("utf-8")).hexdigest()


def exact_evidence_hash(result: dict[str, Any]) -> str:
    canonical = {
        table: [
            [row.get(field) for field in fields]
            for row in result[table]
        ]
        for table, fields in EVIDENCE_FIELDS.items()
    }
    return sha256_json(canonical)


def resolve_rowids(
    database: sqlite3.Connection, table: str, rowids: set[int]
) -> int:
    resolved = 0
    ordered = sorted(rowids)
    for offset in range(0, len(ordered), 900):
        batch = ordered[offset : offset + 900]
        placeholders = ",".join("?" for _ in batch)
        resolved += int(
            database.execute(
                f'SELECT count(*) FROM "{table}" '
                f"WHERE rowid IN ({placeholders})",
                batch,
            ).fetchone()[0]
        )
    return resolved


def verify_input(
    artifact: Path, expected: dict[str, Any]
) -> tuple[Path, Path]:
    database = artifact / expected["database"]
    manifest_path = artifact / expected["manifest"]
    stream_info = database.parent / "host" / "sqlite" / "stream_info.db"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    assert database.stat().st_size == expected["database_bytes"]
    assert sha256_file(database) == expected["database_sha256"]
    assert manifest["output_bytes"] == expected["database_bytes"]
    assert manifest["output_sha256"] == expected["database_sha256"]
    assert manifest["stream_info"]["output_sha256"] == expected[
        "stream_info_sha256"
    ]
    assert sha256_file(stream_info) == expected["stream_info_sha256"]
    assert manifest["omitted_row_content"] == ["HOST_INFO", "TASK_PMU_INFO"]

    with sqlite3.connect(database) as db:
        assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
        assert db.execute("SELECT count(*) FROM HOST_INFO").fetchone()[0] == 0
        assert db.execute("SELECT count(*) FROM TASK_PMU_INFO").fetchone()[0] == 0
        suspicious = db.execute(
            "SELECT value FROM STRING_IDS "
            "WHERE (value LIKE '%/%' AND value <> 'N/A') "
            "OR value LIKE '%\\%' OR value LIKE '%@%'"
        ).fetchall()
        assert not suspicious, f"identity-like strings retained: {suspicious[:3]}"
    with sqlite3.connect(stream_info) as db:
        assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
        assert db.execute("SELECT count(*) FROM CaptureStreamInfo").fetchone()[0] == 148
    return database, stream_info


def run_traceloom(
    executable: Path, source: Path, output: Path, *, with_sidecar: bool
) -> tuple[dict[str, Any], str, Path | None]:
    output.mkdir()
    result = output / "result.json"
    report = output / "loop_tree_v2.md"
    sidecar = output / "sidecar.db" if with_sidecar else None
    command = [
        str(executable.resolve()),
        str(source),
        "--threads",
        "2",
        "--out",
        str(result),
        "--loop-tree-out",
        str(report),
    ]
    if sidecar is not None:
        command.extend(("--compat-db-out", str(sidecar)))
    subprocess.run(
        command,
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return (
        json.loads(result.read_text(encoding="utf-8")),
        report.read_text(encoding="utf-8"),
        sidecar,
    )


def communication_observation(
    sidecar: Path, source: Path, expected: dict[str, Any]
) -> dict[str, Any]:
    with sqlite3.connect(sidecar) as db:
        replay = db.execute(
            "SELECT count(*), sum(observed_launch_count), avg(dur_us), "
            "sum(dur_us) FROM traceloom_aclgraph_reconstruction_region "
            "WHERE status = 'recognized_complete_pattern'"
        ).fetchone()
        positions = db.execute(
            "WITH first_replay AS ("
            "  SELECT min(first_anchor_idx) AS idx "
            "  FROM traceloom_semantic_node WHERE symbol = 'ReplayUnit T1'"
            ") "
            "SELECT node_id, occurrence_count, total_us "
            "FROM traceloom_semantic_node "
            "WHERE symbol = 'AllReduce' AND occurrence_count = 35 "
            "AND last_anchor_idx < (SELECT idx FROM first_replay) "
            "ORDER BY preorder_idx"
        ).fetchall()
        position_ids = [str(row[0]) for row in positions]
        placeholders = ",".join("?" for _ in position_ids)
        source_rows = db.execute(
            "SELECT e.source_table, e.source_key "
            "FROM traceloom_tree_node_anchor AS t "
            "JOIN traceloom_anchor AS a "
            "  ON a.anchor_id = t.anchor_id AND a.db_idx = t.db_idx "
            "  AND a.device_id = t.device_id "
            "JOIN traceloom_event AS e "
            "  ON e.event_id = a.event_id AND e.db_idx = a.db_idx "
            "  AND e.device_id = a.device_id "
            f"WHERE t.node_id IN ({placeholders}) "
            "ORDER BY t.node_id, t.occurrence_idx",
            position_ids,
        ).fetchall()

    assert replay is not None
    assert len(positions) == 8
    assert {int(row[1]) for row in positions} == {35}
    assert len(source_rows) == 280
    assert {str(row[0]) for row in source_rows} == {"COMMUNICATION_OP"}
    source_keys = {int(row[1]) for row in source_rows}
    assert len(source_keys) == 280

    with sqlite3.connect(source) as db:
        ordered = sorted(source_keys)
        resolved = 0
        for offset in range(0, len(ordered), 900):
            batch = ordered[offset : offset + 900]
            raw_placeholders = ",".join("?" for _ in batch)
            resolved += int(
                db.execute(
                    "SELECT count(*) FROM COMMUNICATION_OP "
                    f"WHERE opId IN ({raw_placeholders})",
                    batch,
                ).fetchone()[0]
            )
    assert resolved == len(source_keys)

    observed = {
        "replay_structural_positions": int(replay[0]),
        "replay_launches": int(replay[1]),
        "replay_average_us": f"{float(replay[2]):.3f}",
        "replay_total_us": f"{float(replay[3]):.3f}",
        "allreduce_structural_positions": len(positions),
        "allreduce_occurrences": sum(int(row[1]) for row in positions),
        "allreduce_average_us": (
            f"{sum(float(row[2]) for row in positions) / len(positions):.3f}"
        ),
        "allreduce_total_us": f"{sum(float(row[2]) for row in positions):.3f}",
        "resolved_communication_source_rows": resolved,
    }
    assert observed == expected, json.dumps(observed, indent=2, sort_keys=True)
    return observed


def verify_exact_shape(
    result: dict[str, Any], source: Path, expected: dict[str, Any]
) -> tuple[int, int]:
    occurrences = result["graph_launch_occurrences"]
    bodies = result["graph_launch_bodies"]
    body_members = result["graph_launch_body_members"]
    regions = result["replay_composition_regions"]
    region_members = result["replay_composition_region_members"]
    units = result["replay_units"]
    unit_members = result["replay_unit_launch_members"]
    templates = result["graph_templates"]

    occurrence_ids = {int(row["occurrence_id"]) for row in occurrences}
    assert occurrence_ids == set(range(1110))
    assert Counter(int(row["graph_launch_occurrence_id"]) for row in bodies) == Counter(
        occurrence_ids
    )
    assert len(body_members) == expected["graph_launch_body_member_count"]
    assert {row["source_table"] for row in body_members} == {"TASK"}

    assert len(regions) == 30
    assert {row["status"] for row in regions} == {"recognized_complete_pattern"}
    assert {int(row["observed_launch_count"]) for row in regions} == {37}
    assert Counter(
        int(row["graph_launch_occurrence_id"]) for row in region_members
    ) == Counter(occurrence_ids)

    assert len(units) == 30
    members_by_unit: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for row in unit_members:
        members_by_unit[int(row["replay_unit_id"])].append(row)
    assert set(members_by_unit) == set(range(30))
    expected_roles = ["head", *(["layer"] * 35), "tail"]
    for members in members_by_unit.values():
        ordered = sorted(members, key=lambda row: int(row["member_order"]))
        assert [row["role"] for row in ordered] == expected_roles
    assert Counter(
        int(row["graph_launch_occurrence_id"]) for row in unit_members
    ) == Counter(occurrence_ids)

    assert len(templates) == 1
    assert templates[0]["slot_count"] == 37
    assert templates[0]["body_sequence_hash"] == 5525691127430048629

    host_rowids = {int(row["host_api_source_row_id"]) for row in occurrences}
    task_rowids = {int(row["source_row_id"]) for row in body_members}
    for row in occurrences:
        assert row["host_api_source_table"] == "CANN_API"
        assert row["task_source_table"] == "TASK"
        for field in (
            "model_execute_source_row_id",
            "notify_wait_source_row_id",
            "notify_record_source_row_id",
        ):
            if row.get(field) is not None:
                task_rowids.add(int(row[field]))

    with sqlite3.connect(source) as db:
        resolved_hosts = resolve_rowids(db, "CANN_API", host_rowids)
        resolved_tasks = resolve_rowids(db, "TASK", task_rowids)
    assert resolved_hosts == len(host_rowids) == expected["resolved_host_source_rows"]
    assert resolved_tasks == len(task_rowids) == expected["resolved_task_source_rows"]
    return resolved_hosts, resolved_tasks


def observation(
    result: dict[str, Any],
    report: str,
    source: Path,
    expected: dict[str, Any],
    communication: dict[str, Any] | None = None,
) -> dict[str, Any]:
    stats = result["stats"]
    selected_stats = {field: int(stats[field]) for field in expected["stats"]}
    assert selected_stats == expected["stats"]
    projection = result["anchor_projection"]
    selected_projection = {
        field: int(projection[field]) for field in expected["anchor_projection"]
    }
    assert selected_projection == expected["anchor_projection"]
    resolved_hosts, resolved_tasks = verify_exact_shape(result, source, expected)
    node_count, tree_hash = root_observation(report)
    observed = {
        "stats": selected_stats,
        "anchor_projection": selected_projection,
        "graph_launch_body_member_count": len(result["graph_launch_body_members"]),
        "resolved_host_source_rows": resolved_hosts,
        "resolved_task_source_rows": resolved_tasks,
        "root_node_count": node_count,
        "root_tree_sha256": tree_hash,
        "exact_evidence_sha256": exact_evidence_hash(result),
    }
    if communication is not None:
        observed["communication_localization"] = communication
    frozen = {field: expected[field] for field in observed}
    assert observed == frozen, json.dumps(observed, indent=2, sort_keys=True)
    return observed


def main() -> int:
    args = parse_args()
    repo = Path(__file__).resolve().parents[3]
    artifact = repo / "examples" / "paper_artifacts" / "ascend_tp2_exact"
    expected = json.loads((artifact / "expected.json").read_text(encoding="utf-8"))

    with tempfile.TemporaryDirectory(prefix="traceloom-tp2-exact-") as temp:
        temp_root = Path(temp)
        for rank in ("device2", "device3"):
            profile_expected = expected["profiles"][rank]
            source, _stream_info = verify_input(artifact, profile_expected)
            result, report, sidecar = run_traceloom(
                args.traceloom,
                source,
                temp_root / f"reduced-{rank}",
                with_sidecar=True,
            )
            assert sidecar is not None
            communication = communication_observation(
                sidecar,
                source,
                profile_expected["communication_localization"],
            )
            reduced_observation = observation(
                result, report, source, profile_expected, communication
            )

            reference = getattr(args, f"reference_{rank}")
            if reference is not None:
                reference_source = next(reference.glob("msprof_*.db"))
                reference_result, reference_report, _ = run_traceloom(
                    args.traceloom,
                    reference_source,
                    temp_root / f"reference-{rank}",
                    with_sidecar=False,
                )
                reference_observation = observation(
                    reference_result,
                    reference_report,
                    reference_source,
                    profile_expected,
                )
                assert reference_observation == {
                    field: value
                    for field, value in reduced_observation.items()
                    if field != "communication_localization"
                }
            print(
                f"{rank}: 30 exact H+Lx35+T units, 1110 launches, "
                f"{profile_expected['resolved_host_source_rows']} host + "
                f"{profile_expected['resolved_task_source_rows']} task rows, "
                "8 AllReduce positions / 280 raw communication rows; PASS"
            )

    print("TP2 exact checkout artifact: 2/2 PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
