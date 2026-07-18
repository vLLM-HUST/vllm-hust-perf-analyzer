#include "traceloom/compat/report_tree_rows.h"
#include "traceloom/testing/test_util.h"

#include <stdexcept>
#include <vector>

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "memory", "TASK", 0);
  const SymbolId memcpy = ir.symbols.intern("Memcpy");
  const SymbolId matmul = ir.symbols.intern("MatMul");

  ir.trace_events.append(source, 9, 0, 3, 0, 1000, memcpy);
  const TraceEventId event0 =
      ir.trace_events.append(source, 10, 0, 3, 2000, 3000, matmul);
  const TraceEventId event1 =
      ir.trace_events.append(source, 11, 0, 3, 10003000, 10005000, matmul);
  const AnchorId anchor0 =
      ir.anchors.append(source, event0, ReplayUnitId::invalid(),
                        AnchorKind::kDeviceEvent, matmul, 0, 3, 2000, 3000);
  const AnchorId anchor1 =
      ir.anchors.append(source, event1, ReplayUnitId::invalid(),
                        AnchorKind::kDeviceEvent, matmul, 0, 3, 10003000,
                        10005000);
  ir.tokens.append(anchor0, matmul, 0, 0, 2000, 3000);
  ir.tokens.append(anchor1, matmul, 0, 1, 10003000, 10005000);

  const compat::NodeCoverageSqlRows rows =
      compat::build_native_report_tree_node_coverage_sql_rows(ir, 7, "tree");

  require(rows.nodes.size() == 3);
  require(rows.edges.size() == 2);
  require(rows.node_anchors.size() == 6);
  require(rows.anchor_primary_nodes.size() == 2);
  require(rows.loop_nodes.size() == 1);

  require(rows.nodes[0].node_id == "node-N001");
  require(rows.nodes[0].local_node_id == "N001");
  require(rows.nodes[0].kind == "seq");
  require(rows.nodes[0].anchor_count == 2);
  require(rows.nodes[0].compute_us == 3.0);
  require(rows.nodes[0].idle_us == 10000.0);
  require(rows.nodes[0].avg_idle_us == 10000.0);
  require(rows.nodes[0].avg_total_us == 10003.0);
  require(rows.nodes[0].aux_events == 0.0);
  require(rows.nodes[0].aux_us == 0.0);

  require(rows.nodes[1].kind == "repeat");
  require(rows.nodes[1].repeat_count == 2);
  require(rows.nodes[1].occurrence_count == 1);
  require(rows.nodes[1].anchor_count == 2);
  require(rows.nodes[1].avg_total_us == rows.nodes[1].total_us);
  require(rows.nodes[1].aux_events == 0.0);
  require(rows.nodes[1].aux_us == 0.0);
  require(rows.loop_nodes[0].node_id == rows.nodes[1].node_id);
  require(rows.loop_nodes[0].repeat_count == 2);
  require(rows.loop_nodes[0].occurrence_count == 1);
  require(rows.loop_nodes[0].idle_us == 10000.0);
  require(rows.loop_nodes[0].total_us == 10003.0);

  require(rows.nodes[2].kind == "atom");
  require(rows.nodes[2].occurrence_count == 2);
  require(rows.nodes[2].anchor_count == 2);
  require(rows.nodes[2].anchors_per_occurrence == 1.0);
  require(rows.nodes[2].idle_us == 10000.0);
  require(rows.nodes[2].avg_idle_us == 5000.0);
  require(rows.nodes[2].self_us == 3.0);
  require(rows.nodes[2].aux_events == 0.0);
  require(rows.nodes[2].aux_us == 0.0);

  require(rows.node_anchors[0].node_id == "node-N001");
  require(rows.node_anchors[0].anchor_id == "anchor-0");
  require(rows.node_anchors[0].coverage_kind == "body");
  require(rows.node_anchors[4].node_id == "node-N003");
  require(rows.node_anchors[4].anchor_id == "anchor-0");
  require(rows.node_anchors[4].coverage_kind == "self");
  require(rows.node_anchors[4].repeat_context == "N002#1");
  require(rows.node_anchors[4].compute_us == 1.0);
  require(rows.node_anchors[4].total_us == 1.0);
  require(rows.node_anchors[4].self_us == 1.0);
  require(rows.node_anchors[5].anchor_id == "anchor-1");
  require(rows.node_anchors[5].repeat_context == "N002#2");
  require(rows.node_anchors[5].compute_us == 2.0);
  require(rows.node_anchors[5].idle_us == 10000.0);
  require(rows.node_anchors[5].total_us == 10002.0);

  require(rows.anchor_primary_nodes[0].anchor_id == "anchor-0");
  require(rows.anchor_primary_nodes[0].node_id == "node-N003");
  require(rows.anchor_primary_nodes[0].reason == "atom_leaf");
  require(rows.anchor_primary_nodes[1].anchor_id == "anchor-1");

  const compat::LoopTreeSqlRows loop_rows =
      compat::split_loop_tree_sql_rows(rows);
  require(loop_rows.nodes.size() == rows.nodes.size());
  require(loop_rows.edges.size() == rows.edges.size());
  require(loop_rows.loop_nodes.size() == rows.loop_nodes.size());

  const compat::NodeAnchorCoverageSqlRows coverage_rows =
      compat::split_node_anchor_coverage_sql_rows(rows);
  require(coverage_rows.node_anchors.size() == rows.node_anchors.size());
  require(coverage_rows.anchor_primary_nodes.size() ==
          rows.anchor_primary_nodes.size());

  const compat::SemanticTreeSqlRows semantic_rows =
      compat::build_native_report_tree_semantic_sql_rows(ir, 7, "tree-1",
                                                         "anchor_tree");
  require(semantic_rows.trees.size() == 1);
  require(semantic_rows.nodes.size() == rows.nodes.size());
  require(semantic_rows.edges.size() == rows.edges.size());

  require(semantic_rows.trees[0].tree_id == "tree-1");
  require(semantic_rows.trees[0].root_node_id == "node-N001");
  require(semantic_rows.trees[0].semantic_projection == "native_report_tree");
  require(semantic_rows.trees[0].auxiliary_attribution ==
          "native_aux_attribution");

  require(semantic_rows.nodes[0].node_id == "node-N001");
  require(semantic_rows.nodes[0].tree_id == "tree-1");
  require(semantic_rows.nodes[0].preorder_idx == 0);
  require(semantic_rows.nodes[0].node_type == "Seq");
  require(semantic_rows.nodes[0].semantic_kind == "seq");
  require(semantic_rows.nodes[0].anchor_count == 2);
  require(semantic_rows.nodes[0].start_ns == 2000);
  require(semantic_rows.nodes[0].end_ns == 10005000);
  require(semantic_rows.nodes[0].idle_us == 10000.0);
  require(semantic_rows.nodes[0].avg_idle_us == 10000.0);
  require(semantic_rows.nodes[0].total_us == 10003.0);
  require(semantic_rows.nodes[0].aux_event_count == 0.0);
  require(semantic_rows.nodes[0].aux_us == 0.0);

  require(semantic_rows.nodes[1].parent_node_id == "node-N001");
  require(semantic_rows.nodes[1].node_type == "Repeat");
  require(semantic_rows.nodes[1].loop_depth == 1);
  require(semantic_rows.nodes[2].parent_node_id == "node-N002");
  require(semantic_rows.nodes[2].parent_local_node_id == "N002");
  require(semantic_rows.nodes[2].self_us == 3.0);
  require(semantic_rows.nodes[2].aux_event_count == 0.0);
  require(semantic_rows.nodes[2].aux_us == 0.0);
  require(semantic_rows.edges[0].tree_id == "tree-1");
  require(semantic_rows.edges[0].parent_node_id == "node-N001");
  require(semantic_rows.edges[0].child_node_id == "node-N002");

  const std::vector<compat::SemanticTreeHeaderSqlRow> semantic_catalog =
      compat::split_semantic_tree_catalog_sql_rows(semantic_rows);
  require(semantic_catalog.size() == semantic_rows.trees.size());
  require(semantic_catalog[0].tree_id == "tree-1");

  const compat::SemanticGraphSqlRows semantic_graph =
      compat::split_semantic_graph_sql_rows(semantic_rows);
  require(semantic_graph.nodes.size() == semantic_rows.nodes.size());
  require(semantic_graph.edges.size() == semantic_rows.edges.size());

  // Overlapping anchor streams contribute wall time once. Communication wins
  // the classification for the overlap, while self_us retains raw durations.
  NativeIr overlap_ir;
  const SourceRefId overlap_source =
      overlap_ir.source_refs.append("fixture", "overlap", "TASK", 0);
  const SymbolId overlap_matmul = overlap_ir.symbols.intern("MatMul");
  const SymbolId all_reduce = overlap_ir.symbols.intern("AllReduce");
  const TraceEventId overlap_compute = overlap_ir.trace_events.append(
      overlap_source, 1, 0, 1, 0, 10000, overlap_matmul);
  const TraceEventId overlap_comm = overlap_ir.trace_events.append(
      overlap_source, 2, 0, 2, 5000, 15000, all_reduce);
  const AnchorId overlap_anchor0 = overlap_ir.anchors.append(
      overlap_source, overlap_compute, ReplayUnitId::invalid(),
      AnchorKind::kDeviceEvent, overlap_matmul, 0, 1, 0, 10000);
  const AnchorId overlap_anchor1 = overlap_ir.anchors.append(
      overlap_source, overlap_comm, ReplayUnitId::invalid(),
      AnchorKind::kCommunication, all_reduce, 0, 2, 5000, 15000);
  overlap_ir.tokens.append(overlap_anchor0, overlap_matmul, 0, 0, 0, 10000);
  overlap_ir.tokens.append(overlap_anchor1, all_reduce, 0, 1, 5000, 15000);
  const compat::NodeCoverageSqlRows overlap_rows =
      compat::build_native_report_tree_node_coverage_sql_rows(overlap_ir);
  require(overlap_rows.nodes.front().compute_us == 5.0);
  require(overlap_rows.nodes.front().comm_us == 10.0);
  require(overlap_rows.nodes.front().idle_us == 0.0);
  require(overlap_rows.nodes.front().total_us == 15.0);

  // Prelude classification uses disjoint wall-clock buckets, includes an
  // event that began before the gap, and keeps overlapping/wait evidence in
  // the non-additive aux overlay.
  NativeIr prelude_ir;
  const SourceRefId prelude_source =
      prelude_ir.source_refs.append("fixture", "prelude", "TASK", 0);
  const SymbolId first = prelude_ir.symbols.intern("First");
  const SymbolId second = prelude_ir.symbols.intern("Second");
  const SymbolId aux_exec = prelude_ir.symbols.intern("AuxExec");
  const SymbolId event_wait = prelude_ir.symbols.intern("EventWait");
  const SymbolId prelude_all_reduce =
      prelude_ir.symbols.intern("AllReduce");
  const TraceEventId first_event = prelude_ir.trace_events.append(
      prelude_source, 1, 0, 1, 1000, 2000, first);
  const TraceEventId second_event = prelude_ir.trace_events.append(
      prelude_source, 2, 0, 1, 10000, 11000, second);
  prelude_ir.trace_events.append(prelude_source, 3, 0, 2, 1500, 7000,
                                 aux_exec);
  prelude_ir.trace_events.append(prelude_source, 4, 0, 3, 4000, 9000,
                                 aux_exec);
  const TraceEventId prelude_comm = prelude_ir.trace_events.append(
      prelude_source, 5, 0, 4, 6000, 9500, prelude_all_reduce);
  prelude_ir.trace_events.append(prelude_source, 6, 0, 5, 9500, 10000,
                                 event_wait);
  prelude_ir.communication_ops.append(
      prelude_source, prelude_comm, -1, -1, 0, 1, prelude_all_reduce);
  const AnchorId first_anchor = prelude_ir.anchors.append(
      prelude_source, first_event, ReplayUnitId::invalid(),
      AnchorKind::kDeviceEvent, first, 0, 1, 1000, 2000);
  const AnchorId second_anchor = prelude_ir.anchors.append(
      prelude_source, second_event, ReplayUnitId::invalid(),
      AnchorKind::kDeviceEvent, second, 0, 1, 10000, 11000);
  prelude_ir.tokens.append(first_anchor, first, 0, 0, 1000, 2000);
  prelude_ir.tokens.append(second_anchor, second, 0, 1, 10000, 11000);
  const compat::NodeCoverageSqlRows prelude_rows =
      compat::build_native_report_tree_node_coverage_sql_rows(prelude_ir);
  require(prelude_rows.nodes.front().compute_us == 6.0);
  require(prelude_rows.nodes.front().comm_us == 3.5);
  require(prelude_rows.nodes.front().idle_us == 0.5);
  require(prelude_rows.nodes.front().total_us == 10.0);
  require(prelude_rows.nodes.front().aux_events == 4.0);
  require(prelude_rows.nodes.front().aux_us == 14.0);

  NativeIr bad_ir;
  const SourceRefId bad_source =
      bad_ir.source_refs.append("fixture", "bad", "TASK", 0);
  bad_ir.tokens.append(AnchorId(99), matmul, 0, 0, 0, 1);
  bool rejected_bad_token_anchor = false;
  try {
    (void)compat::build_report_tokens_from_native_ir(bad_ir);
  } catch (const std::invalid_argument&) {
    rejected_bad_token_anchor = true;
  }
  require(rejected_bad_token_anchor);
  (void)bad_source;

  return 0;
}
