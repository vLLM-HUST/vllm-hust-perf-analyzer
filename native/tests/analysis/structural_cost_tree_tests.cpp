#include "traceloom/analysis/structural_cost_tree.h"
#include "traceloom/analysis/structural_occurrence_builder.h"
#include "traceloom/testing/test_util.h"

#include <vector>

namespace {

traceloom::StructuralProjectionToken token(std::uint32_t ordinal,
                                            traceloom::SymbolId symbol,
                                            const char* op) {
  traceloom::StructuralProjectionToken out;
  out.ordinal = ordinal;
  out.symbol_id = symbol;
  out.display_op = op;
  out.display_category = "exec";
  out.anchor_kind = traceloom::StructuralAnchorKind::kExec;
  out.start_ns = static_cast<std::int64_t>(ordinal) * 10;
  out.end_ns = out.start_ns + 5;
  return out;
}

traceloom::StructuralCostLeaf leaf(std::uint32_t id,
                               std::uint32_t token_ordinal,
                               std::int64_t duration_ns) {
  traceloom::StructuralCostLeaf out;
  out.id = traceloom::StructuralCostLeafId(id);
  out.token_ordinal = token_ordinal;
  out.duration_ns = duration_ns;
  out.source_label = "token_fixture";
  return out;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  const SymbolId matmul(1);
  const std::vector<StructuralProjectionToken> tokens{
      token(0, matmul, "MatMul"),
      token(1, matmul, "MatMul"),
  };

  const StructuralOccurrenceGraph tree = build_structural_occurrence_graph_from_tokens(tokens);
  const std::vector<StructuralCostLeaf> leaves{
      leaf(0, 0, 7),
      leaf(1, 1, 11),
  };
  const StructuralCostTree cost_tree = build_structural_cost_tree(tree, leaves);

  require(cost_tree.diagnostics.empty());
  require(cost_tree.metrics.size() == tree.occurrences.size() + leaves.size());

  const StructuralNodeOccurrenceId root_id = tree.occurrences[0].id;
  const StructuralNodeOccurrenceId repeat_id = tree.occurrences[1].id;
  const StructuralNodeOccurrenceId first_atom_id = tree.occurrences[2].id;
  const StructuralNodeOccurrenceId second_atom_id = tree.occurrences[3].id;

  const StructuralCostMetric* root_metric =
      find_structural_cost_metric(cost_tree, root_id);
  const StructuralCostMetric* repeat_metric =
      find_structural_cost_metric(cost_tree, repeat_id);
  const StructuralCostMetric* first_atom_metric =
      find_structural_cost_metric(cost_tree, first_atom_id);
  const StructuralCostMetric* second_atom_metric =
      find_structural_cost_metric(cost_tree, second_atom_id);
  require(root_metric != nullptr);
  require(repeat_metric != nullptr);
  require(first_atom_metric != nullptr);
  require(second_atom_metric != nullptr);

  require(root_metric->self_duration_ns == 0);
  require(root_metric->direct_child_duration_ns == 18);
  require(root_metric->total_duration_ns == 18);
  require(root_metric->direct_structural_child_count == 1);
  require(root_metric->direct_cost_leaf_count == 0);
  require(root_metric->subtree_cost_leaf_count == 2);

  require(repeat_metric->direct_child_duration_ns == 18);
  require(repeat_metric->total_duration_ns == 18);
  require(repeat_metric->direct_structural_child_count == 2);
  require(repeat_metric->direct_cost_leaf_count == 0);
  require(repeat_metric->subtree_cost_leaf_count == 2);

  require(first_atom_metric->direct_child_duration_ns == 7);
  require(first_atom_metric->total_duration_ns == 7);
  require(first_atom_metric->direct_structural_child_count == 0);
  require(first_atom_metric->direct_cost_leaf_count == 1);
  require(first_atom_metric->subtree_cost_leaf_count == 1);

  require(second_atom_metric->direct_child_duration_ns == 11);
  require(second_atom_metric->total_duration_ns == 11);
  require(second_atom_metric->direct_cost_leaf_count == 1);
  require(second_atom_metric->subtree_cost_leaf_count == 1);

  const StructuralCostMetric* first_leaf_metric =
      find_structural_cost_leaf_metric(cost_tree, StructuralCostLeafId(0));
  require(first_leaf_metric != nullptr);
  require(first_leaf_metric->self_duration_ns == 7);
  require(first_leaf_metric->direct_child_duration_ns == 0);
  require(first_leaf_metric->total_duration_ns == 7);

  std::uint32_t direct_atom_leaf_edges = 0;
  for (const StructuralCostTreeEdge& edge : cost_tree.edges) {
    if (edge.child_kind == StructuralCostItemKind::kCostLeaf) {
      require(edge.parent_occurrence_id == first_atom_id ||
              edge.parent_occurrence_id == second_atom_id);
      direct_atom_leaf_edges += 1;
    }
  }
  require(direct_atom_leaf_edges == 2);

  const StructuralCostTree orphan =
      build_structural_cost_tree(tree, std::vector<StructuralCostLeaf>{leaf(0, 99, 3)});
  require(!orphan.diagnostics.empty());
  require(orphan.diagnostics[0].severity == DiagnosticSeverity::kError);
  require(orphan.diagnostics[0].code == "cost_tree_orphan_cost_leaf");
  require(orphan.edges.empty());
  require(orphan.metrics.empty());

  const StructuralCostTree invalid_leaf_id =
      build_structural_cost_tree(tree, std::vector<StructuralCostLeaf>{leaf(1, 0, 3)});
  require(!invalid_leaf_id.diagnostics.empty());
  require(invalid_leaf_id.diagnostics[0].severity == DiagnosticSeverity::kError);
  require(invalid_leaf_id.diagnostics[0].code ==
          "cost_tree_invalid_cost_leaf_id");
  require(invalid_leaf_id.edges.empty());
  require(invalid_leaf_id.metrics.empty());

  return 0;
}
