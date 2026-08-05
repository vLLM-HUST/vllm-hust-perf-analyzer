#!/usr/bin/env python3
"""Verify the checkout-bundled mapped-gather perturbation artifact."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sqlite3
import statistics
import subprocess
import tempfile
from pathlib import Path
from typing import Any


ROOT_ROW = re.compile(r"^ *N\d+ ")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--traceloom", type=Path, required=True)
    parser.add_argument("--reference-span", type=Path)
    parser.add_argument("--reference-mapped", type=Path)
    args = parser.parse_args()
    if bool(args.reference_span) != bool(args.reference_mapped):
        parser.error("both reference databases must be supplied together")
    return args


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def scalar(db: sqlite3.Connection, query: str) -> int:
    row = db.execute(query).fetchone()
    assert row is not None
    return int(row[0])


def root_observation(markdown: str) -> tuple[int, str]:
    root = markdown.partition("## Root")[2]
    fenced = root.partition("```")[2].partition("```")[0].strip() + "\n"
    count = sum(bool(ROOT_ROW.match(line)) for line in fenced.splitlines())
    return count, hashlib.sha256(fenced.encode("utf-8")).hexdigest()


def verify_capture_receipt(
    artifact: Path,
    variant: str,
    expected: dict[str, Any],
    contract: dict[str, Any],
) -> None:
    path = artifact / expected["capture_receipt"]
    assert sha256_file(path) == expected["capture_receipt_sha256"]
    receipt = json.loads(path.read_text(encoding="utf-8"))
    serialized = json.dumps(receipt, sort_keys=True)
    for forbidden in (
        "/workspace",
        "host_ptr",
        "device_ptr",
        "owner_pid",
        "requested_path",
        '"handle"',
    ):
        assert forbidden not in serialized
    case = receipt["case"]
    repository = receipt["repositories"]["vllm_ascend"]
    artifacts = receipt["artifacts"]

    assert receipt["variant"] == variant
    assert receipt["correctness"] == "pass"
    assert receipt["device_name"] == contract["device_name"]
    assert receipt["cann"] == contract["cann"]
    for field in (
        "dtype",
        "parts",
        "block_bytes",
        "selected_blocks",
        "span_count",
        "warmup_iterations",
        "measured_iterations",
    ):
        assert case[field] == contract[field]
    assert case["profiled_logical_ops"] == expected["target_task_rows"]
    assert repository == {
        "commit": contract["vllm_ascend_commit"],
        "tree": contract["vllm_ascend_tree"],
        "dirty": False,
    }
    assert artifacts["benchmark_script"]["sha256"] == contract[
        "benchmark_sha256"
    ]
    assert artifacts["capture_script"]["sha256"] == contract[
        "capture_script_sha256"
    ]
    assert artifacts["torch_extension"]["sha256"] == contract[
        "torch_extension_sha256"
    ]
    assert artifacts["opapi_library"]["sha256"] == contract[
        "opapi_library_sha256"
    ]

    wall_ms = [float(value) for value in receipt["measured_wall_ms"]]
    assert len(wall_ms) == contract["measured_iterations"]
    assert f"{statistics.fmean(wall_ms):.6f}" == expected["mean_wall_ms"]
    assert f"{statistics.median(wall_ms):.6f}" == expected["median_wall_ms"]


def verify_input(artifact: Path, expected: dict[str, Any]) -> Path:
    database = artifact / expected["database"]
    manifest = json.loads(
        (artifact / expected["manifest"]).read_text(encoding="utf-8")
    )
    assert database.stat().st_size == expected["database_bytes"]
    assert sha256_file(database) == expected["database_sha256"]
    assert manifest["output_bytes"] == expected["database_bytes"]
    assert manifest["output_sha256"] == expected["database_sha256"]
    assert manifest["source_sha256"] == expected["source_database_sha256"]
    assert manifest["omitted_row_content"] == ["HOST_INFO", "TASK_PMU_INFO"]
    assert manifest["source_rowid_policy"] == "preserved"

    with sqlite3.connect(database) as db:
        assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
        assert scalar(db, "SELECT count(*) FROM HOST_INFO") == 0
        assert scalar(db, "SELECT count(*) FROM TASK_PMU_INFO") == 0
        suspicious = db.execute(
            "SELECT value FROM STRING_IDS "
            "WHERE (value LIKE '%/%' AND value <> 'N/A') "
            "OR value LIKE '%\\%' OR value LIKE '%@%'"
        ).fetchall()
        assert not suspicious, f"identity-like strings retained: {suspicious[:3]}"
    return database


def verify_source_rows(
    variant: str, database: Path, expected: dict[str, Any]
) -> dict[str, int]:
    with sqlite3.connect(database) as db:
        assert scalar(db, "SELECT count(*) FROM TASK") == expected["stats"][
            "task_count"
        ]
        if variant == "span":
            predicate = (
                "FROM TASK AS t JOIN MEMCPY_INFO AS m USING(globalTaskId) "
                "WHERE m.memcpyOperation=1 AND m.size=16384"
            )
            api_name = "MemcpyAsync_Huge"
            assert scalar(db, f"SELECT sum(m.size) {predicate}") == expected[
                "target_submitted_bytes"
            ]
        else:
            predicate = (
                "FROM TASK AS t JOIN COMPUTE_TASK_INFO AS c USING(globalTaskId) "
                "JOIN STRING_IDS AS s ON s.id=c.name "
                "WHERE s.value='KvCacheBlockGather'"
            )
            api_name = "aclnnKvCacheBlockGather"

        observed = {
            "target_task_rows": scalar(db, f"SELECT count(*) {predicate}"),
            "distinct_target_task_rows": scalar(
                db, f"SELECT count(DISTINCT t.rowid) {predicate}"
            ),
            "target_min_task_rowid": scalar(db, f"SELECT min(t.rowid) {predicate}"),
            "target_max_task_rowid": scalar(db, f"SELECT max(t.rowid) {predicate}"),
            "target_api_rows": scalar(
                db,
                "SELECT count(*) FROM CANN_API AS a "
                "JOIN STRING_IDS AS s ON s.id=a.name "
                f"WHERE s.value='{api_name}'",
            ),
            "distinct_target_api_rows": scalar(
                db,
                "SELECT count(DISTINCT a.rowid) FROM CANN_API AS a "
                "JOIN STRING_IDS AS s ON s.id=a.name "
                f"WHERE s.value='{api_name}'",
            ),
            "target_min_api_rowid": scalar(
                db,
                "SELECT min(a.rowid) FROM CANN_API AS a "
                "JOIN STRING_IDS AS s ON s.id=a.name "
                f"WHERE s.value='{api_name}'",
            ),
            "target_max_api_rowid": scalar(
                db,
                "SELECT max(a.rowid) FROM CANN_API AS a "
                "JOIN STRING_IDS AS s ON s.id=a.name "
                f"WHERE s.value='{api_name}'",
            ),
        }
    assert observed["target_task_rows"] == expected["target_task_rows"]
    assert observed["distinct_target_task_rows"] == expected["target_task_rows"]
    assert observed["distinct_target_api_rows"] == expected["target_api_rows"]
    for field in (
        "target_min_task_rowid",
        "target_max_task_rowid",
        "target_api_rows",
        "target_min_api_rowid",
        "target_max_api_rowid",
    ):
        assert observed[field] == expected[field]
    return observed


def run_traceloom(
    executable: Path, source: Path, rules: Path, output: Path
) -> tuple[dict[str, Any], str]:
    output.mkdir()
    result = output / "result.json"
    report = output / "loop_tree_v2.md"
    subprocess.run(
        [
            str(executable.resolve()),
            str(source.resolve()),
            "--threads",
            "2",
            "--extend-classification-rules",
            str(rules.resolve()),
            "--out",
            str(result),
            "--loop-tree-out",
            str(report),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return (
        json.loads(result.read_text(encoding="utf-8")),
        report.read_text(encoding="utf-8"),
    )


def analyzer_observation(
    result: dict[str, Any], report: str, expected: dict[str, Any]
) -> dict[str, Any]:
    stats = {field: int(result["stats"][field]) for field in expected["stats"]}
    assert stats == expected["stats"]
    projection = {
        field: int(result["anchor_projection"][field])
        for field in expected["anchor_projection"]
    }
    assert projection == expected["anchor_projection"]
    coverage = result["semantic_operator_coverage"]
    assert int(coverage["unknown_task_count"]) == 0
    assert int(coverage["unregistered_operator_occurrence_count"]) == 1
    assert [row["operator"] for row in coverage["unregistered_operators"]] == [
        "ZerosLike"
    ]
    assert "KvCacheBlockGather" not in {
        row["operator"] for row in coverage["unregistered_operators"]
    }

    root_node_count, root_hash = root_observation(report)
    repeat = f"Rep x{expected['repeat_count']}"
    assert repeat in report
    assert expected["repeat_symbol"] in report
    observed = {
        "stats": stats,
        "anchor_projection": projection,
        "repeat_count": expected["repeat_count"],
        "repeat_symbol": expected["repeat_symbol"],
        "root_node_count": root_node_count,
        "root_tree_sha256": root_hash,
    }
    frozen = {field: expected[field] for field in observed}
    assert observed == frozen, json.dumps(observed, indent=2, sort_keys=True)
    return observed


def main() -> int:
    args = parse_args()
    repo = Path(__file__).resolve().parents[3]
    artifact = repo / "examples" / "paper_artifacts" / "ascend_mapped_gather"
    expected = json.loads((artifact / "expected.json").read_text(encoding="utf-8"))
    rules = artifact / "target-signal-rules.tsv"
    assert sha256_file(rules) == expected["target_signal_rules_sha256"]

    observations: dict[str, dict[str, Any]] = {}
    with tempfile.TemporaryDirectory(prefix="traceloom-mapped-gather-") as temp:
        temp_root = Path(temp)
        for variant in ("span", "mapped"):
            profile_expected = expected["profiles"][variant]
            verify_capture_receipt(
                artifact, variant, profile_expected, expected["capture_contract"]
            )
            source = verify_input(artifact, profile_expected)
            source_rows = verify_source_rows(variant, source, profile_expected)
            result, report = run_traceloom(
                args.traceloom, source, rules, temp_root / variant
            )
            analyzed = analyzer_observation(result, report, profile_expected)
            observations[variant] = {"source_rows": source_rows, "analysis": analyzed}

            reference = getattr(args, f"reference_{variant}")
            if reference is not None:
                assert sha256_file(reference) == profile_expected[
                    "source_database_sha256"
                ]
                assert verify_source_rows(
                    variant, reference, profile_expected
                ) == source_rows
                reference_result, reference_report = run_traceloom(
                    args.traceloom,
                    reference,
                    rules,
                    temp_root / f"reference-{variant}",
                )
                assert analyzer_observation(
                    reference_result, reference_report, profile_expected
                ) == analyzed

            print(
                f"{variant}: {profile_expected['target_task_rows']} target task "
                f"rows -> Rep x{profile_expected['repeat_count']}; PASS"
            )

    span_count = observations["span"]["source_rows"]["target_task_rows"]
    mapped_count = observations["mapped"]["source_rows"]["target_task_rows"]
    assert span_count == mapped_count * 512
    print("mapped-gather checkout artifact: correct outputs, 512x target delta; PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
