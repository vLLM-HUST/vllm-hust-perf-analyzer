"""Python interface to the bundled native analyzer; importing this never loads vLLM."""

from ._api import (
    AnalysisDatabase,
    AnalysisError,
    analyze,
    bundled_rules,
    export_perfetto,
)

__all__ = [
    "AnalysisDatabase",
    "AnalysisError",
    "analyze",
    "bundled_rules",
    "export_perfetto",
]
