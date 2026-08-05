#!/usr/bin/env python3
"""Deterministically reduce a monolithic Ascend profiler SQLite by time.

The output preserves original SQLite rowids for source tables, retains every
original table schema, and follows task/string dependencies needed by
TraceLoom's monolithic Ascend adapter. Host identity is always omitted;
TASK_PMU_INFO is omitted by default for size but may be retained when the
artifact is specifically meant to exercise realistic input scale.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sqlite3
import sys
from pathlib import Path


TIME_TABLES = {"TASK", "CANN_API", "COMMUNICATION_OP"}
DEPENDENT_TABLES = {"COMPUTE_TASK_INFO", "COMMUNICATION_TASK_INFO"}
ALWAYS_EMPTY_TABLES = {"HOST_INFO"}
STRING_COLUMNS = {
    "CANN_API": ("name",),
    "TASK": ("taskType",),
    "COMMUNICATION_OP": ("opName", "groupName", "algType", "opType"),
    "COMMUNICATION_TASK_INFO": (
        "name",
        "taskType",
        "groupName",
        "rdmaType",
        "transportType",
        "dataType",
        "linkType",
    ),
    "COMPUTE_TASK_INFO": (
        "name",
        "taskType",
        "opType",
        "inputFormats",
        "inputDataTypes",
        "inputShapes",
        "outputFormats",
        "outputDataTypes",
        "outputShapes",
        "attrInfo",
        "opState",
        "hf32Eligible",
    ),
}


def quote_identifier(value: str) -> str:
    return '"' + value.replace('"', '""') + '"'


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def table_columns(db: sqlite3.Connection, schema: str, table: str) -> list[str]:
    rows = db.execute(
        f"PRAGMA {quote_identifier(schema)}.table_info({quote_identifier(table)})"
    ).fetchall()
    return [str(row[1]) for row in rows]


def integer_primary_key(
    db: sqlite3.Connection, schema: str, table: str
) -> str | None:
    rows = db.execute(
        f"PRAGMA {quote_identifier(schema)}.table_info({quote_identifier(table)})"
    ).fetchall()
    primary = [row for row in rows if int(row[5]) != 0]
    if len(primary) == 1 and str(primary[0][2]).strip().upper() == "INTEGER":
        return str(primary[0][1])
    return None


def copy_rows(
    db: sqlite3.Connection, table: str, where_sql: str = "1", params: tuple = ()
) -> int:
    columns = table_columns(db, "src", table)
    if not columns:
        return 0
    names = ", ".join(quote_identifier(column) for column in columns)
    table_name = quote_identifier(table)
    if integer_primary_key(db, "src", table) is None:
        insert_columns = "rowid, " + names
        select_columns = "rowid, " + names
    else:
        insert_columns = names
        select_columns = names
    db.execute(
        f"INSERT INTO main.{table_name} ({insert_columns}) "
        f"SELECT {select_columns} FROM src.{table_name} "
        f"WHERE {where_sql} ORDER BY rowid",
        params,
    )
    return int(db.execute("SELECT changes()").fetchone()[0])


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--start-ns", type=int, required=True)
    parser.add_argument("--end-ns", type=int, required=True)
    parser.add_argument(
        "--stream-info-source",
        type=Path,
        help=(
            "optional source host/sqlite/stream_info.db; its CaptureStreamInfo "
            "table is copied to the sibling path required by the Ascend adapter"
        ),
    )
    parser.add_argument(
        "--keep-task-pmu",
        action="store_true",
        help="retain TASK_PMU_INFO for a scale/folding artifact",
    )
    parser.add_argument("--manifest-out", type=Path)
    args = parser.parse_args()
    if args.start_ns >= args.end_ns:
        parser.error("--start-ns must be less than --end-ns")
    if args.source.resolve() == args.output.resolve():
        parser.error("source and output must differ")
    return args


def reduce_stream_info(source: Path, output: Path) -> dict[str, object]:
    """Copy the small, numeric capture map while preserving schema and rowids."""
    if not source.is_file():
        raise FileNotFoundError(source)
    if output.exists():
        raise FileExistsError(output)
    output.parent.mkdir(parents=True, exist_ok=True)

    db = sqlite3.connect(output)
    try:
        db.execute("PRAGMA journal_mode=DELETE")
        db.execute("PRAGMA synchronous=FULL")
        db.execute("ATTACH DATABASE ? AS src", (str(source),))
        schema = db.execute(
            "SELECT sql FROM src.sqlite_master "
            "WHERE type='table' AND name='CaptureStreamInfo' AND sql IS NOT NULL"
        ).fetchone()
        if schema is None:
            raise RuntimeError("stream-info source has no CaptureStreamInfo table")
        db.execute(str(schema[0]))
        copied = copy_rows(db, "CaptureStreamInfo")
        db.commit()
        integrity = str(db.execute("PRAGMA integrity_check").fetchone()[0])
        if integrity != "ok":
            raise RuntimeError(f"stream-info integrity_check failed: {integrity}")
        db.execute("DETACH DATABASE src")
        db.execute("VACUUM")
        db.commit()
    except BaseException:
        db.close()
        try:
            output.unlink()
        except FileNotFoundError:
            pass
        raise
    finally:
        try:
            db.close()
        except Exception:
            pass

    output.chmod(0o644)
    return {
        "source_sha256": sha256(source),
        "source_bytes": source.stat().st_size,
        "output_sha256": sha256(output),
        "output_bytes": output.stat().st_size,
        "copied_rows": copied,
        "source_rowid_policy": "preserved",
    }


def main() -> int:
    args = parse_args()
    source = args.source.resolve()
    output = args.output.resolve()
    if not source.is_file():
        raise FileNotFoundError(source)
    if output.exists():
        raise FileExistsError(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    empty_tables = set(ALWAYS_EMPTY_TABLES)
    if not args.keep_task_pmu:
        empty_tables.add("TASK_PMU_INFO")

    db = sqlite3.connect(output)
    try:
        db.execute("PRAGMA journal_mode=DELETE")
        db.execute("PRAGMA synchronous=FULL")
        db.execute("ATTACH DATABASE ? AS src", (str(source),))
        schema_rows = db.execute(
            "SELECT type, name, sql FROM src.sqlite_master "
            "WHERE name NOT LIKE 'sqlite_%' AND sql IS NOT NULL "
            "ORDER BY CASE type WHEN 'table' THEN 0 ELSE 1 END, name"
        ).fetchall()
        tables = [str(row[1]) for row in schema_rows if row[0] == "table"]
        for object_type, _name, sql in schema_rows:
            if object_type == "table":
                db.execute(str(sql))

        copied: dict[str, int] = {}
        for table in tables:
            if table in empty_tables or table == "STRING_IDS" or table in DEPENDENT_TABLES:
                copied[table] = 0
            elif table in TIME_TABLES:
                copied[table] = copy_rows(
                    db,
                    table,
                    "NOT (endNs < ? OR startNs > ?)",
                    (args.start_ns, args.end_ns),
                )
            else:
                copied[table] = copy_rows(db, table)

        selected_task = "SELECT globalTaskId FROM main.TASK"
        if "COMPUTE_TASK_INFO" in tables:
            copied["COMPUTE_TASK_INFO"] = copy_rows(
                db,
                "COMPUTE_TASK_INFO",
                f"globalTaskId IN ({selected_task})",
            )
        if "COMMUNICATION_TASK_INFO" in tables:
            copied["COMMUNICATION_TASK_INFO"] = copy_rows(
                db,
                "COMMUNICATION_TASK_INFO",
                f"globalTaskId IN ({selected_task})",
            )

        db.execute("CREATE TEMP TABLE needed_string_id(id INTEGER PRIMARY KEY)")
        for table, columns in STRING_COLUMNS.items():
            if table not in tables:
                continue
            available = set(table_columns(db, "main", table))
            for column in columns:
                if column not in available:
                    continue
                db.execute(
                    "INSERT OR IGNORE INTO needed_string_id(id) "
                    f"SELECT {quote_identifier(column)} "
                    f"FROM main.{quote_identifier(table)} "
                    f"WHERE {quote_identifier(column)} IS NOT NULL"
                )
        copied["STRING_IDS"] = copy_rows(
            db, "STRING_IDS", "id IN (SELECT id FROM needed_string_id)"
        )

        for object_type, _name, sql in schema_rows:
            if object_type == "index" and sql:
                db.execute(str(sql))
        db.commit()
        integrity = str(db.execute("PRAGMA integrity_check").fetchone()[0])
        if integrity != "ok":
            raise RuntimeError(f"output integrity_check failed: {integrity}")
        db.execute("DETACH DATABASE src")
        db.execute("VACUUM")
        db.commit()
    except BaseException:
        db.close()
        try:
            output.unlink()
        except FileNotFoundError:
            pass
        raise
    finally:
        try:
            db.close()
        except Exception:
            pass

    output.chmod(0o644)

    stream_info: dict[str, object] | None = None
    if args.stream_info_source:
        stream_output = output.parent / "host" / "sqlite" / "stream_info.db"
        stream_info = reduce_stream_info(
            args.stream_info_source.resolve(), stream_output
        )

    manifest = {
        "schema_version": "traceloom-ascend-reduction-v1",
        "source_sha256": sha256(source),
        "source_bytes": source.stat().st_size,
        "output_sha256": sha256(output),
        "output_bytes": output.stat().st_size,
        "window": {"start_ns": args.start_ns, "end_ns": args.end_ns},
        "copied_rows": dict(sorted(copied.items())),
        "omitted_row_content": sorted(empty_tables),
        "source_rowid_policy": "preserved",
    }
    if stream_info is not None:
        manifest["stream_info"] = stream_info
    rendered = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    if args.manifest_out:
        args.manifest_out.parent.mkdir(parents=True, exist_ok=True)
        args.manifest_out.write_text(rendered, encoding="utf-8")
        args.manifest_out.chmod(0o644)
    else:
        sys.stdout.write(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
