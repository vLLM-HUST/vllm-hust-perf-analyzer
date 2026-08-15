#pragma once

#include "traceloom/analysis/structural_occurrence_builder.h"
#include "traceloom/report/report_tree.h"

namespace traceloom {

using ReportTreeBuildConfig = StructuralOccurrenceBuildConfig;
using ReportMacroVisibility = StructuralMacroVisibility;
using ReportMacroDefinition = StructuralMacroDefinition;
using ReportGrammarItemKind = StructuralGrammarItemKind;
using ReportGrammarItem = StructuralGrammarItem;
using ReportGrammarEvidence = StructuralGrammarEvidence;
using ReportGraphTilingStatus = StructuralGraphTilingStatus;
using ReportGraphReplayEvidence = StructuralGraphReplayEvidence;

ReportTree build_report_tree_from_tokens(
    const std::vector<ReportToken>& tokens,
    ReportTreeBuildConfig config = ReportTreeBuildConfig{});

ReportTree build_report_tree_from_grammar(
    const std::vector<ReportToken>& tokens,
    const ReportGrammarEvidence& grammar,
    ReportTreeBuildConfig config = ReportTreeBuildConfig{});

ReportTree build_report_tree_from_grammar(
    const std::vector<ReportToken>& tokens,
    const ReportGrammarEvidence& grammar,
    const ReportGraphReplayEvidence& graph,
    ReportTreeBuildConfig config = ReportTreeBuildConfig{});

ReportTree build_report_tree_from_grammar_state(
    const std::vector<ReportToken>& tokens,
    const GlobalGrammarState& state,
    ReportTreeBuildConfig config = ReportTreeBuildConfig{});

void validate_report_tree_or_throw(const ReportTree& tree,
                                   std::uint32_t token_count);

}  // namespace traceloom
