#!/usr/bin/env python3
"""Build a claim-preserving, reviewer-safe CUDA/Nsight SQLite artifact."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sqlite3
from pathlib import Path
from typing import Any


RETAINED_TABLES = (
    "CUDA_GRAPH_NODE_EVENTS",
    "CUPTI_ACTIVITY_KIND_CUDA_EVENT",
    "CUPTI_ACTIVITY_KIND_GRAPH_TRACE",
    "CUPTI_ACTIVITY_KIND_KERNEL",
    "CUPTI_ACTIVITY_KIND_MEMCPY",
    "CUPTI_ACTIVITY_KIND_MEMSET",
    "CUPTI_ACTIVITY_KIND_RUNTIME",
    "CUPTI_ACTIVITY_KIND_SYNCHRONIZATION",
    "StringIds",
)

STRING_REFERENCE_COLUMNS = {
    "CUDA_GRAPH_NODE_EVENTS": ("nameId",),
    "CUPTI_ACTIVITY_KIND_CUDA_EVENT": ("eventId",),
    "CUPTI_ACTIVITY_KIND_KERNEL": (
        "demangledName",
        "shortName",
        "mangledName",
    ),
    "CUPTI_ACTIVITY_KIND_MEMCPY": ("copyKind",),
    "CUPTI_ACTIVITY_KIND_RUNTIME": ("nameId",),
    "CUPTI_ACTIVITY_KIND_SYNCHRONIZATION": ("syncType",),
}

SENSITIVE_TEXT = re.compile(
    r"(?i)(?:https?://[^/\s:@]+:[^@\s/]+@|"
    r"(?:password|passwd|secret|token|api[_-]?key)\s*[:=]|"
    r"/(?:home|root)/)"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--manifest", type=Path, required=True)
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def quote_identifier(value: str) -> str:
    return '"' + value.replace('"', '""') + '"'


def table_names(db: sqlite3.Connection) -> set[str]:
    return {
        str(row[0])
        for row in db.execute(
            "SELECT name FROM sqlite_master WHERE type='table'"
        )
    }


def table_columns(db: sqlite3.Connection, table: str) -> list[sqlite3.Row]:
    return list(db.execute(f"PRAGMA table_info({quote_identifier(table)})"))


def referenced_string_ids(
    source: sqlite3.Connection, present_tables: set[str]
) -> set[int]:
    ids: set[int] = set()
    for table, desired_columns in STRING_REFERENCE_COLUMNS.items():
        if table not in present_tables:
            continue
        available = {str(row[1]) for row in table_columns(source, table)}
        for column in desired_columns:
            if column not in available:
                continue
            query = (
                f"SELECT DISTINCT {quote_identifier(column)} "
                f"FROM {quote_identifier(table)} "
                f"WHERE {quote_identifier(column)} IS NOT NULL"
            )
            ids.update(int(row[0]) for row in source.execute(query))
    return ids


def create_table(
    source: sqlite3.Connection, output: sqlite3.Connection, table: str
) -> None:
    row = source.execute(
        "SELECT sql FROM sqlite_master WHERE type='table' AND name=?", (table,)
    ).fetchone()
    if row is None or not row[0]:
        raise RuntimeError(f"missing CREATE TABLE statement for {table}")
    output.execute(str(row[0]))


def copy_rows(
    source: sqlite3.Connection,
    output: sqlite3.Connection,
    table: str,
    string_ids: set[int],
) -> tuple[int, int]:
    columns = table_columns(source, table)
    names = [str(row[1]) for row in columns]
    column_list = ", ".join(quote_identifier(name) for name in names)
    integer_primary_key = any(
        int(row[5]) == 1 and str(row[2]).upper() == "INTEGER" for row in columns
    )
    if table == "StringIds":
        placeholders = ",".join("?" for _ in string_ids)
        if not placeholders:
            rows: list[sqlite3.Row] = []
        else:
            rows = list(
                source.execute(
                    f"SELECT {column_list} FROM {quote_identifier(table)} "
                    f"WHERE id IN ({placeholders}) ORDER BY id",
                    tuple(sorted(string_ids)),
                )
            )
        for row in rows:
            text = str(row[names.index("value")])
            if SENSITIVE_TEXT.search(text):
                raise RuntimeError(
                    "a referenced StringIds value contains private or "
                    "credential-bearing text"
                )
        insert_columns = column_list
        values = rows
    elif integer_primary_key:
        insert_columns = column_list
        values = list(
            source.execute(
                f"SELECT {column_list} FROM {quote_identifier(table)} "
                "ORDER BY rowid"
            )
        )
    else:
        insert_columns = "rowid, " + column_list
        values = list(
            source.execute(
                f"SELECT rowid, {column_list} FROM {quote_identifier(table)} "
                "ORDER BY rowid"
            )
        )
    if values:
        placeholders = ", ".join("?" for _ in values[0])
        output.executemany(
            f"INSERT INTO {quote_identifier(table)} ({insert_columns}) "
            f"VALUES ({placeholders})",
            values,
        )
    source_count = int(
        source.execute(
            f"SELECT count(*) FROM {quote_identifier(table)}"
        ).fetchone()[0]
    )
    return source_count, len(values)


def build_manifest(
    source_path: Path,
    output_path: Path,
    rows: dict[str, dict[str, int]],
    retained_string_ids: int,
) -> dict[str, Any]:
    return {
        "format": "traceloom-cuda-nsys-reduction-v1",
        "source_bytes": source_path.stat().st_size,
        "source_sha256": sha256_file(source_path),
        "output_bytes": output_path.stat().st_size,
        "output_sha256": sha256_file(output_path),
        "retained_tables": sorted(rows),
        "row_counts": rows,
        "source_rowid_policy": "preserved",
        "string_id_policy": "retain only values referenced by analyzed tables",
        "retained_string_ids": retained_string_ids,
        "omitted_content": "unreferenced strings and non-analyzer metadata tables",
        "privacy_check": "referenced strings reject credentials and home paths",
    }


def main() -> int:
    args = parse_args()
    source_path = args.source.resolve()
    output_path = args.output.resolve()
    manifest_path = args.manifest.resolve()
    if output_path == source_path:
        raise RuntimeError("source and output must differ")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    if output_path.exists():
        output_path.unlink()

    with sqlite3.connect(source_path) as source:
        source.row_factory = sqlite3.Row
        present = table_names(source)
        if "StringIds" not in present or "CUPTI_ACTIVITY_KIND_KERNEL" not in present:
            raise RuntimeError("input is not a supported CUDA/Nsight SQLite export")
        retained = [name for name in RETAINED_TABLES if name in present]
        string_ids = referenced_string_ids(source, present)
        with sqlite3.connect(output_path) as output:
            output.execute("PRAGMA page_size=4096")
            output.execute("PRAGMA journal_mode=OFF")
            output.execute("PRAGMA synchronous=OFF")
            for table in retained:
                create_table(source, output, table)
            rows: dict[str, dict[str, int]] = {}
            for table in retained:
                source_count, output_count = copy_rows(
                    source, output, table, string_ids
                )
                rows[table] = {
                    "source": source_count,
                    "retained": output_count,
                }
            output.commit()
            integrity = str(output.execute("PRAGMA integrity_check").fetchone()[0])
            if integrity != "ok":
                raise RuntimeError(f"reduced SQLite integrity check failed: {integrity}")
            output.execute("VACUUM")

    manifest = build_manifest(source_path, output_path, rows, len(string_ids))
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(manifest, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
