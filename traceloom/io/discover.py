from __future__ import annotations

import csv
import json
import re
import sqlite3
from pathlib import Path
from typing import List


KEY_TOP_FILES = (
    "run_meta.env",
    "workload_result.json",
    "msprof.log",
    "prof_dirs.txt",
    "key_files.txt",
    "exit_code.txt",
)


def resolve_run_id(run_dir: Path) -> str:
    return run_dir.resolve().name


def _table_exists(conn: sqlite3.Connection, name: str) -> bool:
    row = conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1",
        (name,),
    ).fetchone()
    return row is not None


def _table_row_count(conn: sqlite3.Connection, name: str) -> int:
    try:
        row = conn.execute(f'SELECT COUNT(*) FROM "{name}"').fetchone()
    except sqlite3.Error:
        return 0
    return int(row[0] if row and row[0] is not None else 0)


def _table_names(conn: sqlite3.Connection) -> list[str]:
    return [
        str(row[0])
        for row in conn.execute("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")
        if row[0] is not None
    ]


def _is_hipprof_db(db: Path) -> bool:
    try:
        with sqlite3.connect(f"file:{db.resolve()}?immutable=1", uri=True) as conn:
            tables = _table_names(conn)
            trace_tables = [
                name
                for name in tables
                if name.startswith("HIPOPS_") or name.startswith("HIPCOPY_") or name.startswith("HIP_")
            ]
            return any(_table_row_count(conn, name) > 0 for name in trace_tables)
    except sqlite3.Error:
        return False


def _is_rocprof_csv(path: Path) -> bool:
    try:
        with path.open("r", encoding="utf-8", newline="", errors="replace") as f:
            reader = csv.reader(f)
            header = next(reader, [])
    except (OSError, StopIteration):
        return False
    required = {"Kernel_Name", "Start_Timestamp", "End_Timestamp"}
    return required.issubset(set(header))


def _safe_stem(path: Path) -> str:
    text = "_".join(path.with_suffix("").parts[-4:])
    text = re.sub(r"[^A-Za-z0-9_.-]+", "_", text)
    return text[-120:] or "rocprof"


def _convert_rocprof_csv_to_sqlite(csv_path: Path, target: Path) -> Path:
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        target.unlink()
    with sqlite3.connect(str(target)) as conn:
        conn.execute(
            """
            CREATE TABLE ROCPROF_KERNEL (
                Dispatch_ID INTEGER,
                GPU_ID INTEGER,
                Queue_ID INTEGER,
                Queue_Index INTEGER,
                PID INTEGER,
                TID INTEGER,
                GRD TEXT,
                WGR TEXT,
                LDS INTEGER,
                SCR INTEGER,
                Arch_VGPR INTEGER,
                ACCUM_VGPR INTEGER,
                SGPR INTEGER,
                Wave_Size INTEGER,
                SIG TEXT,
                OBJ TEXT,
                Kernel_Name TEXT,
                Start_Timestamp INTEGER,
                End_Timestamp INTEGER,
                Correlation_ID TEXT,
                raw_json TEXT
            )
            """
        )
        conn.execute("CREATE TABLE ROCPROF_METADATA (key TEXT PRIMARY KEY, value TEXT NOT NULL)")
        conn.execute(
            "INSERT INTO ROCPROF_METADATA(key, value) VALUES (?, ?)",
            ("source_csv", str(csv_path.resolve())),
        )
        rows = []
        with csv_path.open("r", encoding="utf-8", newline="", errors="replace") as f:
            reader = csv.DictReader(f)
            for row in reader:
                if not row or not row.get("Kernel_Name"):
                    continue
                rows.append(
                    (
                        _int(row.get("Dispatch_ID")),
                        _int(row.get("GPU_ID")),
                        _int(row.get("Queue_ID")),
                        _int(row.get("Queue_Index")),
                        _int(row.get("PID")),
                        _int(row.get("TID")),
                        row.get("GRD", ""),
                        row.get("WGR", ""),
                        _int(row.get("LDS")),
                        _int(row.get("SCR")),
                        _int(row.get("Arch_VGPR")),
                        _int(row.get("ACCUM_VGPR")),
                        _int(row.get("SGPR")),
                        _int(row.get("Wave_Size")),
                        row.get("SIG", ""),
                        row.get("OBJ", ""),
                        row.get("Kernel_Name", ""),
                        _int(row.get("Start_Timestamp")),
                        _int(row.get("End_Timestamp")),
                        row.get("Correlation_ID", ""),
                        json.dumps(dict(row), ensure_ascii=False, sort_keys=True),
                    )
                )
        conn.executemany(
            """
            INSERT INTO ROCPROF_KERNEL(
                Dispatch_ID, GPU_ID, Queue_ID, Queue_Index, PID, TID, GRD, WGR,
                LDS, SCR, Arch_VGPR, ACCUM_VGPR, SGPR, Wave_Size, SIG, OBJ,
                Kernel_Name, Start_Timestamp, End_Timestamp, Correlation_ID, raw_json
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            rows,
        )
        conn.commit()
    return target.resolve()


def _int(value: object) -> int | None:
    if value is None or value == "":
        return None
    try:
        return int(str(value))
    except ValueError:
        return None


def discover_msprof_dbs(run_dir: Path) -> List[Path]:
    out: List[Path] = []
    for db in sorted(run_dir.glob("PROF_*/msprof_*.db")):
        with sqlite3.connect(str(db)) as conn:
            if _table_exists(conn, "TASK"):
                out.append(db.resolve())
    if not out:
        raise FileNotFoundError(f"no msprof DB with TASK table under run_dir={run_dir}")
    return out


def discover_hygon_profile_dbs(run_dir: Path, *, generated_dir: Path) -> List[Path]:
    out: List[Path] = []
    for db in sorted(run_dir.rglob("*.db")):
        if db.name.endswith(".traceloom_augmented.db"):
            continue
        if _is_hipprof_db(db):
            out.append(db.resolve())

    generated: List[Path] = []
    for csv_path in sorted(run_dir.rglob("*.csv")):
        if not _is_rocprof_csv(csv_path):
            continue
        target = generated_dir / f"{_safe_stem(csv_path)}.rocprof.sqlite"
        generated.append(_convert_rocprof_csv_to_sqlite(csv_path, target))

    return out + generated


def has_hygon_profile_data(run_dir: Path) -> bool:
    for db in sorted(run_dir.rglob("*.db")):
        if db.name.endswith(".traceloom_augmented.db"):
            continue
        if _is_hipprof_db(db):
            return True
    return any(_is_rocprof_csv(csv_path) for csv_path in sorted(run_dir.rglob("*.csv")))


def discover_profile_dbs(run_dir: Path, *, generated_dir: Path | None = None) -> List[Path]:
    try:
        return discover_msprof_dbs(run_dir)
    except FileNotFoundError:
        pass
    if generated_dir is None:
        generated_dir = run_dir / ".traceloom_generated"
    out = discover_hygon_profile_dbs(run_dir, generated_dir=generated_dir)
    if not out:
        raise FileNotFoundError(f"no supported msprof or Hygon profile data under run_dir={run_dir}")
    return out


def inventory_raw_layout(run_dir: Path) -> dict:
    run_dir = run_dir.resolve()
    prof_dirs = sorted(p.name for p in run_dir.glob("PROF_*") if p.is_dir())
    present_top_files = [name for name in KEY_TOP_FILES if (run_dir / name).exists()]
    return {
        "run_dir": str(run_dir),
        "prof_dir_count": len(prof_dirs),
        "prof_dirs": prof_dirs,
        "top_files_present": present_top_files,
    }
