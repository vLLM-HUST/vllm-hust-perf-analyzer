import unittest
from collections import Counter

from traceloom.compute_prelude_timeline import (
    MainEvent,
    _aclgraph_atom_main_events,
    _classify_kernel_role,
    _main_event_source_key,
    _merge_aclgraph_atoms_with_main_events,
)
from traceloom.ascend_aclgraph import (
    _canonical_graph_body_token,
    _canonical_graph_noise_token,
    _format_template_signature,
    _infer_graph_replay_unit_count,
    _overlap_rows_by_interval,
    _split_segments_into_replay_units,
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


def _task(start: int, end: int, global_task_id: int) -> dict[str, int | str]:
    return {
        "start_ns": start,
        "end_ns": end,
        "stream_id": 7,
        "task_id": global_task_id,
        "global_task_id": global_task_id,
        "task_label": "KERNEL_AICORE",
    }


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

    def test_aclgraph_template_signature_ignores_repetition_counts(self) -> None:
        short_segment = [_task(0, 5, 1), _task(10, 15, 2), _task(20, 25, 3)]
        long_segment = [
            _task(0, 5, 1),
            _task(6, 9, 2),
            _task(10, 15, 3),
            _task(16, 19, 4),
            _task(20, 25, 5),
            _task(26, 29, 6),
        ]
        short_compute = {
            1: {"op_type": "MatMulV2"},
            2: {"op_type": "Cast"},
            3: {"op_type": "MatMulV2"},
        }
        long_compute = {
            1: {"op_type": "MatMulV2"},
            2: {"op_type": "MatMulV2"},
            3: {"op_type": "Cast"},
            4: {"op_type": "Cast"},
            5: {"op_type": "MatMulV2"},
            6: {"op_type": "MatMulV2"},
        }

        self.assertEqual(
            _format_template_signature(short_segment, short_compute),
            _format_template_signature(long_segment, long_compute),
        )
        self.assertNotIn(":2", _format_template_signature(long_segment, long_compute))

    def test_aclgraph_template_signature_ignores_execution_order(self) -> None:
        first = [_task(0, 5, 1), _task(10, 15, 2), _task(20, 25, 3)]
        second = [_task(0, 5, 1), _task(10, 15, 2), _task(20, 25, 3)]

        self.assertEqual(
            _format_template_signature(
                first,
                {
                    1: {"op_type": "MatMulV2"},
                    2: {"op_type": "Cast"},
                    3: {"op_type": "RmsNorm"},
                },
            ),
            _format_template_signature(
                second,
                {
                    1: {"op_type": "Cast"},
                    2: {"op_type": "MatMulV2"},
                    3: {"op_type": "RmsNorm"},
                },
            ),
        )

    def test_aclgraph_template_signature_keeps_token_vocabulary(self) -> None:
        first = [_task(0, 5, 1), _task(10, 15, 2), _task(20, 25, 3)]
        second = [_task(0, 5, 1), _task(10, 15, 2), _task(20, 25, 3)]

        self.assertNotEqual(
            _format_template_signature(
                first,
                {
                    1: {"op_type": "MatMulV2"},
                    2: {"op_type": "Cast"},
                    3: {"op_type": "RmsNorm"},
                },
            ),
            _format_template_signature(
                second,
                {
                    1: {"op_type": "MatMulV2"},
                    2: {"op_type": "SwiGlu"},
                    3: {"op_type": "RmsNorm"},
                },
            ),
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

    def test_aclgraph_atom_prefers_template_symbol_when_present(self) -> None:
        graph_events = _aclgraph_atom_main_events(
            [
                {
                    "graph_provider": "aclgraph",
                    "graph_event_idx": 1,
                    "graph_type_symbol": "G005",
                    "graph_type_label": "ACLGraphType G005",
                    "graph_template_symbol": "T001",
                    "graph_template_label": "ACLGraphTemplate T001",
                    "graph_replay_symbol": "T001x4",
                    "graph_replay_label": "ACLGraph T001 x4",
                    "source_key": "provider=aclgraph;replay_idx=1",
                    "source_table": "ACLGRAPH_REPLAY",
                    "device_id": 0,
                    "stream_id": 7,
                    "start_ns": 10,
                    "end_ns": 20,
                }
            ]
        )

        self.assertEqual(graph_events[0].symbol, "T001")
        self.assertEqual(graph_events[0].event.label, "ACLGraphTemplate T001")

    def test_aclgraph_replay_unit_classifies_as_anchor(self) -> None:
        graph_events = _aclgraph_atom_main_events(
            [
                {
                    "graph_provider": "aclgraph",
                    "graph_event_idx": 1,
                    "graph_template_symbol": "T001",
                    "graph_template_label": "ACLGraphTemplate T001",
                    "source_key": "provider=aclgraph;replay_idx=1",
                    "source_table": "ACLGRAPH_REPLAY",
                    "task_type": "ACL_GRAPH_REPLAY_UNIT",
                    "device_id": 0,
                    "stream_id": 7,
                    "start_ns": 10,
                    "end_ns": 20,
                }
            ]
        )

        role, reason = _classify_kernel_role(graph_events[0], [])

        self.assertEqual(role, "anchor")
        self.assertEqual(reason, "anchor_aclgraph_replay_unit")

    def test_aclgraph_launch_classifies_as_graph_not_anchor(self) -> None:
        graph_events = _aclgraph_atom_main_events(
            [
                {
                    "graph_provider": "aclgraph",
                    "graph_event_idx": 1,
                    "graph_template_symbol": "T001",
                    "graph_template_label": "ACLGraphTemplate T001",
                    "source_key": "provider=aclgraph;replay_idx=1",
                    "source_table": "ACLGRAPH_REPLAY",
                    "task_type": "ACL_GRAPH_REPLAY",
                    "device_id": 0,
                    "stream_id": 7,
                    "start_ns": 10,
                    "end_ns": 20,
                }
            ]
        )

        role, reason = _classify_kernel_role(graph_events[0], [])

        self.assertEqual(role, "graph")
        self.assertEqual(reason, "graph_launch_or_envelope")

    def test_aclgraph_replay_unit_count_prefers_body_landmark(self) -> None:
        unit_count, source = _infer_graph_replay_unit_count(
            Counter({"index|Index": 4, "matmul|MatMul": 448, "shape|Shape": 224}),
            Counter({"NOTIFY_WAIT": 116, "MODEL_EXECUTE": 115}),
        )

        self.assertEqual(unit_count, 4)
        self.assertEqual(source, "body:index|Index")

    def test_aclgraph_activity_splits_into_replay_units_by_body_landmark(self) -> None:
        segment = [
            _task(0, 5, 1),
            _task(10, 15, 2),
            _task(20, 25, 3),
            _task(100, 105, 4),
            _task(110, 115, 5),
            _task(120, 125, 6),
            _task(200, 205, 7),
            _task(210, 215, 8),
            _task(220, 225, 9),
        ]
        compute = {
            1: {"op_type": "MatMulV2"},
            2: {"op_type": "GatherV2"},
            3: {"op_type": "RmsNorm"},
            4: {"op_type": "MatMulV2"},
            5: {"op_type": "GatherV2"},
            6: {"op_type": "RmsNorm"},
            7: {"op_type": "MatMulV2"},
            8: {"op_type": "GatherV2"},
            9: {"op_type": "RmsNorm"},
        }

        unit_segments, metadata = _split_segments_into_replay_units(
            [segment],
            semantic_by_key={},
            compute=compute,
        )

        self.assertEqual([len(unit) for unit in unit_segments], [3, 3, 3])
        self.assertEqual([row["graph_activity_unit_idx"] for row in metadata], [1, 2, 3])
        self.assertEqual({row["graph_activity_unit_count"] for row in metadata}, {3})
        self.assertEqual({row["graph_activity_split_source"] for row in metadata}, {"body:global_landmark_midpoint"})

    def test_aclgraph_replay_units_ignore_activity_boundaries(self) -> None:
        first_activity = [
            _task(0, 5, 1),
            _task(10, 15, 2),
            _task(20, 25, 3),
            _task(100, 105, 4),
        ]
        second_activity = [
            _task(110, 115, 5),
            _task(120, 125, 6),
        ]
        compute = {
            1: {"op_type": "MatMulV2"},
            2: {"op_type": "GatherV2"},
            3: {"op_type": "RmsNorm"},
            4: {"op_type": "MatMulV2"},
            5: {"op_type": "GatherV2"},
            6: {"op_type": "RmsNorm"},
        }

        unit_segments, metadata = _split_segments_into_replay_units(
            [first_activity, second_activity],
            semantic_by_key={},
            compute=compute,
        )

        self.assertEqual(len(unit_segments), 2)
        self.assertEqual([len(unit) for unit in unit_segments], [3, 3])
        self.assertEqual(metadata[1]["graph_activity_indices"], "1..2")

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
