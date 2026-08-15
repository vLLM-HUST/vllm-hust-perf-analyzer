#pragma once

#include <string>
#include <vector>

#include "traceloom/analysis/structural_occurrence_graph.h"

namespace traceloom {

struct GlobalGrammarState;

struct StructuralOccurrenceBuildConfig {
  bool fold_adjacent_runs = true;
  std::uint32_t min_run_length = 2;
};

enum class StructuralMacroVisibility {
  kInline,
  kKeepRepeat,
};

struct StructuralMacroDefinition {
  std::string macro_name;
  std::vector<SymbolId> body_symbols;
  StructuralMacroVisibility visibility = StructuralMacroVisibility::kInline;
};

enum class StructuralGrammarItemKind {
  kSymbol,
  kMacro,
};

struct StructuralGrammarItem {
  StructuralGrammarItemKind kind = StructuralGrammarItemKind::kSymbol;
  SymbolId symbol_id;
  std::string macro_name;

  static StructuralGrammarItem symbol(SymbolId symbol_id);
  static StructuralGrammarItem macro(std::string macro_name);
};

struct StructuralGrammarEvidence {
  std::vector<StructuralMacroDefinition> macros;
  std::vector<StructuralGrammarItem> final_sequence;
};

enum class StructuralGraphTilingStatus {
  kNone,
  kExact,
  kGap,
  kOverlap,
  kAmbiguous,
};

struct StructuralGraphReplayEvidence {
  StructuralGraphTilingStatus tiling_status = StructuralGraphTilingStatus::kNone;
  std::string diagnostic_code;
};

StructuralOccurrenceGraph build_structural_occurrence_graph_from_tokens(
    const std::vector<StructuralProjectionToken>& tokens,
    StructuralOccurrenceBuildConfig config = StructuralOccurrenceBuildConfig{});

StructuralOccurrenceGraph build_structural_occurrence_graph_from_grammar(
    const std::vector<StructuralProjectionToken>& tokens,
    const StructuralGrammarEvidence& grammar,
    StructuralOccurrenceBuildConfig config = StructuralOccurrenceBuildConfig{});

StructuralOccurrenceGraph build_structural_occurrence_graph_from_grammar(
    const std::vector<StructuralProjectionToken>& tokens,
    const StructuralGrammarEvidence& grammar,
    const StructuralGraphReplayEvidence& graph,
    StructuralOccurrenceBuildConfig config = StructuralOccurrenceBuildConfig{});

StructuralOccurrenceGraph build_structural_occurrence_graph_from_grammar_state(
    const std::vector<StructuralProjectionToken>& tokens,
    const GlobalGrammarState& state,
    StructuralOccurrenceBuildConfig config = StructuralOccurrenceBuildConfig{});

void validate_structural_occurrence_graph_or_throw(
    const StructuralOccurrenceGraph& graph, std::uint32_t token_count);

}  // namespace traceloom
