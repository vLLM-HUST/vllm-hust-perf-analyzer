#!/usr/bin/env python3
"""Run the preregistered deterministic pattern strong-scaling protocol."""

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


STAGES = ("scan_ms", "reduce_ms", "scan_plus_reduce_ms")
HASHES = ("occurrences_sha256", "diagnostics_sha256", "reduced_sha256")


def integer_tuple(value: str, name: str) -> tuple[int, ...]:
    try:
        result = tuple(int(item) for item in value.split(","))
    except ValueError:
        raise argparse.ArgumentTypeError(f"{name} must be comma-separated integers")
    if not result or any(item <= 0 for item in result) or len(set(result)) != len(result):
        raise argparse.ArgumentTypeError(f"{name} must contain unique positive integers")
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--benchmark", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--tokens", default="100000,1000000,4000000")
    parser.add_argument("--threads", default="1,2,4,8,16,32")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.runs < 5:
        parser.error("--runs must be at least 5")
    args.tokens = integer_tuple(args.tokens, "--tokens")
    args.threads = integer_tuple(args.threads, "--threads")
    if args.threads[0] != 1:
        parser.error("--threads must begin with 1")
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
    with tempfile.TemporaryDirectory(prefix="traceloom-pattern-scaling-") as temp:
        stdout = Path(temp) / "stdout.json"
        stderr = Path(temp) / "stderr.log"
        arguments = [
            str(executable),
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
        exit_code = os.waitstatus_to_exitcode(status)
        if exit_code != 0:
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
    if not executable.is_file():
        raise SystemExit(f"benchmark executable not found: {executable}")
    repo = Path(__file__).resolve().parents[3]
    cache = executable.parent.parent / "CMakeCache.txt"
    build_type = cache_value(cache, "CMAKE_BUILD_TYPE")
    benchmarks_enabled = cache_value(cache, "TRACELOOM_NATIVE_BUILD_BENCHMARKS")
    if build_type != "Release" or benchmarks_enabled != "ON":
        raise SystemExit("protocol requires a Release benchmark-enabled build")

    sizes: list[dict[str, Any]] = []
    for tokens in args.tokens:
        by_thread: dict[int, list[dict[str, Any]]] = defaultdict(list)
        for run_index in range(args.runs):
            schedule = args.threads if run_index % 2 == 0 else args.threads[::-1]
            for threads in schedule:
                by_thread[threads].append(run_once(executable, tokens, threads))

        reference = by_thread[1][0]
        for samples in by_thread.values():
            for sample in samples:
                for field in (
                    "candidate_occurrences",
                    "candidate_diagnostics",
                    "reduced_candidates",
                    *HASHES,
                ):
                    assert sample[field] == reference[field]

        baseline = {
            stage: statistics.median(sample[stage] for sample in by_thread[1])
            for stage in STAGES
        }
        rows: list[dict[str, Any]] = []
        for threads in args.threads:
            samples = by_thread[threads]
            row: dict[str, Any] = {
                "threads": threads,
                "runs": samples,
                "process_wall_seconds": summary(
                    [sample["process_wall_seconds"] for sample in samples]
                ),
                "peak_rss_kib": summary(
                    [float(sample["peak_rss_kib"]) for sample in samples]
                ),
                "stages_ms": {},
            }
            for stage in STAGES:
                values = [sample[stage] for sample in samples]
                median = statistics.median(values)
                row["stages_ms"][stage] = {
                    **summary(values),
                    "speedup_vs_1": round(baseline[stage] / median, 3),
                    "parallel_efficiency": round(
                        baseline[stage] / median / threads, 3
                    ),
                }
            rows.append(row)
        sizes.append(
            {
                "tokens": tokens,
                "partitions": reference["partitions"],
                "protected_intervals": reference["protected_intervals"],
                "candidate_occurrences": reference["candidate_occurrences"],
                "candidate_diagnostics": reference["candidate_diagnostics"],
                "reduced_candidates": reference["reduced_candidates"],
                **{field: reference[field] for field in HASHES},
                "thread_results": rows,
            }
        )

    receipt = {
        "schema_version": "traceloom-pattern-scaling-v1",
        "generated_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "protocol": {
            "tokens": list(args.tokens),
            "threads": list(args.threads),
            "runs_per_point": args.runs,
            "partition_tokens": 4096,
            "halo_tokens": 3,
            "protected_intervals": 8,
            "candidate_lengths": [2, 3],
            "schedule": "balanced forward/reverse thread order",
            "process_model": "fresh process per sample; sequence construction excluded from stage timings",
        },
        "analyzer_commit": git_value(repo, "rev-parse", "HEAD"),
        "analyzer_tree_clean": not bool(git_value(repo, "status", "--short")),
        "build": {
            "type": build_type,
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
