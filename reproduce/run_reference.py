#!/usr/bin/env python3
"""Run TraceLoom reference reproduction workflows.

The script writes generated artifacts under ``traceloom/out`` by default.
"""

from __future__ import annotations

import argparse
import json
import shlex
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT_ROOT = PROJECT_ROOT / "out" / "reproduce"


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def strip_separator(args: Sequence[str]) -> list[str]:
    values = list(args)
    if values and values[0] == "--":
        return values[1:]
    return values


def ensure_dir(path: Path) -> Path:
    path.mkdir(parents=True, exist_ok=True)
    return path


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def run_command(command: Sequence[str], *, cwd: Path, dry_run: bool) -> dict[str, Any]:
    command_list = [str(item) for item in command]
    if dry_run:
        print("[dry-run] " + shlex.join(command_list))
        return {"command": command_list, "cwd": str(cwd), "returncode": None, "dry_run": True}

    print("+ " + shlex.join(command_list))
    completed = subprocess.run(command_list, cwd=cwd, check=True)
    return {"command": command_list, "cwd": str(cwd), "returncode": completed.returncode, "dry_run": False}


def analyzer_command(args: argparse.Namespace, profile_dir: Path, analysis_dir: Path) -> list[str]:
    command = [
        sys.executable,
        "-m",
        "traceloom",
        "analysis",
        str(profile_dir),
        "--out-dir",
        str(analysis_dir),
        "--top-devices-global",
        str(args.top_devices_global),
        "--max-main-events-per-device",
        str(args.max_main_events_per_device),
        "--max-macro-defs",
        str(args.max_macro_defs),
    ]
    if args.devices:
        command.extend(["--devices", args.devices])
    if args.kernel_role_file is not None:
        command.extend(["--kernel-role-file", str(args.kernel_role_file)])
    return command


def write_manifest(
    run_dir: Path,
    *,
    name: str,
    backend: str,
    status: str,
    commands: list[dict[str, Any]],
    artifacts: dict[str, str],
    notes: list[str] | None = None,
) -> None:
    payload = {
        "name": name,
        "backend": backend,
        "status": status,
        "generated_at": utc_now(),
        "project_root": str(PROJECT_ROOT),
        "run_dir": str(run_dir),
        "artifacts": artifacts,
        "commands": commands,
        "notes": notes or [],
    }
    write_json(run_dir / "reproduce_manifest.json", payload)
    write_json(run_dir.parent / "latest_manifest.json", payload)


def add_common_analyzer_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--top-devices-global", type=int, default=0)
    parser.add_argument("--devices", default="")
    parser.add_argument("--max-main-events-per-device", type=int, default=5000)
    parser.add_argument("--max-macro-defs", type=int, default=32)
    parser.add_argument("--kernel-role-file", type=Path, default=None)
    parser.add_argument("--dry-run", action="store_true")


def cmd_analyze_msprof(args: argparse.Namespace) -> int:
    profile_dir = args.profile_dir.resolve()
    run_dir = ensure_dir((args.out_root / args.name).resolve())
    analysis_dir = ensure_dir(run_dir / "analysis")

    commands = [
        run_command(analyzer_command(args, profile_dir, analysis_dir), cwd=PROJECT_ROOT, dry_run=args.dry_run)
    ]
    status = "dry_run" if args.dry_run else "complete"
    write_manifest(
        run_dir,
        name=args.name,
        backend="ascend_cann_msprof_existing",
        status=status,
        commands=commands,
        artifacts={
            "profile_dir": str(profile_dir),
            "analysis_dir": str(analysis_dir),
            "manifest": str(run_dir / "reproduce_manifest.json"),
        },
    )
    print(f"TraceLoom artifacts: {analysis_dir}")
    return 0


