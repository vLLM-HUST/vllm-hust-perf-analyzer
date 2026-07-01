#include "traceloom/report/anchor_internal_cost_breakdown.h"
#include "traceloom/report/report_tree_builder.h"
#include "traceloom/testing/test_util.h"

#include <vector>

namespace {

traceloom::ReportToken token(std::uint32_t ordinal,
                             traceloom::SymbolId symbol,
                             const char* op,
                             traceloom::ReportAnchorKind kind) {
  traceloom::ReportToken out;
  out.ordinal = ordinal;
  out.symbol_id = symbol;
  out.display_op = op;
  out.display_category = "exec";
  out.anchor_kind = kind;
  out.start_ns = static_cast<std::int64_t>(ordinal) * 10;
  out.end_ns = out.start_ns + 5;
  return out;
}

traceloom::AnchorCostComponentLeaf component(
    std::uint32_t id,
    std::uint32_t token_ordinal,
    traceloom::AnchorCostComponentKind kind,
    std::int64_t duration_ns) {
  traceloom::AnchorCostComponentLeaf out;
  out.id = traceloom::ReportCostLeafId(id);
  out.token_ordinal = token_ordinal;
  out.kind = kind;
  out.duration_ns = duration_ns;
  return out;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  const std::vector<ReportToken> tokens{
      token(0, SymbolId(1), "MatMul", ReportAnchorKind::kExec),
      token(1, SymbolId(2), "H", ReportAnchorKind::kGraphH),
      token(2, SymbolId(3), "Memcpy", ReportAnchorKind::kExec),
  };

  const ReportTree tree = build_report_tree_from_tokens(tokens);
  std::vector<AnchorCostComponentLeaf> leaves{
      component(0, 0, AnchorCostComponentKind::kSelf, 100),
      component(1, 0, AnchorCostComponentKind::kAux, 20),
      component(2, 1, AnchorCostComponentKind::kGraphChild, 300),
      component(3, 1, AnchorCostComponentKind::kAux, 5),
      component(4, 2, AnchorCostComponentKind::kResidual, 0),
  };
  leaves[2].raw_child_task_count = 3;
  leaves[2].source_ref_count = 3;
  leaves[2].top_ops = "MatMul:2;RmsNorm:1";
  leaves[4].diagnostic_flags = "partial_overlap_diagnostic_only";

  const AnchorInternalCostBreakdown breakdown =
      build_anchor_internal_cost_breakdown(tree, tokens, leaves);

  require(breakdown.diagnostics.empty());
  require(breakdown.rows.size() == 3);

  const AnchorInternalCostBreakdownRow& exec = breakdown.rows[0];
  require(exec.anchor_idx == 1);
  require(exec.symbol == "MatMul");
  require(exec.anchor_kind == ReportAnchorKind::kExec);
  require(exec.self_ns == 100);
  require(exec.aux_ns == 20);
  require(exec.graph_child_ns == 0);
  require(exec.residual_ns == 0);
  require(exec.total_ns == 120);

  const AnchorInternalCostBreakdownRow& graph_h = breakdown.rows[1];
  require(graph_h.anchor_idx == 2);
  require(graph_h.symbol == "H");
  require(graph_h.anchor_kind == ReportAnchorKind::kGraphH);
  require(graph_h.self_ns == 0);
  require(graph_h.aux_ns == 5);
  require(graph_h.graph_child_ns == 300);
  require(graph_h.residual_ns == 0);
  require(graph_h.total_ns == 305);
  require(graph_h.raw_child_task_count == 3);
  require(graph_h.source_ref_count == 3);
  require(graph_h.top_ops == "MatMul:2;RmsNorm:1");

  const AnchorInternalCostBreakdownRow& residual = breakdown.rows[2];
  require(residual.anchor_idx == 3);
  require(residual.total_ns == 0);
  require(residual.diagnostic_flags == "partial_overlap_diagnostic_only");

  const AnchorInternalCostBreakdown orphan =
      build_anchor_internal_cost_breakdown(
          tree, tokens,
          std::vector<AnchorCostComponentLeaf>{
              component(0, 99, AnchorCostComponentKind::kSelf, 1)});
  require(!orphan.diagnostics.empty());
  require(orphan.diagnostics[0].severity == DiagnosticSeverity::kError);
  require(orphan.diagnostics[0].code == "anchor_cost_orphan_component_leaf");
  require(orphan.rows.empty());

  const AnchorInternalCostBreakdown invalid_leaf =
      build_anchor_internal_cost_breakdown(
          tree, tokens,
          std::vector<AnchorCostComponentLeaf>{
              component(1, 0, AnchorCostComponentKind::kSelf, 1)});
  require(!invalid_leaf.diagnostics.empty());
  require(invalid_leaf.diagnostics[0].severity == DiagnosticSeverity::kError);
  require(invalid_leaf.diagnostics[0].code ==
          "anchor_cost_invalid_component_leaf_id");
  require(invalid_leaf.rows.empty());

  return 0;
}
