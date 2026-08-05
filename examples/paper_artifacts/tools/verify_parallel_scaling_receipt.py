#!/usr/bin/env python3
"""Verify the checked Kunpeng thread-scaling receipt and its claim boundary."""

from __future__ import annotations

import argparse
import hashlib
import json
import statistics
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--traceloom", type=Path, required=True)
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def summary(values: list[float]) -> dict[str, float]:
    return {
        "median": round(statistics.median(values), 6),
        "minimum": min(values),
        "maximum": max(values),
    }


def verify_summaries(row: dict[str, Any], baseline: dict[str, float]) -> None:
    samples = row["runs"]
    wall = [sample["wall_seconds"] for sample in samples]
    assert row["wall_seconds"] == summary(wall)
    assert row["wall_speedup_vs_1"] == round(
        baseline["wall_seconds"] / statistics.median(wall), 3
    )
    rss = [float(sample["peak_rss_kib"]) for sample in samples]
    assert row["peak_rss_kib"] == summary(rss)
    for group in ("timing_ms", "pipeline_timing_ms"):
        for field, observed in row[group].items():
            values = [sample[group][field] for sample in samples]
            expected = {
                **summary(values),
                "speedup_vs_1": round(
                    baseline[f"{group}.{field}"] / statistics.median(values), 3
                ),
            }
            assert observed == expected


def main() -> int:
    _args = parse_args()
    repo = Path(__file__).resolve().parents[3]
    fixture = repo / "examples" / "kickstart_smoke"
    expected = json.loads(
        (fixture / "current-expected.json").read_text(encoding="utf-8")
    )
    expected_by_device = {profile["device_id"]: profile for profile in expected["profiles"]}
    receipt = json.loads(
        (
            repo
            / "examples"
            / "paper_artifacts"
            / "kickstart_performance"
            / "kunpeng920-thread-scaling.json"
        ).read_text(encoding="utf-8")
    )

    assert receipt["schema_version"] == "traceloom-thread-scaling-v1"
    assert receipt["analyzer_tree_clean"] is True
    assert receipt["build"]["type"] == "Release"
    assert receipt["runs_per_thread_per_profile"] == 5
    assert receipt["thread_counts"] == [1, 2, 4, 8]
    assert len(receipt["profiles"]) == 2

    for profile in receipt["profiles"]:
        frozen = expected_by_device[profile["device_id"]]
        source = fixture / profile["source_path"]
        assert source.stat().st_size == profile["input_bytes"] == frozen["bytes"]
        assert sha256_file(source) == profile["source_sha256"] == frozen["sha256"]
        assert profile["selected_trace_events"] == frozen["stats"]["trace_event_count"]
        assert profile["semantic_anchors"] == frozen["stats"]["anchor_count"]
        assert profile["rendered_root_nodes"] == frozen["root_node_rows"]

        rows = profile["thread_results"]
        assert [row["threads"] for row in rows] == receipt["thread_counts"]
        assert all(len(row["runs"]) == 5 for row in rows)
        hashes = {
            sample["loop_tree_sha256"]
            for row in rows
            for sample in row["runs"]
        }
        assert hashes == {profile["loop_tree_sha256_all_runs"]}

        one = rows[0]
        baseline: dict[str, float] = {
            "wall_seconds": one["wall_seconds"]["median"]
        }
        for group in ("timing_ms", "pipeline_timing_ms"):
            for field, observed in one[group].items():
                baseline[f"{group}.{field}"] = observed["median"]
        for row in rows:
            verify_summaries(row, baseline)

        eight = rows[-1]
        assert eight["timing_ms"]["load_task_rows_ms"]["speedup_vs_1"] >= 2.5
        assert eight["timing_ms"]["load_ms"]["speedup_vs_1"] >= 1.3
        assert (
            eight["pipeline_timing_ms"]["candidate_scan_map"]["speedup_vs_1"]
            >= 1.2
        )
        assert max(row["wall_speedup_vs_1"] for row in rows) < 1.1
        print(
            f"device{profile['device_id']}: deterministic across 1/2/4/8 threads; "
            f"TASK {eight['timing_ms']['load_task_rows_ms']['speedup_vs_1']:.3f}x, "
            f"end-to-end {eight['wall_speedup_vs_1']:.3f}x at 8 threads; PASS"
        )

    print("parallel-scaling receipt: 2/2 PASS (stage speedup; bounded total claim)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
