#!/usr/bin/env python3
"""Verify the checkout-bundled preregistered fresh-TP2 negative."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sqlite3
import subprocess
import tempfile
from pathlib import Path
from typing import Any


STAT_FIELDS = (
    "trace_event_count",
    "anchor_count",
    "captured_graph_instance_count",
    "captured_graph_stream_count",
    "graph_launch_occurrence_count",
    "replay_composition_recognized_region_count",
    "replay_composition_unrecognized_region_count",
    "exact_replay_unit_count",
    "replay_unit_launch_member_count",
)
PROJECTION_FIELDS = (
    "device_event_anchors",
    "communication_anchors",
    "preserved_unclassified_task_events",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--traceloom", type=Path, required=True)
    parser.add_argument(
        "--reference-root",
        type=Path,
        help="optional retained exact-tp2-fresh campaign root",
    )
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def resolve_rowids(database: sqlite3.Connection, table: str, rowids: set[int]) -> int:
    resolved = 0
    ordered = sorted(rowids)
    for offset in range(0, len(ordered), 900):
        batch = ordered[offset : offset + 900]
        placeholders = ",".join("?" for _ in batch)
        resolved += int(
            database.execute(
                f'SELECT count(*) FROM "{table}" WHERE rowid IN ({placeholders})',
                batch,
            ).fetchone()[0]
        )
    return resolved


def verify_receipts(artifact: Path, expected: dict[str, Any]) -> None:
    for name, digest in expected["historical_receipts"].items():
        path = artifact / name
        assert sha256_file(path) == digest
        with path.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream, delimiter="\t"))
        assert len(rows) == 6
        assert {row["output_sha256"] for row in rows} == {
            expected["workload_receipt"]["output_text_sha256"]
        }
        assert {row["tokens_sha256"] for row in rows} == {
            expected["workload_receipt"]["token_strings_sha256"]
        }
        assert {row["msprof_rc"] for row in rows} == {"0"}
        if name == "preregistered.tsv":
            assert {row["exact_units"] for row in rows} == {"0"}
            assert {row["analysis_status"] for row in rows} == {"invalid_input"}
            for rank in ("0", "1"):
                fingerprints = {
                    row["topology_sha256"] for row in rows if row["rank"] == rank
                }
                assert len(fingerprints) > 1
        else:
            assert {row["analysis_status"] for row in rows} == {"ok"}
            assert {int(row["exact_units"]) for row in rows} == {27, 28}
            assert sum(row["unknown_regions"] == "1" for row in rows) == 4


def verify_input(profile: Path, frozen: dict[str, Any]) -> Path:
    database = profile / "PROF_REDUCED" / "msprof_fresh.db"
    stream_info = profile / "PROF_REDUCED" / "host" / "sqlite" / "stream_info.db"
    manifest = json.loads(
        (profile / "reduction-manifest.json").read_text(encoding="utf-8")
    )
    assert database.stat().st_size == frozen["database_bytes"]
    assert sha256_file(database) == frozen["database_sha256"]
    assert manifest["output_sha256"] == frozen["database_sha256"]
    assert manifest["source_sha256"] == frozen["raw_database_sha256"]
    assert manifest["stream_info"]["output_sha256"] == frozen["stream_info_sha256"]
    assert sha256_file(stream_info) == frozen["stream_info_sha256"]
    assert manifest["omitted_row_content"] == ["HOST_INFO", "TASK_PMU_INFO"]
    assert manifest["source_rowid_policy"] == "preserved"

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
        assert db.execute("SELECT count(*) FROM CaptureStreamInfo").fetchone()[0] == 153
    return database


def run_traceloom(executable: Path, source: Path, output: Path) -> dict[str, Any]:
    output.mkdir()
    result_path = output / "result.json"
    subprocess.run(
        [
            str(executable.resolve()),
            str(source),
            "--threads",
            "2",
            "--out",
            str(result_path),
            "--loop-tree-out",
            str(output / "loop-tree.md"),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return json.loads(result_path.read_text(encoding="utf-8"))


def observation(result: dict[str, Any], source: Path) -> dict[str, Any]:
    stats = result["stats"]
    projection = result["anchor_projection"]
    bodies = result["replay_body_templates"]
    assert len(bodies) == 1
    unknown = sorted(
        row["status"]
        for row in result["replay_composition_regions"]
        if row["status"] != "recognized_complete_pattern"
    )

    host_rowids = {
        int(row["host_api_source_row_id"])
        for row in result["graph_launch_occurrences"]
    }
    task_rowids = {
        int(row["source_row_id"]) for row in result["graph_launch_body_members"]
    }
    for row in result["graph_launch_occurrences"]:
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
    assert resolved_hosts == len(host_rowids)
    assert resolved_tasks == len(task_rowids)

    body = bodies[0]
    return {
        "stats": [int(stats[field]) for field in STAT_FIELDS],
        "projection": [int(projection[field]) for field in PROJECTION_FIELDS],
        "body": [
            int(body["normalized_task_count"]),
            int(body["stream_count"]),
            int(body["exact_sequence_hash"]),
        ],
        "unknown_statuses": unknown,
        "resolved_host_source_rows": resolved_hosts,
        "resolved_task_source_rows": resolved_tasks,
    }


def frozen_observation(profile: dict[str, Any]) -> dict[str, Any]:
    fields = (
        "stats",
        "projection",
        "body",
        "unknown_statuses",
        "resolved_host_source_rows",
        "resolved_task_source_rows",
    )
    return {field: profile[field] for field in fields}


def discover_references(root: Path, expected: dict[str, Any]) -> dict[str, Path]:
    wanted = {
        profile["raw_database_sha256"]: label
        for label, profile in expected["profiles"].items()
    }
    found: dict[str, Path] = {}
    for path in root.rglob("msprof_*.db"):
        if path.name == "stream_info.db":
            continue
        digest = sha256_file(path)
        label = wanted.get(digest)
        if label is not None:
            found[label] = path
    assert set(found) == set(expected["profiles"]), (
        f"reference root supplied {sorted(found)}, expected "
        f"{sorted(expected['profiles'])}"
    )
    return found


def main() -> int:
    args = parse_args()
    repo = Path(__file__).resolve().parents[3]
    artifact = repo / "examples" / "paper_artifacts" / "ascend_tp2_fresh_negative"
    expected = json.loads((artifact / "expected.json").read_text(encoding="utf-8"))
    assert expected["claim"]["preregistered_outcome"] == "NOT_REPRODUCED"
    verify_receipts(artifact, expected)
    references = (
        discover_references(args.reference_root, expected)
        if args.reference_root is not None
        else {}
    )

    observations: dict[str, dict[str, Any]] = {}
    with tempfile.TemporaryDirectory(prefix="traceloom-tp2-fresh-negative-") as temp:
        temp_root = Path(temp)
        for label, profile in expected["profiles"].items():
            database = verify_input(artifact / label, profile)
            current = observation(
                run_traceloom(args.traceloom, database, temp_root / label), database
            )
            assert current == frozen_observation(profile), json.dumps(
                {"expected": frozen_observation(profile), "observed": current},
                indent=2,
                sort_keys=True,
            )
            observations[label] = current
            if label in references:
                reference = observation(
                    run_traceloom(
                        args.traceloom,
                        references[label],
                        temp_root / f"reference-{label}",
                    ),
                    references[label],
                )
                assert reference == current
            print(
                f"{label}: exact={current['stats'][7]}, "
                f"unknown={current['stats'][6]}, body={current['body'][0]}/"
                f"{current['body'][1]}; PASS"
            )

    claim = expected["claim"]
    assert all(
        row["stats"][7] != claim["expected_exact_units_per_rank"]
        or row["stats"][8] != claim["expected_launch_members_per_rank"]
        or row["stats"][6] != claim["expected_unknown_regions_per_rank"]
        for row in observations.values()
    )
    assert sum(bool(row["unknown_statuses"]) for row in observations.values()) == 4
    assert {tuple(row["body"]) for row in observations.values()} == {
        (9, 2, 16470478170119524675),
        (411, 2, 5933614199419680576),
    }
    print("fresh TP2 preregistered stability: NOT_REPRODUCED; artifact 6/6 PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
