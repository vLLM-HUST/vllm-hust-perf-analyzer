"""Reproduce Decode All-to-All Buffer Reuse paper-facing CANN experiment tables."""

from __future__ import annotations

import csv
import json
import shutil
import statistics
import subprocess
import sys
from pathlib import Path
from typing import Any


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT_ROOT = PROJECT_ROOT / "out" / "reproduce"
DEFAULT_BASELINE_TAG = "thesis_20260507_npu3467_baseline_aiv_profile"
DEFAULT_OPTIMIZED_TAG = "thesis_20260507_npu3467_optimized_aiv_profile"


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fields: list[str] = []
    seen: set[str] = set()
    for row in rows:
        for key in row:
            if key not in seen:
                seen.add(key)
                fields.append(key)
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def paper_expected_rows(expected_csv: Path) -> list[dict[str, Any]]:
    if not expected_csv.exists():
        raise FileNotFoundError(f"missing paper key-path table: {expected_csv}")
    rows: list[dict[str, Any]] = []
    for row in read_csv_rows(expected_csv):
        rows.append(
            {
                "metric": row["metric"],
                "unit": row["unit"],
                "baseline": float(row["baseline"]),
                "optimized": float(row["optimized"]),
                "delta_pct": "" if row.get("delta_pct", "") == "" else float(row["delta_pct"]),
            }
        )
    return rows


def as_float(row: dict[str, Any], key: str) -> float:
    return float(row[key])


def pct(new: float, old: float) -> float:
    return (new - old) / old * 100.0


def metric_pair(name: str, unit: str, baseline: float, optimized: float) -> dict[str, Any]:
    return {
        "metric": name,
        "unit": unit,
        "baseline": baseline,
        "optimized": optimized,
        "delta_pct": "" if baseline == 0 else pct(optimized, baseline),
    }


def stats(values: list[float]) -> dict[str, Any]:
    if not values:
        return {}
    mean = statistics.mean(values)
    std = statistics.stdev(values) if len(values) > 1 else 0.0
    return {
        "n": len(values),
        "mean": mean,
        "std": std,
        "cv": std / mean if mean else 0.0,
        "min": min(values),
        "max": max(values),
        "values": values,
    }


def load_ab_runs(reports: Path, prefix: str) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for path in sorted(reports.glob(f"{prefix}_pair*.json")):
        data = read_json(path)
        if data.get("status") != "ok":
            continue
        rows.append(
            {
                "file": path.name,
                "throughput_rps": float(data["request_throughput_rps"]),
                "generate_seconds": float(data["generate_seconds"]),
                "init_seconds": float(data["init_seconds"]),
                "total_seconds": float(data["total_seconds"]),
                "generated_tokens_estimate": int(data.get("generated_tokens_estimate", 0)),
            }
        )
    return rows


def summarize_macro_ab(reports: Path) -> dict[str, Any]:
    baseline = load_ab_runs(reports, "baseline")
    optimized = load_ab_runs(reports, "optimized")
    if not baseline or not optimized:
        raise FileNotFoundError(f"missing valid baseline/optimized A/B reports in {reports}")

    by_pair: dict[str, dict[str, Any]] = {}
    for row in baseline:
        pair = row["file"].removeprefix("baseline_pair").removesuffix(".json")
        by_pair.setdefault(pair, {})["baseline"] = row
    for row in optimized:
        pair = row["file"].removeprefix("optimized_pair").removesuffix(".json")
        by_pair.setdefault(pair, {})["optimized"] = row

    paired: list[dict[str, Any]] = []
    for pair, vals in sorted(by_pair.items(), key=lambda kv: int(kv[0])):
        if "baseline" not in vals or "optimized" not in vals:
            continue
        b = float(vals["baseline"]["throughput_rps"])
        p = float(vals["optimized"]["throughput_rps"])
        paired.append({"pair": int(pair), "baseline": b, "optimized": p, "delta_pct": pct(p, b)})

    b_values = [float(row["throughput_rps"]) for row in baseline]
    p_values = [float(row["throughput_rps"]) for row in optimized]
    b_mean = statistics.mean(b_values)
    p_mean = statistics.mean(p_values)
    return {
        "protocol": {
            "scope": "Decode All-to-All Buffer Reuse under HCCL_OP_EXPANSION_MODE=AIV",
            "pairs": len(paired),
            "warmups_discarded": ["warmup_baseline", "warmup_optimized"],
        },
        "baseline_runs": baseline,
        "optimized_runs": optimized,
        "baseline_throughput_rps": stats(b_values),
        "optimized_throughput_rps": stats(p_values),
        "paired_delta_pct": stats([row["delta_pct"] for row in paired]),
        "comparison": {
            "baseline_mean_rps": b_mean,
            "optimized_mean_rps": p_mean,
            "unpaired_delta_pct": pct(p_mean, b_mean),
        },
        "pairs": paired,
    }


