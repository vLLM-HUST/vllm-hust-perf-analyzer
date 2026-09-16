"""Small, process-isolated bindings to the single production C++ implementation."""

from __future__ import annotations

import os
import sqlite3
import subprocess
import tempfile
from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from pathlib import Path

PathLike = str | os.PathLike[str]


def native_binary() -> Path:
    binary = Path(__file__).resolve().parent / "_native" / "bin" / "traceloom"
    if not binary.is_file():
        raise FileNotFoundError(
            "Bundled native analyzer missing; install a built TraceLoom wheel"
        )
    return binary


def bundled_rules(name: str) -> Path:
    """Return a packaged YAML path: 'scheduler-step' or 'deepseekv4'."""
    if name not in {"scheduler-step", "deepseekv4"}:
        raise ValueError("Unknown bundled rules; choose scheduler-step or deepseekv4")
    return (
        native_binary().parent.parent
        / "share"
        / "traceloom"
        / "models"
        / f"{name}.yaml"
    )


class AnalysisError(RuntimeError):
    """Native failure, with exit status and a bounded tail of diagnostic output."""

    def __init__(self, command: Sequence[str], returncode: int, diagnostic_tail: str):
        self.command = tuple(command)
        self.returncode = returncode
        self.diagnostic_tail = diagnostic_tail
        super().__init__(
            f"TraceLoom exited with status {returncode}:\n{diagnostic_tail}"
        )


def _run(arguments: Sequence[str], timeout: float | None) -> None:
    command = [str(native_binary()), *arguments]
    # Native logs can be large. Keep them off Python's heap and exception messages bounded.
    with tempfile.TemporaryFile() as log:
        process = subprocess.run(
            command, stdout=log, stderr=log, timeout=timeout, check=False
        )
        if process.returncode:
            size = log.tell()
            log.seek(max(0, size - 65536))
            raise AnalysisError(
                command, process.returncode, log.read().decode("utf-8", "replace")
            )


def _file(path: PathLike) -> Path:
    resolved = Path(path).expanduser().resolve(strict=True)
    if not resolved.is_file():
        raise ValueError(
            "Python analyze() expects one profiler database file; use the CLI for directory discovery"
        )
    return resolved


@dataclass(frozen=True)
class AnalysisDatabase:
    """An AugDB file, not an in-memory copy or a live inference-engine handle."""

    path: Path

    def __post_init__(self) -> None:
        object.__setattr__(self, "path", _file(self.path))

    def query(
        self, sql: str, parameters: Sequence = (), *, limit: int = 1000
    ) -> list[dict]:
        """Execute one read-only query, returning at most limit rows as dictionaries.

        Raises ValueError rather than silently truncating a larger result. This
        is a local analysis interface, not an endpoint for untrusted SQL.
        """
        if isinstance(limit, bool) or not isinstance(limit, int) or limit < 1:
            raise ValueError("limit must be a positive integer")
        connection = sqlite3.connect(self.path.as_uri() + "?mode=ro", uri=True)
        try:
            connection.row_factory = sqlite3.Row
            connection.execute("PRAGMA query_only=ON")
            rows = connection.execute(sql, parameters).fetchmany(limit + 1)
            if len(rows) > limit:
                raise ValueError(
                    f"Query exceeds row limit {limit}; narrow it or supply a larger limit"
                )
            return [dict(row) for row in rows]
        finally:
            connection.close()

    def export_perfetto(
        self, output: PathLike, *, timeout: float | None = None
    ) -> Path:
        return export_perfetto(self, output, timeout=timeout)


def analyze(
    source: PathLike,
    *,
    output: PathLike,
    context: Iterable[PathLike] = (),
    rules_config: PathLike | None = None,
    structural_order: str = "device",
    threads: int | None = None,
    timeout: float | None = None,
) -> AnalysisDatabase:
    """Analyze one profiler SQLite file using the bundled native implementation.

    Context producers and YAML rules are optional. Native atomic publication and
    source-protection checks remain authoritative. TimeoutExpired is propagated.
    """
    source_path = _file(source)
    target = Path(output).expanduser().resolve()
    if isinstance(context, (str, os.PathLike)):
        raise TypeError("context must be an iterable of paths, not a single path")
    if structural_order not in {"device", "host-launch"}:
        raise ValueError("structural_order must be device or host-launch")
    command = [
        str(source_path),
        "--output",
        str(target),
        "--structural-order",
        structural_order,
    ]
    for producer in context:
        command.extend(["--context", str(_file(producer))])
    if rules_config is not None:
        command.extend(["--rules-config", str(_file(rules_config))])
    if threads is not None:
        if isinstance(threads, bool) or not isinstance(threads, int) or threads < 1:
            raise ValueError("threads must be a positive integer")
        command.extend(["--threads", str(threads)])
    _run(command, timeout)
    return AnalysisDatabase(target)


def export_perfetto(
    database: AnalysisDatabase | PathLike,
    output: PathLike,
    *,
    timeout: float | None = None,
) -> Path:
    """Export the existing AugDB to JSON or .json.gz; gzip output requires gzip."""
    source = (
        database.path if isinstance(database, AnalysisDatabase) else _file(database)
    )
    target = Path(output).expanduser().resolve()
    if target == source or (target.exists() and target.samefile(source)):
        raise ValueError("Perfetto output must not overwrite the analysis database")
    _run(["export-perfetto", str(source), "--output", str(target)], timeout)
    if not target.is_file():
        raise FileNotFoundError(target)
    return target
