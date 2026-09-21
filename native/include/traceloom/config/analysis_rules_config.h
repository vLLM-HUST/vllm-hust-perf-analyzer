#pragma once
#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/pattern/macro_match_rules.h"
#include <optional>
#include "traceloom/analysis/marked_structure.h"

namespace traceloom::config {
// A model document resolves on top of built-in (or environment-selected) policies.
// Modes are explicit: extend adds IDs, override replaces existing IDs, replace
// replaces the entire stage. Identity/cost correctness gates remain in code.
struct AnalysisRulesConfig {
  std::optional<SignalClassificationRuleset> classification;
  std::optional<StructuralSymbolNormalizationRuleset> symbols;
  std::optional<EventReconciliationRuleset> reconciliation;
  MacroMatchRules macro_matching;
  MarkedStructureRules structure;
  MarkedStructureRules replay_structure;
  std::string source_path;
  std::string source_yaml;
  void apply(FlatAnchorBuildConfig& config) const;
};
AnalysisRulesConfig load_analysis_rules_config(const std::string& path,
    const std::string& executable_path = "");
}  // namespace traceloom::config
