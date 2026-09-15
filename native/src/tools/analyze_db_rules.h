#pragma once
#include "traceloom/config/analysis_rules_config.h"
namespace traceloom::tools {
struct AnalysisRuleOptions {
  std::optional<traceloom::config::AnalysisRulesConfig> rules_config;
  std::string rules_config_path;
  traceloom::MacroMatchRules match_rules;
  std::string classification_rules_path;
  std::string extend_classification_rules_path;
  std::vector<std::string> classification_rule_overrides;
  std::string symbol_rules_path;
  std::string extend_symbol_rules_path;
  std::string event_reconciliation_rules_path;
  std::string extend_event_reconciliation_rules_path;
};
void resolve_analysis_rule_options(AnalysisRuleOptions& options, const std::string& executable_path);
void apply_analysis_rule_options(const AnalysisRuleOptions& options, const std::string& executable_path,
    FlatAnchorBuildConfig& config);
}  // namespace traceloom::tools
