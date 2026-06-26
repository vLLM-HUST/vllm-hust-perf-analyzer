from __future__ import annotations

import argparse
import configparser
import sqlite3
import sys
from pathlib import Path
from typing import Sequence

from .collective_tag import format_collective_tag_summary, run_collective_tag
from .compute_prelude_timeline import (
    ComputePreludeConfig,
    _format_console_summary,
    _resolve_msprof_raw_dir,
    run_compute_prelude_timeline,
)
from .report import parse_attach, report_to_file


def _config_dir(path: Path) -> Path:
    return path.resolve().parent


def _load_config(path: Path) -> configparser.ConfigParser:
    cfg = configparser.ConfigParser(interpolation=None)
    with path.open("r", encoding="utf-8") as f:
        cfg.read_file(f)
    return cfg


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
        anchor_grammar_input=args.anchor_grammar_input
        if args.anchor_grammar_input is not None
        else _str_option(cfg, "analysis", "anchor_grammar_input", ComputePreludeConfig.anchor_grammar_input),
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


def cmd_collective_tag(args: argparse.Namespace) -> int:
    try:
        meta = run_collective_tag(
            analysis_dir=args.analysis_dir.resolve(),
            run_name=args.run_name,
            expected_world_size=args.expected_world_size,
        )
    except (FileNotFoundError, ValueError, sqlite3.Error) as exc:
        raise SystemExit(str(exc)) from exc
    if args.json:
        import json

        print(json.dumps(meta, ensure_ascii=False, indent=2))
    else:
        print(format_collective_tag_summary(meta))
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
    parser.add_argument(
        "--anchor-grammar-input",
        choices=("raw", "run-compressed"),
        default=None,
        help="Use raw anchor events or adjacent same-label run tokens for anchor grammar discovery.",
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
    parser = argparse.ArgumentParser(prog="traceloom", description="TraceLoom offline msprof analyzer.")
    subparsers = parser.add_subparsers(dest="command")

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

    collective_tag = subparsers.add_parser(
        "collective-tag",
        help="Tag cross-device collective candidates in an existing TraceLoom analysis bundle.",
    )
    collective_tag.add_argument(
        "analysis_dir",
        type=Path,
        help="TraceLoom analysis bundle containing dbNN.traceloom_augmented.db, or a raw dir with traceloom/.",
    )
    collective_tag.add_argument(
        "--run-name",
        default=None,
        help="Prefix for generated candidate_collective_key values. Defaults to the profile/run directory name.",
    )
    collective_tag.add_argument(
        "--expected-world-size",
        type=int,
        default=None,
        help="Expected number of device members per candidate. Defaults to analyzed device count.",
    )
    collective_tag.add_argument("--json", action="store_true")
    collective_tag.set_defaults(func=cmd_collective_tag)

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