def select_dev6_rank1(profile_dir: Path) -> dict[str, str]:
    rows = read_csv_rows(profile_dir / "device_summary.csv")
    for row in rows:
        if row.get("device_id") == "6" and row.get("global_rank") == "1":
            return row
    for row in rows:
        if row.get("global_rank") == "1":
            return row
    raise RuntimeError(f"no rank1 device row in {profile_dir / 'device_summary.csv'}")


def matching_rows(path: Path, summary_row: dict[str, str]) -> list[dict[str, str]]:
    rows = read_csv_rows(path)
    return [
        row
        for row in rows
        if row.get("db_idx") == summary_row.get("db_idx")
        and row.get("device_id") == summary_row.get("device_id")
        and row.get("global_rank") == summary_row.get("global_rank")
    ]


def root_node(profile_dir: Path, summary_row: dict[str, str]) -> dict[str, str]:
    for row in matching_rows(profile_dir / "compute_anchor_node_metrics.csv", summary_row):
        if row.get("path") == "root":
            return row
    raise RuntimeError(f"no root node row in {profile_dir}")


def top_repeat(profile_dir: Path, summary_row: dict[str, str]) -> dict[str, str] | None:
    repeats = [
        row
        for row in matching_rows(profile_dir / "compute_anchor_node_metrics.csv", summary_row)
        if row.get("type") == "Repeat" and int(float(row.get("anchor_count", 0))) > 0
    ]
    if not repeats:
        return None
    repeats.sort(key=lambda row: as_float(row, "total_us"), reverse=True)
    return repeats[0]


def repeat_cost(row: dict[str, str] | None, key: str) -> float:
    return as_float(row, key) if row is not None else 0.0


def workload_throughput(profile_dir: Path) -> float:
    path = profile_dir / "workload_result.json"
    data = read_json(path)
    return float(data["request_throughput_rps"])


