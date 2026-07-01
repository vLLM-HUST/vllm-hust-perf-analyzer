#include "traceloom/compat/report_tree_rows.h"
#include "traceloom/testing/test_util.h"

#include <stdexcept>

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "memory", "TASK", 0);
  const SymbolId matmul = ir.symbols.intern("MatMul");

  const TraceEventId event0 =
      ir.trace_events.append(source, 10, 0, 3, 1000, 2000, matmul);
  const TraceEventId event1 =
      ir.trace_events.append(source, 11, 0, 3, 2500, 4500, matmul);
  const AnchorId anchor0 =
      ir.anchors.append(source, event0, ReplayUnitId::invalid(),
                        AnchorKind::kDeviceEvent, matmul, 0, 3, 1000, 2000);
  const AnchorId anchor1 =
      ir.anchors.append(source, event1, ReplayUnitId::invalid(),
                        AnchorKind::kDeviceEvent, matmul, 0, 3, 2500, 4500);
  ir.tokens.append(anchor0, matmul, 0, 0, 1000, 2000);
  ir.tokens.append(anchor1, matmul, 0, 1, 2500, 4500);

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
  require(rows.nodes[0].avg_total_us == 3.0);

  require(rows.nodes[1].kind == "repeat");
  require(rows.nodes[1].repeat_count == 2);
  require(rows.nodes[1].anchor_count == 2);
  require(rows.loop_nodes[0].node_id == rows.nodes[1].node_id);
  require(rows.loop_nodes[0].repeat_count == 2);
  require(rows.loop_nodes[0].total_us == 3.0);

  require(rows.nodes[2].kind == "atom");
  require(rows.nodes[2].occurrence_count == 2);
  require(rows.nodes[2].anchor_count == 2);
  require(rows.nodes[2].anchors_per_occurrence == 1.0);
  require(rows.nodes[2].self_us == 3.0);

  require(rows.node_anchors[0].node_id == "node-N001");
  require(rows.node_anchors[0].anchor_id == "anchor-0");
  require(rows.node_anchors[0].coverage_kind == "body");
  require(rows.node_anchors[4].node_id == "node-N003");
  require(rows.node_anchors[4].anchor_id == "anchor-0");
  require(rows.node_anchors[4].coverage_kind == "self");
  require(rows.node_anchors[4].repeat_context == "N002#0");
  require(rows.node_anchors[5].anchor_id == "anchor-1");
  require(rows.node_anchors[5].repeat_context == "N002#0");

  require(rows.anchor_primary_nodes[0].anchor_id == "anchor-0");
  require(rows.anchor_primary_nodes[0].node_id == "node-N003");
  require(rows.anchor_primary_nodes[0].reason == "atom_leaf");
  require(rows.anchor_primary_nodes[1].anchor_id == "anchor-1");

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
