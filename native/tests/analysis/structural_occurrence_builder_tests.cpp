#include "traceloom/analysis/structural_occurrence_builder.h"
#include "traceloom/testing/test_util.h"

#include <utility>
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

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  const SymbolId matmul(1);
  const std::vector<StructuralProjectionToken> run_tokens{
      token(0, matmul, "MatMul"),
      token(1, matmul, "MatMul"),
      token(2, matmul, "MatMul"),
      token(3, matmul, "MatMul"),
  };
  const StructuralOccurrenceGraph run_tree =
      build_structural_occurrence_graph_from_tokens(run_tokens);
  require(run_tree.node_defs.size() == 3);
  require(run_tree.occurrences.size() == 6);
  require(run_tree.edges.size() == 5);
  require(run_tree.coverage.size() == 6);

  const StructuralNodeDef& root = run_tree.node_defs[0];
  const StructuralNodeDef& repeat = run_tree.node_defs[1];
  const StructuralNodeDef& atom = run_tree.node_defs[2];
  require(root.local_node_id == "N001");
  require(root.kind == StructuralNodeKind::kSeq);
  require(repeat.local_node_id == "N002");
  require(repeat.kind == StructuralNodeKind::kRepeat);
  require(repeat.display_op == "Rep x4");
  require(repeat.repeat_count == 4);
  require(atom.local_node_id == "N003");
  require(atom.kind == StructuralNodeKind::kAtom);
  require(atom.display_op == "MatMul");
  require(atom.display_category == "exec");
  require(structural_occurrence_count_for_def(run_tree, atom.id) == 4);

  const StructuralNodeOccurrence& root_occ = run_tree.occurrences[0];
  const StructuralNodeOccurrence& repeat_occ = run_tree.occurrences[1];
  require(root_occ.token_start_ordinal == 0);
  require(root_occ.token_end_ordinal == 4);
  require(!root_occ.parent_occurrence_id.valid());
  require(repeat_occ.parent_occurrence_id == root_occ.id);
  require(repeat_occ.token_start_ordinal == 0);
  require(repeat_occ.token_end_ordinal == 4);
  for (std::size_t i = 2; i < run_tree.occurrences.size(); ++i) {
    const StructuralNodeOccurrence& occ = run_tree.occurrences[i];
    require(occ.parent_occurrence_id == repeat_occ.id);
    require(occ.node_def_id == atom.id);
    require(occ.token_end_ordinal == occ.token_start_ordinal + 1);
    require(occ.repeat_iteration == i - 1);
  }

  const SymbolId rmsnorm(2);
  const SymbolId rope(3);
  const std::vector<StructuralProjectionToken> mixed_tokens{
      token(0, rmsnorm, "RmsNorm"),
      token(1, rope, "Rope"),
      token(2, rmsnorm, "RmsNorm"),
      token(3, rope, "Rope"),
  };
  const StructuralOccurrenceGraph mixed_tree =
      build_structural_occurrence_graph_from_tokens(mixed_tokens);
  require(mixed_tree.node_defs.size() == 5);
  require(mixed_tree.occurrences.size() == 5);
  require(mixed_tree.edges.size() == 4);
  require(mixed_tree.node_defs[0].local_node_id == "N001");
  require(mixed_tree.node_defs[1].display_op == "RmsNorm");
  require(mixed_tree.node_defs[2].display_op == "Rope");
  require(mixed_tree.node_defs[3].display_op == "RmsNorm");
  require(mixed_tree.node_defs[4].display_op == "Rope");
  for (const StructuralNodeDef& def : mixed_tree.node_defs) {
    require(def.kind != StructuralNodeKind::kRepeat);
  }

  bool caught_bad_min_run = false;
  try {
    (void)build_structural_occurrence_graph_from_tokens(run_tokens,
                                        StructuralOccurrenceBuildConfig{true, 0});
  } catch (const std::invalid_argument&) {
    caught_bad_min_run = true;
  }
  require(caught_bad_min_run);

  const auto rejects_tree = [&](StructuralOccurrenceGraph tree) {
    try {
      validate_structural_occurrence_graph_or_throw(tree,
                                    static_cast<std::uint32_t>(run_tokens.size()));
    } catch (const std::invalid_argument&) {
      return true;
    }
    return false;
  };

  StructuralOccurrenceGraph bad_def_id = run_tree;
  bad_def_id.node_defs[1].id = StructuralNodeDefId(99);
  require(rejects_tree(std::move(bad_def_id)));

  StructuralOccurrenceGraph bad_occurrence_index = run_tree;
  bad_occurrence_index.occurrences.back().occurrence_index_for_def = 0;
  require(rejects_tree(std::move(bad_occurrence_index)));

  StructuralOccurrenceGraph bad_cached_count = run_tree;
  bad_cached_count.occurrence_counts_by_def[atom.id.value()] -= 1;
  require(rejects_tree(std::move(bad_cached_count)));

  StructuralOccurrenceGraph unrealized_definition = run_tree;
  StructuralNodeDef unused = unrealized_definition.node_defs.back();
  unused.id = StructuralNodeDefId(
      static_cast<StructuralNodeDefId::value_type>(unrealized_definition.node_defs.size()));
  unused.local_node_id = "N004";
  unrealized_definition.node_defs.push_back(std::move(unused));
  unrealized_definition.occurrence_counts_by_def.push_back(0);
  require(rejects_tree(std::move(unrealized_definition)));

  return 0;
}
