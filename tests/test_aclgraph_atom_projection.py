import unittest

from traceloom.compute_prelude_timeline import (
    MainEvent,
    _aclgraph_atom_main_events,
    _main_event_source_key,
    _merge_aclgraph_atoms_with_main_events,
)
from traceloom.ascend_aclgraph import (
    _canonical_graph_body_token,
    _canonical_graph_noise_token,
    _overlap_rows_by_interval,
)
from traceloom.msprof_reader import StreamEvent, canonical_device_label


def _event(start: int, end: int, label: str) -> MainEvent:
    ev = StreamEvent(
        start_ns=start,
        end_ns=end,
        device_id=0,
        stream_id=1,
        task_id=start,
        global_task_id=start,
        connection_id=-1,
        task_type="KERNEL",
        label=label,
        category="exec",
        source_table="TASK",
        source_key=f"task={label}",
    )
    return MainEvent(event=ev, role="compute", symbol=label)


class AclGraphAtomProjectionTests(unittest.TestCase):
    def test_device_semantic_label_prefers_op_type(self) -> None:
        self.assertEqual(
            canonical_device_label("aclnnAddmm_MatMulCommon_MatMulV2", "MatMulV2", category="exec"),
            "MatMul",
        )
        self.assertEqual(
            canonical_device_label("aclnnAddmm_CastAiCore_Cast", "Cast", category="exec"),
            "Cast",
        )

    def test_aclgraph_body_hash_uses_anchor_compute_tokens(self) -> None:
        matmul = {"task_label": "KERNEL_AICORE"}
        notify = {"task_label": "NOTIFY_RECORD"}
        cast = {"task_label": "KERNEL_AIVEC"}
        shape = {"task_label": "KERNEL_AIVEC"}

        self.assertEqual(
            _canonical_graph_body_token(matmul, {"op_type": "MatMulV2"}),
            "matmul|MatMul",
        )
        self.assertEqual(_canonical_graph_body_token(notify, {}), "")
        self.assertEqual(_canonical_graph_body_token(cast, {"op_type": "Cast"}), "cast|Cast")
        self.assertEqual(_canonical_graph_body_token(shape, {"op_type": "Reshape"}), "shape|Shape")
        self.assertEqual(
            _canonical_graph_noise_token(notify, {}),
            "control_or_transfer:notify_record",
        )

    def test_graph_atom_replaces_fully_covered_outer_events(self) -> None:
        partial = _event(0, 12, "A")
        covered = _event(14, 18, "B")
        outside = _event(30, 40, "C")
        graph_rows = [
            {
                "graph_provider": "aclgraph",
                "graph_event_idx": 1,
                "graph_type_symbol": "G001",
                "graph_type_label": "ACLGraphType G001",
                "source_key": "provider=aclgraph;replay_idx=1",
                "source_table": "ACLGRAPH_REPLAY",
                "task_type": "ACL_GRAPH_REPLAY",
                "device_id": 0,
                "stream_id": 7,
                "raw_child_streams": "7..8",
                "start_ns": 10,
                "end_ns": 20,
            }
        ]

        graph_events = _aclgraph_atom_main_events(graph_rows)
        merged, covered_keys, diagnostics = _merge_aclgraph_atoms_with_main_events(
            [partial, covered, outside],
            graph_events,
        )

        self.assertEqual([item.symbol for item in merged], ["A", "G001", "C"])
        self.assertIn(_main_event_source_key(covered), covered_keys)
        self.assertNotIn(_main_event_source_key(partial), covered_keys)
        self.assertIn("partial_overlap_kept", {row["relation"] for row in diagnostics})
        self.assertEqual(graph_events[0].event.category, "graph")
        self.assertEqual(graph_events[0].source_stream_ids, (7, 8))

    def test_interval_sweep_matches_naive_overlap_semantics(self) -> None:
        intervals = [(10, 20), (30, 40), (15, 15)]
        rows = [
            {"start_ns": 0, "end_ns": 9, "stream_id": 1, "label": "before"},
            {"start_ns": 5, "end_ns": 10, "stream_id": 2, "label": "touch_start"},
            {"start_ns": 15, "end_ns": 16, "stream_id": 1, "label": "inside"},
            {"start_ns": 20, "end_ns": 25, "stream_id": 3, "label": "touch_end"},
            {"start_ns": 25, "end_ns": 35, "stream_id": 1, "label": "second"},
            {"start_ns": 40, "end_ns": 45, "stream_id": 1, "label": "half_open_touch"},
        ]

        closed = _overlap_rows_by_interval(intervals, rows, touching_overlaps=True)
        closed_naive = [
            [
                row
                for row in sorted(rows, key=lambda item: (item["start_ns"], item["end_ns"], item["stream_id"]))
                if row["start_ns"] <= end and row["end_ns"] >= start
            ]
            for start, end in intervals
        ]
        self.assertEqual(closed, closed_naive)

        half_open = _overlap_rows_by_interval(intervals, rows, touching_overlaps=False)
        half_open_naive = [
            [
                row
                for row in sorted(rows, key=lambda item: (item["start_ns"], item["end_ns"], item["stream_id"]))
                if row["start_ns"] < end and row["end_ns"] > start
            ]
            for start, end in intervals
        ]
        self.assertEqual(half_open, half_open_naive)


if __name__ == "__main__":
    unittest.main()
