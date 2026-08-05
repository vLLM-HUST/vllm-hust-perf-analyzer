#!/usr/bin/env python3
"""Verify current TraceLoom folding on the checked-in medium Ascend pair."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sqlite3
import subprocess
import tempfile
from pathlib import Path
from typing import Any


ROOT_ROW = re.compile(r"^(?P<indent> *)(?P<node>N\d+) (?P<label>.+?)\s+\|")
REPEAT = re.compile(r"^Rep x(?P<count>\d+)$")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def root_rows(markdown: str) -> list[tuple[int, str]]:
    root = markdown.partition("## Root")[2]
    fenced = root.partition("```")[2].partition("```")[0]
    rows: list[tuple[int, str]] = []
    for line in fenced.splitlines():
        match = ROOT_ROW.match(line)
        if match:
            rows.append((len(match.group("indent")), match.group("label").strip()))
    return rows


def nested_repeat_pairs(rows: list[tuple[int, str]]) -> set[tuple[int, int]]:
    stack: list[tuple[int, int]] = []
    pairs: set[tuple[int, int]] = set()
    for indent, label in rows:
        while stack and stack[-1][0] >= indent:
            stack.pop()
        repeat = REPEAT.match(label)
        if repeat:
            count = int(repeat.group("count"))
            pairs.update((ancestor, count) for _depth, ancestor in stack)
            stack.append((indent, count))
    return pairs


def select_fields(source: dict[str, Any], expected: dict[str, Any]) -> dict[str, Any]:
    return {key: source[key] for key in expected}


def verify_input(
    path: Path, manifest_path: Path, expected: dict[str, Any]
) -> None:
    assert path.stat().st_size == expected["bytes"]
    assert sha256_file(path) == expected["sha256"]
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    assert manifest["output_bytes"] == expected["bytes"]
    assert manifest["output_sha256"] == expected["sha256"]
    assert manifest["omitted_row_content"] == ["HOST_INFO"]
    assert manifest["copied_rows"]["TASK_PMU_INFO"] > 0
    with sqlite3.connect(path) as db:
        assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
        assert db.execute("SELECT count(*) FROM HOST_INFO").fetchone()[0] == 0
        assert db.execute("SELECT count(*) FROM TASK_PMU_INFO").fetchone()[0] > 0
        suspicious = db.execute(
            "SELECT value FROM STRING_IDS "
            "WHERE (value LIKE '%/%' AND value <> 'N/A') "
            "OR value LIKE '%\\%' OR value LIKE '%@%'"
        ).fetchall()
        assert not suspicious, f"path or identity-like strings retained: {suspicious[:3]}"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--traceloom", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo = Path(__file__).resolve().parents[3]
    fixture = repo / "examples" / "kickstart_smoke"
    expected = json.loads(
        (fixture / "current-expected.json").read_text(encoding="utf-8")
    )
    required_pairs = {tuple(pair) for pair in expected["required_nested_repeats"]}

    total_bytes = 0
    total_events = 0
    total_anchors = 0
    total_nodes = 0
    with tempfile.TemporaryDirectory(prefix="traceloom-kickstart-folding-") as temp:
        temp_root = Path(temp)
        for index, profile in enumerate(expected["profiles"]):
            source = fixture / profile["path"]
            verify_input(source, fixture / profile["manifest"], profile)
            total_bytes += source.stat().st_size
            output = temp_root / f"device{index}"
            output.mkdir()
            report = output / "loop_tree_v2.md"
            result = output / "result.json"
            subprocess.run(
                [
                    str(args.traceloom.resolve()),
                    str(source),
                    "--threads",
                    "2",
                    "--loop-tree-out",
                    str(report),
                    "--out",
                    str(result),
                ],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            observed = json.loads(result.read_text(encoding="utf-8"))
            assert select_fields(observed["stats"], profile["stats"]) == profile["stats"]
            assert (
                select_fields(observed["anchor_projection"], profile["anchor_projection"])
                == profile["anchor_projection"]
            )
            assert (
                observed["stats"]["replay_composition_unrecognized_region_count"]
                == profile["unrecognized_graph_regions"]
            )

            rows = root_rows(report.read_text(encoding="utf-8"))
            assert len(rows) == profile["root_node_rows"]
            assert required_pairs <= nested_repeat_pairs(rows)
            anchor_ratio = profile["stats"]["anchor_count"] / len(rows)
            event_ratio = profile["stats"]["trace_event_count"] / len(rows)
            assert anchor_ratio >= expected["minimum_anchor_to_node_ratio"]
            assert event_ratio >= expected["minimum_event_to_node_ratio"]
            total_events += profile["stats"]["trace_event_count"]
            total_anchors += profile["stats"]["anchor_count"]
            total_nodes += len(rows)
            print(
                f"device{profile['device_id']}: {profile['stats']['trace_event_count']} "
                f"events -> {profile['stats']['anchor_count']} anchors -> "
                f"{len(rows)} nodes; PASS"
            )

    print(
        f"pair: {total_bytes} bytes, {total_events} events -> {total_anchors} "
        f"anchors -> {total_nodes} nodes; PASS"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
