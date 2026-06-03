from __future__ import annotations

import argparse
import configparser
import os
import shlex
import shutil
import sqlite3
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Sequence

from .compute_prelude_timeline import (
    ComputePreludeConfig,
    _format_console_summary,
    _resolve_msprof_raw_dir,
    run_compute_prelude_timeline,
)
from .report import parse_attach, report_to_file


TEMPLATE = """# TraceLoom profile config.
# Edit this file, then run:
#   traceloom run traceloom.profile.ini
#   traceloom analyze runs/local-msprof/msprof_raw

[profile]
name = local-msprof

[paths]
# Paths are resolved relative to this config file unless absolute.
run_dir = runs/local-msprof
profile_dir = runs/local-msprof/msprof_raw
analysis_dir = runs/local-msprof/msprof_raw/traceloom
log_file = runs/local-msprof/workload.log

[workload]
cwd = .
command = python3 examples/workloads/pytorch_ddp_matmul/train.py
# Optional newline-separated KEY=VALUE entries.
env =

[profiler]
backend = ascend_msprof
executable = msprof
# Extra args are parsed with shell-like quoting, for example:
# extra_args = --aic-metrics=PipeUtilization --sys-hardware-mem=on
extra_args =

[analysis]
# 0 means analyze every discovered device. Set to a positive number to keep
# only the highest-ranked devices, or set devices = 3,4,5,6 to pin physical IDs.
top_devices_global = 0
devices =
max_main_events_per_device = 5000
max_macro_defs = 32
summary_top_loops = 12
output_mode = bundle

[docker]
# Docker is optional. In local mode, TraceLoom runs msprof directly.
# Set enabled = true and container = <name> to run "docker exec <container> ...".
# Or set enabled = true and image = <image> to run "docker run --rm <image> ...".
enabled = false
container =
image =
workdir =
volumes =
env =
devices =
network =
shm_size =
extra_args =
"""


def _config_dir(path: Path) -> Path:
    return path.resolve().parent


def _load_config(path: Path) -> configparser.ConfigParser:
    cfg = configparser.ConfigParser(interpolation=None)
    with path.open("r", encoding="utf-8") as f:
        cfg.read_file(f)
    return cfg


def _path_value(cfg: configparser.ConfigParser, config_path: Path, section: str, key: str, default: str) -> Path:
    raw = cfg.get(section, key, fallback=default).strip()
    path = Path(raw)
    if path.is_absolute():
        return path
    return (_config_dir(config_path) / path).resolve()


def _optional_path_value(
    cfg: configparser.ConfigParser,
    config_path: Path,
    section: str,
    key: str,
) -> Path | None:
    raw = cfg.get(section, key, fallback="").strip()
    if not raw:
        return None
    path = Path(raw)
    if path.is_absolute():
        return path
    return (_config_dir(config_path) / path).resolve()


def _split_words(value: str) -> List[str]:
    value = value.strip()
    return shlex.split(value) if value else []


def _list_value(cfg: configparser.ConfigParser, section: str, key: str) -> List[str]:
    raw = cfg.get(section, key, fallback="")
    out: List[str] = []
    for line in raw.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        out.extend(part.strip() for part in line.split(",") if part.strip())
    return out


def _env_value(cfg: configparser.ConfigParser, section: str, key: str) -> Dict[str, str]:
    out: Dict[str, str] = {}
    for item in _list_value(cfg, section, key):
        if "=" not in item:
            raise SystemExit(f"[{section}] {key} entry must be KEY=VALUE: {item}")
        name, value = item.split("=", 1)
        out[name.strip()] = value.strip()
    return out


def _bool_value(cfg: configparser.ConfigParser, section: str, key: str, default: bool = False) -> bool:
    if not cfg.has_option(section, key):
        return default
    return cfg.getboolean(section, key)


def _int_option(
    cfg: configparser.ConfigParser,
    section: str,
    key: str,
    default: int,
) -> int:
    if cfg.has_option(section, key):
        return cfg.getint(section, key)
    return default


def _float_option(
    cfg: configparser.ConfigParser,
    section: str,
    key: str,
    default: float,
) -> float:
    if cfg.has_option(section, key):
        return cfg.getfloat(section, key)
    return default


def _str_option(
    cfg: configparser.ConfigParser,
    section: str,
    key: str,
    default: str,
) -> str:
    if cfg.has_option(section, key):
        return cfg.get(section, key).strip()
    return default


