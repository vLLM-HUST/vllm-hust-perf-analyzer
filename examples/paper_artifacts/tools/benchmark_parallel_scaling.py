#!/usr/bin/env python3
"""Measure deterministic thread scaling on the medium Ascend pair."""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import platform
import re
import statistics
import subprocess
import tempfile
import time
from collections import defaultdict
from pathlib import Path
from typing import Any


ROOT_ROW = re.compile(r"^ *N\d+ ")
TIMING = re.compile(r"^timing ([a-z0-9_]+)=([0-9.eE+-]+)$", re.MULTILINE)
TIMING_FIELDS = (
    "load_task_rows_ms",
    "load_ms",
    "native_pipeline_ms",
    "idle_evidence_pipeline_ms",
    "loop_tree_rows_ms",
)
PIPELINE_TIMING_FIELDS = (
    "build_anchor_tokens",
    "candidate_scan_map",
    "candidate_reduce",
    "materialization",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--traceloom", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--threads", default="1,2,4,8")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.runs < 5:
        parser.error("--runs must be at least 5")
    try:
        args.threads = tuple(int(value) for value in args.threads.split(","))
    except ValueError:
        parser.error("--threads must be a comma-separated integer list")
    if not args.threads or args.threads[0] != 1 or any(
        value <= 0 for value in args.threads
    ):
        parser.error("--threads must begin with 1 and contain positive values")
    if len(set(args.threads)) != len(args.threads):
        parser.error("--threads must not contain duplicates")
    return args


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def root_node_count(markdown: Path) -> int:
    text = markdown.read_text(encoding="utf-8")
    root = text.partition("## Root")[2]
    fenced = root.partition("```")[2].partition("```")[0]
    return sum(bool(ROOT_ROW.match(line)) for line in fenced.splitlines())


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
        ["git", *arguments],
        cwd=repo,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    ).stdout.strip()


def cpu_model() -> str:
    output = subprocess.run(
        ["lscpu"], check=True, text=True, stdout=subprocess.PIPE
    ).stdout
    for line in output.splitlines():
        if line.startswith("Model name:"):
            return line.partition(":")[2].strip()
    return platform.processor() or "unknown"


def compiler_version(compiler: str | None) -> str | None:
    if not compiler:
        return None
    return subprocess.run(
        [compiler, "--version"],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    ).stdout.splitlines()[0]


def run_once(
    executable: Path,
    source: Path,
    output: Path,
    expected: dict[str, Any],
    threads: int,
) -> dict[str, Any]:
    output.mkdir()
    report = output / "loop_tree.md"
    result = output / "result.json"
    stderr = output / "stderr.log"
    arguments = [
        str(executable),
        str(source),
        "--threads",
        str(threads),
        "--out",
        str(result),
        "--loop-tree-out",
        str(report),
        "--timings",
    ]
    file_actions = [
        (os.POSIX_SPAWN_OPEN, 1, os.devnull, os.O_WRONLY, 0o644),
        (os.POSIX_SPAWN_OPEN, 2, str(stderr), os.O_WRONLY | os.O_CREAT, 0o644),
    ]
    environment = dict(os.environ)
    environment["LC_ALL"] = "C"

    start = time.perf_counter()
    pid = os.posix_spawn(
        str(executable), arguments, environment, file_actions=file_actions
    )
    _pid, status, usage = os.wait4(pid, 0)
    wall_seconds = time.perf_counter() - start
    exit_code = os.waitstatus_to_exitcode(status)
    if exit_code != 0:
        raise RuntimeError(f"TraceLoom exited with status {exit_code}")

    observed = json.loads(result.read_text(encoding="utf-8"))
    for field, value in expected["stats"].items():
        assert observed["stats"][field] == value
    assert root_node_count(report) == expected["root_node_rows"]
    timing = {key: float(value) for key, value in TIMING.findall(stderr.read_text())}
    missing = set(TIMING_FIELDS) - set(timing)
    assert not missing, f"missing timing fields: {sorted(missing)}"
    pipeline_timing = observed["timing_ms"]
    missing = set(PIPELINE_TIMING_FIELDS) - set(pipeline_timing)
    assert not missing, f"missing pipeline timing fields: {sorted(missing)}"

    return {
        "wall_seconds": round(wall_seconds, 6),
        "peak_rss_kib": int(usage.ru_maxrss),
        "timing_ms": {field: round(timing[field], 6) for field in TIMING_FIELDS},
        "pipeline_timing_ms": {
            field: round(float(pipeline_timing[field]), 6)
            for field in PIPELINE_TIMING_FIELDS
        },
        "loop_tree_sha256": sha256_file(report),
    }


def summarize(samples: list[float]) -> dict[str, float]:
    return {
        "median": round(statistics.median(samples), 6),
        "minimum": min(samples),
        "maximum": max(samples),
    }


