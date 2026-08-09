#!/usr/bin/env python3
"""Verify the checkout-bundled CUDA real-model graph artifact."""

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


SENSITIVE_TEXT = re.compile(
    r"(?i)(?:https?://[^/\s:@]+:[^@\s/]+@|"
    r"(?:password|passwd|secret|token|api[_-]?key)\s*[:=]|"
    r"/(?:home|root)/)"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--traceloom", type=Path, required=True)
    parser.add_argument("--reference-dir", type=Path)
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def canonical_observation(result: dict[str, Any]) -> bytes:
    result = dict(result)
    for field in ("source", "memory", "timing_ms", "threads"):
        result.pop(field, None)
    return (
        json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n"
    ).encode("utf-8")


def canonical_loop_tree(path: Path) -> bytes:
    lines = path.read_text(encoding="utf-8").splitlines()
    normalized = [
        "- source_db: `<artifact>`" if line.startswith("- source_db:") else line
        for line in lines
    ]
    return ("\n".join(normalized) + "\n").encode("utf-8")


def verify_reduced_input(artifact: Path, spec: dict[str, Any]) -> Path:
    database = artifact / spec["database"]
    manifest = json.loads(
        (artifact / spec["reduction_manifest"]).read_text(encoding="utf-8")
    )
    assert database.stat().st_size == manifest["output_bytes"]
    assert sha256_file(database) == manifest["output_sha256"]
    assert manifest["format"] == "traceloom-cuda-nsys-reduction-v1"
    assert manifest["source_rowid_policy"] == "preserved"
    assert manifest["output_bytes"] <= 10 * 1024 * 1024

    with sqlite3.connect(database) as db:
        assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
        tables = {
            str(row[0])
            for row in db.execute(
                "SELECT name FROM sqlite_master WHERE type='table'"
            )
        }
        assert tables == set(manifest["retained_tables"])
        for table, counts in manifest["row_counts"].items():
            observed = int(db.execute(f'SELECT count(*) FROM "{table}"').fetchone()[0])
            assert observed == counts["retained"]
            if table != "StringIds":
                assert counts["retained"] == counts["source"]
        strings = [str(row[0]) for row in db.execute("SELECT value FROM StringIds")]
        assert not [value for value in strings if SENSITIVE_TEXT.search(value)]
    return database


def run_analysis(
    executable: Path,
    database: Path,
    output: Path,
    label: str,
    sidecar: bool,
) -> tuple[dict[str, Any], Path, Path | None]:
    output.mkdir()
    result_path = output / "result.json"
    loop_path = output / "loop.md"
    command = [
        str(executable.resolve()),
        "--source-db",
        str(database.resolve()),
        "--source-kind",
        "cuda_nsys_sqlite",
        "--threads",
        "2",
        "--out",
        str(result_path),
        "--loop-tree-out",
        str(loop_path),
        "--loop-tree-db-label",
        label,
    ]
    sidecar_path: Path | None = None
    if sidecar:
        sidecar_path = output / "sidecar.db"
        command.extend(("--compat-db-out", str(sidecar_path)))
    subprocess.run(command, check=True, capture_output=True, text=True)
    return (
        json.loads(result_path.read_text(encoding="utf-8")),
        loop_path,
        sidecar_path,
    )


def template_sequence(result: dict[str, Any]) -> list[int]:
    return [
        int(row["graph_template_id"])
        for row in sorted(
            result["replay_units"], key=lambda row: int(row["start_ns"])
        )
    ]


def stable_node_summary(result: dict[str, Any]) -> dict[str, Any]:
    stats = result["stats"]
    template = result["replay_body_templates"]
    return {
        "launches": stats["graph_launch_occurrence_count"],
        "bodies": stats["graph_launch_body_count"],
        "members": stats["graph_launch_body_member_count"],
        "templates": [
            {
                "hash": row["exact_sequence_hash"],
                "compute": row["compute_task_count"],
                "communication": row["communication_task_count"],
                "data_move": row["data_move_task_count"],
                "members": row["normalized_task_count"],
                "streams": row["stream_count"],
            }
            for row in template
        ],
        "recognized": stats["replay_composition_recognized_region_count"],
        "unknown": stats["replay_composition_unrecognized_region_count"],
        "exact": stats["exact_replay_unit_count"],
        "sequence": template_sequence(result),
        "statuses": [row["status"] for row in result["replay_composition_regions"]],
    }


def verify_provenance(database: Path, result: dict[str, Any]) -> None:
    wanted: dict[str, set[int]] = defaultdict(set)
    per_body: Counter[int] = Counter()
    for member in result["graph_launch_body_members"]:
        wanted[str(member["source_table"])].add(int(member["source_row_id"]))
        per_body[int(member["graph_launch_body_id"])] += 1
    assert len(wanted) == 2
    assert set(wanted) == {
        "CUPTI_ACTIVITY_KIND_KERNEL",
        "CUPTI_ACTIVITY_KIND_MEMCPY",
    }
    assert sorted(per_body.values()) == [9881] * 5
    with sqlite3.connect(database) as db:
        for table, source_rows in wanted.items():
            existing = {
                int(row[0]) for row in db.execute(f'SELECT rowid FROM "{table}"')
            }
            assert source_rows <= existing


def verify_sidecar(sidecar: Path) -> None:
    with sqlite3.connect(sidecar) as db:
        rows = db.execute(
            "SELECT graph_provider, boundary_policy, identity_policy, "
            "order_policy, shape_policy, status, count(*) "
            "FROM traceloom_aclgraph_reconstruction_region "
            "GROUP BY 1,2,3,4,5,6"
        ).fetchall()
    assert rows == [
        (
            "cuda",
            "direct_observed_graph_launch",
            "cuda_graph_node_set",
            "host_submission_order",
            "single_graph",
            "recognized_complete_pattern",
            5,
        )
    ]


def verify_replay_internal(
    database: Path, result: dict[str, Any], expected: dict[str, Any]
) -> None:
    replay = result["replay_internal_cost_map"]
    receipt = expected["replay_internal_cost_map"]
    selected = {
        "unit_count": len(replay["units"]),
        "resolved_launch_count": int(replay["resolved_launch_count"]),
        "unsupported_launch_count": int(replay["unsupported_launch_count"]),
        "fully_supported_unit_count": int(replay["fully_supported_unit_count"]),
        "partially_supported_unit_count": int(replay["partially_supported_unit_count"]),
        "unsupported_unit_count": int(replay["unsupported_unit_count"]),
        "member_count": len(replay["members"]),
        "aligned_aggregate_count": len(replay["aligned_aggregates"]),
        "aggregation_scope": sorted(
            {row["aggregation_scope"] for row in replay["aligned_aggregates"]}
        ),
    }
    assert selected == receipt, json.dumps(selected, indent=2, sort_keys=True)
    assert replay["issues"] == []
    assert all(
        unit["supported"]
        and unit["resolved_launch_count"] == unit["launch_member_count"]
        and unit["launch_member_count"] == 1
        for unit in replay["units"]
    )
    assert all(
        row["aggregation_scope"] == "role_collapsed"
        and row["distribution_supported"]
        and row["scheduled_work_share_supported"]
        for row in replay["aligned_aggregates"]
    )
    assert all(
        member["scheduled_work_share_supported"] for member in replay["members"]
    )
    # Member source provenance: replay members are exactly the JSON body
    # members, whose per-row source rows are resolved into the database below
    # (verify_provenance); the recorded source-table distribution is exact.
    body_members = result["graph_launch_body_members"]
    pairs = {
        (int(member["graph_launch_body_id"]),
         int(member["graph_launch_body_member_id"]))
        for member in replay["members"]
    }
    body_pairs = {
        (int(member["graph_launch_body_id"]),
         int(member["graph_launch_body_member_id"]))
        for member in body_members
    }
    assert pairs == body_pairs
    assert len(pairs) == len(body_members) == len(replay["members"])
    tables = Counter(str(member["source_table"]) for member in replay["members"])
    assert dict(tables) == {
        "CUPTI_ACTIVITY_KIND_KERNEL": 48865,
        "CUPTI_ACTIVITY_KIND_MEMCPY": 540,
    }
    verify_provenance(database, result)


def verify_reference(
    executable: Path,
    artifact: Path,
    expected: dict[str, Any],
    reference: Path,
    temporary: Path,
) -> None:
    reducer = artifact.parent / "tools" / "reduce_cuda_nsys_sqlite.py"
    traces = reference / "traces"
    for key, stem, label in (
        ("node_level", "real_model_graph_node", "cuda-real-model-primary"),
        ("repeat", "real_model_graph_node_repeat", "cuda-real-model-repeat"),
        ("graph_level", "real_model_graph_graph", "cuda-real-model-graph-level"),
    ):
        spec = expected[key]
        source = traces / f"{stem}.sqlite"
        manifest = json.loads(
            (artifact / spec["reduction_manifest"]).read_text(encoding="utf-8")
        )
        assert source.stat().st_size == manifest["source_bytes"]
        assert sha256_file(source) == manifest["source_sha256"]
        rebuilt = temporary / f"rebuilt-{stem}.sqlite"
        rebuilt_manifest = temporary / f"rebuilt-{stem}.json"
        subprocess.run(
            [
                str(reducer),
                str(source),
                str(rebuilt),
                "--manifest",
                str(rebuilt_manifest),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        assert sha256_file(rebuilt) == manifest["output_sha256"]
        full_result, _, _ = run_analysis(
            executable, source, temporary / f"full-{stem}", label, False
        )
        assert sha256_bytes(canonical_observation(full_result)) == spec[
            "canonical_observation_sha256"
        ]


def main() -> int:
    args = parse_args()
    executable = args.traceloom.resolve()
    assert executable.is_file()
    artifact = Path(__file__).resolve().parent.parent / "cuda_real_model_graph"
    expected = json.loads((artifact / "expected.json").read_text(encoding="utf-8"))
    assert sha256_file(artifact / "correctness.json") == expected["correctness_sha256"]
    assert sha256_file(artifact / "replay_oracle.json") == expected[
        "replay_oracle_sha256"
    ]
    correctness = json.loads((artifact / "correctness.json").read_text(encoding="utf-8"))[
        "observation"
    ]
    assert correctness["correct"] and correctness["finite"]
    assert correctness["max_abs_error"] == 0
    assert correctness["mean_abs_error"] == 0
    assert correctness["top10_overlap"] == 10
    assert correctness["eager_tail_sha256_float32"] == correctness[
        "graph_tail_sha256_float32"
    ]

    databases = {
        key: verify_reduced_input(artifact, expected[key])
        for key in ("node_level", "repeat", "graph_level")
    }
    with tempfile.TemporaryDirectory(prefix="traceloom-cuda-real-model-") as root:
        temporary = Path(root)
        results: dict[str, dict[str, Any]] = {}
        loops: dict[str, Path] = {}
        sidecars: dict[str, Path | None] = {}
        for key, label, with_sidecar in (
            ("node_level", "cuda-real-model-primary", True),
            ("repeat", "cuda-real-model-repeat", False),
            ("graph_level", "cuda-real-model-graph-level", False),
        ):
            result, loop, sidecar = run_analysis(
                executable,
                databases[key],
                temporary / key,
                label,
                with_sidecar,
            )
            results[key] = result
            loops[key] = loop
            sidecars[key] = sidecar
            assert sha256_bytes(canonical_observation(result)) == expected[key][
                "canonical_observation_sha256"
            ]
            assert sha256_bytes(canonical_loop_tree(loop)) == expected[key][
                "loop_tree_sha256"
            ]

        primary = results["node_level"]
        verify_replay_internal(
            databases["node_level"], primary, expected["node_level"]
        )
        verify_replay_internal(
            databases["repeat"], results["repeat"], expected["repeat"]
        )

        spec = expected["node_level"]
        stats = primary["stats"]
        assert stats["trace_event_count"] == spec["events"]
        assert stats["anchor_count"] == spec["anchors"]
        assert stats["graph_launch_body_member_count"] == spec["body_members"]
        assert stats["replay_body_template_count"] == spec["body_templates"]
        assert stats["exact_replay_unit_count"] == spec["exact_replay_units"]
        assert stats["replay_composition_recognized_region_count"] == spec[
            "recognized_regions"
        ]
        assert stats["replay_composition_unrecognized_region_count"] == spec[
            "unknown_regions"
        ]
        template = primary["replay_body_templates"][0]
        assert template["exact_sequence_hash"] == spec["template_hash"]
        assert template["normalized_task_count"] == spec["members_per_body"]
        assert template["compute_task_count"] == spec["compute_members_per_body"]
        assert template["data_move_task_count"] == spec[
            "data_move_members_per_body"
        ]
        assert template_sequence(primary) == spec["template_sequence"]
        verify_provenance(databases["node_level"], primary)
        assert sidecars["node_level"] is not None
        verify_sidecar(sidecars["node_level"])

        assert stable_node_summary(results["repeat"]) == stable_node_summary(primary)

        graph_stats = results["graph_level"]["stats"]
        assert graph_stats["replay_unit_count"] == 5
        assert graph_stats["exact_replay_unit_count"] == 0
        assert graph_stats["graph_launch_body_count"] == 0
        assert graph_stats["replay_body_template_count"] == 0

        rerun, rerun_loop, rerun_sidecar = run_analysis(
            executable,
            databases["node_level"],
            temporary / "node-level-rerun",
            "cuda-real-model-primary",
            True,
        )
        assert canonical_observation(rerun) == canonical_observation(primary)
        assert rerun_loop.read_bytes() == loops["node_level"].read_bytes()
        assert rerun_sidecar is not None and sidecars["node_level"] is not None
        assert rerun_sidecar.read_bytes() == sidecars["node_level"].read_bytes()

        if args.reference_dir is not None:
            verify_reference(
                executable,
                artifact,
                expected,
                args.reference_dir.resolve(),
                temporary,
            )

    print(
        "CUDA real-model graph artifact: PASS "
        "(5 exact node-level units, stable 9,881-member template, "
        "graph-level fail-closed)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
