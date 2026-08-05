#!/usr/bin/env python3
"""Measure end-to-end TraceLoom analysis cost on the medium checkout pair."""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import platform
import re
import sqlite3
import statistics
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Any


ROOT_ROW = re.compile(r"^ *N\d+ ")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--traceloom", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def table_row_count(database: Path) -> int:
    with sqlite3.connect(database) as db:
        tables = [
            str(row[0])
            for row in db.execute(
                "SELECT name FROM sqlite_master "
                "WHERE type='table' AND name NOT LIKE 'sqlite_%'"
            )
        ]
        total = 0
        for table in tables:
            quoted = table.replace('"', '""')
            total += int(db.execute(f'SELECT count(*) FROM "{quoted}"').fetchone()[0])
        return total


def root_node_count(markdown: Path) -> int:
    text = markdown.read_text(encoding="utf-8")
    root = text.partition("## Root")[2]
    fenced = root.partition("```")[2].partition("```")[0]
    return sum(bool(ROOT_ROW.match(line)) for line in fenced.splitlines())


def run_once(
    executable: Path,
    source: Path,
    output: Path,
    expected: dict[str, Any],
) -> dict[str, Any]:
    output.mkdir()
    report = output / "loop_tree_v2.md"
    result = output / "result.json"
    sidecar = output / "sidecar.db"
    arguments = [
        str(executable),
        str(source),
        "--threads",
        "2",
        "--loop-tree-out",
        str(report),
        "--out",
        str(result),
        "--compat-db-out",
        str(sidecar),
    ]
    file_actions = [
        (os.POSIX_SPAWN_OPEN, 1, os.devnull, os.O_WRONLY, 0o644),
        (os.POSIX_SPAWN_OPEN, 2, os.devnull, os.O_WRONLY, 0o644),
    ]
    environment = dict(os.environ)
    environment["LC_ALL"] = "C"

    start = time.perf_counter()
    pid = os.posix_spawn(
        str(executable), arguments, environment, file_actions=file_actions
    )
    _pid, status, usage = os.wait4(pid, 0)
    elapsed = time.perf_counter() - start
    exit_code = os.waitstatus_to_exitcode(status)
    if exit_code != 0:
        raise RuntimeError(f"TraceLoom exited with status {exit_code}")

    observed = json.loads(result.read_text(encoding="utf-8"))
    stats = observed["stats"]
    for field, value in expected["stats"].items():
        assert stats[field] == value, (field, stats[field], value)
    nodes = root_node_count(report)
    assert nodes == expected["root_node_rows"]

    return {
        "wall_seconds": round(elapsed, 6),
        "peak_rss_kib": int(usage.ru_maxrss),
        "output_bytes": {
            "loop_tree_markdown": report.stat().st_size,
            "result_json": result.stat().st_size,
            "compatibility_sidecar": sidecar.stat().st_size,
        },
        "rendered_root_nodes": nodes,
    }


def cache_value(cache: Path, key: str) -> str | None:
    if not cache.is_file():
        return None
    prefix = f"{key}:"
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line.startswith(prefix):
            return line.partition("=")[2]
    return None


def cpu_model() -> str:
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.is_file():
        for line in cpuinfo.read_text(encoding="utf-8").splitlines():
            if line.lower().startswith("model name"):
                return line.partition(":")[2].strip()
    return platform.processor() or "unknown"


def git_value(repo: Path, *arguments: str) -> str:
    return subprocess.run(
        ["git", *arguments],
        cwd=repo,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    ).stdout.strip()


def compiler_version(compiler: str | None) -> str | None:
    if not compiler:
        return None
    return subprocess.run(
        [compiler, "--version"],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    ).stdout.splitlines()[0]


def main() -> int:
    args = parse_args()
    if args.runs < 5:
        raise SystemExit("--runs must be at least 5 for the paper protocol")
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
            f"paper protocol requires CMAKE_BUILD_TYPE=Release, observed {build_type!r}"
        )

    profiles: list[dict[str, Any]] = []
    with tempfile.TemporaryDirectory(prefix="traceloom-kickstart-benchmark-") as temp:
        temp_root = Path(temp)
        for index, profile in enumerate(expected["profiles"]):
            source = (fixture / profile["path"]).resolve()
            assert source.stat().st_size == profile["bytes"]
            assert sha256_file(source) == profile["sha256"]
            runs = [
                run_once(
                    executable,
                    source,
                    temp_root / f"device{index}-run{run_index + 1}",
                    profile,
                )
                for run_index in range(args.runs)
            ]
            output_shapes = {json.dumps(run["output_bytes"], sort_keys=True) for run in runs}
            assert len(output_shapes) == 1, "output sizes changed between identical runs"
            assert len({run["rendered_root_nodes"] for run in runs}) == 1
            wall = [run["wall_seconds"] for run in runs]
            rss = [run["peak_rss_kib"] for run in runs]
            profiles.append(
                {
                    "device_id": profile["device_id"],
                    "source_path": profile["path"],
                    "source_sha256": profile["sha256"],
                    "input_bytes": profile["bytes"],
                    "raw_sqlite_rows": table_row_count(source),
                    "selected_trace_events": profile["stats"]["trace_event_count"],
                    "semantic_anchors": profile["stats"]["anchor_count"],
                    "rendered_root_nodes": profile["root_node_rows"],
                    "event_to_node_ratio": round(
                        profile["stats"]["trace_event_count"]
                        / profile["root_node_rows"],
                        3,
                    ),
                    "runs": runs,
                    "wall_seconds": {
                        "first": wall[0],
                        "median": round(statistics.median(wall), 6),
                        "minimum": min(wall),
                        "maximum": max(wall),
                    },
                    "peak_rss_kib": {
                        "median": int(statistics.median(rss)),
                        "minimum": min(rss),
                        "maximum": max(rss),
                    },
                    "output_bytes": runs[0]["output_bytes"],
                }
            )

    receipt = {
        "schema_version": "traceloom-kickstart-performance-v1",
        "generated_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "scope": (
            "fresh process per run; warm OS page cache allowed; end-to-end parse, "
            "analysis, Loop Tree, JSON, and compatibility-sidecar materialization"
        ),
        "runs_per_profile": args.runs,
        "threads": 2,
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
            "traceloom INPUT --threads 2 --loop-tree-out TREE "
            "--out RESULT --compat-db-out SIDECAR"
        ),
        "profiles": profiles,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(receipt, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