def cmd_ascend_msprof(args: argparse.Namespace) -> int:
    workload = strip_separator(args.workload)
    if not workload:
        raise SystemExit("ascend-msprof requires a workload command after --")
    if shutil.which("msprof") is None and not args.dry_run:
        raise SystemExit("msprof was not found in PATH. Activate the CANN environment first.")

    run_dir = ensure_dir((args.out_root / args.name).resolve())
    profile_dir = ensure_dir((args.profile_dir or (run_dir / "msprof_raw")).resolve())
    analysis_dir = ensure_dir(run_dir / "analysis")

    workload_text = shlex.join(workload)
    msprof_command = [
        "msprof",
        f"--output={profile_dir}",
        f"--application={workload_text}",
        *args.msprof_arg,
    ]

    commands = [
        run_command(msprof_command, cwd=PROJECT_ROOT, dry_run=args.dry_run),
        run_command(analyzer_command(args, profile_dir, analysis_dir), cwd=PROJECT_ROOT, dry_run=args.dry_run),
    ]
    status = "dry_run" if args.dry_run else "complete"
    write_manifest(
        run_dir,
        name=args.name,
        backend="ascend_cann_msprof",
        status=status,
        commands=commands,
        artifacts={
            "profile_dir": str(profile_dir),
            "analysis_dir": str(analysis_dir),
            "manifest": str(run_dir / "reproduce_manifest.json"),
        },
    )
    print(f"TraceLoom artifacts: {analysis_dir}")
    return 0


def cmd_decode_a2a_buffer_reuse(args: argparse.Namespace) -> int:
    from reproduce.decode_a2a_buffer_reuse import run_decode_a2a_buffer_reuse

    run_decode_a2a_buffer_reuse(
        source_root=args.source_root.resolve(),
        out_root=args.out_root.resolve(),
        mode=args.mode,
        baseline_run_dir=args.baseline_run_dir.resolve() if args.baseline_run_dir else None,
        optimized_run_dir=args.optimized_run_dir.resolve() if args.optimized_run_dir else None,
        top_devices_global=args.top_devices_global,
        devices=args.devices,
        max_main_events_per_device=args.max_main_events_per_device,
        max_macro_defs=args.max_macro_defs,
        dry_run=args.dry_run,
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run TraceLoom reference reproduction workflows.")
    parser.add_argument(
        "--out-root",
        type=Path,
        default=DEFAULT_OUT_ROOT,
        help="Artifact root. Defaults to traceloom/out/reproduce.",
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    analyze = subparsers.add_parser("analyze-msprof", help="Analyze an existing Ascend/CANN msprof directory.")
    analyze.add_argument("profile_dir", type=Path)
    analyze.add_argument("--name", default="msprof_existing")
    add_common_analyzer_args(analyze)
    analyze.set_defaults(func=cmd_analyze_msprof)

    ascend = subparsers.add_parser("ascend-msprof", help="Run msprof on a workload, then analyze the profile.")
    ascend.add_argument("--name", default="ascend_reference")
    ascend.add_argument("--profile-dir", type=Path, default=None)
    ascend.add_argument("--msprof-arg", action="append", default=[], help="Extra raw argument passed to msprof.")
    add_common_analyzer_args(ascend)
    ascend.add_argument("workload", nargs=argparse.REMAINDER, help="Workload command after --")
    ascend.set_defaults(func=cmd_ascend_msprof)

    paper = subparsers.add_parser("decode-a2a-buffer-reuse", help="Reproduce the CANN Decode All-to-All Buffer Reuse paper tables.")
    paper.add_argument(
        "--source-root",
        type=Path,
        default=PROJECT_ROOT.parent / "template-of-thesis" / "experiments-data" / "run_20260507_npu3456",
        help="Decode All-to-All Buffer Reuse experiment bundle root.",
    )
    paper.add_argument("--mode", choices=("bundle", "bundle-recomputed", "raw-analysis"), default="bundle")
    paper.add_argument("--baseline-run-dir", type=Path, default=None)
    paper.add_argument("--optimized-run-dir", type=Path, default=None)
    add_common_analyzer_args(paper)
    paper.set_defaults(func=cmd_decode_a2a_buffer_reuse)

    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    args.out_root = args.out_root.resolve()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
