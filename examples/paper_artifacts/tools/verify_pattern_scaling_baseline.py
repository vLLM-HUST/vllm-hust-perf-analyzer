#!/usr/bin/env python3
"""Verify the preregistered pattern-scaling baseline and negative boundary."""

from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path
from typing import Any


STAGES = ("scan_ms", "reduce_ms", "scan_plus_reduce_ms")
HASHES = ("occurrences_sha256", "diagnostics_sha256", "reduced_sha256")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--traceloom", type=Path, required=True)
    return parser.parse_args()


def summary(values: list[float]) -> dict[str, float]:
    return {
        "median": round(statistics.median(values), 6),
        "minimum": min(values),
        "maximum": max(values),
    }


def verify_row(row: dict[str, Any], baseline: dict[str, float]) -> None:
    samples = row["runs"]
    assert len(samples) == 5
    assert row["process_wall_seconds"] == summary(
        [sample["process_wall_seconds"] for sample in samples]
    )
    assert row["peak_rss_kib"] == summary(
        [float(sample["peak_rss_kib"]) for sample in samples]
    )
    for stage in STAGES:
        values = [sample[stage] for sample in samples]
        median = statistics.median(values)
        expected = {
            **summary(values),
            "speedup_vs_1": round(baseline[stage] / median, 3),
            "parallel_efficiency": round(
                baseline[stage] / median / row["threads"], 3
            ),
        }
        assert row["stages_ms"][stage] == expected


def main() -> int:
    _args = parse_args()
    repo = Path(__file__).resolve().parents[3]
    receipt = json.loads(
        (
            repo
            / "examples"
            / "paper_artifacts"
            / "kickstart_performance"
            / "kunpeng920-pattern-scaling-baseline.json"
        ).read_text(encoding="utf-8")
    )
    assert receipt["schema_version"] == "traceloom-pattern-scaling-v1"
    assert receipt["analyzer_tree_clean"] is True
    assert receipt["protocol"]["tokens"] == [100000, 1000000, 4000000]
    assert receipt["protocol"]["threads"] == [1, 2, 4, 8, 16, 32]
    assert receipt["protocol"]["runs_per_point"] == 5
    assert receipt["build"]["type"] == "Release"

    scan8: list[float] = []
    for size in receipt["sizes"]:
        rows = size["thread_results"]
        assert [row["threads"] for row in rows] == [1, 2, 4, 8, 16, 32]
        reference = rows[0]["runs"][0]
        for row in rows:
            for sample in row["runs"]:
                for field in (
                    "candidate_occurrences",
                    "candidate_diagnostics",
                    "reduced_candidates",
                    *HASHES,
                ):
                    assert sample[field] == reference[field]
        baseline = {
            stage: rows[0]["stages_ms"][stage]["median"] for stage in STAGES
        }
        for row in rows:
            verify_row(row, baseline)
        eight = next(row for row in rows if row["threads"] == 8)
        scan8.append(eight["stages_ms"]["scan_ms"]["speedup_vs_1"])
        print(
            f"{size['tokens']} tokens: scan8={scan8[-1]:.3f}x, "
            f"pipeline8={eight['stages_ms']['scan_plus_reduce_ms']['speedup_vs_1']:.3f}x; PASS"
        )

    assert scan8[-1] > scan8[0]
    assert 2.0 <= scan8[-1] < 4.0
    largest = receipt["sizes"][-1]["thread_results"]
    one = largest[0]["stages_ms"]
    eight = next(row for row in largest if row["threads"] == 8)["stages_ms"]
    assert one["reduce_ms"]["median"] > 5 * one["scan_ms"]["median"]
    assert eight["scan_plus_reduce_ms"]["speedup_vs_1"] < 1.25
    print(
        "pattern strong scaling: PARTIAL; preregistered 4x scan threshold "
        "NOT_REPRODUCED; global reduce dominates; receipt PASS"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
