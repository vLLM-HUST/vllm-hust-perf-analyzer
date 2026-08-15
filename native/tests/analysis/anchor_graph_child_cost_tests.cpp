#include "traceloom/analysis/anchor_graph_child_cost.h"
#include "traceloom/analysis/anchor_internal_cost_breakdown.h"
#include "traceloom/analysis/structural_occurrence_builder.h"
#include "traceloom/testing/test_util.h"

#include <vector>

namespace {

traceloom::AnchorGraphChildWindow window(std::uint32_t token_ordinal,
                                         std::uint32_t stream_id,
                                         std::int64_t start_ns,
                                         std::int64_t end_ns) {
  traceloom::AnchorGraphChildWindow out;
  out.token_ordinal = token_ordinal;
  out.stream_id = stream_id;
  out.start_ns = start_ns;
  out.end_ns = end_ns;
  return out;
}

traceloom::AnchorGraphChildTask task(std::uint32_t stream_id,
                                     std::int64_t start_ns,
                                     std::int64_t end_ns,
                                     std::int64_t duration_ns,
                                     const char* op,
                                     traceloom::SourceRefId source_ref_id) {
  traceloom::AnchorGraphChildTask out;
  out.stream_id = stream_id;
  out.start_ns = start_ns;
  out.end_ns = end_ns;
  out.duration_ns = duration_ns;
  out.op = op;
  out.source_ref_id = source_ref_id;
  return out;
}

traceloom::StructuralProjectionToken token(
    std::uint32_t ordinal,
    traceloom::SymbolId symbol,
    const char* op,
    traceloom::StructuralAnchorKind kind) {
  traceloom::StructuralProjectionToken out;
  out.ordinal = ordinal;
  out.symbol_id = symbol;
  out.display_op = op;
  out.display_category = "graph";
  out.anchor_kind = kind;
  out.start_ns = static_cast<std::int64_t>(ordinal) * 100;
  out.end_ns = out.start_ns + 100;
  return out;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  const std::vector<AnchorGraphChildWindow> windows{
      window(0, 7, 100, 200),
      window(1, 7, 200, 320),
  };
  const std::vector<AnchorGraphChildTask> tasks{
      task(7, 110, 150, 40, "MatMul", SourceRefId(1)),
      task(7, 150, 180, 30, "RmsNorm", SourceRefId(2)),
      task(7, 205, 300, 95, "MatMul", SourceRefId(3)),
      task(8, 110, 150, 100, "WrongStream", SourceRefId(4)),
      task(7, 90, 110, 20, "Boundary", SourceRefId(5)),
  };

  AnchorGraphChildCostConfig config;
  config.first_leaf_id = StructuralCostLeafId(2);
  const AnchorGraphChildCostResult graph_cost =
      build_anchor_graph_child_cost_components(windows, tasks, config);

  require(graph_cost.component_leaves.size() == 2);
  require(graph_cost.diagnostics.size() == 1);
  require(graph_cost.diagnostics[0].severity == DiagnosticSeverity::kWarning);
  require(graph_cost.diagnostics[0].code ==
          "anchor_graph_child_partial_overlap");

  const AnchorCostComponentLeaf& first = graph_cost.component_leaves[0];
  require(first.id == StructuralCostLeafId(2));
  require(first.token_ordinal == 0);
  require(first.kind == AnchorCostComponentKind::kGraphChild);
  require(first.duration_ns == 70);
  require(first.raw_child_task_count == 2);
  require(first.source_ref_count == 2);
  require(first.top_ops == "MatMul:40;RmsNorm:30");
  require(first.diagnostic_flags == "partial_overlap_diagnostic_only");

  const AnchorCostComponentLeaf& second = graph_cost.component_leaves[1];
  require(second.id == StructuralCostLeafId(3));
  require(second.token_ordinal == 1);
  require(second.duration_ns == 95);
  require(second.raw_child_task_count == 1);
  require(second.source_ref_count == 1);
  require(second.top_ops == "MatMul:95");
  require(second.diagnostic_flags.empty());

  const std::vector<StructuralProjectionToken> structural_tokens{
      token(0, SymbolId(1), "ACLH", StructuralAnchorKind::kGraphH),
      token(1, SymbolId(2), "ACLL", StructuralAnchorKind::kGraphL),
  };
  const StructuralOccurrenceGraph tree =
      build_structural_occurrence_graph_from_tokens(structural_tokens);
  std::vector<AnchorCostComponentLeaf> leaves = graph_cost.component_leaves;
  for (std::size_t i = 0; i < leaves.size(); ++i) {
    leaves[i].id = StructuralCostLeafId(static_cast<std::uint32_t>(i));
  }
  const AnchorInternalCostBreakdown breakdown =
      build_anchor_internal_cost_breakdown(tree, structural_tokens, leaves);
  require(breakdown.diagnostics.empty());
  require(breakdown.rows.size() == 2);
  require(breakdown.rows[0].graph_child_ns == 70);
  require(breakdown.rows[0].total_ns == 70);
  require(breakdown.rows[0].raw_child_task_count == 2);
  require(breakdown.rows[0].top_ops == "MatMul:40;RmsNorm:30");
  require(breakdown.rows[1].graph_child_ns == 95);
  require(breakdown.rows[1].total_ns == 95);

  const AnchorGraphChildCostResult duplicate_owner =
      build_anchor_graph_child_cost_components(
          std::vector<AnchorGraphChildWindow>{
              window(0, 7, 100, 250),
              window(1, 7, 150, 300),
          },
          std::vector<AnchorGraphChildTask>{
              task(7, 180, 200, 20, "MatMul", SourceRefId(6)),
          });
  require(duplicate_owner.component_leaves.empty());
  require(!duplicate_owner.diagnostics.empty());
  require(duplicate_owner.diagnostics[0].severity == DiagnosticSeverity::kError);
  require(duplicate_owner.diagnostics[0].code ==
          "anchor_graph_child_duplicate_task_owner");

  const std::vector<AnchorGraphChildSummary> summaries{
      AnchorGraphChildSummary{0, 1000, 1100, 100, 4, 1, "Embedding:4", ""},
      AnchorGraphChildSummary{1, 1100, 1200, 100, 20, 1, "MatMul:16", ""},
  };
  const AnchorGraphChildCostResult summary_cost =
      build_anchor_graph_child_summary_components(summaries);
  require(summary_cost.diagnostics.empty());
  require(summary_cost.component_leaves.size() == 2);
  require(summary_cost.component_leaves[0].id == StructuralCostLeafId(0));
  require(summary_cost.component_leaves[0].token_ordinal == 0);
  require(summary_cost.component_leaves[0].duration_ns == 100);
  require(summary_cost.component_leaves[0].raw_child_task_count == 4);
  require(summary_cost.component_leaves[0].source_ref_count == 1);
  require(summary_cost.component_leaves[0].top_ops == "Embedding:4");
  require(summary_cost.component_leaves[1].duration_ns == 100);
  require(summary_cost.component_leaves[1].raw_child_task_count == 20);
  require(summary_cost.component_leaves[1].top_ops == "MatMul:16");

  const AnchorGraphChildCostResult invalid_summary =
      build_anchor_graph_child_summary_components(
          std::vector<AnchorGraphChildSummary>{
              AnchorGraphChildSummary{0, 10, 5, 1, 0, 0, "", ""},
          });
  require(!invalid_summary.diagnostics.empty());
  require(invalid_summary.diagnostics[0].severity == DiagnosticSeverity::kError);
  require(invalid_summary.diagnostics[0].code ==
          "anchor_graph_child_invalid_summary_range");

  return 0;
}