def _parse_device_ids(value: str) -> tuple[int, ...] | None:
    value = value.strip()
    if not value:
        return None
    return tuple(int(part.strip()) for part in value.split(",") if part.strip())


def _analysis_config_from_args(args: argparse.Namespace) -> ComputePreludeConfig:
    cfg = configparser.ConfigParser(interpolation=None)
    if args.config is not None:
        cfg = _load_config(args.config.resolve())
    return ComputePreludeConfig(
        top_devices_global=args.top_devices_global
        if args.top_devices_global is not None
        else _int_option(cfg, "analysis", "top_devices_global", ComputePreludeConfig.top_devices_global),
        max_main_events_per_device=args.max_main_events_per_device
        if args.max_main_events_per_device is not None
        else _int_option(
            cfg,
            "analysis",
            "max_main_events_per_device",
            ComputePreludeConfig.max_main_events_per_device,
        ),
        max_macro_defs=args.max_macro_defs
        if args.max_macro_defs is not None
        else _int_option(cfg, "analysis", "max_macro_defs", ComputePreludeConfig.max_macro_defs),
        collective_episode_gap_us=args.collective_episode_gap_us
        if args.collective_episode_gap_us is not None
        else _float_option(
            cfg,
            "analysis",
            "collective_episode_gap_us",
            ComputePreludeConfig.collective_episode_gap_us,
        ),
        min_main_event_us=args.min_main_event_us
        if args.min_main_event_us is not None
        else _float_option(cfg, "analysis", "min_main_event_us", ComputePreludeConfig.min_main_event_us),
        readable_macro_mode=args.readable_macro_mode
        if args.readable_macro_mode is not None
        else _str_option(cfg, "analysis", "readable_macro_mode", ComputePreludeConfig.readable_macro_mode),
        kernel_role_file=args.kernel_role_file,
        summary_top_loops=args.summary_top_loops
        if args.summary_top_loops is not None
        else _int_option(cfg, "analysis", "summary_top_loops", ComputePreludeConfig.summary_top_loops),
        device_ids=_parse_device_ids(
            args.devices if args.devices is not None else _str_option(cfg, "analysis", "devices", "")
        ),
        output_mode=args.output_mode
        if args.output_mode is not None
        else _str_option(cfg, "analysis", "output_mode", ComputePreludeConfig.output_mode),
    )


def cmd_create_config(args: argparse.Namespace) -> int:
    output = args.output.resolve()
    if output.exists() and not args.force:
        raise SystemExit(f"refusing to overwrite existing config: {output} (use --force)")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(TEMPLATE, encoding="utf-8")
    print(f"created config: {output}")
    return 0


def _msprof_command(cfg: configparser.ConfigParser, profile_dir: Path) -> List[str]:
    workload_command = cfg.get("workload", "command", fallback="").strip()
    if not workload_command:
        raise SystemExit("[workload] command is required")
    executable = cfg.get("profiler", "executable", fallback="msprof").strip() or "msprof"
    extra_args = _split_words(cfg.get("profiler", "extra_args", fallback=""))
    return [
        executable,
        f"--output={profile_dir}",
        f"--application={workload_command}",
        *extra_args,
    ]


def _docker_command(cfg: configparser.ConfigParser, inner_command: Sequence[str]) -> List[str]:
    inner_shell = shlex.join([str(part) for part in inner_command])
    container = cfg.get("docker", "container", fallback="").strip()
    image = cfg.get("docker", "image", fallback="").strip()
    workdir = cfg.get("docker", "workdir", fallback="").strip()
    docker_env = _env_value(cfg, "docker", "env")
    extra_args = _split_words(cfg.get("docker", "extra_args", fallback=""))

    if container:
        command = ["docker", "exec"]
        if workdir:
            command.extend(["-w", workdir])
        for name, value in docker_env.items():
            command.extend(["-e", f"{name}={value}"])
        command.extend(extra_args)
        command.extend([container, "sh", "-lc", inner_shell])
        return command

    if not image:
        raise SystemExit("[docker] enabled=true requires either container or image")

    command = ["docker", "run", "--rm"]
    if workdir:
        command.extend(["-w", workdir])
    network = cfg.get("docker", "network", fallback="").strip()
    if network:
        command.extend(["--network", network])
    shm_size = cfg.get("docker", "shm_size", fallback="").strip()
    if shm_size:
        command.extend(["--shm-size", shm_size])
    for volume in _list_value(cfg, "docker", "volumes"):
        command.extend(["-v", volume])
    for device in _list_value(cfg, "docker", "devices"):
        command.extend(["--device", device])
    for name, value in docker_env.items():
        command.extend(["-e", f"{name}={value}"])
    command.extend(extra_args)
    command.extend([image, "sh", "-lc", inner_shell])
    return command


