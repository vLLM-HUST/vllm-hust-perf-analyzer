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
    AclGraphAnalysis,
    aclgraph_analysis_to_semantic_fixture,
    _build_capture_dictionary_rows,
    _canonical_graph_body_token,
    _canonical_graph_noise_token,
    _canonical_capture_api_token,
    _format_template_sequence_signature,
    _format_template_signature,
    _infer_graph_replay_unit_count,
    _match_replay_segment_to_capture_dictionary,
    _overlap_rows_by_interval,
    _split_segments_into_replay_units,
)
from traceloom.msprof_reader import StreamEvent, canonical_device_label


def _event(start: int, end: int, label: str, *, stream_id: int = 1) -> MainEvent:
    ev = StreamEvent(
        start_ns=start,
        end_ns=end,
        device_id=0,
        stream_id=stream_id,
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

    def test_aclgraph_template_signature_keeps_discrete_repetition_counts(self) -> None:
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

        self.assertNotEqual(
            _format_template_signature(short_segment, short_compute),
            _format_template_signature(long_segment, long_compute),
        )
        self.assertIn("body_multiset_v1", _format_template_signature(long_segment, long_compute))
        self.assertNotIn("us", _format_template_signature(long_segment, long_compute))

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

    def test_aclgraph_template_sequence_signature_keeps_execution_order_for_diagnostics(self) -> None:
        first = [_task(0, 5, 1), _task(10, 15, 2), _task(20, 25, 3)]
        second = [_task(0, 5, 1), _task(10, 15, 2), _task(20, 25, 3)]

        self.assertNotEqual(
            _format_template_sequence_signature(
                first,
                {
                    1: {"op_type": "MatMulV2"},
                    2: {"op_type": "Cast"},
                    3: {"op_type": "RmsNorm"},
                },
            ),
            _format_template_sequence_signature(
                second,
                {
                    1: {"op_type": "Cast"},
                    2: {"op_type": "MatMulV2"},
                    3: {"op_type": "RmsNorm"},
                },
            ),
        )

    def test_aclgraph_template_signature_normalizes_absolute_stream_ids(self) -> None:
        first = [
            {**_task(0, 5, 1), "stream_id": 36},
            {**_task(10, 15, 2), "stream_id": 54},
            {**_task(20, 25, 3), "stream_id": 36},
        ]
        second = [
            {**_task(100, 105, 1), "stream_id": 53},
            {**_task(110, 115, 2), "stream_id": 94},
            {**_task(120, 125, 3), "stream_id": 53},
        ]
        compute = {
            1: {"op_type": "MatMulV2"},
            2: {"op_type": "Cast"},
            3: {"op_type": "RmsNorm"},
        }

        self.assertEqual(
            _format_template_signature(first, compute),
            _format_template_signature(second, compute),
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

    def test_capture_api_tokens_share_replay_vocabulary(self) -> None:
        self.assertEqual(_canonical_capture_api_token("aclnnMm"), "matmul|MatMul")
        self.assertEqual(_canonical_capture_api_token("aclnnAddmm"), "matmul|MatMul")
        self.assertEqual(_canonical_capture_api_token("aclnnAddRmsNormBias"), "norm|RmsNorm")
        self.assertEqual(_canonical_capture_api_token("aclnnSwiGlu"), "swiglu|SwiGlu")
        self.assertEqual(_canonical_capture_api_token("aclnnInnerApplyRotaryPosEmb"), "rope|Rope")
        self.assertEqual(_canonical_capture_api_token("aclnnEmbedding"), "index|Embedding")
        self.assertEqual(_canonical_capture_api_token("Slice_Tiling"), "shape|Shape")

    def test_capture_dictionary_clusters_head_layer_tail_slots(self) -> None:
        slots = [
            {
                "capture_dictionary_kind": "head",
                "capture_slot_in_group": 1,
                "capture_group_count": 3,
                "body_token_count": 4,
                "body_match_signature": "index|Embedding:1\nmatmul|MatMul:1\nnorm|RmsNorm:1\nrope|Rope:1",
                "body_signature": "index|Embedding:1\nmatmul|MatMul:1\nnorm|RmsNorm:1\nrope|Rope:1",
            },
            {
                "capture_dictionary_kind": "layer",
                "capture_slot_in_group": 2,
                "capture_group_count": 3,
                "body_token_count": 8,
                "body_match_signature": "matmul|MatMul:4\nnorm|RmsNorm:2\nrope|Rope:1\nswiglu|SwiGlu:1",
                "body_signature": "matmul|MatMul:4\nnorm|RmsNorm:2\nrope|Rope:1\nswiglu|SwiGlu:1",
            },
            {
                "capture_dictionary_kind": "tail",
                "capture_slot_in_group": 29,
                "capture_group_count": 3,
                "body_token_count": 6,
                "body_match_signature": "matmul|MatMul:3\nnorm|RmsNorm:2\nswiglu|SwiGlu:1",
                "body_signature": "matmul|MatMul:3\nnorm|RmsNorm:2\nswiglu|SwiGlu:1",
            },
        ]

        dictionary = _build_capture_dictionary_rows(slots, db_idx=1, device_id=0)

        self.assertEqual([row["capture_dictionary_symbol"] for row in dictionary], ["H", "L", "T"])
        self.assertEqual([row["capture_slot_indices"] for row in dictionary], ["1", "2", "29"])

    def test_replay_tiling_matches_stream_subslots_to_capture_dictionary(self) -> None:
        dictionary = _build_capture_dictionary_rows(
            [
                {
                    "capture_dictionary_kind": "head",
                    "capture_slot_in_group": 1,
                    "capture_group_count": 1,
                    "body_token_count": 4,
                    "body_match_signature": "index|Embedding:1\nmatmul|MatMul:1\nnorm|RmsNorm:1\nrope|Rope:1",
                    "body_signature": "index|Embedding:1\nmatmul|MatMul:1\nnorm|RmsNorm:1\nrope|Rope:1",
                },
                {
                    "capture_dictionary_kind": "layer",
                    "capture_slot_in_group": 2,
                    "capture_group_count": 1,
                    "body_token_count": 8,
                    "body_match_signature": "matmul|MatMul:4\nnorm|RmsNorm:2\nrope|Rope:1\nswiglu|SwiGlu:1",
                    "body_signature": "matmul|MatMul:4\nnorm|RmsNorm:2\nrope|Rope:1\nswiglu|SwiGlu:1",
                },
                {
                    "capture_dictionary_kind": "tail",
                    "capture_slot_in_group": 3,
                    "capture_group_count": 1,
                    "body_token_count": 6,
                    "body_match_signature": "matmul|MatMul:3\nnorm|RmsNorm:2\nswiglu|SwiGlu:1",
                    "body_signature": "matmul|MatMul:3\nnorm|RmsNorm:2\nswiglu|SwiGlu:1",
                },
            ],
            db_idx=1,
            device_id=0,
        )
        segment = [
            {**_task(0, 1, 1), "stream_id": 10},
            {**_task(2, 3, 2), "stream_id": 10},
            {**_task(4, 5, 3), "stream_id": 10},
            {**_task(6, 7, 4), "stream_id": 10},
            {**_task(10, 11, 5), "stream_id": 11},
            {**_task(12, 13, 6), "stream_id": 11},
            {**_task(14, 15, 7), "stream_id": 11},
            {**_task(16, 17, 8), "stream_id": 11},
            {**_task(18, 19, 9), "stream_id": 11},
            {**_task(20, 21, 10), "stream_id": 11},
            {**_task(22, 23, 11), "stream_id": 11},
            {**_task(24, 25, 12), "stream_id": 11},
            {**_task(30, 31, 13), "stream_id": 12},
            {**_task(32, 33, 14), "stream_id": 12},
            {**_task(34, 35, 15), "stream_id": 12},
            {**_task(36, 37, 16), "stream_id": 12},
            {**_task(38, 39, 17), "stream_id": 12},
            {**_task(40, 41, 18), "stream_id": 12},
        ]
        compute = {
            1: {"op_type": "GatherV2"},
            2: {"op_type": "MatMulV2"},
            3: {"op_type": "RmsNorm"},
            4: {"op_type": "ApplyRotaryPosEmb"},
            5: {"op_type": "MatMulV2"},
            6: {"op_type": "MatMulV2"},
            7: {"op_type": "MatMulV2"},
            8: {"op_type": "MatMulV2"},
            9: {"op_type": "AddRmsNormBias"},
            10: {"op_type": "AddRmsNormBias"},
            11: {"op_type": "SwiGlu"},
            12: {"op_type": "ApplyRotaryPosEmb"},
            13: {"op_type": "MatMulV2"},
            14: {"op_type": "MatMulV2"},
            15: {"op_type": "MatMulV2"},
            16: {"op_type": "AddRmsNormBias"},
            17: {"op_type": "AddRmsNormBias"},
            18: {"op_type": "SwiGlu"},
        }

        tiling = _match_replay_segment_to_capture_dictionary(segment, compute=compute, capture_dictionary_rows=dictionary)

        self.assertEqual(tiling["coverage"], "3/3")
        self.assertEqual(tiling["sequence"], "H,L,T")

    def test_replay_tiling_attributes_low_confidence_subslots_to_nearest_symbol(self) -> None:
        dictionary = _build_capture_dictionary_rows(
            [
                {
                    "capture_dictionary_kind": "layer",
                    "capture_slot_in_group": 2,
                    "capture_group_count": 1,
                    "body_token_count": 8,
                    "body_match_signature": "matmul|MatMul:4\nnorm|RmsNorm:2\nrope|Rope:1\nswiglu|SwiGlu:1",
                    "body_signature": "matmul|MatMul:4\nnorm|RmsNorm:2\nrope|Rope:1\nswiglu|SwiGlu:1",
                },
            ],
            db_idx=1,
            device_id=0,
        )
        segment = [{**_task(idx * 2, idx * 2 + 1, idx + 1), "stream_id": 10} for idx in range(20)]
        compute = {idx + 1: {"op_type": "MatMulV2"} for idx in range(20)}

        tiling = _match_replay_segment_to_capture_dictionary(segment, compute=compute, capture_dictionary_rows=dictionary)

        self.assertEqual(tiling["coverage"], "0/1")
        self.assertEqual(tiling["sequence"], "L")
        self.assertEqual(tiling["top_mismatches"], "layer:1")

    def test_graph_atom_replaces_fully_covered_outer_events(self) -> None:
        partial = _event(0, 12, "A", stream_id=7)
        covered = _event(14, 18, "B", stream_id=7)
        outside = _event(30, 40, "C", stream_id=7)
        other_stream = _event(14, 18, "D", stream_id=1)
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

        merged, covered_keys, _diagnostics = _merge_aclgraph_atoms_with_main_events(
            [other_stream],
            graph_events,
        )
        self.assertEqual([item.symbol for item in merged], ["G001", "D"])
        self.assertNotIn(_main_event_source_key(other_stream), covered_keys)

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

    def test_aclgraph_atom_prefers_tiling_symbol_when_present(self) -> None:
        graph_events = _aclgraph_atom_main_events(
            [
                {
                    "graph_provider": "aclgraph",
                    "graph_event_idx": 1,
                    "graph_type_symbol": "G005",
                    "graph_type_label": "ACLGraphType G005",
                    "graph_template_symbol": "T001",
                    "graph_template_label": "ACLGraphTemplate T001",
                    "graph_tiling_symbol": "D001",
                    "graph_tiling_label": "ACLGraph H/L/T H,L,T",
                    "source_key": "provider=aclgraph;replay_idx=1",
                    "source_table": "ACLGRAPH_REPLAY",
                    "device_id": 0,
                    "stream_id": 7,
                    "start_ns": 10,
                    "end_ns": 20,
                }
            ]
        )

        self.assertEqual(graph_events[0].symbol, "D001")
        self.assertEqual(graph_events[0].event.label, "ACLGraph H/L/T H,L,T")

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

    def test_aclgraph_analysis_exports_native_semantic_fixture(self) -> None:
        analysis = AclGraphAnalysis(
            step_rows=[],
            replay_rows=[
                {
                    "graph_provider": "aclgraph",
                    "graph_event_idx": 1,
                    "graph_activity_idx": 1,
                    "graph_activity_unit_idx": 1,
                    "graph_activity_unit_count": 2,
                    "graph_activity_expected_unit_count": 2,
                    "graph_activity_unit_source": "body:landmark",
                    "graph_activity_split_source": "body:landmark_midpoint",
                    "replay_tiling_policy": "capture_dictionary_stream_subslot_nearest_v2",
                    "replay_tiling_subslot_count": 3,
                    "replay_tiling_sequence": "H,L,T",
                    "replay_tiling_coverage": "3/3",
                    "replay_tiling_matched_count": 3,
                    "replay_tiling_unmatched_count": 0,
                    "replay_tiling_subslots_json": (
                        '[{"subslot_idx":1,"symbol":"H","kind":"head","matched":1,'
                        '"start_ns":1000,"end_ns":1100,"stream_id":7},'
                        '{"subslot_idx":2,"symbol":"L","kind":"layer","matched":1,'
                        '"start_ns":1100,"end_ns":1200,"stream_id":7},'
                        '{"subslot_idx":3,"symbol":"T","kind":"tail","matched":1,'
                        '"start_ns":1200,"end_ns":1300,"stream_id":7}]'
                    ),
                    "stream_id": 7,
                    "start_ns": 1000,
                    "end_ns": 1300,
                    "raw_child_task_count": 30,
                },
                {
                    "graph_provider": "aclgraph",
                    "graph_event_idx": 2,
                    "graph_activity_idx": 1,
                    "graph_activity_unit_idx": 2,
                    "graph_activity_unit_count": 2,
                    "graph_activity_expected_unit_count": 2,
                    "graph_activity_unit_source": "body:landmark",
                    "graph_activity_split_source": "body:landmark_midpoint",
                    "replay_tiling_policy": "capture_dictionary_stream_subslot_nearest_v2",
                    "replay_tiling_subslot_count": 3,
                    "replay_tiling_sequence": "H,?,T",
                    "replay_tiling_coverage": "2/3",
                    "replay_tiling_matched_count": 2,
                    "replay_tiling_unmatched_count": 1,
                    "replay_tiling_subslots_json": (
                        '[{"subslot_idx":1,"symbol":"H","kind":"head","matched":1,'
                        '"start_ns":1300,"end_ns":1400,"stream_id":7},'
                        '{"subslot_idx":2,"symbol":"?","kind":"unknown","matched":0,'
                        '"start_ns":1400,"end_ns":1500,"stream_id":7},'
                        '{"subslot_idx":3,"symbol":"T","kind":"tail","matched":1,'
                        '"start_ns":1500,"end_ns":1600,"stream_id":7}]'
                    ),
                    "stream_id": 7,
                    "start_ns": 1300,
                    "end_ns": 1600,
                    "raw_child_task_count": 20,
                },
            ],
            envelope_rows=[],
            semantic_task_rows=[],
            model_stream_rows=[],
            top_op_rows=[],
            capture_slot_rows=[
                {
                    "capture_slot_idx": 1,
                    "capture_group_idx": 1,
                    "capture_group_size": 3,
                    "capture_slot_in_group": 1,
                    "capture_dictionary_kind": "head",
                    "capture_dictionary_symbol": "H",
                    "body_match_signature": "index|Embedding:1",
                },
                {
                    "capture_slot_idx": 2,
                    "capture_group_idx": 1,
                    "capture_group_size": 3,
                    "capture_slot_in_group": 2,
                    "capture_dictionary_kind": "layer",
                    "capture_dictionary_symbol": "L",
                    "body_match_signature": "matmul|MatMul:2",
                },
                {
                    "capture_slot_idx": 3,
                    "capture_group_idx": 1,
                    "capture_group_size": 3,
                    "capture_slot_in_group": 3,
                    "capture_dictionary_kind": "tail",
                    "capture_dictionary_symbol": "T",
                    "body_match_signature": "norm|RmsNorm:1",
                },
            ],
            capture_dictionary_rows=[
                {
                    "capture_dictionary_idx": 1,
                    "capture_dictionary_kind": "head",
                    "capture_dictionary_symbol": "H",
                    "capture_slot_count": 1,
                    "unique_match_signature_count": 1,
                },
                {
                    "capture_dictionary_idx": 2,
                    "capture_dictionary_kind": "layer",
                    "capture_dictionary_symbol": "L",
                    "capture_slot_count": 1,
                    "unique_match_signature_count": 1,
                },
                {
                    "capture_dictionary_idx": 3,
                    "capture_dictionary_kind": "tail",
                    "capture_dictionary_symbol": "T",
                    "capture_slot_count": 1,
                    "unique_match_signature_count": 1,
                },
            ],
            summary={},
        )

        fixture = aclgraph_analysis_to_semantic_fixture(
            analysis,
            fixture_id="unit_aclgraph_python_assets",
        )

        self.assertEqual(fixture["schema_version"], "aclgraph-fixture-v1")
        assets = fixture["assets"]
        self.assertEqual(len(assets["capture_slots"]), 3)
        self.assertEqual([row["slot_symbol"] for row in assets["capture_dictionary"]], ["H", "L", "T"])
        self.assertEqual(len(assets["replay_activities"]), 1)
        self.assertEqual(len(assets["replay_units"]), 2)
        self.assertEqual(len(assets["replay_subslots"]), 6)
        self.assertEqual(
            [row["symbol"] for row in assets["hlt_anchor_seeds"]],
            ["ACLH", "ACLL", "ACLT", "ACLH", "ACLT"],
        )
        self.assertEqual(
            fixture["golden"]["flat_hlt_sequence"],
            "ACLH ACLL ACLT ACLH ACLT",
        )
        self.assertEqual(
            fixture["golden"]["diagnostic_codes"],
            {"replay_tiling_partial_coverage": 1},
        )


if __name__ == "__main__":
    unittest.main()
