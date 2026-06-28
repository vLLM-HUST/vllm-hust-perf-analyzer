from pathlib import Path
import unittest

from traceloom.collective_tag import LoopNode, _assign_loop_pairs, _loop_signature, run_collective_tag
from traceloom.compute_prelude_timeline import _display_collective_label


class CollectiveTagTests(unittest.TestCase):
    def test_collective_labels_are_displayed_semantically(self) -> None:
        self.assertEqual(_display_collective_label("hcom_allReduce__123_456_789"), "AllReduce")
        self.assertEqual(_display_collective_label("hcom_allGather__123_456_789"), "AllGather")

    def test_expected_world_size_must_be_positive(self) -> None:
        with self.assertRaisesRegex(ValueError, "positive integer"):
            run_collective_tag(analysis_dir=Path("/does/not/matter"), expected_world_size=0)

    def test_loop_signature_uses_primitive_anchor_motif(self) -> None:
        motif = (
            "compute:matmul:addmm",
            "compute:rope:rope",
            "compute:matmul:mm",
            "collective:allreduce:allReduce",
            "compute:norm:norm",
        )

        signature_21 = _loop_signature(anchor_tokens=motif * 21, collective_pattern="")
        signature_11_double = _loop_signature(anchor_tokens=(motif * 2) * 11, collective_pattern="")

        self.assertEqual(signature_21, signature_11_double)

    def test_loop_pair_assignment_matches_equivalent_motif_shapes(self) -> None:
        motif = (
            "compute:matmul:addmm",
            "compute:rope:rope",
            "compute:matmul:mm",
            "collective:allreduce:allReduce",
            "compute:norm:norm",
        )
        signature_21 = _loop_signature(anchor_tokens=motif * 21, collective_pattern="")
        signature_11_double = _loop_signature(anchor_tokens=(motif * 2) * 11, collective_pattern="")
        loops = [
            LoopNode(
                db_name="db01.traceloom_augmented.db",
                db_idx=1,
                device_id=0,
                member_id="db01:dev0",
                node_id="db01:dev0:anchor_tree:N152",
                local_node_id="N152",
                repeat_count=21,
                occurrence_count=1,
                anchor_count=210,
                anchors_per_occurrence=210,
                first_anchor_idx=1467,
                level=1,
                path="root.26",
                collective_pattern="4:allReduce,9:allReduce",
                signature=signature_21,
            ),
            LoopNode(
                db_name="db02.traceloom_augmented.db",
                db_idx=2,
                device_id=1,
                member_id="db02:dev1",
                node_id="db02:dev1:anchor_tree:N150",
                local_node_id="N150",
                repeat_count=11,
                occurrence_count=1,
                anchor_count=220,
                anchors_per_occurrence=220,
                first_anchor_idx=1463,
                level=1,
                path="root.25",
                collective_pattern="4:allReduce,9:allReduce",
                signature=signature_11_double,
            ),
        ]

        paired = _assign_loop_pairs(loops)
        pair_ids = {loop.pair_id for loop in paired}

        self.assertEqual(len(paired), 2)
        self.assertEqual(len(pair_ids), 1)
        self.assertTrue(next(iter(pair_ids)).startswith("LP_M005_01_"))


if __name__ == "__main__":
    unittest.main()
