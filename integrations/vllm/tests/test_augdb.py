"""CPU-only real exporter -> native CLI -> SQL tests using synthetic profiler rows."""

import json
import os
import sqlite3
import subprocess
import tempfile
import unittest
from contextlib import contextmanager
from pathlib import Path
from unittest.mock import patch

from test_context import Output, context, scheduler

ROOT = Path(__file__).resolve().parents[3]
BINARY = os.environ.get(
    "TRACELOOM_TEST_BINARY", str(ROOT / "build/native-tests/native/traceloom")
)
FIXTURE = ROOT / "native/tests/fixtures/ascend_sqlite/minimal_smoke/msprof.sql"


class AugDbTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="context ' test ")
        self.root = Path(self.temp.name)
        self.raw = self.root / "profile.db"
        self.out = self.root / "analysis.db"
        self.env = patch.dict(
            os.environ,
            {
                "TRACELOOM_CONTEXT_DIR": str(self.root / "context"),
                "TRACELOOM_RUN_ID": "test-import",
            },
        )
        self.env.start()
        self.markers = []
        self.output = Output()
        context.record_step(scheduler(), self.output)
        with context.execution_scope(self.output, 0, self.marker):
            pass
        context.close_all()
        self.inputs = sorted((self.root / "context").glob("*.jsonl"))
        with sqlite3.connect(self.raw) as db:
            db.executescript(FIXTURE.read_text())
            db.executescript("""
              INSERT INTO STRING_IDS VALUES(101,'aclrtLaunchKernel'),(102,'launch');
              CREATE TABLE CANN_API(startNs INTEGER,endNs INTEGER,connectionId INTEGER,
                                    name INTEGER,globalTid INTEGER,type INTEGER);
              INSERT INTO CANN_API VALUES(20,40,700,101,476741369863,102);
              CREATE TABLE PYTORCH_API(startNs INTEGER,endNs INTEGER,name INTEGER,globalTid INTEGER);
            """)
            db.execute("INSERT INTO STRING_IDS VALUES(103,?)", (self.markers[0],))
            db.execute("INSERT INTO PYTORCH_API VALUES(10,90,103,476741369863)")

    def tearDown(self):
        context.close_all()
        self.env.stop()
        self.temp.cleanup()

    @contextmanager
    def marker(self, name):
        self.markers.append(name)
        yield

    def run_cli(self, inputs=None, output=None, success=True, extra=()):
        cmd = [
            BINARY,
            str(self.raw),
            "--threads",
            "1",
            "--output",
            str(output or self.out),
        ]
        for f in self.inputs if inputs is None else inputs:
            cmd.extend(["--context", str(f)])
        p = subprocess.run(
            cmd + list(extra), capture_output=True, text=True, timeout=30, check=False
        )
        if success:
            self.assertEqual(p.returncode, 0, p.stderr)
        else:
            self.assertNotEqual(p.returncode, 0, p.stderr)
        return p

    def rows(self, sql, args=()):
        with sqlite3.connect(self.out) as db:
            return db.execute(sql, args).fetchall()

    def rewrite(self, change):
        for f in self.inputs:
            rows = [json.loads(x) for x in f.read_text().splitlines()]
            rows = change(rows)
            for r in rows:
                if r["type"] == "summary":
                    r["written_records"] = sum(
                        x["type"] in ("step", "execution") for x in rows
                    )
            f.write_text("".join(json.dumps(x) + "\n" for x in rows))

    def test_export_import_and_reverse_audit(self):
        original = self.raw.read_bytes()
        baseline = self.root / "baseline.db"
        self.run_cli(inputs=[], output=baseline)
        self.run_cli()
        self.assertEqual(self.raw.read_bytes(), original)
        self.assertEqual(self.rows("PRAGMA integrity_check"), [("ok",)])
        self.assertEqual(
            self.rows("SELECT support_state FROM traceloom_v_execution_context"),
            [("supported_marker",)],
        )
        rows = self.rows(
            "SELECT step_id,event_id,start_ns,end_ns FROM traceloom_v_context_device_work"
        )
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0][0], self.output.traceloom_step["step_id"])
        self.assertEqual(
            rows[0][2:], (100, 160)
        )  # Device work extends past host marker.
        self.assertEqual(
            self.rows(
                "SELECT total_scheduled_tokens,scheduled_requests FROM traceloom_scheduler_step"
            ),
            [(4, 1)],
        )
        for table in [
            "traceloom_event",
            "traceloom_anchor",
            "traceloom_anchor_cost_breakdown",
        ]:
            with sqlite3.connect(baseline) as b:
                self.assertEqual(
                    self.rows("SELECT * FROM " + table),
                    b.execute("SELECT * FROM " + table).fetchall(),
                )
        with sqlite3.connect(self.out) as db:
            for (sql,) in db.execute(
                "SELECT example_sql FROM traceloom_analysis_surface WHERE surface_name IN ('scheduler_steps','execution_context','context_device_work','context_records','context_sources','scheduler_requests')"
            ).fetchall():
                db.execute(sql).fetchall()
        self.assertEqual(
            self.rows("SELECT DISTINCT capture_state FROM traceloom_context_source"),
            [("closed",)],
        )

    def test_absent_marker_remains_unmatched(self):
        with sqlite3.connect(self.raw) as db:
            db.execute("DELETE FROM PYTORCH_API")
        self.run_cli()
        self.assertEqual(
            self.rows("SELECT support_state FROM traceloom_v_execution_context"),
            [("marker_not_found",)],
        )
        self.assertEqual(self.rows("SELECT * FROM traceloom_v_context_device_work"), [])

    def test_duplicate_marker_rejected_as_ambiguous(self):
        with sqlite3.connect(self.raw) as db:
            db.execute("INSERT INTO PYTORCH_API SELECT * FROM PYTORCH_API")
        self.run_cli()
        self.assertEqual(
            self.rows(
                "SELECT support_state,marker_count FROM traceloom_v_execution_context"
            ),
            [("ambiguous_marker", 2)],
        )
        self.assertEqual(self.rows("SELECT * FROM traceloom_v_context_device_work"), [])

    def test_missing_decision_is_not_empty_step(self):
        self.rewrite(lambda rows: [r for r in rows if r["type"] != "step"])
        self.run_cli()
        self.assertEqual(
            self.rows("SELECT support_state FROM traceloom_v_execution_context"),
            [("missing_step",)],
        )
        self.assertEqual(self.rows("SELECT * FROM traceloom_v_context_device_work"), [])

    def test_other_host_thread_not_associated(self):
        with sqlite3.connect(self.raw) as db:
            db.execute("UPDATE PYTORCH_API SET globalTid=globalTid+1")
        self.run_cli()
        self.assertEqual(
            self.rows("SELECT support_state FROM traceloom_v_execution_context"),
            [("supported_marker",)],
        )
        self.assertEqual(self.rows("SELECT * FROM traceloom_v_context_device_work"), [])

    def test_bad_tokens_preserve_existing_output_atomically(self):
        self.run_cli()
        original = self.out.read_bytes()

        def bad(rows):
            for r in rows:
                if r["type"] == "step":
                    r["total_scheduled_tokens"] = 900
            return rows

        self.rewrite(bad)
        self.run_cli(success=False)
        self.assertEqual(self.out.read_bytes(), original)
        self.assertEqual(list(self.root.glob("analysis.db.tmp.*")), [])

    def test_duplicate_files_or_identities_fail(self):
        self.run_cli(inputs=self.inputs + self.inputs, success=False)
        duplicate = self.root / "duplicate.jsonl"
        duplicate.write_bytes(self.inputs[0].read_bytes())
        self.run_cli(inputs=self.inputs + [duplicate], success=False)

    def test_unknown_schema_and_partial_line_fail(self):
        def bad(rows):
            rows[0]["schema"] = "future"
            return rows

        self.rewrite(bad)
        self.run_cli(success=False)
        self.inputs[0].write_text('{"broken":')
        self.run_cli(success=False)

    def test_unclosed_capture_is_retained(self):
        self.rewrite(lambda rows: [r for r in rows if r["type"] != "summary"])
        self.run_cli()
        self.assertEqual(
            self.rows("SELECT DISTINCT capture_state FROM traceloom_context_source"),
            [("unclosed",)],
        )

    def test_reported_loss_is_not_hidden(self):
        def loss(rows):
            for r in rows:
                if r["type"] == "summary":
                    r["dropped_records"] = 3
            return rows

        self.rewrite(loss)
        self.run_cli()
        self.assertEqual(
            self.rows("SELECT DISTINCT capture_state FROM traceloom_context_source"),
            [("dropped_records",)],
        )

    def test_no_implicit_profiler_join_on_timestamp(self):
        with sqlite3.connect(self.raw) as db:
            db.execute("UPDATE STRING_IDS SET value='ordinary_scope' WHERE id=103")
        self.run_cli()
        self.assertEqual(
            self.rows("SELECT support_state FROM traceloom_v_execution_context"),
            [("marker_not_found",)],
        )

    def test_output_cannot_overwrite_context(self):
        original = self.inputs[0].read_bytes()
        self.run_cli(output=self.inputs[0], success=False)
        self.assertEqual(self.inputs[0].read_bytes(), original)

    def test_empty_step_remains_queryable(self):
        self.rewrite(lambda rows: [r for r in rows if r["type"] != "execution"])
        self.run_cli()
        self.assertEqual(
            self.rows(
                "SELECT recorded_executions,supported_markers FROM traceloom_v_scheduler_step_context"
            ),
            [(0, 0)],
        )
        sql = self.rows(
            "SELECT example_sql FROM traceloom_projection_recipe WHERE projection_name='step_execution_context'"
        )[0][0]
        rows = self.rows(
            sql,
            {"run_id": "test-import", "step_id": self.output.traceloom_step["step_id"]},
        )
        self.assertEqual(len(rows), 1)
        self.assertIsNone(rows[0][-2])

    def test_overlapping_execution_scopes_do_not_double_assign_work(self):
        new_id = "second-invocation"

        def duplicate_execution(rows):
            out = []
            for r in rows:
                out.append(r)
                if r["type"] == "execution":
                    r = dict(
                        r, execution_id=new_id, marker="traceloom.execution." + new_id
                    )
                    out.append(r)
            return out

        self.rewrite(duplicate_execution)
        with sqlite3.connect(self.raw) as db:
            db.execute(
                "INSERT INTO STRING_IDS VALUES(104,?)",
                ("traceloom.execution." + new_id,),
            )
            db.execute("INSERT INTO PYTORCH_API VALUES(10,90,104,476741369863)")
        self.run_cli()
        self.assertEqual(
            self.rows(
                "SELECT DISTINCT support_state FROM traceloom_context_runtime_call"
            ),
            [("ambiguous_execution_scope",)],
        )
        self.assertEqual(self.rows("SELECT * FROM traceloom_v_context_device_work"), [])

    def test_invalid_cache_counter_is_rejected(self):
        def bad(rows):
            for r in rows:
                if r["type"] == "step":
                    r["cache"]["free_blocks"] = -1
            return rows

        self.rewrite(bad)
        self.run_cli(success=False)

    def test_execution_phase_coverage_and_visible_timeline(self):
        self.rewrite(
            lambda rows: [
                dict(r, phase="sample_tokens") if r["type"] == "execution" else r
                for r in rows
            ]
        )
        timeline = self.root / "timeline.json"
        self.run_cli(extra=("--perfetto-out", str(timeline)))
        self.assertEqual(
            self.rows("SELECT phase FROM traceloom_v_context_device_work"),
            [("sample_tokens",)],
        )
        self.assertEqual(
            self.rows(
                "SELECT support_state FROM traceloom_v_context_device_coverage WHERE event_id='event-0'"
            ),
            [("supported_step",)],
        )
        events = json.loads(timeline.read_text())["traceEvents"]
        host = [e for e in events if e.get("cat") == "traceloom.context.host"]
        device = [e for e in events if e.get("cat") == "traceloom.context.device"]
        envelopes = [
            e for e in events if e.get("cat") == "traceloom.context.step_envelope"
        ]
        self.assertEqual(len(host), 1)
        self.assertEqual(len(device), 1)
        self.assertEqual(len(envelopes), 1)
        self.assertEqual(host[0]["args"]["phase"], "sample_tokens")
        self.assertEqual(
            device[0]["args"]["step_id"], self.output.traceloom_step["step_id"]
        )
        # Host scope is not incorrectly stretched to include device completion.
        self.assertLess(
            host[0]["ts"] + host[0]["dur"], device[0]["ts"] + device[0]["dur"]
        )

    def test_legacy_execution_phase_defaults_and_invalid_phase_rejected(self):
        self.rewrite(
            lambda rows: [{k: v for k, v in r.items() if k != "phase"} for r in rows]
        )
        self.run_cli()
        self.assertEqual(
            self.rows("SELECT phase FROM traceloom_context_execution"),
            [("execute_model",)],
        )
        self.rewrite(
            lambda rows: [
                dict(r, phase="guessed") if r["type"] == "execution" else r
                for r in rows
            ]
        )
        self.run_cli(success=False)

    def test_provider_text_nanosecond_timestamps(self):
        with sqlite3.connect(self.raw) as db:
            db.executescript("""
            ALTER TABLE PYTORCH_API RENAME TO old_api;
            CREATE TABLE PYTORCH_API(startNs TEXT,endNs TEXT,name INTEGER,globalTid INTEGER);
            INSERT INTO PYTORCH_API SELECT * FROM old_api;
            DROP TABLE old_api;
            """)
        self.run_cli()
        self.assertEqual(
            self.rows("SELECT support_state FROM traceloom_v_execution_context"),
            [("supported_marker",)],
        )
        with sqlite3.connect(self.raw) as db:
            db.execute("UPDATE PYTORCH_API SET startNs='10invalid'")
        self.run_cli()
        self.assertEqual(
            self.rows("SELECT support_state FROM traceloom_v_execution_context"),
            [("marker_not_found",)],
        )

    def queued_fixture(self):
        with sqlite3.connect(self.raw) as db:
            db.executescript("""
            ALTER TABLE PYTORCH_API ADD COLUMN connectionId INTEGER;
            ALTER TABLE PYTORCH_API ADD COLUMN type INTEGER;
            CREATE TABLE CONNECTION_IDS(id INTEGER,connectionId INTEGER);
            INSERT INTO CONNECTION_IDS VALUES(1,12345),(2,12345);
            INSERT INTO STRING_IDS VALUES(104,'Enqueue@kernel'),(105,'Dequeue@kernel');
            INSERT INTO PYTORCH_API VALUES(20,30,104,476741369863,1,50002);
            INSERT INTO PYTORCH_API VALUES(180,250,105,476741369864,2,50002);
            UPDATE CANN_API SET startNs=200,endNs=240,globalTid=476741369864;
            UPDATE TASK SET startNs=300,endNs=360 WHERE connectionId=700;
            """)

    def test_explicit_queue_bridge_across_threads_and_delayed_dispatch(self):
        self.queued_fixture()
        self.run_cli()
        self.assertEqual(
            self.rows("SELECT association_basis FROM traceloom_v_context_device_work"),
            [("task_queue_connection_then_provider_relation",)],
        )
        self.assertEqual(
            self.rows("SELECT connection_id FROM traceloom_context_queue_runtime"),
            [(12345,)],
        )
        self.assertEqual(
            self.rows("SELECT start_ns,end_ns FROM traceloom_v_context_device_work"),
            [(300, 360)],
        )

    def test_queue_correlation_mismatch_and_duplicates_are_not_guessed(self):
        self.queued_fixture()
        with sqlite3.connect(self.raw) as db:
            db.execute("UPDATE CONNECTION_IDS SET connectionId=12346 WHERE id=2")
        self.run_cli()
        self.assertEqual(
            self.rows("SELECT COUNT(*) FROM traceloom_v_context_device_work"), [(0,)]
        )
        with sqlite3.connect(self.raw) as db:
            db.execute("UPDATE CONNECTION_IDS SET connectionId=12345 WHERE id=2")
            db.execute("INSERT INTO PYTORCH_API VALUES(40,50,104,476741369863,1,50002)")
        self.run_cli()
        self.assertEqual(
            self.rows("SELECT COUNT(*) FROM traceloom_v_context_device_work"), [(0,)]
        )

    def test_queue_bridge_requires_cann_thread_and_containment(self):
        self.queued_fixture()
        with sqlite3.connect(self.raw) as db:
            db.execute("UPDATE CANN_API SET globalTid=476741369865")
        self.run_cli()
        self.assertEqual(
            self.rows("SELECT COUNT(*) FROM traceloom_v_context_device_work"), [(0,)]
        )
        with sqlite3.connect(self.raw) as db:
            db.execute("UPDATE CANN_API SET globalTid=476741369864,endNs=260")
        self.run_cli()
        self.assertEqual(
            self.rows("SELECT COUNT(*) FROM traceloom_v_context_device_work"), [(0,)]
        )

    def test_step_summary_and_concrete_structure_join(self):
        self.run_cli()
        params = {"run_id": "test-import"}
        integration = Path(__file__).resolve().parents[1]
        summary = self.rows(
            (integration / "step-device-summary.sql").read_text(), params
        )
        self.assertEqual(len(summary), 1)
        self.assertEqual(summary[0][-4], 1)
        self.assertAlmostEqual(summary[0][-3], 0.06)
        self.assertAlmostEqual(summary[0][-2], 0.06)
        self.assertAlmostEqual(summary[0][-1], 0.06)
        occurrence = self.rows(
            "SELECT parent_occurrence_id FROM traceloom_v_position_member WHERE event_id='event-0' LIMIT 1"
        )[0][0]
        linked = self.rows(
            (integration / "occurrence-step-context.sql").read_text(),
            {"occurrence_id": occurrence},
        )
        self.assertEqual(linked[0][1], self.output.traceloom_step["step_id"])
        self.assertEqual(linked[0][-2], 1)

    def test_scheduler_partition_config_and_exact_instance_membership(self):
        # Two identical A/B bodies, one on each known step. Device intervals
        # may lie outside the marker; only recorded identity supplies the cut.
        second = Output()
        context.record_step(scheduler(), second)
        with context.execution_scope(second, 0, self.marker):
            pass
        context.close_all()
        self.inputs = sorted((self.root / "context").glob("*.jsonl"))
        with sqlite3.connect(self.raw) as db:
            db.executescript("""
            DELETE FROM TASK;
            DELETE FROM COMPUTE_TASK_INFO;
            DELETE FROM COMMUNICATION_OP;
            DELETE FROM COMMUNICATION_TASK_INFO;
            DELETE FROM CANN_API;
            INSERT INTO STRING_IDS VALUES(106,'RmsNorm');
            INSERT INTO TASK VALUES
              (100,120,0,700,9001,1,10,0,3,99,2),
              (120,140,0,701,9002,1,10,0,3,100,2),
              (300,320,0,702,9003,1,10,0,3,101,2),
              (320,340,0,703,9004,1,10,0,3,102,2);
            INSERT INTO COMPUTE_TASK_INFO VALUES
              (9001,20,21,22),(9002,106,106,22),
              (9003,20,21,22),(9004,106,106,22);
            INSERT INTO CANN_API VALUES
              (20,30,700,101,476741369863,102),
              (40,50,701,101,476741369863,102),
              (210,220,702,101,476741369863,102),
              (240,250,703,101,476741369863,102);
            """)
            db.execute("INSERT INTO STRING_IDS VALUES(107,?)", (self.markers[-1],))
            db.execute("INSERT INTO PYTORCH_API VALUES(200,290,107,476741369863)")
        rules = ROOT / "configs/scheduler-step.yaml"
        self.run_cli(extra=("--rules-config", str(rules)))
        roots = self.rows("""SELECT occurrence_id,position_id FROM traceloom_v_position_occurrence
          WHERE parent_occurrence_id=(SELECT occurrence_id FROM traceloom_v_position_occurrence
                                      WHERE parent_occurrence_id IS NULL)""")
        self.assertEqual(len(roots), 2)
        self.assertEqual(roots[0][1], roots[1][1])  # definitions still shared
        sql = (
            Path(__file__).resolve().parents[1] / "occurrence-step-context.sql"
        ).read_text()
        self.assertEqual(
            [len(self.rows(sql, {"occurrence_id": r[0]})) for r in roots], [1, 1]
        )
        self.assertEqual(
            self.rows(
                "SELECT value FROM traceloom_metadata WHERE key='candidate_partition_by'"
            ),
            [("scheduler_step",)],
        )
        # No silent downgrade without worker linkage; previous output survives.
        before = self.out.read_bytes()
        self.run_cli(inputs=[], success=False, extra=("--rules-config", str(rules)))
        self.assertEqual(self.out.read_bytes(), before)

    def exact_replay_profile(self):
        fixture = ROOT / "native/tests/fixtures/ascend_sqlite/exact_hlt"
        self.raw = self.root / "exact" / "msprof.db"
        self.raw.parent.mkdir()
        with sqlite3.connect(self.raw) as db:
            db.executescript((fixture / "msprof.sql").read_text())
            db.execute(
                "CREATE TABLE PYTORCH_API(startNs INTEGER,endNs INTEGER,name TEXT,globalTid INTEGER)"
            )
            # Complete replay waves: prefill H/L/T, three decode H/L/T,
            # then an incomplete H/L suffix. Parallel body lanes overlap.
            for i, (start, end) in enumerate(
                [(80, 299), (380, 599), (680, 899), (980, 1199), (1280, 1499)]
            ):
                if i:
                    output = Output()
                    context.record_step(scheduler(), output)
                    with context.execution_scope(output, 0, self.marker):
                        pass
                    context.close_all()
                db.execute(
                    "INSERT INTO PYTORCH_API VALUES(?,?,?,1)",
                    (start, end, self.markers[-1]),
                )
        stream = self.raw.parent / "host/sqlite/stream_info.db"
        stream.parent.mkdir(parents=True)
        with sqlite3.connect(stream) as db:
            db.executescript((fixture / "host/sqlite/stream_info.sql").read_text())
        self.inputs = sorted((self.root / "context").glob("*.jsonl"))

    def test_scheduler_exact_replay_members_and_partitions(self):
        self.exact_replay_profile()
        self.run_cli()
        members = self.rows(
            "SELECT * FROM traceloom_graph_body_member ORDER BY launch_id,member_id"
        )
        launches = self.rows("SELECT * FROM traceloom_graph_launch ORDER BY launch_id")
        self.assertTrue(members)
        linked = self.rows(
            "SELECT DISTINCT launch_id,member_id FROM traceloom_v_context_replay_member"
        )
        self.assertEqual(len(linked), len(members))
        self.assertEqual(
            self.rows(
                "SELECT COUNT(*) FROM traceloom_v_context_replay_member WHERE association_basis NOT LIKE '%exact_replay_member'"
            ),
            [(0,)],
        )
        self.run_cli(
            extra=("--rules-config", str(ROOT / "configs/scheduler-step.yaml"))
        )
        self.assertEqual(
            members,
            self.rows(
                "SELECT * FROM traceloom_graph_body_member ORDER BY launch_id,member_id"
            ),
        )
        self.assertEqual(
            launches,
            self.rows("SELECT * FROM traceloom_graph_launch ORDER BY launch_id"),
        )
        self.assertEqual(
            self.rows(
                "SELECT COUNT(*) FROM (SELECT replay_unit_id FROM traceloom_v_context_replay_launch GROUP BY replay_unit_id HAVING COUNT(DISTINCT step_id)>1)"
            ),
            [(0,)],
        )
        self.assertEqual(
            self.rows(
                "SELECT COUNT(*) FROM traceloom_v_context_device_coverage WHERE candidate_steps>1"
            ),
            [(0,)],
        )
        # Every non-root occurrence stays within one supplied step, including
        # nested repeat instances; templates may still be reused across steps.
        query = (ROOT / "integrations/vllm/occurrence-step-context.sql").read_text()
        for (occurrence,) in self.rows(
            "SELECT occurrence_id FROM traceloom_v_position_occurrence WHERE parent_occurrence_id IS NOT NULL"
        ):
            self.assertLessEqual(
                len(self.rows(query, {"occurrence_id": occurrence})), 1
            )
        self.assertTrue(self.rows("SELECT * FROM traceloom_v_context_anchor"))

    def test_replay_timeline_realizes_each_launch_and_parallel_lane(self):
        self.exact_replay_profile()
        self.run_cli()
        destination = self.root / "replay.json"

        def export():
            subprocess.run(
                [
                    BINARY,
                    "export-perfetto",
                    str(self.out),
                    "--output",
                    str(destination),
                ],
                check=True,
                capture_output=True,
                timeout=30,
            )
            return json.loads(destination.read_text())

        trace = export()
        members = [
            e for e in trace["traceEvents"] if e.get("cat") == "traceloom.replay.member"
        ]
        expected = self.rows(
            "SELECT launch_id,member_id,start_ns,end_ns,stream_id FROM traceloom_replay_cost_member"
        )
        self.assertEqual(len(members), len(expected))
        by_identity = {
            (e["args"]["launch_id"], e["args"]["member_id"]): e for e in members
        }
        self.assertEqual(len(by_identity), len(expected))
        # Two parallel streams must not be accidentally stacked on one lane.
        streams = {}
        for launch, member, start, end, stream in expected:
            e = by_identity[launch, member]
            self.assertEqual(e["pid"], 110)  # primary analysis track, not raw/context
            self.assertAlmostEqual(e["dur"], (end - start) / 1000, places=3)
            streams.setdefault(stream, set()).add(e["tid"])
        tids = [tid for lanes in streams.values() for tid in lanes]
        self.assertEqual(len(tids), len(set(tids)))
        # Missing realization evidence must not borrow another launch's member.
        lost = expected[0][1]
        with sqlite3.connect(self.out) as db:
            db.execute(
                "DELETE FROM traceloom_replay_cost_aggregate_member WHERE member_id=?",
                (lost,),
            )
        remaining = [
            e
            for e in export()["traceEvents"]
            if e.get("cat") == "traceloom.replay.member"
        ]
        self.assertLess(len(remaining), len(members))
        self.assertNotIn(lost, [e["args"]["member_id"] for e in remaining])

    def test_replay_repeat_iteration_geometry(self):
        self.exact_replay_profile()
        with sqlite3.connect(self.raw) as db:
            db.executescript("""
            UPDATE TASK SET startNs=startNs*100,endNs=endNs*100;
            UPDATE CANN_API SET startNs=startNs*100,endNs=endNs*100;
            UPDATE PYTORCH_API SET startNs=startNs*100,endNs=endNs*100;
            INSERT INTO TASK
            SELECT startNs+10,endNs+10,deviceId,connectionId,globalTaskId,
                   globalPid,taskType,contextId,streamId,taskId+10000,modelId
            FROM TASK WHERE taskType=30;
            """)
        self.run_cli()
        destination = self.root / "repeat-replay.json"
        subprocess.run(
            [BINARY, "export-perfetto", str(self.out), "--output", str(destination)],
            check=True,
            capture_output=True,
            timeout=30,
        )
        events = json.loads(destination.read_text())["traceEvents"]
        bodies = [e for e in events if e.get("cat") == "traceloom.replay.repeat_body"]
        self.assertTrue(
            bodies
        )  # atomic repeat bodies must also have iteration tracks
        for event in bodies:
            self.assertEqual(
                event["args"]["position_end_exclusive"]
                - event["args"]["position_start"],
                1,
            )
            self.assertAlmostEqual(event["dur"], 0.1, places=6)

    def test_replay_tp1_control_rows_do_not_require_communication_table(self):
        self.exact_replay_profile()
        with sqlite3.connect(self.raw) as db:
            db.executescript("""
            DROP TABLE COMMUNICATION_TASK_INFO;
            INSERT INTO STRING_IDS VALUES(201,'NOP'),(202,'MEM_WAIT_VALUE');
            INSERT INTO TASK VALUES
              (116,117,0,8001,8001,1,201,0,42,1001,10),
              (117,118,0,8002,8002,1,202,0,42,1002,10);
            """)
        self.run_cli()
        count = self.rows("SELECT COUNT(*) FROM traceloom_graph_body_member")[0][0]
        self.assertGreater(count, 0)
        self.assertTrue(
            self.rows(
                "SELECT * FROM traceloom_v_context_replay_launch WHERE graph_launch_occurrence_id=0"
            )
        )
        # Unknown executable work is NOT made safe by classifying known controls.
        with sqlite3.connect(self.raw) as db:
            db.execute("UPDATE STRING_IDS SET value='UNKNOWN_EXECUTABLE' WHERE id=201")
        self.run_cli()
        self.assertEqual(
            self.rows(
                "SELECT COUNT(*) FROM traceloom_v_context_replay_launch WHERE graph_launch_occurrence_id=0"
            ),
            [(0,)],
        )

    def test_official_torch_export_discovers_one_raw_capture_mapping(self):
        self.exact_replay_profile()
        capture = self.raw.parent
        output = capture / "ASCEND_PROFILER_OUTPUT"
        output.mkdir()
        official = output / "ascend_pytorch_profiler_0.db"
        self.raw.rename(official)
        self.raw = official
        raw = capture / "PROF_first"
        raw.mkdir()
        (capture / "host").rename(raw / "host")
        self.run_cli(
            extra=("--rules-config", str(ROOT / "configs/scheduler-step.yaml"))
        )
        self.assertTrue(self.rows("SELECT * FROM traceloom_v_context_replay_member"))
        # Two sibling containers are ambiguous even if only one was parsed.
        (capture / "PROF_second").mkdir()
        self.run_cli()
        self.assertEqual(
            self.rows("SELECT COUNT(*) FROM traceloom_graph_body_member"), [(0,)]
        )

    def test_replay_member_conflicting_direct_step_is_not_assigned(self):
        self.exact_replay_profile()
        # First wave's member (connection 400) also has a directly correlated
        # runtime endpoint inside the second step. Neither wins by timestamp.
        with sqlite3.connect(self.raw) as db:
            db.execute("INSERT INTO CANN_API VALUES(450,455,0,1,400,20)")
        self.run_cli()
        ambiguous = self.rows(
            "SELECT event_id FROM traceloom_v_context_device_coverage WHERE candidate_steps>1"
        )
        self.assertTrue(ambiguous)
        for (event,) in ambiguous:
            self.assertEqual(
                self.rows(
                    "SELECT COUNT(*) FROM traceloom_v_context_replay_member WHERE event_id=?",
                    (event,),
                ),
                [(0,)],
            )
            self.assertEqual(
                self.rows(
                    "SELECT COUNT(*) FROM traceloom_v_context_device_work WHERE event_id=?",
                    (event,),
                ),
                [(0,)],
            )

    def test_scheduler_replay_cross_step_or_missing_fails_closed(self):
        self.exact_replay_profile()
        self.run_cli()
        before = self.out.read_bytes()
        with sqlite3.connect(self.raw) as db:
            db.execute("UPDATE PYTORCH_API SET endNs=499 WHERE startNs=380")
            db.execute("UPDATE PYTORCH_API SET startNs=580 WHERE startNs=680")
        p = self.run_cli(
            success=False,
            extra=("--rules-config", str(ROOT / "configs/scheduler-step.yaml")),
        )
        self.assertIn(
            "protected replay has missing or conflicting step identity", p.stderr
        )
        self.assertEqual(before, self.out.read_bytes())
        # Unconstrained analysis retains evidence even when a replay unit
        # spans supplied steps; it must not silently 'repair' that evidence.
        self.run_cli()
        self.assertTrue(
            self.rows(
                "SELECT replay_unit_id FROM traceloom_v_context_replay_launch GROUP BY replay_unit_id HAVING COUNT(DISTINCT step_id)>1"
            )
        )
        with sqlite3.connect(self.raw) as db:
            db.execute("DELETE FROM PYTORCH_API")
        self.run_cli()
        self.assertEqual(
            self.rows("SELECT COUNT(*) FROM traceloom_v_context_replay_member"), [(0,)]
        )

    def test_partition_unknown_config_is_rejected(self):
        rules = self.root / "bad.yaml"
        rules.write_text("""schema: traceloom-analysis-rules-v1
macro_matching:
  partition_by: closest_timestamp
""")
        self.run_cli(success=False, extra=("--rules-config", str(rules)))


if __name__ == "__main__":
    unittest.main()
