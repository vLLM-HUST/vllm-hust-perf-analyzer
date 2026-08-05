#!/usr/bin/env python3
"""Run the preregistered partition-local pattern map/reduce campaign."""

from __future__ import annotations

import argparse
import datetime
import json
import os
import platform
import statistics
import subprocess
import tempfile
import time
from collections import defaultdict
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--benchmark", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.runs < 5:
        parser.error("--runs must be at least 5")
    return args


def cache_value(cache: Path, key: str) -> str | None:
    if not cache.is_file():
        return None
    prefix = f"{key}:"
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line.startswith(prefix):
            return line.partition("=")[2]
    return None


def git_value(repo: Path, *arguments: str) -> str:
    return subprocess.run(
        ["git", *arguments], cwd=repo, check=True, text=True, stdout=subprocess.PIPE
    ).stdout.strip()


def cpu_model() -> str:
    output = subprocess.run(
        ["lscpu"], check=True, text=True, stdout=subprocess.PIPE
    ).stdout
    for line in output.splitlines():
        if line.startswith("Model name:"):
            return line.partition(":")[2].strip()
    return platform.processor() or "unknown"


def run_once(executable: Path, tokens: int, threads: int) -> dict[str, Any]:
    with tempfile.TemporaryDirectory(prefix="traceloom-local-reduce-") as temp:
        stdout = Path(temp) / "stdout.json"
        stderr = Path(temp) / "stderr.log"
        arguments = [
            str(executable),
            "--mode",
            "local-reduce",
            "--tokens",
            str(tokens),
            "--threads",
            str(threads),
            "--partition-tokens",
            "4096",
            "--halo-tokens",
            "3",
            "--protected-intervals",
            "8",
        ]
        file_actions = [
            (os.POSIX_SPAWN_OPEN, 1, str(stdout), os.O_WRONLY | os.O_CREAT, 0o644),
            (os.POSIX_SPAWN_OPEN, 2, str(stderr), os.O_WRONLY | os.O_CREAT, 0o644),
        ]
        start = time.perf_counter()
        pid = os.posix_spawn(str(executable), arguments, dict(os.environ), file_actions=file_actions)
        _pid, status, usage = os.wait4(pid, 0)
        wall_seconds = time.perf_counter() - start
        if os.waitstatus_to_exitcode(status) != 0:
            raise RuntimeError(stderr.read_text(encoding="utf-8"))
        sample = json.loads(stdout.read_text(encoding="utf-8"))
        sample["process_wall_seconds"] = round(wall_seconds, 6)
        sample["peak_rss_kib"] = int(usage.ru_maxrss)
        return sample


def summary(values: list[float]) -> dict[str, float]:
    return {
        "median": round(statistics.median(values), 6),
        "minimum": min(values),
        "maximum": max(values),
    }


def main() -> int:
    args = parse_args()
    executable = args.benchmark.resolve()
    repo = Path(__file__).resolve().parents[3]
    cache = executable.parent.parent / "CMakeCache.txt"
    if cache_value(cache, "CMAKE_BUILD_TYPE") != "Release":
        raise SystemExit("protocol requires a Release build")
    baseline = json.loads(
        (
            repo
            / "examples"
            / "paper_artifacts"
            / "kickstart_performance"
            / "kunpeng920-pattern-scaling-baseline.json"
        ).read_text(encoding="utf-8")
    )
    tokens_list = tuple(baseline["protocol"]["tokens"])
    thread_counts = tuple(baseline["protocol"]["threads"])
    baseline_by_tokens = {row["tokens"]: row for row in baseline["sizes"]}

    sizes: list[dict[str, Any]] = []
    for tokens in tokens_list:
        by_thread: dict[int, list[dict[str, Any]]] = defaultdict(list)
        for run_index in range(args.runs):
            schedule = thread_counts if run_index % 2 == 0 else thread_counts[::-1]
            for threads in schedule:
                by_thread[threads].append(run_once(executable, tokens, threads))
        frozen = baseline_by_tokens[tokens]
        reference = by_thread[1][0]
        for samples in by_thread.values():
            for sample in samples:
                assert sample["schema_version"] == "traceloom-pattern-aggregate-sample-v1"
                assert sample["candidate_occurrences"] == frozen["candidate_occurrences"]
                assert sample["candidate_diagnostics"] == frozen["candidate_diagnostics"]
                assert sample["reduced_candidates"] == frozen["reduced_candidates"]
                assert sample["diagnostics_sha256"] == frozen["diagnostics_sha256"]
                assert sample["reduced_sha256"] == frozen["reduced_sha256"]

        baseline_ms = statistics.median(
            sample["map_reduce_ms"] for sample in by_thread[1]
        )
        rows: list[dict[str, Any]] = []
        for threads in thread_counts:
            samples = by_thread[threads]
            values = [sample["map_reduce_ms"] for sample in samples]
            median = statistics.median(values)
            rows.append(
                {
                    "threads": threads,
                    "runs": samples,
                    "map_reduce_ms": {
                        **summary(values),
                        "speedup_vs_1": round(baseline_ms / median, 3),
                        "parallel_efficiency": round(baseline_ms / median / threads, 3),
                    },
                    "process_wall_seconds": summary(
                        [sample["process_wall_seconds"] for sample in samples]
                    ),
                    "peak_rss_kib": summary(
                        [float(sample["peak_rss_kib"]) for sample in samples]
                    ),
                }
            )
        sizes.append(
            {
                "tokens": tokens,
                "partitions": reference["partitions"],
                "candidate_occurrences": reference["candidate_occurrences"],
                "candidate_diagnostics": reference["candidate_diagnostics"],
                "reduced_candidates": reference["reduced_candidates"],
                "diagnostics_sha256": reference["diagnostics_sha256"],
                "reduced_sha256": reference["reduced_sha256"],
                "baseline_global_scan_reduce_1t_ms": baseline_by_tokens[tokens][
                    "thread_results"
                ][0]["stages_ms"]["scan_plus_reduce_ms"]["median"],
                "thread_results": rows,
            }
        )

    receipt = {
        "schema_version": "traceloom-pattern-local-reduce-scaling-v1",
        "generated_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "protocol": {
            "tokens": list(tokens_list),
            "threads": list(thread_counts),
            "runs_per_point": args.runs,
            "partition_tokens": 4096,
            "halo_tokens": 3,
            "protected_intervals": 8,
            "candidate_lengths": [2, 3],
            "schedule": "balanced forward/reverse thread order",
            "mode": "partition-local reduce plus deterministic summary merge",
        },
        "baseline_receipt": "kunpeng920-pattern-scaling-baseline.json",
        "analyzer_commit": git_value(repo, "rev-parse", "HEAD"),
        "analyzer_tree_clean": not bool(git_value(repo, "status", "--short")),
        "build": {
            "type": cache_value(cache, "CMAKE_BUILD_TYPE"),
            "compiler": cache_value(cache, "CMAKE_CXX_COMPILER"),
        },
        "host": {
            "architecture": platform.machine(),
            "cpu_model": cpu_model(),
            "logical_cpus": os.cpu_count(),
            "kernel": platform.release(),
        },
        "sizes": sizes,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(receipt, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
