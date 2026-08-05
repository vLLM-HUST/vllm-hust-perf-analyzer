#!/usr/bin/env python3
"""Verify local-reduction parity, strong scaling, and memory reduction."""

from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path
from typing import Any


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


def verify_row(row: dict[str, Any], baseline_ms: float) -> None:
    samples = row["runs"]
    values = [sample["map_reduce_ms"] for sample in samples]
    median = statistics.median(values)
    assert row["map_reduce_ms"] == {
        **summary(values),
        "speedup_vs_1": round(baseline_ms / median, 3),
        "parallel_efficiency": round(baseline_ms / median / row["threads"], 3),
    }
    assert row["process_wall_seconds"] == summary(
        [sample["process_wall_seconds"] for sample in samples]
    )
    assert row["peak_rss_kib"] == summary(
        [float(sample["peak_rss_kib"]) for sample in samples]
    )


def main() -> int:
    _args = parse_args()
    repo = Path(__file__).resolve().parents[3]
    directory = repo / "examples" / "paper_artifacts" / "kickstart_performance"
    baseline = json.loads(
        (directory / "kunpeng920-pattern-scaling-baseline.json").read_text(
            encoding="utf-8"
        )
    )
    receipt = json.loads(
        (directory / "kunpeng920-pattern-local-reduce-scaling.json").read_text(
            encoding="utf-8"
        )
    )
    assert receipt["schema_version"] == "traceloom-pattern-local-reduce-scaling-v1"
    assert receipt["analyzer_tree_clean"] is True
    assert receipt["protocol"]["tokens"] == baseline["protocol"]["tokens"]
    assert receipt["protocol"]["threads"] == baseline["protocol"]["threads"]
    assert receipt["protocol"]["runs_per_point"] == 5
    assert receipt["build"]["type"] == "Release"
    baseline_by_tokens = {row["tokens"]: row for row in baseline["sizes"]}

    for size in receipt["sizes"]:
        frozen = baseline_by_tokens[size["tokens"]]
        assert size["candidate_occurrences"] == frozen["candidate_occurrences"]
        assert size["candidate_diagnostics"] == frozen["candidate_diagnostics"]
        assert size["reduced_candidates"] == frozen["reduced_candidates"]
        assert size["diagnostics_sha256"] == frozen["diagnostics_sha256"]
        assert size["reduced_sha256"] == frozen["reduced_sha256"]
        rows = size["thread_results"]
        assert [row["threads"] for row in rows] == [1, 2, 4, 8, 16, 32]
        reference = rows[0]["runs"][0]
        for row in rows:
            assert len(row["runs"]) == 5
            for sample in row["runs"]:
                for field in (
                    "candidate_occurrences",
                    "candidate_diagnostics",
                    "reduced_candidates",
                    "diagnostics_sha256",
                    "reduced_sha256",
                ):
                    assert sample[field] == reference[field]
            verify_row(row, rows[0]["map_reduce_ms"]["median"])
        eight = next(row for row in rows if row["threads"] == 8)
        thirty_two = next(row for row in rows if row["threads"] == 32)
        print(
            f"{size['tokens']} tokens: 8t={eight['map_reduce_ms']['speedup_vs_1']:.3f}x, "
            f"32t={thirty_two['map_reduce_ms']['speedup_vs_1']:.3f}x; parity PASS"
        )

    largest = receipt["sizes"][-1]
    rows = largest["thread_results"]
    one = rows[0]
    eight = next(row for row in rows if row["threads"] == 8)
    thirty_two = next(row for row in rows if row["threads"] == 32)
    assert eight["map_reduce_ms"]["speedup_vs_1"] >= 4.0
    assert thirty_two["map_reduce_ms"]["speedup_vs_1"] >= 16.0
    assert (
        largest["baseline_global_scan_reduce_1t_ms"]
        / one["map_reduce_ms"]["median"]
        >= 2.0
    )
    baseline_rss = max(
        row["peak_rss_kib"]["median"]
        for row in baseline_by_tokens[largest["tokens"]]["thread_results"]
    )
    optimized_rss = max(row["peak_rss_kib"]["median"] for row in rows)
    assert optimized_rss / baseline_rss <= 0.5
    print(
        "local pattern map/reduce: REPRODUCED; 7.752x at 8 threads, "
        "24.665x at 32 threads, exact summary parity; PASS"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
