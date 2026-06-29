import re
import unittest
from pathlib import Path

from traceloom.compute_prelude_timeline import (
    DeviceSelection,
    _augment_tree_node_cost_metrics,
    _report_filename_map,
    _render_graph_replay_type_summary,
    _render_tree_payload_readable,
)


class TreeNodeIdTests(unittest.TestCase):
    def test_report_filename_prefers_short_device_name(self) -> None:
        names = _report_filename_map(
            [
                DeviceSelection(
                    global_rank=2,
                    db_idx=1,
                    db_path=Path("/tmp/db1.sqlite"),
                    device_id=2,
                    main_event_count=1,
                    exec_us=1.0,
                    data_move_us=0.0,
                    total_main_us=1.0,
                ),
                DeviceSelection(
                    global_rank=1,
                    db_idx=2,
                    db_path=Path("/tmp/db2.sqlite"),
                    device_id=3,
                    main_event_count=1,
                    exec_us=1.0,
                    data_move_us=0.0,
                    total_main_us=1.0,
                ),
            ]
        )

        self.assertEqual(names[(1, 2)], "report_dev2.md")
        self.assertEqual(names[(2, 3)], "report_dev3.md")

    def test_readable_tree_uses_cost_metric_node_ids(self) -> None:
        payload = {
            "root": {
                "type": "Seq",
                "node_id": "N001",
                "items": [
                    {
                        "ord": 1,
                        "node": {
                            "type": "Atom",
                            "node_id": "N010",
                            "symbol": "A",
                            "op_label": "first",
                            "category": "exec",
                        },
                    },
                    {
                        "ord": 2,
                        "node": {
                            "type": "Repeat",
                            "node_id": "N020",
                            "count": 2,
                            "body": {
                                "type": "Seq",
                                "node_id": "N030",
                                "items": [
                                    {
                                        "ord": 1,
                                        "node": {
                                            "type": "Atom",
                                            "node_id": "N040",
                                            "symbol": "B",
                                            "op_label": "body",
                                            "category": "exec",
                                        },
                                    }
                                ],
                            },
                        },
                    },
                ],
            }
        }
        step_rows = [
            {"role": "compute", "dur_us": 1.0, "trace_anchor_count": 1, "symbol": "A"},
            {"role": "compute", "dur_us": 2.0, "trace_anchor_count": 1, "symbol": "B"},
            {"role": "compute", "dur_us": 3.0, "trace_anchor_count": 1, "symbol": "B"},
        ]
        aux_rows = [{} for _ in step_rows]

        metric_rows, _link_rows = _augment_tree_node_cost_metrics(
            payload,
            step_rows=step_rows,
            aux_slot_rows=aux_rows,
            macro_def_tokens={},
        )
        readable = _render_tree_payload_readable(payload)

        metric_ids = {str(row["node_id"]) for row in metric_rows}
        readable_ids = set(re.findall(r"\bN\d{3}\b", readable))

        self.assertEqual(readable_ids, metric_ids)
        self.assertIn("N003 Rep x2", readable)
        self.assertNotIn("N020 Rep x2", readable)
        self.assertNotIn("N030 Seq", readable)
        self.assertIn("op", readable)
        self.assertIn("avg_idle", readable)
        self.assertNotIn("Atom A", readable)

    def test_graph_replay_type_summary_groups_non_adjacent_launches(self) -> None:
        graph_rows = [
            {
                "graph_replay_symbol": "T001x4",
                "graph_template_symbol": "T001",
                "graph_exact_type_symbol": "G010",
                "graph_replay_unit_count": 4,
                "graph_replay_unit_source": "body:index|Index",
                "graph_body_hash": "hash-a",
                "dur_us": 10.0,
                "raw_child_task_count": 12,
                "raw_top_ops": "MatMul:8",
                "raw_control_tasks": "NOTIFY_WAIT:116",
            },
            {
                "graph_replay_symbol": "T002x1",
                "graph_template_symbol": "T002",
                "graph_exact_type_symbol": "G020",
                "graph_replay_unit_count": 1,
                "graph_replay_unit_source": "fallback:single",
                "graph_body_hash": "hash-b",
                "dur_us": 5.0,
                "raw_child_task_count": 3,
                "raw_top_ops": "RmsNorm:2",
            },
            {
                "graph_replay_symbol": "T001x4",
                "graph_template_symbol": "T001",
                "graph_exact_type_symbol": "G010",
                "graph_replay_unit_count": 4,
                "graph_replay_unit_source": "body:index|Index",
                "graph_body_hash": "hash-a",
                "dur_us": 15.0,
                "raw_child_task_count": 13,
                "raw_top_ops": "MatMul:9",
                "raw_control_tasks": "NOTIFY_WAIT:116",
            },
        ]

        summary = "\n".join(_render_graph_replay_type_summary(graph_rows))

        self.assertIn("| T001 | 2 | `T001x4:2` | `4:2` | 8 | 25.0 | 12.5 | 25 |", summary)
        self.assertIn("`G010:2`", summary)
        self.assertIn("`MatMul:17`", summary)
        self.assertIn("| T002 | 1 | `T002x1:1` | `1:1` | 1 | 5.0 | 5.0 | 3 |", summary)


if __name__ == "__main__":
    unittest.main()
