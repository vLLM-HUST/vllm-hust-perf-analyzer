from __future__ import annotations

import csv
import json
import re
import sqlite3
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import IO, Sequence
from urllib.parse import quote


_IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


@dataclass(frozen=True)
class ReportResult:
    columns: tuple[str, ...]
    rows: tuple[tuple[object, ...], ...]


def run_sql_report(
    *,
    db_path: Path,
    sql: str,
    attaches: Sequence[tuple[str, Path]] = (),
) -> ReportResult:
    """Run one report query against a TraceLoom augmented SQLite database."""
    query = sql.strip()
    if not query:
        raise ValueError("report SQL is empty")

    with _connect_readonly(db_path) as conn:
        conn.execute("PRAGMA query_only=ON")
        for alias, path in attaches:
            if not _IDENTIFIER_RE.match(alias):
                raise ValueError(f"invalid SQLite attach alias: {alias!r}")
            conn.execute(f'ATTACH DATABASE ? AS "{alias}"', (str(path.resolve()),))
        cursor = conn.execute(query)
        if cursor.description is None:
            raise ValueError("report SQL must return rows; use a SELECT or WITH query")
        columns = tuple(str(item[0]) for item in cursor.description)
        rows = tuple(tuple(row) for row in cursor.fetchall())
        return ReportResult(columns=columns, rows=rows)


def write_report(result: ReportResult, *, fmt: str, out: IO[str]) -> None:
    if fmt == "csv":
        writer = csv.writer(out)
        writer.writerow(result.columns)
        writer.writerows(result.rows)
        return
    if fmt == "tsv":
        writer = csv.writer(out, delimiter="\t", lineterminator="\n")
        writer.writerow(result.columns)
        writer.writerows(result.rows)
        return
    if fmt == "json":
        payload = [dict(zip(result.columns, row)) for row in result.rows]
        json.dump(payload, out, ensure_ascii=False, indent=2)
        out.write("\n")
        return
    if fmt == "md":
        _write_markdown_table(result, out)
        return
    raise ValueError(f"unsupported report format: {fmt}")


def report_to_file(
    *,
    db_path: Path,
    sql: str,
    fmt: str,
    output_path: Path | None,
    attaches: Sequence[tuple[str, Path]] = (),
) -> ReportResult:
    result = run_sql_report(db_path=db_path, sql=sql, attaches=attaches)
    if output_path is None:
        write_report(result, fmt=fmt, out=sys.stdout)
    else:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with output_path.open("w", encoding="utf-8", newline="") as out:
            write_report(result, fmt=fmt, out=out)
    return result


def parse_attach(value: str) -> tuple[str, Path]:
    if "=" not in value:
        raise ValueError("attach must be ALIAS=PATH")
    alias, path = value.split("=", 1)
    alias = alias.strip()
    path = path.strip()
    if not alias or not path:
        raise ValueError("attach must be ALIAS=PATH")
    if not _IDENTIFIER_RE.match(alias):
        raise ValueError(f"invalid SQLite attach alias: {alias!r}")
    return alias, Path(path).expanduser()


def _connect_readonly(path: Path) -> sqlite3.Connection:
    resolved = path.resolve()
    if not resolved.exists():
        raise FileNotFoundError(f"database not found: {resolved}")
    uri = f"file:{quote(str(resolved))}?mode=ro"
    return sqlite3.connect(uri, uri=True)


def _write_markdown_table(result: ReportResult, out: IO[str]) -> None:
    columns = [_md_cell(column) for column in result.columns]
    out.write("| " + " | ".join(columns) + " |\n")
    out.write("| " + " | ".join("---" for _ in columns) + " |\n")
    for row in result.rows:
        out.write("| " + " | ".join(_md_cell(value) for value in row) + " |\n")


def _md_cell(value: object) -> str:
    if value is None:
        text = ""
    else:
        text = str(value)
    return text.replace("\\", "\\\\").replace("|", "\\|").replace("\n", " ")
