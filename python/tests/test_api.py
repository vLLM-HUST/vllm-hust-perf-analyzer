"""Installed-wheel acceptance against a real native pipeline and a small SQL fixture."""

import gzip
import importlib.metadata
import json
import os
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import traceloom
from traceloom._api import native_binary

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "native/tests/fixtures/ascend_sqlite/minimal_smoke/msprof.sql"


class PythonApiTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="traceloom wheel ' ")
        self.root = Path(self.temp.name)
        self.source = self.root / "input # profile.db"
        self.output = self.root / "analysis.db"
        with sqlite3.connect(self.source) as database:
            database.executescript(FIXTURE.read_text())

    def tearDown(self):
        self.temp.cleanup()

    def analyze(self, **kwargs):
        return traceloom.analyze(
            self.source, output=self.output, threads=1, timeout=30, **kwargs
        )

    def test_bundled_native_and_optional_runtime(self):
        self.assertNotIn("vllm", sys.modules)
        self.assertNotIn("torch", sys.modules)
        self.assertEqual(importlib.metadata.version("traceloom-vllm-context"), "0.1.0")
        self.assertTrue(native_binary().is_file())
        self.assertTrue(traceloom.bundled_rules("deepseekv4").is_file())
        self.assertTrue(traceloom.bundled_rules("scheduler-step").is_file())
        with self.assertRaises(ValueError):
            traceloom.bundled_rules("../../other")
        result = subprocess.run(
            [sys.executable, "-m", "traceloom", "--version"],
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertIn("0.1.0", result.stdout)

    def test_analysis_query_export_and_reopen(self):
        result = self.analyze()
        self.assertEqual(result.path, self.output)
        rows = result.query("SELECT COUNT(*) AS n FROM traceloom_v_position_occurrence")
        self.assertGreater(rows[0]["n"], 0)
        self.assertEqual(
            result.query("SELECT ? AS value", ("quotes ' and space",)),
            [{"value": "quotes ' and space"}],
        )
        with self.assertRaises(ValueError):
            result.query("SELECT 1 UNION ALL SELECT 2", limit=1)
        with self.assertRaises(sqlite3.OperationalError):
            result.query("CREATE TABLE should_not_exist(x)")
        with self.assertRaises(ValueError):
            result.query("SELECT 1", limit=0)
        export = result.export_perfetto(self.root / "trace.json.gz", timeout=30)
        with gzip.open(export, "rt") as stream:
            self.assertGreater(len(json.load(stream)["traceEvents"]), 0)
        reopened = traceloom.AnalysisDatabase(self.output)
        self.assertEqual(reopened.query("SELECT 1 AS n"), [{"n": 1}])
        plain = traceloom.export_perfetto(self.output, self.root / "trace.json")
        self.assertTrue(plain.is_file())

    def test_invalid_context_preserves_output_and_inputs(self):
        original = self.source.read_bytes()
        self.analyze()
        before = self.output.read_bytes()
        context = self.root / "invalid.jsonl"
        context.write_text("not-json\n")
        with self.assertRaises(traceloom.AnalysisError) as caught:
            self.analyze(context=[context])
        self.assertNotEqual(caught.exception.returncode, 0)
        self.assertLessEqual(len(caught.exception.diagnostic_tail.encode()), 3 * 65536)
        self.assertEqual(self.output.read_bytes(), before)
        self.assertEqual(self.source.read_bytes(), original)
        self.assertEqual(context.read_text(), "not-json\n")

    def test_rules_forwarded_and_native_rejection_preserved(self):
        with self.assertRaises(traceloom.AnalysisError):
            self.analyze(rules_config=traceloom.bundled_rules("scheduler-step"))
        self.assertFalse(self.output.exists())
        result = self.analyze(rules_config=traceloom.bundled_rules("deepseekv4"))
        self.assertTrue(result.path.is_file())

    def test_output_cannot_replace_input_or_database(self):
        before = self.source.read_bytes()
        with self.assertRaises(traceloom.AnalysisError):
            traceloom.analyze(self.source, output=self.source, threads=1)
        self.assertEqual(self.source.read_bytes(), before)
        result = self.analyze()
        with self.assertRaises(ValueError):
            result.export_perfetto(self.output)
        alias = self.root / "alias.db"
        os.link(self.output, alias)
        with self.assertRaises(ValueError):
            result.export_perfetto(alias)

    def test_invalid_arguments_and_timeout(self):
        with self.assertRaises(TypeError):
            self.analyze(context="single-file.jsonl")
        with self.assertRaises(ValueError):
            self.analyze(structural_order="guessed")
        with self.assertRaises(ValueError):
            traceloom.analyze(self.root, output=self.output)
        with self.assertRaises(FileNotFoundError):
            traceloom.analyze(self.root / "missing", output=self.output)
        with self.assertRaises(ValueError):
            traceloom.analyze(self.source, output=self.output, threads=0)
        with self.assertRaises(subprocess.TimeoutExpired):
            traceloom.analyze(self.source, output=self.output, threads=1, timeout=0)


if __name__ == "__main__":
    unittest.main()