def keypath_metrics(baseline_profile: Path, optimized_profile: Path, macro_ab: dict[str, Any]) -> list[dict[str, Any]]:
    b_summary = select_dev6_rank1(baseline_profile)
    p_summary = select_dev6_rank1(optimized_profile)
    b_root = root_node(baseline_profile, b_summary)
    p_root = root_node(optimized_profile, p_summary)
    b_repeat = top_repeat(baseline_profile, b_summary)
    p_repeat = top_repeat(optimized_profile, p_summary)

    return [
        metric_pair(
            "macro.mean_request_throughput",
            "requests/s",
            float(macro_ab["comparison"]["baseline_mean_rps"]),
            float(macro_ab["comparison"]["optimized_mean_rps"]),
        ),
        {
            "metric": "macro.paired_delta_mean",
            "unit": "percent",
            "baseline": 0.0,
            "optimized": float(macro_ab["paired_delta_pct"]["mean"]),
            "delta_pct": "",
        },
        metric_pair(
            "profile.workload_request_throughput",
            "requests/s under msprof",
            workload_throughput(baseline_profile),
            workload_throughput(optimized_profile),
        ),
        metric_pair("traceloom.dev6.anchor_root.total_us", "us", as_float(b_root, "total_us"), as_float(p_root, "total_us")),
        metric_pair("traceloom.dev6.anchor_root.compute_us", "us", as_float(b_root, "compute_us"), as_float(p_root, "compute_us")),
        metric_pair("traceloom.dev6.anchor_root.comm_us", "us", as_float(b_root, "comm_us"), as_float(p_root, "comm_us")),
        metric_pair("traceloom.dev6.anchor_root.idle_us", "us", as_float(b_root, "idle_us"), as_float(p_root, "idle_us")),
        metric_pair("traceloom.dev6.anchor_root.aux_us", "us", as_float(b_root, "aux_us"), as_float(p_root, "aux_us")),
        metric_pair(
            "traceloom.dev6.anchor_timeline.active_us",
            "us",
            as_float(b_summary, "exec_us") + as_float(b_summary, "data_move_us"),
            as_float(p_summary, "exec_us") + as_float(p_summary, "data_move_us"),
        ),
        metric_pair(
            "traceloom.dev6.anchor_timeline.collective_us",
            "us",
            as_float(b_summary, "used_collective_us"),
            as_float(p_summary, "used_collective_us"),
        ),
        metric_pair(
            "traceloom.dev6.projected_main.total_us",
            "us",
            as_float(b_summary, "used_total_main_us"),
            as_float(p_summary, "used_total_main_us"),
        ),
        metric_pair(
            "traceloom.dev6.projected_main.collective_us",
            "us",
            as_float(b_summary, "used_collective_us"),
            as_float(p_summary, "used_collective_us"),
        ),
        metric_pair(
            "traceloom.dev6.prelude_wait_us",
            "us",
            as_float(b_summary, "prelude_wait_us"),
            as_float(p_summary, "prelude_wait_us"),
        ),
        metric_pair(
            "traceloom.dev6.repeat_x35.active_us",
            "us",
            repeat_cost(b_repeat, "compute_us") + repeat_cost(b_repeat, "comm_us"),
            repeat_cost(p_repeat, "compute_us") + repeat_cost(p_repeat, "comm_us"),
        ),
        metric_pair(
            "traceloom.dev6.repeat_x35.collective_us",
            "us",
            repeat_cost(b_repeat, "comm_us"),
            repeat_cost(p_repeat, "comm_us"),
        ),
        metric_pair(
            "traceloom.dev6.repeat_x35.present",
            "count",
            1.0 if b_repeat is not None else 0.0,
            1.0 if p_repeat is not None else 0.0,
        ),
        metric_pair(
            "traceloom.dev6.anchor_count",
            "count",
            as_float(b_summary, "anchor_event_count"),
            as_float(p_summary, "anchor_event_count"),
        ),
    ]


