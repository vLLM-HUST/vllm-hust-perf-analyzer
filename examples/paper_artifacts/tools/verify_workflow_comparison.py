#!/usr/bin/env python3
"""Verify the fixed-input workflow-comparison receipt used by the paper."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import sqlite3
import subprocess
import tempfile
from pathlib import Path
from typing import Any


ROOT_ROW = re.compile(r"^ *N\d+ ")
REPEAT_ROW = re.compile(r"^ *N\d+ Rep x\d+ ")


def top_k(database: Path) -> list[list[Any]]:
    with sqlite3.connect(database) as db:
        rows = db.execute(
            "SELECT s.value, count(*), sum(t.endNs-t.startNs) "
            "FROM TASK t "
            "JOIN COMPUTE_TASK_INFO c USING(globalTaskId) "
            "JOIN STRING_IDS s ON s.id=c.name "
            "GROUP BY s.value "
            "ORDER BY sum(t.endNs-t.startNs) DESC, s.value ASC LIMIT 3"
        ).fetchall()
    return [[str(name), int(count), int(duration)] for name, count, duration in rows]


def raw_execute_candidates(database: Path) -> int:
    with sqlite3.connect(database) as db:
        return int(
            db.execute(
                "SELECT count(*) FROM CANN_API c "
                "JOIN STRING_IDS s ON s.id=c.name "
                "WHERE s.value='aclmdlRIExecuteAsync'"
            ).fetchone()[0]
        )


def root_counts(markdown: str) -> tuple[int, int]:
    root = markdown.partition("## Root")[2]
    fenced = root.partition("```")[2].partition("```")[0]
    node_count = 0
    repeat_count = 0
    for line in fenced.splitlines():
        if ROOT_ROW.match(line):
            node_count += 1
        if REPEAT_ROW.match(line):
            repeat_count += 1
    return node_count, repeat_count


def run_traceloom(
    executable: Path,
    source: Path,
    output: Path,
    with_sidecar: bool,
) -> tuple[dict[str, Any], Path | None]:
    output.mkdir()
    report = output / "loop_tree_v2.md"
    result = output / "result.json"
    command = [
        str(executable.resolve()),
        str(source),
        "--threads",
        "2",
        "--loop-tree-out",
        str(report),
        "--out",
        str(result),
    ]
    sidecar: Path | None = None
    if with_sidecar:
        sidecar = output / "sidecar.db"
        command.extend(("--compat-db-out", str(sidecar)))
    subprocess.run(
        command,
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    observed = json.loads(result.read_text(encoding="utf-8"))
    nodes, repeats = root_counts(report.read_text(encoding="utf-8"))
    observed["_root_node_count"] = nodes
    observed["_repeat_node_count"] = repeats
    return observed, sidecar


def source_link_orphans(sidecar: sqlite3.Connection, source: Path) -> int:
    sidecar.execute("ATTACH DATABASE ? AS source", (str(source),))
    total = 0
    for table in ("TASK", "COMMUNICATION_OP"):
        total += int(
            sidecar.execute(
                "SELECT count(*) FROM traceloom_event_source e "
                f"WHERE e.source_table='{table}' AND NOT EXISTS ("
                f"SELECT 1 FROM source.{table} s "
                "WHERE s.rowid=CAST(e.source_key AS INTEGER))"
            ).fetchone()[0]
        )
    return total


def exact_observation(sidecar_path: Path, source: Path) -> dict[str, Any]:
    with sqlite3.connect(sidecar_path) as db:
        units = db.execute(
            "SELECT unit_id, kind, total_us, evidence_status "
            "FROM traceloom_structural_unit ORDER BY unit_order"
        ).fetchall()
        return {
            "exact_graph_unit_count": sum(
                1 for _unit, kind, _cost, evidence in units
                if kind == "graph_unit" and evidence == "exact"
            ),
            "open_boundary_count": sum(
                1 for _unit, kind, _cost, evidence in units
                if kind == "unrecognized" and evidence.startswith("unrecognized_open_")
            ),
            "ordered_units": [str(row[0]) for row in units],
            "graph_total_us": [
                round(float(row[2]), 6) for row in units if row[1] == "graph_unit"
            ],
            "structural_total_us": [
                round(float(row[2]), 6)
                for row in units
                if row[1] == "structural_unit"
            ],
            "source_link_orphan_count": source_link_orphans(db, source),
        }


def repeat_only_observation(result: dict[str, Any]) -> dict[str, int]:
    stats = result["stats"]
    return {
        "anchor_count": int(stats["anchor_count"]),
        "exact_replay_unit_count": int(stats["exact_replay_unit_count"]),
        "unrecognized_region_count": int(
            stats["replay_composition_unrecognized_region_count"]
        ),
        "root_node_count": int(result["_root_node_count"]),
        "repeat_node_count": int(result["_repeat_node_count"]),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--traceloom", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo = Path(__file__).resolve().parents[3]
    artifact = repo / "examples" / "paper_artifacts"
    expected = json.loads(
        (artifact / "workflow_comparison" / "expected.json").read_text(
            encoding="utf-8"
        )
    )

    with tempfile.TemporaryDirectory(prefix="traceloom-workflow-comparison-") as temp:
        temp_root = Path(temp)
        for kind in ("stock", "fused"):
            profile = artifact / "ascend_interleaved" / kind / "PROF_REDUCED"
            source = profile / f"msprof_{kind}.db"
            exact_result, sidecar = run_traceloom(
                args.traceloom, source, temp_root / f"exact-{kind}", True
            )
            assert sidecar is not None

            isolated = temp_root / f"repeat-source-{kind}"
            isolated.mkdir()
            repeat_source = isolated / f"msprof_{kind}.db"
            shutil.copyfile(source, repeat_source)
            repeat_result, _unused = run_traceloom(
                args.traceloom,
                repeat_source,
                temp_root / f"repeat-output-{kind}",
                False,
            )

            observed = {
                "top_k": top_k(source),
                "raw_execute_candidates": raw_execute_candidates(source),
                "repeat_only": repeat_only_observation(repeat_result),
                "traceloom": exact_observation(sidecar, source),
            }
            assert observed == expected["profiles"][kind], json.dumps(
                observed, indent=2, sort_keys=True
            )
            assert exact_result["stats"]["exact_replay_unit_count"] == 4
            print(f"{kind}: PASS")

    stock = expected["profiles"]["stock"]["traceloom"]
    fused = expected["profiles"]["fused"]["traceloom"]
    assert all(
        candidate < baseline
        for baseline, candidate in zip(
            stock["structural_total_us"], fused["structural_total_us"]
        )
    )
    assert all(
        candidate > baseline
        for baseline, candidate in zip(
            stock["graph_total_us"], fused["graph_total_us"]
        )
    )
    print("cost localization receipt: inter-graph units down, graph units up; PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
