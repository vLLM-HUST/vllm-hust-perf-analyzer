#!/usr/bin/env python3
"""Print a bounded acceptance and discovery summary for one TraceLoom AugDB."""

from __future__ import annotations

import argparse
import hashlib
import sqlite3
import sys
from collections.abc import Mapping
from pathlib import Path


REQUIRED_OBJECTS = {
    "traceloom_metadata",
    "traceloom_analysis_surface",
    "traceloom_projection_recipe",
    "traceloom_raw_source_database",
    "traceloom_v_position",
}

METADATA_KEYS = (
    "artifact_kind",
    "traceloom_schema_version",
    "source_kind",
    "input_format",
    "input_scope",
    "input_evidence_state",
    "source_embedded",
    "source_sha256",
    "source_size_bytes",
    "trace_event_count",
    "runtime_call_count",
    "anchor_count",
    "evidence_role_policy_id",
    "evidence_role_policy_version",
    "evidence_role_manifest_sha256",
    "event_reconciliation_policy_id",
    "event_reconciliation_policy_version",
    "event_reconciliation_manifest_sha256",
    "analytical_projection_contract",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("database", type=Path)
    parser.add_argument("--top", type=int, default=10, help="top structural rows (default: 10)")
    parser.add_argument(
        "--catalog",
        action="store_true",
        help="print every analysis surface and projection recipe",
    )
    parser.add_argument("--quick-check", action="store_true", help="run PRAGMA quick_check")
    parser.add_argument("--sha256", action="store_true", help="hash the complete AugDB")
    args = parser.parse_args()
    if args.top < 0 or args.top > 100:
        parser.error("--top must be between 0 and 100")
    return args


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def section(title: str) -> None:
    print(f"\n== {title} ==")


def rows(db: sqlite3.Connection, sql: str, parameters: tuple[object, ...] = ()) -> list[sqlite3.Row]:
    return list(db.execute(sql, parameters))


def print_rows(
    values: list[Mapping[str, object]],
    columns: tuple[str, ...],
    max_width: int | None = 80,
) -> None:
    if not values:
        print("(none)")
        return
    widths = {column: len(column) for column in columns}
    rendered: list[dict[str, str]] = []
    for row in values:
        item = {column: "NULL" if row[column] is None else str(row[column]) for column in columns}
        rendered.append(item)
        for column in columns:
            width = max(widths[column], len(item[column]))
            widths[column] = min(max_width, width) if max_width else width
    print("  ".join(column.ljust(widths[column]) for column in columns))
    print("  ".join("-" * widths[column] for column in columns))
    for item in rendered:
        print("  ".join(item[column][: widths[column]].ljust(widths[column]) for column in columns))


def inspect(
    database: Path,
    top: int,
    quick_check: bool,
    show_sha256: bool,
    show_catalog: bool,
) -> int:
    path = database.expanduser().resolve()
    if not path.is_file():
        print(f"error: not a regular file: {path}", file=sys.stderr)
        return 2

    print(f"database: {path}")
    print(f"size_bytes: {path.stat().st_size}")
    if show_sha256:
        print(f"sha256: {digest(path)}")

    try:
        db = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
        db.row_factory = sqlite3.Row
        object_names = {
            row["name"]
            for row in db.execute(
                "SELECT name FROM sqlite_schema WHERE type IN ('table','view')"
            )
        }
        missing = sorted(REQUIRED_OBJECTS - object_names)
        if missing:
            print(f"error: not a complete TraceLoom AugDB; missing: {', '.join(missing)}", file=sys.stderr)
            db.close()
            return 3

        metadata = {
            row["key"]: row["value"]
            for row in db.execute("SELECT key, value FROM traceloom_metadata")
        }
        section("acceptance")
        failures: list[str] = []
        if metadata.get("artifact_kind") != "queryable_database_timeline":
            failures.append("artifact_kind is not queryable_database_timeline")
        if metadata.get("source_embedded") != "true":
            failures.append("source_embedded is not true")
        source_count = db.execute("SELECT count(*) FROM traceloom_raw_source_database").fetchone()[0]
        if source_count == 0:
            failures.append("raw source catalog is empty")
        if quick_check:
            result = db.execute("PRAGMA quick_check").fetchone()[0]
            print(f"quick_check: {result}")
            if result != "ok":
                failures.append(f"quick_check={result}")
        print(f"contract: {'PASS' if not failures else 'FAIL'}")
        print(f"raw_source_count: {source_count}")
        for failure in failures:
            print(f"failure: {failure}")

        section("metadata")
        print_rows(
            [dict(key=key, value=metadata[key]) for key in METADATA_KEYS if key in metadata],
            ("key", "value"),
            max_width=None,
        )

        section("embedded raw sources")
        print_rows(
            rows(
                db,
                "SELECT source_id, source_ordinal, source_path, embedded_mode, "
                "size_bytes, sha256 FROM traceloom_raw_source_database "
                "ORDER BY source_ordinal",
            ),
            ("source_id", "source_ordinal", "source_path", "embedded_mode", "size_bytes", "sha256"),
            max_width=None,
        )

        section("query interface")
        surface_count = db.execute("SELECT count(*) FROM traceloom_analysis_surface").fetchone()[0]
        recipe_count = db.execute("SELECT count(*) FROM traceloom_projection_recipe").fetchone()[0]
        print(f"analysis_surface_count: {surface_count}")
        print(f"projection_recipe_count: {recipe_count}")
        print("hint: rerun with --catalog to print the complete self-describing interface")

        if show_catalog:
            section("analysis surfaces")
            print_rows(
                rows(
                    db,
                    "SELECT surface_name, relation_name, purpose "
                    "FROM traceloom_analysis_surface ORDER BY surface_name",
                ),
                ("surface_name", "relation_name", "purpose"),
            )

            section("projection recipes")
            print_rows(
                rows(
                    db,
                    "SELECT projection_name, population_mode, resolution, "
                    "observation_domain, measure_lens FROM traceloom_projection_recipe "
                    "ORDER BY display_order",
                ),
                (
                    "projection_name",
                    "population_mode",
                    "resolution",
                    "observation_domain",
                    "measure_lens",
                ),
                max_width=None,
            )

        if top:
            section(f"top {top} structural positions by total_us")
            print_rows(
                rows(
                    db,
                    "SELECT position_id, display_path, node_type, repeat_count, "
                    "occurrence_count, round(total_us,3) AS total_us, "
                    "round(total_us / max(occurrence_count,1),3) AS mean_realization_us "
                    "FROM traceloom_v_position "
                    "ORDER BY total_us DESC, db_idx, device_id, preorder_idx LIMIT ?",
                    (top,),
                ),
                (
                    "position_id",
                    "display_path",
                    "node_type",
                    "repeat_count",
                    "occurrence_count",
                    "total_us",
                    "mean_realization_us",
                ),
                max_width=None,
            )

        db.close()
        return 0 if not failures else 4
    except sqlite3.Error as error:
        print(f"error: SQLite inspection failed: {error}", file=sys.stderr)
        return 5


def main() -> int:
    args = parse_args()
    return inspect(
        args.database,
        args.top,
        args.quick_check,
        args.sha256,
        args.catalog,
    )


if __name__ == "__main__":
    raise SystemExit(main())
