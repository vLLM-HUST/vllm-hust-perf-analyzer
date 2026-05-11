from __future__ import annotations

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


def discover_msprof_dbs(run_dir: Path) -> List[Path]:
    out: List[Path] = []
    for db in sorted(run_dir.glob("PROF_*/msprof_*.db")):
        with sqlite3.connect(str(db)) as conn:
            if _table_exists(conn, "TASK"):
                out.append(db.resolve())
    if not out:
        raise FileNotFoundError(f"no msprof DB with TASK table under run_dir={run_dir}")
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
