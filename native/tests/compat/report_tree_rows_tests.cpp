#include "traceloom/compat/report_tree_rows.h"
#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/pattern/grammar_state.h"
#include "traceloom/report/report_tree_builder.h"
#include "traceloom/testing/test_util.h"

#include <map>
#include <set>
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
  require(rows.nodes[1].avg_compute_us == 1.5);
  require(rows.nodes[1].avg_idle_us == 5000.0);
  require(rows.nodes[1].avg_total_us == 5001.5);
  require(rows.nodes[1].aux_events == 0.0);
  require(rows.nodes[1].aux_us == 0.0);
  require(rows.loop_nodes[0].node_id == rows.nodes[1].node_id);
  require(rows.loop_nodes[0].repeat_count == 2);
  require(rows.loop_nodes[0].occurrence_count == 1);
  require(rows.loop_nodes[0].idle_us == 10000.0);
  require(rows.loop_nodes[0].total_us == 10003.0);
  require(rows.loop_nodes[0].avg_total_us == 5001.5);

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
  require(semantic_rows.nodes[1].avg_total_us == 5001.5);
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

  // A semantic ReplayUnit hierarchy preserves the same overlap-safe root
  // wall clock as the flat tree. Communication owns its overlap with graph
  // compute, and structural parent/child costs are views rather than additive
  // siblings.
  NativeIr semantic_cost_ir;
  const SourceRefId semantic_cost_source = semantic_cost_ir.source_refs.append(
      "fixture", "semantic_cost", "TASK", 0);
  const SymbolId semantic_h = semantic_cost_ir.symbols.intern("ACLH");
  const SymbolId semantic_l = semantic_cost_ir.symbols.intern("ACLL");
  const SymbolId semantic_t = semantic_cost_ir.symbols.intern("ACLT");
  const SymbolId semantic_eager = semantic_cost_ir.symbols.intern("Eager");
  const auto append_semantic_token =
      [&](std::uint64_t source_row_id, SymbolId symbol, AnchorKind kind,
          std::uint32_t stream_id, std::int64_t start_ns,
          std::int64_t end_ns) {
        const TraceEventId event = semantic_cost_ir.trace_events.append(
            semantic_cost_source, source_row_id, 0, stream_id, start_ns,
            end_ns, symbol);
        const AnchorId anchor = semantic_cost_ir.anchors.append(
            semantic_cost_source, event, ReplayUnitId::invalid(), kind, symbol,
            0, stream_id, start_ns, end_ns);
        semantic_cost_ir.tokens.append(
            anchor, symbol, 0,
            static_cast<std::uint32_t>(semantic_cost_ir.tokens.size()),
            start_ns, end_ns);
      };
  append_semantic_token(1, semantic_h, AnchorKind::kGraphH, 1, 0, 10000);
  append_semantic_token(2, semantic_l, AnchorKind::kCommunication, 2, 5000,
                        15000);
  append_semantic_token(3, semantic_t, AnchorKind::kGraphT, 1, 15000, 20000);
  append_semantic_token(4, semantic_eager, AnchorKind::kDeviceEvent, 1, 20000,
                        25000);
  append_semantic_token(5, semantic_h, AnchorKind::kGraphH, 1, 25000, 35000);
  append_semantic_token(6, semantic_l, AnchorKind::kCommunication, 2, 30000,
                        40000);
  append_semantic_token(7, semantic_t, AnchorKind::kGraphT, 1, 40000, 45000);
  append_semantic_token(8, semantic_eager, AnchorKind::kDeviceEvent, 1, 45000,
                        50000);

  const std::vector<ReportToken> semantic_cost_tokens =
      compat::build_report_tokens_from_native_ir(semantic_cost_ir);
  const ReportTree flat_cost_tree =
      build_report_tree_from_tokens(semantic_cost_tokens);

  const SymbolId semantic_unit_symbol(1000);
  GlobalGrammarState semantic_cost_state;
  semantic_cost_state.stage = GrammarStage::kDone;
  semantic_cost_state.nodes = {
      GrammarNode{GrammarNodeId(0), semantic_unit_symbol, MacroDefId(0), 0, 3,
                  0, 20000, GrammarChunkId(0), GrammarNodeId::invalid(),
                  GrammarNodeId(1), true},
      GrammarNode{GrammarNodeId(1), semantic_eager, MacroDefId::invalid(), 3, 4,
                  20000, 25000, GrammarChunkId(0), GrammarNodeId(0),
                  GrammarNodeId(2), true},
      GrammarNode{GrammarNodeId(2), semantic_unit_symbol, MacroDefId(0), 4, 7,
                  25000, 45000, GrammarChunkId(0), GrammarNodeId(1),
                  GrammarNodeId(3), true},
      GrammarNode{GrammarNodeId(3), semantic_eager, MacroDefId::invalid(), 7, 8,
                  45000, 50000, GrammarChunkId(0), GrammarNodeId(2),
                  GrammarNodeId::invalid(), true},
  };
  semantic_cost_state.chunks = {GrammarChunk{
      GrammarChunkId(0), 0, 0, GrammarNodeId(0), GrammarNodeId(3), 4, 0}};
  semantic_cost_state.macro_defs = {MacroDefRow{
      MacroDefId(0), semantic_unit_symbol, MacroLevel::kSemantic,
      {semantic_h, semantic_l, semantic_t}, 3, 2, 2, 0, "ReplayUnit T1"}};
  semantic_cost_state.live_node_count = 4;
  const ReportTree semantic_cost_tree = build_report_tree_from_grammar_state(
      semantic_cost_tokens, semantic_cost_state);

  const compat::NodeCoverageSqlRows flat_cost_rows =
      compat::build_report_tree_node_coverage_sql_rows(flat_cost_tree,
                                                       semantic_cost_tokens);
  const compat::NodeCoverageSqlRows semantic_cost_rows =
      compat::build_report_tree_node_coverage_sql_rows(
          semantic_cost_tree, semantic_cost_tokens);
  require(flat_cost_rows.nodes.front().total_us == 50.0,
          "flat semantic fixture wall clock");
  require(semantic_cost_rows.nodes.front().total_us ==
              flat_cost_rows.nodes.front().total_us,
          "semantic and flat roots conserve the same wall clock");
  require(semantic_cost_rows.nodes.front().compute_us == 30.0,
          "semantic fixture compute cost");
  require(semantic_cost_rows.nodes.front().comm_us == 20.0,
          "semantic fixture communication cost");
  std::uint32_t semantic_unit_anchor_count = 0;
  double semantic_unit_total_us = 0.0;
  for (const compat::VizNodeSqlRow& row : semantic_cost_rows.nodes) {
    if (row.label == "ReplayUnit T1") {
      semantic_unit_anchor_count += row.anchor_count;
      semantic_unit_total_us += row.total_us;
    }
  }
  require(semantic_unit_anchor_count == 6, "semantic unit anchor count");
  require(semantic_unit_total_us == 40.0,
          "semantic unit overlap-safe wall clock");

  bool rejected_nonconserved_cost = false;
  try {
    std::vector<ReportToken> bad_cost_tokens = semantic_cost_tokens;
    bad_cost_tokens.front().timeline_anchor_us += 1.0;
    (void)compat::build_report_tree_node_coverage_sql_rows(
        semantic_cost_tree, bad_cost_tokens);
  } catch (const std::invalid_argument&) {
    rejected_nonconserved_cost = true;
  }
  require(rejected_nonconserved_cost,
          "non-conserved normalized wall clock must be rejected");

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

  // ---- Multi-device: one independently recovered report tree per device.
  // Device 0 owns a repeated [MatMul, AllReduce] pattern; device 1 owns a
  // distinct repeated Softmax run. Partitioning must never combine them into
  // one structural unit or stamp both trees with device 0.
  NativeIr multi_ir;
  const SourceRefId multi_source =
      multi_ir.source_refs.append("fixture", "multi_device", "TASK", 0);
  const SymbolId multi_matmul = multi_ir.symbols.intern("MatMul");
  const SymbolId multi_all_reduce = multi_ir.symbols.intern("AllReduce");
  const SymbolId multi_softmax = multi_ir.symbols.intern("Softmax");
  const auto append_multi_token = [&](std::uint32_t device_id,
                                      std::uint64_t source_row_id,
                                      SymbolId symbol, AnchorKind kind,
                                      std::int64_t start_ns,
                                      std::int64_t end_ns) {
    const TraceEventId event = multi_ir.trace_events.append(
        multi_source, source_row_id, device_id, 3, start_ns, end_ns, symbol);
    const AnchorId anchor = multi_ir.anchors.append(
        multi_source, event, ReplayUnitId::invalid(), kind, symbol, device_id,
        3, start_ns, end_ns);
    multi_ir.tokens.append(
        anchor, symbol, device_id,
        static_cast<std::uint32_t>(multi_ir.tokens.size()), start_ns, end_ns);
  };
  // Device 0: MatMul x3 then AllReduce x3 (two distinct adjacent runs).
  for (std::uint32_t step = 0; step < 3; ++step) {
    const std::int64_t base = 1000 + static_cast<std::int64_t>(step) * 1000;
    append_multi_token(0, step + 1, multi_matmul, AnchorKind::kDeviceEvent,
                       base, base + 600);
  }
  for (std::uint32_t step = 0; step < 3; ++step) {
    const std::int64_t base = 4000 + static_cast<std::int64_t>(step) * 1000;
    append_multi_token(0, step + 10, multi_all_reduce,
                       AnchorKind::kCommunication, base, base + 400);
  }
  // Device 1: Softmax x4 (a distinct adjacent run).
  for (std::uint32_t step = 0; step < 4; ++step) {
    const std::int64_t base = 3000 + static_cast<std::int64_t>(step) * 1000;
    append_multi_token(1, 100 + step, multi_softmax,
                       AnchorKind::kDeviceEvent, base, base + 300);
  }

  const std::vector<compat::NativeReportDevicePartition> partitions =
      compat::partition_report_tokens_by_device(multi_ir);
  require(partitions.size() == 2, "two device partitions");
  require(partitions[0].device_id == 0);
  require(partitions[1].device_id == 1);
  require(partitions[0].tokens.size() == 6, "device 0 partition tokens");
  require(partitions[1].tokens.size() == 4, "device 1 partition tokens");
  for (const compat::NativeReportDevicePartition& partition : partitions) {
    for (const ReportToken& token : partition.tokens) {
      require(token.device_id == partition.device_id,
              "partition owns only its own device tokens");
    }
  }

  compat::NativeCompatibilitySidecarOptions multi_options;
  multi_options.materialize_grammar_report_tree = false;
  const std::vector<compat::NativeDeviceReportTree> multi_trees =
      compat::build_native_device_report_trees(multi_ir, multi_options);
  require(multi_trees.size() == 2, "one tree per device");
  require(multi_trees[0].device_id == 0);
  require(multi_trees[1].device_id == 1);
  require(multi_trees[0].tokens.size() == 6);
  require(multi_trees[1].tokens.size() == 4);

  const compat::NodeCoverageSqlRows multi_rows =
      compat::build_native_loop_tree_node_coverage_rows(multi_ir,
                                                        multi_options);
  std::set<std::uint32_t> multi_device_ids;
  for (const compat::VizNodeSqlRow& node : multi_rows.nodes) {
    require(node.view_name == "native_report_tree");
    multi_device_ids.insert(node.device_id);
  }
  require(multi_device_ids == std::set<std::uint32_t>({0, 1}),
          "rows carry true per-device ids");

  // Each device has its own Seq root; no combined structural unit exists.
  const compat::VizNodeSqlRow* root0 = nullptr;
  const compat::VizNodeSqlRow* root1 = nullptr;
  for (const compat::VizNodeSqlRow& node : multi_rows.nodes) {
    if (node.kind == "seq" && node.device_id == 0) {
      root0 = &node;
    }
    if (node.kind == "seq" && node.device_id == 1) {
      root1 = &node;
    }
  }
  require(root0 != nullptr, "device 0 Seq root");
  require(root1 != nullptr, "device 1 Seq root");
  require(root0->node_id == "node-d0-N001", "device-scoped node id");
  require(root1->node_id == "node-d1-N001", "device-scoped node id");
  require(root0->anchor_count == 6);
  require(root1->anchor_count == 4);
  require(root0->first_anchor_idx == 1 && root0->last_anchor_idx == 6);
  require(root1->first_anchor_idx == 7 && root1->last_anchor_idx == 10);

  // Device 0 folds MatMul x3 and AllReduce x3; device 1 folds Softmax x4.
  // Every repeat node belongs to its own device with its own anchors.
  std::uint32_t device0_repeats = 0;
  std::uint32_t device1_repeats = 0;
  for (const compat::LoopNodeSqlRow& loop : multi_rows.loop_nodes) {
    if (loop.device_id == 0) {
      require(loop.repeat_count == 3 && loop.anchor_count == 3,
              "device 0 repeat folds only device 0 anchors");
      ++device0_repeats;
    }
    if (loop.device_id == 1 && loop.repeat_count == 4 &&
        loop.anchor_count == 4) {
      ++device1_repeats;
    }
  }
  require(device0_repeats == 2, "device 0 repeats preserved");
  require(device1_repeats == 1, "device 1 repeat preserved");

  // No edge crosses devices, and node-anchor provenance stays per device.
  for (const compat::VizEdgeSqlRow& edge : multi_rows.edges) {
    require(edge.device_id == 0 || edge.device_id == 1);
  }
  std::set<std::string> device0_nodes;
  std::set<std::string> device1_nodes;
  for (const compat::VizNodeAnchorSqlRow& row : multi_rows.node_anchors) {
    if (row.device_id == 0) {
      device0_nodes.insert(row.node_id);
    } else {
      device1_nodes.insert(row.node_id);
    }
  }
  require(device0_nodes.size() >= 3, "device 0 anchors land in device 0 nodes");
  require(device1_nodes.size() >= 2, "device 1 anchors land in device 1 nodes");

  // Per-device cost hierarchy: device 0 root spans its own wall clock only.
  require(root0->total_us == 5.4, "device 0 root wall clock");
  require(root1->total_us == 3.3, "device 1 root wall clock");

  // Semantic rows materialize one catalog entry per device with distinct
  // tree ids and scoped root ids.
  const compat::SemanticTreeSqlRows multi_semantic =
      compat::build_native_report_tree_semantic_sql_rows(
          multi_ir, 0, "native-report-tree", "anchor_tree");
  require(multi_semantic.trees.size() == 2,
          "one semantic tree per device");
  require(multi_semantic.trees[0].device_id == 0);
  require(multi_semantic.trees[1].device_id == 1);
  require(multi_semantic.trees[0].tree_id == "native-report-tree-d0");
  require(multi_semantic.trees[1].tree_id == "native-report-tree-d1");
  require(multi_semantic.trees[0].root_node_id == "node-d0-N001");
  require(multi_semantic.trees[1].root_node_id == "node-d1-N001");

  // Cross-device protected intervals fail closed instead of inventing a
  // combined replay structure.
  NativeIr cross_ir = multi_ir;
  cross_ir.protected_intervals.append(
      ProtectedIntervalKind::kGraphReplayUnit, BoundaryPolicy::kNoCross,
      TokenId(0), TokenId(7), AnchorId(0), AnchorId(7),
      cross_ir.source_refs.rows().front().id);
  bool rejected_cross_device_interval = false;
  try {
    (void)compat::build_native_device_report_trees(cross_ir, multi_options);
  } catch (const std::invalid_argument&) {
    rejected_cross_device_interval = true;
  }
  require(rejected_cross_device_interval,
          "cross-device protected intervals must fail closed");

  // ---- Replay-unit device attribution validates every present anchor
  // bound. A unit whose bounds disagree on device ownership, or whose
  // previously-unchecked bound is out of range, must fail closed instead of
  // being misattributed to the first checked bound.
  NativeIr disagree_ir;
  const SourceRefId disagree_source =
      disagree_ir.source_refs.append("fixture", "disagree_bounds", "TASK", 0);
  const SymbolId disagree_symbol = disagree_ir.symbols.intern("MatMul");
  const TraceEventId disagree_event0 = disagree_ir.trace_events.append(
      disagree_source, 1, 0, 3, 0, 100, disagree_symbol);
  const TraceEventId disagree_event1 = disagree_ir.trace_events.append(
      disagree_source, 2, 1, 3, 200, 300, disagree_symbol);
  const AnchorId disagree_anchor0 = disagree_ir.anchors.append(
      disagree_source, disagree_event0, ReplayUnitId::invalid(),
      AnchorKind::kDeviceEvent, disagree_symbol, 0, 3, 0, 100);
  const AnchorId disagree_anchor1 = disagree_ir.anchors.append(
      disagree_source, disagree_event1, ReplayUnitId::invalid(),
      AnchorKind::kDeviceEvent, disagree_symbol, 1, 3, 200, 300);
  const ReplayUnitId disagree_unit = disagree_ir.replay_units.append(
      GraphTemplateId::invalid(), disagree_source, disagree_anchor0,
      disagree_anchor1, TraceEventId::invalid());
  (void)disagree_unit;
  bool rejected_disagreeing_bounds = false;
  try {
    (void)compat::replay_unit_device_map(disagree_ir);
  } catch (const std::invalid_argument&) {
    rejected_disagreeing_bounds = true;
  }
  require(rejected_disagreeing_bounds,
          "replay unit with device-disagreeing anchor bounds must fail "
          "closed");

  // The unchecked direction: first bound in range, second bound out of
  // range. The old attribution selected the first valid bound and never
  // looked at the second, so this must now fail closed.
  NativeIr unused_bound_ir;
  const SourceRefId unused_bound_source = unused_bound_ir.source_refs.append(
      "fixture", "unused_bound", "TASK", 0);
  const SymbolId unused_bound_symbol =
      unused_bound_ir.symbols.intern("MatMul");
  const TraceEventId unused_bound_event0 = unused_bound_ir.trace_events.append(
      unused_bound_source, 1, 0, 3, 0, 100, unused_bound_symbol);
  const AnchorId unused_bound_anchor0 = unused_bound_ir.anchors.append(
      unused_bound_source, unused_bound_event0, ReplayUnitId::invalid(),
      AnchorKind::kDeviceEvent, unused_bound_symbol, 0, 3, 0, 100);
  unused_bound_ir.replay_units.append(
      GraphTemplateId::invalid(), unused_bound_source, unused_bound_anchor0,
      AnchorId(99), TraceEventId::invalid());
  bool rejected_unused_bound_out_of_range = false;
  try {
    (void)compat::replay_unit_device_map(unused_bound_ir);
  } catch (const std::invalid_argument&) {
    rejected_unused_bound_out_of_range = true;
  }
  require(rejected_unused_bound_out_of_range,
          "replay unit with an out-of-range second anchor bound must fail "
          "closed");

  // Well-formed units are attributed to their bound device, and units with
  // no valid bound are left unclaimed for the caller's fallback.
  NativeIr well_formed_ir;
  const SourceRefId well_formed_source = well_formed_ir.source_refs.append(
      "fixture", "well_formed_bounds", "TASK", 0);
  const SymbolId well_formed_symbol =
      well_formed_ir.symbols.intern("MatMul");
  const TraceEventId well_formed_event0 =
      well_formed_ir.trace_events.append(
          well_formed_source, 1, 0, 3, 0, 100, well_formed_symbol);
  const TraceEventId well_formed_event1 =
      well_formed_ir.trace_events.append(
          well_formed_source, 2, 0, 3, 200, 300, well_formed_symbol);
  const AnchorId well_formed_anchor0 = well_formed_ir.anchors.append(
      well_formed_source, well_formed_event0, ReplayUnitId::invalid(),
      AnchorKind::kDeviceEvent, well_formed_symbol, 0, 3, 0, 100);
  const AnchorId well_formed_anchor1 = well_formed_ir.anchors.append(
      well_formed_source, well_formed_event1, ReplayUnitId::invalid(),
      AnchorKind::kDeviceEvent, well_formed_symbol, 0, 3, 200, 300);
  const ReplayUnitId well_formed_unit = well_formed_ir.replay_units.append(
      GraphTemplateId::invalid(), well_formed_source, well_formed_anchor0,
      well_formed_anchor1, TraceEventId::invalid());
  const ReplayUnitId unclaimed_unit = well_formed_ir.replay_units.append(
      GraphTemplateId::invalid(), well_formed_source, AnchorId::invalid(),
      AnchorId::invalid(), TraceEventId::invalid());
  const std::map<ReplayUnitId::value_type, std::uint32_t> well_formed_devices =
      compat::replay_unit_device_map(well_formed_ir);
  require(well_formed_devices.at(well_formed_unit.value()) == 0,
          "well-formed replay unit attributed to its bound device");
  require(well_formed_devices.find(unclaimed_unit.value()) ==
              well_formed_devices.end(),
          "bound-less replay unit stays unclaimed");

  // ---- Grammar-enabled multi-device recovery with a same-device protected
  // replay interval. The per-device IR projection must remap TokenIds to a
  // dense per-device table (the grammar state machine requires dense ids)
  // while retaining original anchor and event ids.
  NativeIr replay_ir;
  const SourceRefId replay_source =
      replay_ir.source_refs.append("fixture", "multi_device_replay", "TASK",
                                   0);
  const SymbolId replay_matmul = replay_ir.symbols.intern("MatMul");
  const SymbolId replay_softmax = replay_ir.symbols.intern("Softmax");
  const GraphTemplateId replay_template =
      replay_ir.graph_templates.append(replay_source, 12345, 4);
  const ReplayCompositionCandidateId replay_candidate =
      replay_ir.replay_composition_candidates.append(
          replay_source, 1, GraphLaunchOccurrenceId::invalid(),
          GraphLaunchOccurrenceId::invalid(), 4, 0, 4, 1, 0, 4242,
          ReplayCompositionIdentityPolicy::kGraphConnection,
          ReplayCompositionOrderPolicy::kDeviceExecutionOrder,
          ReplayCompositionShapePolicy::kHeadRepeatedLayerTail,
          ReplayCompositionBoundaryPolicy::kExactOneShotLeadingComposition);
  const ReplayCompositionRegionId replay_region =
      replay_ir.replay_composition_regions.append(
          replay_candidate, 0, GraphLaunchOccurrenceId::invalid(),
          GraphLaunchOccurrenceId::invalid(), 3000, 6300, 4, 4,
          ReplayCompositionRegionStatus::kRecognizedCompletePattern);
  const ReplayUnitId replay_unit = replay_ir.replay_units.append(
      replay_template, replay_source, AnchorId(6), AnchorId(9),
      TraceEventId::invalid(), replay_region);
  const auto append_replay_token = [&](std::uint32_t device_id,
                                       std::uint64_t source_row_id,
                                       SymbolId symbol, AnchorKind kind,
                                       ReplayUnitId unit_id,
                                       std::int64_t start_ns,
                                       std::int64_t end_ns) {
    const TraceEventId event = replay_ir.trace_events.append(
        replay_source, source_row_id, device_id, 3, start_ns, end_ns, symbol);
    const AnchorId anchor = replay_ir.anchors.append(
        replay_source, event, unit_id, kind, symbol, device_id, 3, start_ns,
        end_ns);
    replay_ir.tokens.append(
        anchor, symbol, device_id,
        static_cast<std::uint32_t>(replay_ir.tokens.size()), start_ns,
        end_ns);
  };
  // Device 0: MatMul x3 then AllReduce x3 (no replay evidence).
  const SymbolId replay_all_reduce = replay_ir.symbols.intern("AllReduce");
  for (std::uint32_t step = 0; step < 3; ++step) {
    const std::int64_t base = 1000 + static_cast<std::int64_t>(step) * 1000;
    append_replay_token(0, step + 1, replay_matmul,
                        AnchorKind::kDeviceEvent, ReplayUnitId::invalid(),
                        base, base + 600);
  }
  for (std::uint32_t step = 0; step < 3; ++step) {
    const std::int64_t base = 4000 + static_cast<std::int64_t>(step) * 1000;
    append_replay_token(0, step + 10, replay_all_reduce,
                        AnchorKind::kCommunication, ReplayUnitId::invalid(),
                        base, base + 400);
  }
  // Device 1: Softmax x4 inside one same-device replay interval (anchors
  // 6..9, events 6..9, original ids).
  for (std::uint32_t step = 0; step < 4; ++step) {
    const std::int64_t base = 3000 + static_cast<std::int64_t>(step) * 1000;
    append_replay_token(1, 100 + step, replay_softmax,
                        AnchorKind::kDeviceEvent, replay_unit, base,
                        base + 300);
  }
  replay_ir.protected_intervals.append(
      ProtectedIntervalKind::kGraphReplayUnit, BoundaryPolicy::kNoCross,
      TokenId(6), TokenId(9), AnchorId(6), AnchorId(9), replay_source);

  require(replay_ir.anchors.row(AnchorId(6)).trace_event_id ==
              TraceEventId(6),
          "device 1 anchors retain original event ids");
  require(replay_ir.anchors.row(AnchorId(9)).trace_event_id ==
              TraceEventId(9),
          "device 1 anchors retain original event ids");

  compat::NativeCompatibilitySidecarOptions replay_options;
  replay_options.materialize_grammar_report_tree = true;
  replay_options.materialize_aux_attribution = false;
  const std::vector<compat::NativeDeviceReportTree> replay_trees =
      compat::build_native_device_report_trees(replay_ir, replay_options);
  require(replay_trees.size() == 2, "one grammar tree per device");
  require(replay_trees[0].device_id == 0);
  require(replay_trees[1].device_id == 1);
  require(replay_trees[0].tokens.size() == 6);
  require(replay_trees[1].tokens.size() == 4);

  bool saw_replay_unit_node = false;
  for (const ReportNodeDef& def : replay_trees[1].tree.node_defs) {
    if (def.kind == ReportNodeKind::kSeq &&
        def.display_op == "ReplayUnit T1") {
      saw_replay_unit_node = true;
    }
  }
  require(saw_replay_unit_node,
          "same-device replay interval recovered through the device "
          "projection with dense TokenId remapping");
  for (const ReportNodeDef& def : replay_trees[0].tree.node_defs) {
    require(!(def.kind == ReportNodeKind::kSeq &&
              def.display_op == "ReplayUnit T1"),
            "device 0 tree must not claim device 1's replay evidence");
  }

  const compat::NodeCoverageSqlRows replay_rows =
      compat::build_native_loop_tree_node_coverage_rows(replay_ir,
                                                        replay_options);
  std::set<std::string> replay_device1_anchor_ids;
  std::uint32_t replay_device1_seq_first = 0;
  std::uint32_t replay_device1_seq_last = 0;
  for (const compat::VizNodeAnchorSqlRow& row : replay_rows.node_anchors) {
    if (row.device_id == 1) {
      replay_device1_anchor_ids.insert(row.anchor_id);
    }
  }
  require(replay_device1_anchor_ids ==
              std::set<std::string>({"anchor-6", "anchor-7", "anchor-8",
                                     "anchor-9"}),
          "device 1 rows retain original anchor ids through the projection");
  for (const compat::VizNodeSqlRow& node : replay_rows.nodes) {
    if (node.device_id == 1 && node.kind == "seq") {
      replay_device1_seq_first = node.first_anchor_idx;
      replay_device1_seq_last = node.last_anchor_idx;
      require(node.anchor_count == 4);
    }
  }
  require(replay_device1_seq_first == 7 && replay_device1_seq_last == 10,
          "device 1 rows keep original global anchor indices");

  return 0;
}