def main() -> int:
    args = parse_args()
    executable = args.traceloom.resolve()
    if not executable.is_file():
        raise SystemExit(f"TraceLoom executable not found: {executable}")

    repo = Path(__file__).resolve().parents[3]
    fixture = repo / "examples" / "kickstart_smoke"
    expected = json.loads(
        (fixture / "current-expected.json").read_text(encoding="utf-8")
    )
    cache = executable.parent.parent / "CMakeCache.txt"
    build_type = cache_value(cache, "CMAKE_BUILD_TYPE")
    compiler = cache_value(cache, "CMAKE_CXX_COMPILER")
    if build_type != "Release":
        raise SystemExit(
            f"paper protocol requires CMAKE_BUILD_TYPE=Release, got {build_type!r}"
        )

    profiles: list[dict[str, Any]] = []
    with tempfile.TemporaryDirectory(prefix="traceloom-thread-scaling-") as temp:
        temp_root = Path(temp)
        for profile_index, profile in enumerate(expected["profiles"]):
            source = (fixture / profile["path"]).resolve()
            assert source.stat().st_size == profile["bytes"]
            assert sha256_file(source) == profile["sha256"]
            by_thread: dict[int, list[dict[str, Any]]] = defaultdict(list)
            for run_index in range(args.runs):
                schedule = args.threads if run_index % 2 == 0 else args.threads[::-1]
                for threads in schedule:
                    sample = run_once(
                        executable,
                        source,
                        temp_root
                        / f"device{profile_index}-run{run_index + 1}-t{threads}",
                        profile,
                        threads,
                    )
                    by_thread[threads].append(sample)

            tree_hashes = {
                sample["loop_tree_sha256"]
                for samples in by_thread.values()
                for sample in samples
            }
            assert len(tree_hashes) == 1, "Loop Tree changed across thread counts"
            baseline = by_thread[1]
            baseline_wall = statistics.median(
                sample["wall_seconds"] for sample in baseline
            )
            baseline_timing = {
                field: statistics.median(
                    sample["timing_ms"][field] for sample in baseline
                )
                for field in TIMING_FIELDS
            }
            baseline_pipeline_timing = {
                field: statistics.median(
                    sample["pipeline_timing_ms"][field] for sample in baseline
                )
                for field in PIPELINE_TIMING_FIELDS
            }
            thread_rows: list[dict[str, Any]] = []
            for threads in args.threads:
                samples = by_thread[threads]
                wall = [sample["wall_seconds"] for sample in samples]
                row = {
                    "threads": threads,
                    "runs": samples,
                    "wall_seconds": summarize(wall),
                    "wall_speedup_vs_1": round(
                        baseline_wall / statistics.median(wall), 3
                    ),
                    "peak_rss_kib": summarize(
                        [float(sample["peak_rss_kib"]) for sample in samples]
                    ),
                    "timing_ms": {},
                    "pipeline_timing_ms": {},
                }
                for field in TIMING_FIELDS:
                    values = [sample["timing_ms"][field] for sample in samples]
                    row["timing_ms"][field] = {
                        **summarize(values),
                        "speedup_vs_1": round(
                            baseline_timing[field] / statistics.median(values), 3
                        ),
                    }
                for field in PIPELINE_TIMING_FIELDS:
                    values = [sample["pipeline_timing_ms"][field] for sample in samples]
                    row["pipeline_timing_ms"][field] = {
                        **summarize(values),
                        "speedup_vs_1": round(
                            baseline_pipeline_timing[field]
                            / statistics.median(values),
                            3,
                        ),
                    }
                thread_rows.append(row)
            profiles.append(
                {
                    "device_id": profile["device_id"],
                    "source_path": profile["path"],
                    "source_sha256": profile["sha256"],
                    "input_bytes": profile["bytes"],
                    "selected_trace_events": profile["stats"]["trace_event_count"],
                    "semantic_anchors": profile["stats"]["anchor_count"],
                    "rendered_root_nodes": profile["root_node_rows"],
                    "loop_tree_sha256_all_runs": next(iter(tree_hashes)),
                    "thread_results": thread_rows,
                }
            )

    receipt = {
        "schema_version": "traceloom-thread-scaling-v1",
        "generated_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "scope": (
            "fresh process per run; balanced forward/reverse thread schedule; warm "
            "OS page cache allowed; ordinary Loop Tree and JSON report path; no "
            "compatibility sidecar"
        ),
        "runs_per_thread_per_profile": args.runs,
        "thread_counts": list(args.threads),
        "analyzer_commit": git_value(repo, "rev-parse", "HEAD"),
        "analyzer_tree_clean": not bool(git_value(repo, "status", "--short")),
        "build": {
            "type": build_type,
            "compiler": compiler,
            "compiler_version": compiler_version(compiler),
        },
        "host": {
            "architecture": platform.machine(),
            "cpu_model": cpu_model(),
            "logical_cpus": os.cpu_count(),
            "kernel": platform.release(),
        },
        "command_template": (
            "traceloom INPUT --threads N --out RESULT --loop-tree-out TREE "
            "--timings"
        ),
        "profiles": profiles,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(receipt, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
