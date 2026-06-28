import re
import unittest

from traceloom.compute_prelude_timeline import (
    _augment_tree_node_cost_metrics,
    _render_tree_payload_readable,
)


class TreeNodeIdTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
