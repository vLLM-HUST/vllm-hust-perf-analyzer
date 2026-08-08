#!/usr/bin/env python3
"""Build Coverage/FAR/boundary/confusion/overhead reports for E4 scans.

The input is an experiment manifest.  Each knob point contains one or more
ground-truth cases and either inline predicted gap intervals or a TraceLoom
sidecar SQLite path.  Durations are evaluated by exact interval intersection;
row counts are never used as a proxy for time.  The tool fails closed when a
prediction does not form an exact, non-overlapping partition of every truth
gap.
"""

from __future__ import annotations

import argparse
import csv
import json
import sqlite3
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


UNATTRIBUTED = "unattributed_visible_idle"


@dataclass(frozen=True)
class Interval:
    start_ns: int
    end_ns: int
    category: str

    @property
    def duration_ns(self) -> int:
        return self.end_ns - self.start_ns


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def _load_json(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as stream:
        value = json.load(stream)
    _require(isinstance(value, dict), f"{path}: root must be an object")
    return value


def _truth_gaps(path: Path) -> list[Interval]:
    truth = _load_json(path)
    rows = []
    for raw in truth.get("intervals", []):
        if raw.get("interval_kind") != "visible_productive_idle":
            continue
        category = raw.get("explanation_category")
        _require(isinstance(category, str) and category,
                 f"{path}: every truth gap needs a category")
        rows.append(Interval(int(raw["start_ns"]), int(raw["end_ns"]), category))
    return _validate_truth(rows, str(path))


def _validate_truth(rows: list[Interval], label: str) -> list[Interval]:
    rows = sorted(rows, key=lambda row: (row.start_ns, row.end_ns, row.category))
    for row in rows:
        _require(row.end_ns > row.start_ns, f"{label}: non-positive interval")
    for previous, current in zip(rows, rows[1:]):
        _require(current.start_ns >= previous.end_ns,
                 f"{label}: overlapping truth intervals")
    return rows


def _inline_prediction(raw_rows: Iterable[dict[str, Any]], label: str) -> list[Interval]:
    result = []
    for raw in raw_rows:
        category = raw.get("category")
        _require(isinstance(category, str) and category,
                 f"{label}: prediction category must be non-empty")
        result.append(Interval(int(raw["start_ns"]), int(raw["end_ns"]), category))
    return sorted(result, key=lambda row: (row.start_ns, row.end_ns, row.category))


def _sidecar_prediction(path: Path, run_id: str | None) -> list[Interval]:
    connection = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    try:
        if run_id is None:
            run_ids = [row[0] for row in connection.execute(
                "select distinct run_id from traceloom_idle_explanation order by run_id"
            )]
            _require(len(run_ids) == 1,
                     f"{path}: run_id is required when sidecar has {len(run_ids)} runs")
            run_id = str(run_ids[0])
        rows = connection.execute(
            "select start_ns, end_ns, category "
            "from traceloom_idle_explanation where run_id=? "
            "order by device_id, start_ns, end_ns, category",
            (run_id,),
        ).fetchall()
    finally:
        connection.close()
    return [Interval(int(start), int(end), str(category))
            for start, end, category in rows]


def _overlap(lhs: Interval, rhs: Interval) -> int:
    return max(0, min(lhs.end_ns, rhs.end_ns) - max(lhs.start_ns, rhs.start_ns))


def _assert_prediction_partition(
    truth: list[Interval], prediction: list[Interval], label: str
) -> None:
    for row in prediction:
        _require(row.end_ns > row.start_ns, f"{label}: non-positive prediction")
    for previous, current in zip(prediction, prediction[1:]):
        _require(current.start_ns >= previous.end_ns,
                 f"{label}: predictions overlap")
    truth_total = sum(row.duration_ns for row in truth)
    predicted_inside = sum(
        _overlap(predicted, actual)
        for predicted in prediction
        for actual in truth
    )
    predicted_total = sum(row.duration_ns for row in prediction)
    _require(predicted_inside == truth_total == predicted_total,
             f"{label}: prediction must exactly partition all truth gaps "
             f"(truth={truth_total}, predicted={predicted_total}, "
             f"inside={predicted_inside})")


def _evaluate_point(point: dict[str, Any], manifest_dir: Path) -> dict[str, Any]:
    confusion: dict[tuple[str, str], int] = defaultdict(int)
    truth_by_category: dict[str, int] = defaultdict(int)
    predicted_by_category: dict[str, int] = defaultdict(int)
    total_gap_ns = 0
    explained_ns = 0
    false_attributed_ns = 0

    cases = point.get("cases")
    _require(isinstance(cases, list) and cases,
             f"{point.get('point_id')}: cases must be a non-empty array")
    for case in cases:
        case_id = str(case.get("case_id", "unnamed"))
        truth_path = (manifest_dir / str(case["ground_truth"])).resolve()
        truth = _truth_gaps(truth_path)
        if "prediction" in case:
            prediction = _inline_prediction(case["prediction"], case_id)
        else:
            sidecar_path = (manifest_dir / str(case["sidecar"])).resolve()
            prediction = _sidecar_prediction(sidecar_path, case.get("run_id"))
        _assert_prediction_partition(truth, prediction, case_id)
        total_gap_ns += sum(row.duration_ns for row in truth)
        for actual in truth:
            truth_by_category[actual.category] += actual.duration_ns
            for predicted in prediction:
                duration = _overlap(actual, predicted)
                if duration == 0:
                    continue
                confusion[(actual.category, predicted.category)] += duration
                predicted_by_category[predicted.category] += duration
                if predicted.category != UNATTRIBUTED:
                    explained_ns += duration
                    if predicted.category != actual.category:
                        false_attributed_ns += duration

    matrix = [
        {"truth_category": truth, "predicted_category": predicted,
         "duration_ns": duration}
        for (truth, predicted), duration in sorted(confusion.items())
    ]
    categories = sorted(set(truth_by_category) | set(predicted_by_category))
    boundary = []
    for category in categories:
        matched = confusion.get((category, category), 0)
        boundary.append({
            "category": category,
            "over_attributed_ns": predicted_by_category[category] - matched,
            "under_attributed_ns": truth_by_category[category] - matched,
        })

    coverage = explained_ns / total_gap_ns if total_gap_ns else None
    far = false_attributed_ns / explained_ns if explained_ns else None
    result = {
        "point_id": str(point["point_id"]),
        "evidence_label": str(point.get("evidence_label", "unspecified")),
        "knobs": point.get("knobs", {}),
        "overhead": point.get("overhead", {}),
        "total_gap_ns": total_gap_ns,
        "explained_ns": explained_ns,
        "false_attributed_ns": false_attributed_ns,
        "coverage": coverage,
        "far": far,
        "category_confusion_matrix": matrix,
        "boundary_error": boundary,
    }
    expected = point.get("expected_metrics")
    if expected is not None:
        for key in ("total_gap_ns", "explained_ns", "false_attributed_ns"):
            _require(result[key] == expected[key],
                     f"{point['point_id']}: expected {key}={expected[key]}, "
                     f"got {result[key]}")
        for key in ("coverage", "far"):
            wanted = expected.get(key)
            actual = result[key]
            if wanted is None:
                _require(actual is None,
                         f"{point['point_id']}: expected {key}=null, got {actual}")
            else:
                _require(actual is not None and abs(actual - float(wanted)) < 1e-12,
                         f"{point['point_id']}: expected {key}={wanted}, got {actual}")
    return result


def evaluate(manifest_path: Path) -> dict[str, Any]:
    manifest = _load_json(manifest_path)
    _require(manifest.get("schema_version") == "idle-evidence-evaluation-v1",
             "unsupported manifest schema_version")
    points = manifest.get("points")
    _require(isinstance(points, list) and points, "manifest points must be non-empty")
    point_ids = [str(point.get("point_id", "")) for point in points]
    _require(all(point_ids) and len(point_ids) == len(set(point_ids)),
             "point_id values must be non-empty and unique")
    return {
        "schema_version": "idle-evidence-evaluation-report-v1",
        "experiment_id": manifest.get("experiment_id"),
        "evidence_label": manifest.get("evidence_label", "unspecified"),
        "metric_definitions": {
            "coverage": "predicted non-unattributed duration / truth visible-gap duration",
            "far": "wrongly categorized predicted non-unattributed duration / predicted non-unattributed duration",
            "boundary_over": "predicted category duration outside same truth category",
            "boundary_under": "truth category duration outside same predicted category",
        },
        "points": [_evaluate_point(point, manifest_path.parent) for point in points],
    }


def _percent(value: float | None) -> str:
    return "NA" if value is None else f"{100.0 * value:.6f}"


def write_csv(report: dict[str, Any], path: Path) -> None:
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow([
            "point_id", "evidence_label", "epsilon_multiplier",
            "minimum_evidence_duration_ns", "marker_density",
            "instrumentation_level", "total_gap_ns", "explained_ns",
            "false_attributed_ns", "coverage_pct", "far_pct",
            "throughput_delta_pct", "ttft_p50_delta_pct",
            "ttft_p95_delta_pct", "itl_p50_delta_pct", "itl_p95_delta_pct",
            "iteration_p50_delta_pct", "iteration_p95_delta_pct",
            "device_productive_time_delta_pct", "marker_count",
        ])
        for point in report["points"]:
            knobs = point["knobs"]
            overhead = point["overhead"]
            writer.writerow([
                point["point_id"], point["evidence_label"],
                knobs.get("epsilon_multiplier"),
                knobs.get("minimum_evidence_duration_ns"),
                knobs.get("marker_density"), knobs.get("instrumentation_level"),
                point["total_gap_ns"], point["explained_ns"],
                point["false_attributed_ns"], _percent(point["coverage"]),
                _percent(point["far"]), overhead.get("throughput_delta_pct"),
                overhead.get("ttft_p50_delta_pct"),
                overhead.get("ttft_p95_delta_pct"),
                overhead.get("itl_p50_delta_pct"),
                overhead.get("itl_p95_delta_pct"),
                overhead.get("iteration_p50_delta_pct"),
                overhead.get("iteration_p95_delta_pct"),
                overhead.get("device_productive_time_delta_pct"),
                overhead.get("marker_count"),
            ])


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines = [
        "# Idle-evidence evaluation",
        "",
        f"- experiment_id: `{report.get('experiment_id')}`",
        f"- evidence_label: `{report.get('evidence_label')}`",
        "",
        "| point | epsilon× | min evidence ns | marker density | instrumentation | Coverage % | FAR % | throughput Δ% | TTFT p95 Δ% | ITL p95 Δ% | iteration p95 Δ% | device productive Δ% | markers |",
        "|---|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for point in report["points"]:
        knobs = point["knobs"]
        overhead = point["overhead"]
        lines.append(
            f"| {point['point_id']} | {knobs.get('epsilon_multiplier', '')} | "
            f"{knobs.get('minimum_evidence_duration_ns', '')} | "
            f"{knobs.get('marker_density', '')} | "
            f"{knobs.get('instrumentation_level', '')} | "
            f"{_percent(point['coverage'])} | {_percent(point['far'])} | "
            f"{overhead.get('throughput_delta_pct', '')} | "
            f"{overhead.get('ttft_p95_delta_pct', '')} | "
            f"{overhead.get('itl_p95_delta_pct', '')} | "
            f"{overhead.get('iteration_p95_delta_pct', '')} | "
            f"{overhead.get('device_productive_time_delta_pct', '')} | "
            f"{overhead.get('marker_count', '')} |"
        )
    for point in report["points"]:
        lines.extend(["", f"## {point['point_id']} confusion matrix", "",
                      "| truth | predicted | duration ns |",
                      "|---|---|---:|"])
        for row in point["category_confusion_matrix"]:
            lines.append(f"| {row['truth_category']} | {row['predicted_category']} | {row['duration_ns']} |")
        lines.extend(["", "| category | over-attributed ns | under-attributed ns |",
                      "|---|---:|---:|"])
        for row in point["boundary_error"]:
            lines.append(f"| {row['category']} | {row['over_attributed_ns']} | {row['under_attributed_ns']} |")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--csv", type=Path)
    parser.add_argument("--markdown", type=Path)
    args = parser.parse_args()
    report = evaluate(args.manifest.resolve())
    if args.json:
        args.json.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                             encoding="utf-8")
    if args.csv:
        write_csv(report, args.csv)
    if args.markdown:
        write_markdown(report, args.markdown)
    if not any((args.json, args.csv, args.markdown)):
        print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
