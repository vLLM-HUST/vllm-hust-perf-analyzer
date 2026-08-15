#include "traceloom/report/report_tree_builder.h"

namespace traceloom {

ReportTree build_report_tree_from_tokens(const std::vector<ReportToken>& tokens,
                                         ReportTreeBuildConfig config) {
  return build_structural_occurrence_graph_from_tokens(tokens, config);
}

ReportTree build_report_tree_from_grammar(
    const std::vector<ReportToken>& tokens,
    const ReportGrammarEvidence& grammar,
    ReportTreeBuildConfig config) {
  return build_structural_occurrence_graph_from_grammar(tokens, grammar,
                                                        config);
}

ReportTree build_report_tree_from_grammar(
    const std::vector<ReportToken>& tokens,
    const ReportGrammarEvidence& grammar,
    const ReportGraphReplayEvidence& graph,
    ReportTreeBuildConfig config) {
  return build_structural_occurrence_graph_from_grammar(tokens, grammar, graph,
                                                        config);
}

ReportTree build_report_tree_from_grammar_state(
    const std::vector<ReportToken>& tokens,
    const GlobalGrammarState& state,
    ReportTreeBuildConfig config) {
  return build_structural_occurrence_graph_from_grammar_state(tokens, state,
                                                              config);
}

void validate_report_tree_or_throw(const ReportTree& tree,
                                   std::uint32_t token_count) {
  validate_structural_occurrence_graph_or_throw(tree, token_count);
}

}  // namespace traceloom