def write_markdown_summary(path: Path, rows: list[dict[str, Any]]) -> None:
    lines = [
        "# Decode All-to-All Buffer Reuse Key Path Comparison",
        "",
        "| Metric | Unit | Baseline | Decode All-to-All Buffer Reuse | Delta % |",
        "| --- | --- | ---: | ---: | ---: |",
    ]
    for row in rows:
        delta = row["delta_pct"]
        delta_text = "" if delta == "" else f"{float(delta):.4f}"
        lines.append(
            f"| `{row['metric']}` | {row['unit']} | {float(row['baseline']):.6g} | "
            f"{float(row['optimized']):.6g} | {delta_text} |"
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def run_traceloom_analysis(
    run_dir: Path,
    out_dir: Path,
    *,
    top_devices_global: int,
    devices: str,
    max_main_events_per_device: int,
    max_macro_defs: int,
    dry_run: bool,
) -> None:
    command = [
        sys.executable,
        "-m",
        "traceloom",
        "analysis",
        str(run_dir),
        "--out-dir",
        str(out_dir),
        "--top-devices-global",
        str(top_devices_global),
        "--max-main-events-per-device",
        str(max_main_events_per_device),
        "--max-macro-defs",
        str(max_macro_defs),
    ]
    if devices:
        command.extend(["--devices", devices])
    if dry_run:
        print("[dry-run] " + " ".join(command))
        return
    subprocess.run(command, cwd=PROJECT_ROOT, check=True)
    for candidate in (run_dir / "workload_result.json", run_dir / "msprof_raw" / "workload_result.json"):
        if candidate.exists():
            shutil.copy2(candidate, out_dir / "workload_result.json")
            break


def validate_against_expected(rows: list[dict[str, Any]], expected_csv: Path) -> dict[str, Any]:
    if not expected_csv.exists():
        return {"status": "skipped", "reason": f"expected CSV missing: {expected_csv}"}
    expected = {row["metric"]: row for row in read_csv_rows(expected_csv)}
    mismatches = []
    for row in rows:
        exp = expected.get(str(row["metric"]))
        if exp is None:
            continue
        for field in ("baseline", "optimized"):
            actual = float(row[field])
            wanted = float(exp[field])
            if abs(actual - wanted) > max(1e-6, abs(wanted) * 1e-9):
                mismatches.append({"metric": row["metric"], "field": field, "actual": actual, "expected": wanted})
    return {"status": "ok" if not mismatches else "mismatch", "mismatches": mismatches}


def run_decode_a2a_buffer_reuse(
    *,
    source_root: Path,
    out_root: Path = DEFAULT_OUT_ROOT,
    mode: str = "bundle",
    baseline_run_dir: Path | None = None,
    optimized_run_dir: Path | None = None,
    baseline_analysis_dir: Path | None = None,
    optimized_analysis_dir: Path | None = None,
    top_devices_global: int = 0,
    devices: str = "",
    max_main_events_per_device: int = 5000,
    max_macro_defs: int = 32,
    dry_run: bool = False,
) -> Path:
    run_dir = out_root / "decode_a2a_buffer_reuse"
    run_dir.mkdir(parents=True, exist_ok=True)

    macro_reports = source_root / "reports" / "ab_decode_a2a_buffer_reuse_aiv"
    macro_ab = summarize_macro_ab(macro_reports)
    write_json(run_dir / "macro_ab_summary.json", macro_ab)

    expected_csv = source_root / "profiles" / "decode_a2a_buffer_reuse_keypath_comparison.csv"
    if mode == "bundle":
        rows = paper_expected_rows(expected_csv)
        source_kind = "paper_expected_bundle"
    elif mode == "bundle-recomputed":
        baseline_profile = source_root / "profiles" / "baseline_aiv_npu3467"
        optimized_profile = source_root / "profiles" / "optimized_aiv_npu3467"
        rows = keypath_metrics(baseline_profile, optimized_profile, macro_ab)
        source_kind = "bundled_analysis_recomputed"
    elif mode == "existing-analysis":
        if baseline_analysis_dir is None or optimized_analysis_dir is None:
            raise ValueError("existing-analysis mode requires baseline_analysis_dir and optimized_analysis_dir")
        rows = keypath_metrics(baseline_analysis_dir, optimized_analysis_dir, macro_ab)
        source_kind = "existing_traceloom_analysis"
    elif mode == "raw-analysis":
        default_analyzer_out = PROJECT_ROOT.parent / "analyzer" / "out"
        baseline_run = baseline_run_dir or default_analyzer_out / DEFAULT_BASELINE_TAG
        optimized_run = optimized_run_dir or default_analyzer_out / DEFAULT_OPTIMIZED_TAG
        baseline_profile = run_dir / "baseline_analysis"
        optimized_profile = run_dir / "optimized_analysis"
        run_traceloom_analysis(
            baseline_run,
            baseline_profile,
            top_devices_global=top_devices_global,
            devices=devices,
            max_main_events_per_device=max_main_events_per_device,
            max_macro_defs=max_macro_defs,
            dry_run=dry_run,
        )
        run_traceloom_analysis(
            optimized_run,
            optimized_profile,
            top_devices_global=top_devices_global,
            devices=devices,
            max_main_events_per_device=max_main_events_per_device,
            max_macro_defs=max_macro_defs,
            dry_run=dry_run,
        )
        if dry_run:
            write_json(
                run_dir / "reproduce_manifest.json",
                {
                    "status": "dry_run",
                    "mode": mode,
                    "source_root": str(source_root),
                    "baseline_run_dir": str(baseline_run),
                    "optimized_run_dir": str(optimized_run),
                    "out_dir": str(run_dir),
                },
            )
            return run_dir
        rows = keypath_metrics(baseline_profile, optimized_profile, macro_ab)
        source_kind = "raw_profiles_reanalyzed"
    else:
        raise ValueError(f"unknown mode: {mode}")

    comparison_csv = run_dir / "decode_a2a_buffer_reuse_keypath_comparison.csv"
    write_csv(comparison_csv, rows)
    write_markdown_summary(run_dir / "decode_a2a_buffer_reuse_keypath_comparison.md", rows)

    validation = validate_against_expected(rows, expected_csv)
    summary = {
        "status": "complete",
        "mode": mode,
        "source_kind": source_kind,
        "source_root": str(source_root),
        "out_dir": str(run_dir),
        "macro_ab_summary": "macro_ab_summary.json",
        "keypath_comparison_csv": "decode_a2a_buffer_reuse_keypath_comparison.csv",
        "keypath_comparison_md": "decode_a2a_buffer_reuse_keypath_comparison.md",
        "validation": validation,
        "notes": [
            "bundle mode emits the checked paper-facing table stored with the experiment bundle.",
            "bundle-recomputed and raw-analysis rerun the current TraceLoom taxonomy before comparing with the checked table.",
        ],
    }
    write_json(run_dir / "decode_a2a_buffer_reuse_summary.json", summary)
    write_json(run_dir / "reproduce_manifest.json", summary)
    print(f"Decode All-to-All Buffer Reuse paper reproduction artifacts: {run_dir}")
    return run_dir


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(description="Reproduce Decode All-to-All Buffer Reuse CANN paper tables.")
    parser.add_argument(
        "--source-root",
        type=Path,
        default=PROJECT_ROOT.parent / "template-of-thesis" / "experiments-data" / "run_20260507_npu3456",
    )
    parser.add_argument("--out-root", type=Path, default=DEFAULT_OUT_ROOT)
    parser.add_argument("--mode", choices=("bundle", "bundle-recomputed", "existing-analysis", "raw-analysis"), default="bundle")
    parser.add_argument("--baseline-run-dir", type=Path, default=None)
    parser.add_argument("--optimized-run-dir", type=Path, default=None)
    parser.add_argument("--baseline-analysis-dir", type=Path, default=None)
    parser.add_argument("--optimized-analysis-dir", type=Path, default=None)
    parser.add_argument("--top-devices-global", type=int, default=0)
    parser.add_argument("--devices", default="")
    parser.add_argument("--max-main-events-per-device", type=int, default=5000)
    parser.add_argument("--max-macro-defs", type=int, default=32)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    run_decode_a2a_buffer_reuse(
        source_root=args.source_root.resolve(),
        out_root=args.out_root.resolve(),
        mode=args.mode,
        baseline_run_dir=args.baseline_run_dir.resolve() if args.baseline_run_dir else None,
        optimized_run_dir=args.optimized_run_dir.resolve() if args.optimized_run_dir else None,
        baseline_analysis_dir=args.baseline_analysis_dir.resolve() if args.baseline_analysis_dir else None,
        optimized_analysis_dir=args.optimized_analysis_dir.resolve() if args.optimized_analysis_dir else None,
        top_devices_global=args.top_devices_global,
        devices=args.devices,
        max_main_events_per_device=args.max_main_events_per_device,
        max_macro_defs=args.max_macro_defs,
        dry_run=args.dry_run,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