def _run_command(command: Sequence[str], *, cwd: Path, log_file: Path, env: Dict[str, str], dry_run: bool) -> None:
    command_text = shlex.join([str(part) for part in command])
    if dry_run:
        print("[dry-run] " + command_text)
        return
    log_file.parent.mkdir(parents=True, exist_ok=True)
    print("+ " + command_text)
    with log_file.open("w", encoding="utf-8") as log:
        log.write(f"# started_at={datetime.now().isoformat(timespec='seconds')}\n")
        log.write(f"# cwd={cwd}\n")
        log.write(f"# command={command_text}\n\n")
        log.flush()
        subprocess.run(
            [str(part) for part in command],
            cwd=str(cwd),
            env={**os.environ, **env},
            stdout=log,
            stderr=subprocess.STDOUT,
            check=True,
        )


def cmd_run(args: argparse.Namespace) -> int:
    config_path = args.config.resolve()
    cfg = _load_config(config_path)
    profile_dir = _path_value(cfg, config_path, "paths", "profile_dir", "msprof_raw")
    log_file = _path_value(cfg, config_path, "paths", "log_file", "workload.log")
    workload_cwd = _path_value(cfg, config_path, "workload", "cwd", ".")
    docker_enabled = _bool_value(cfg, "docker", "enabled", False)
    if not docker_enabled:
        profile_dir.mkdir(parents=True, exist_ok=True)

    command = _msprof_command(cfg, profile_dir)
    if docker_enabled:
        if shutil.which("docker") is None and not args.dry_run:
            raise SystemExit("docker was not found in PATH")
        command = _docker_command(cfg, command)
    elif shutil.which(command[0]) is None and not args.dry_run:
        raise SystemExit(f"{command[0]} was not found in PATH. Activate the profiler environment first.")

    _run_command(
        command,
        cwd=workload_cwd,
        log_file=log_file,
        env=_env_value(cfg, "workload", "env"),
        dry_run=args.dry_run,
    )
    print(f"profile_dir: {profile_dir}")
    print(f"log_file: {log_file}")
    analysis_dir = _path_value(cfg, config_path, "paths", "analysis_dir", str(profile_dir / "traceloom"))
    print(f"next: traceloom analyze {profile_dir}")
    print(f"default_analysis_dir: {analysis_dir}")
    return 0


def cmd_analysis(args: argparse.Namespace) -> int:
    profile_dir = args.profile_dir.resolve()
    try:
        analysis_dir = args.out_dir.resolve() if args.out_dir is not None else _resolve_msprof_raw_dir(profile_dir) / "traceloom"
        meta = run_compute_prelude_timeline(
            run_dir=profile_dir,
            out_dir=analysis_dir,
            config=_analysis_config_from_args(args),
        )
    except (FileNotFoundError, ValueError) as exc:
        raise SystemExit(str(exc)) from exc
    if args.json:
        import json

        print(json.dumps(meta, ensure_ascii=False, indent=2))
    else:
        print(_format_console_summary(meta))
    return 0


def cmd_report(args: argparse.Namespace) -> int:
    if args.sql is None and args.query is None:
        raise SystemExit("report requires --sql or --query")
    if args.sql is not None and args.query is not None:
        raise SystemExit("report accepts only one of --sql or --query")
    sql = args.query if args.query is not None else args.sql.read_text(encoding="utf-8")
    try:
        attaches = [parse_attach(value) for value in args.attach]
        result = report_to_file(
            db_path=args.database.resolve(),
            sql=sql,
            fmt=args.format,
            output_path=args.output.resolve() if args.output is not None else None,
            attaches=attaches,
        )
    except (FileNotFoundError, ValueError, sqlite3.Error) as exc:
        raise SystemExit(str(exc)) from exc
    if args.output is not None:
        print(f"report: {args.output.resolve()} ({len(result.rows)} rows)")
    return 0


