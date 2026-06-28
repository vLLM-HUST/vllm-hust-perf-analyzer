import unittest

from traceloom.compute_prelude_timeline import (
    MainEvent,
    _aclgraph_atom_main_events,
    _main_event_source_key,
    _merge_aclgraph_atoms_with_main_events,
)
from traceloom.msprof_reader import StreamEvent


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


if __name__ == "__main__":
    unittest.main()

