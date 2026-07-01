#pragma once

#include <string>
#include <vector>

#include "traceloom/report/report_tree.h"

namespace traceloom {

struct ReportTreeBuildConfig {
  bool fold_adjacent_runs = true;
  std::uint32_t min_run_length = 2;
};

enum class ReportMacroVisibility {
  kInline,
  kKeepRepeat,
};

struct ReportMacroDefinition {
  std::string macro_name;
  std::vector<SymbolId> body_symbols;
  ReportMacroVisibility visibility = ReportMacroVisibility::kInline;
};

enum class ReportGrammarItemKind {
  kSymbol,
  kMacro,
};

struct ReportGrammarItem {
  ReportGrammarItemKind kind = ReportGrammarItemKind::kSymbol;
  SymbolId symbol_id;
  std::string macro_name;

  static ReportGrammarItem symbol(SymbolId symbol_id);
  static ReportGrammarItem macro(std::string macro_name);
};

struct ReportGrammarEvidence {
  std::vector<ReportMacroDefinition> macros;
  std::vector<ReportGrammarItem> final_sequence;
};

enum class ReportGraphTilingStatus {
  kNone,
  kExact,
  kGap,
  kOverlap,
  kAmbiguous,
};

struct ReportGraphReplayEvidence {
  ReportGraphTilingStatus tiling_status = ReportGraphTilingStatus::kNone;
  std::string diagnostic_code;
};

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

void validate_report_tree_or_throw(const ReportTree& tree,
                                   std::uint32_t token_count);

}  // namespace traceloom