def add_analysis_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--config", type=Path, default=None, help="Optional profile config; reads [analysis] defaults only.")
    parser.add_argument("--top-devices-global", type=int, default=None)
    parser.add_argument(
        "--devices",
        default=None,
        help="Comma-separated physical device IDs to analyze, for example 3,4,5,6. Empty means all ranked devices.",
    )
    parser.add_argument(
        "--max-main-events-per-device",
        type=int,
        default=None,
        help="Maximum main compute/data-move events per device; 0 means no truncation.",
    )
    parser.add_argument(
        "--max-macro-defs",
        type=int,
        default=None,
        help="Maximum macro definitions; 0 means keep discovering while gain is positive.",
    )
    parser.add_argument(
        "--collective-episode-gap-us",
        type=float,
        default=None,
        help="Fallback merge gap for collective TASK fragments when COMMUNICATION_OP is absent.",
    )
    parser.add_argument("--min-main-event-us", type=float, default=None)
    parser.add_argument(
        "--readable-macro-mode",
        choices=("inline", "auto"),
        default=None,
    )
    parser.add_argument("--kernel-role-file", type=Path, default=None)
    parser.add_argument("--summary-top-loops", type=int, default=None)
    parser.add_argument(
        "--output-mode",
        choices=("bundle", "full"),
        default=None,
        help="bundle writes augmented DBs, README, summary, and SQL scripts; full also exports legacy CSV/JSON files.",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="traceloom", description="TraceLoom profile runner and offline analyzer.")
    subparsers = parser.add_subparsers(dest="command")

    create = subparsers.add_parser("create", help="Create editable TraceLoom assets.")
    create_sub = create.add_subparsers(dest="create_command")
    create_config = create_sub.add_parser("config", help="Create an editable profile config template.")
    create_config.add_argument("-o", "--output", type=Path, default=Path("traceloom.profile.ini"))
    create_config.add_argument("--force", action="store_true")
    create_config.set_defaults(func=cmd_create_config)

    run = subparsers.add_parser("run", help="Run profiler/workload from a profile config.")
    run.add_argument("config", type=Path)
    run.add_argument("--dry-run", action="store_true")
    run.set_defaults(func=cmd_run)

    analysis = subparsers.add_parser("analysis", help="Analyze an existing msprof profile directory.")
    analysis.add_argument("profile_dir", type=Path, help="Directory containing PROF_*/msprof_*.db, or a run dir with msprof_raw/.")
    analysis.add_argument("--out-dir", type=Path, default=None)
    add_analysis_options(analysis)
    analysis.add_argument("--json", action="store_true")
    analysis.set_defaults(func=cmd_analysis)

    analyze = subparsers.add_parser("analyze", help="Alias for analysis.")
    analyze.add_argument("profile_dir", type=Path, help="Directory containing PROF_*/msprof_*.db, or a run dir with msprof_raw/.")
    analyze.add_argument("--out-dir", type=Path, default=None)
    add_analysis_options(analyze)
    analyze.add_argument("--json", action="store_true")
    analyze.set_defaults(func=cmd_analysis)

    report = subparsers.add_parser("report", help="Run SQL against a TraceLoom augmented DB and export rows.")
    report.add_argument("database", type=Path, help="TraceLoom augmented SQLite DB, such as db01.traceloom_augmented.db.")
    report.add_argument("--sql", type=Path, default=None, help="SQL file containing a SELECT or WITH query.")
    report.add_argument("--query", default=None, help="Inline SELECT or WITH query.")
    report.add_argument(
        "--format",
        choices=("csv", "tsv", "json", "md"),
        default="csv",
        help="Output format.",
    )
    report.add_argument("-o", "--output", type=Path, default=None, help="Output file. Defaults to stdout.")
    report.add_argument(
        "--attach",
        action="append",
        default=[],
        metavar="ALIAS=PATH",
        help="Attach another SQLite DB for cross-rank queries. Can be passed multiple times.",
    )
    report.set_defaults(func=cmd_report)

    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args_list = list(sys.argv[1:] if argv is None else argv)
    parser = build_parser()
    args = parser.parse_args(args_list)
    func = getattr(args, "func", None)
    if func is None:
        parser.print_help()
        return 2
    return int(func(args))


if __name__ == "__main__":
    raise SystemExit(main())
