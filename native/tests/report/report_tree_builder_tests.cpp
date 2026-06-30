#include "traceloom/report/report_tree_builder.h"
#include "traceloom/testing/test_util.h"

#include <vector>

namespace {

traceloom::ReportToken token(std::uint32_t ordinal,
                             traceloom::SymbolId symbol,
                             const char* op) {
  traceloom::ReportToken out;
  out.ordinal = ordinal;
  out.symbol_id = symbol;
  out.display_op = op;
  out.display_category = "exec";
  out.anchor_kind = traceloom::ReportAnchorKind::kExec;
  out.start_ns = static_cast<std::int64_t>(ordinal) * 10;
  out.end_ns = out.start_ns + 5;
  return out;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  const SymbolId matmul(1);
  const std::vector<ReportToken> run_tokens{
      token(0, matmul, "MatMul"),
      token(1, matmul, "MatMul"),
      token(2, matmul, "MatMul"),
      token(3, matmul, "MatMul"),
  };
  const ReportTree run_tree = build_report_tree_from_tokens(run_tokens);
  require(run_tree.node_defs.size() == 3);
  require(run_tree.occurrences.size() == 6);
  require(run_tree.edges.size() == 5);
  require(run_tree.coverage.size() == 6);

  const ReportNodeDef& root = run_tree.node_defs[0];
  const ReportNodeDef& repeat = run_tree.node_defs[1];
  const ReportNodeDef& atom = run_tree.node_defs[2];
  require(root.local_node_id == "N001");
  require(root.kind == ReportNodeKind::kSeq);
  require(repeat.local_node_id == "N002");
  require(repeat.kind == ReportNodeKind::kRepeat);
  require(repeat.display_op == "Rep x4");
  require(repeat.repeat_count == 4);
  require(atom.local_node_id == "N003");
  require(atom.kind == ReportNodeKind::kAtom);
  require(atom.display_op == "MatMul");
  require(atom.display_category == "exec");
  require(occurrence_count_for_def(run_tree, atom.id) == 4);

  const ReportNodeOccurrence& root_occ = run_tree.occurrences[0];
  const ReportNodeOccurrence& repeat_occ = run_tree.occurrences[1];
  require(root_occ.token_start_ordinal == 0);
  require(root_occ.token_end_ordinal == 4);
  require(!root_occ.parent_occurrence_id.valid());
  require(repeat_occ.parent_occurrence_id == root_occ.id);
  require(repeat_occ.token_start_ordinal == 0);
  require(repeat_occ.token_end_ordinal == 4);
  for (std::size_t i = 2; i < run_tree.occurrences.size(); ++i) {
    const ReportNodeOccurrence& occ = run_tree.occurrences[i];
    require(occ.parent_occurrence_id == repeat_occ.id);
    require(occ.node_def_id == atom.id);
    require(occ.token_end_ordinal == occ.token_start_ordinal + 1);
    require(occ.repeat_iteration == i - 1);
  }

  const SymbolId rmsnorm(2);
  const SymbolId rope(3);
  const std::vector<ReportToken> mixed_tokens{
      token(0, rmsnorm, "RmsNorm"),
      token(1, rope, "Rope"),
      token(2, rmsnorm, "RmsNorm"),
      token(3, rope, "Rope"),
  };
  const ReportTree mixed_tree = build_report_tree_from_tokens(mixed_tokens);
  require(mixed_tree.node_defs.size() == 5);
  require(mixed_tree.occurrences.size() == 5);
  require(mixed_tree.edges.size() == 4);
  require(mixed_tree.node_defs[0].local_node_id == "N001");
  require(mixed_tree.node_defs[1].display_op == "RmsNorm");
  require(mixed_tree.node_defs[2].display_op == "Rope");
  require(mixed_tree.node_defs[3].display_op == "RmsNorm");
  require(mixed_tree.node_defs[4].display_op == "Rope");
  for (const ReportNodeDef& def : mixed_tree.node_defs) {
    require(def.kind != ReportNodeKind::kRepeat);
  }

  bool caught_bad_min_run = false;
  try {
    (void)build_report_tree_from_tokens(run_tokens,
                                        ReportTreeBuildConfig{true, 0});
  } catch (const std::invalid_argument&) {
    caught_bad_min_run = true;
  }
  require(caught_bad_min_run);

  return 0;
}
