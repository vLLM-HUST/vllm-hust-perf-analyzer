#include "analyze_db_rules.h"
#include <stdexcept>
namespace traceloom::tools {
void resolve_analysis_rule_options(AnalysisRuleOptions& options, const std::string& executable_path) {
  if (!options.rules_config_path.empty()) {
    if (!options.match_rules.source_path.empty() ||
        !options.classification_rules_path.empty() || !options.extend_classification_rules_path.empty() ||
        !options.classification_rule_overrides.empty() || !options.symbol_rules_path.empty() ||
        !options.extend_symbol_rules_path.empty() || !options.event_reconciliation_rules_path.empty() ||
        !options.extend_event_reconciliation_rules_path.empty())
      throw std::invalid_argument("--rules-config cannot be mixed with legacy rule flags");
    options.rules_config = traceloom::config::load_analysis_rules_config(
        options.rules_config_path, executable_path);
    options.match_rules = options.rules_config->macro_matching;
  }
}
void apply_analysis_rule_options(const AnalysisRuleOptions& cli, const std::string& executable_path,
    FlatAnchorBuildConfig& anchor_config) {
  if (cli.rules_config) {
    cli.rules_config->apply(anchor_config);
    return;
  }
  anchor_config.classification_rules =
      cli.classification_rules_path.empty()
          ? traceloom::load_default_signal_classification_ruleset(
                executable_path)
          : traceloom::load_signal_classification_ruleset(
                cli.classification_rules_path);
  if (!cli.extend_classification_rules_path.empty()) {
    anchor_config.classification_rules =
        traceloom::extend_signal_classification_ruleset(
            anchor_config.classification_rules,
            traceloom::load_signal_classification_ruleset(
                cli.extend_classification_rules_path));
  }
  for (const std::string& specification :
       cli.classification_rule_overrides) {
    anchor_config.classification_overrides.push_back(
        traceloom::parse_signal_classification_override(specification));
  }
  if (!anchor_config.classification_overrides.empty()) {
    anchor_config.classification_rules =
        traceloom::override_signal_classification_ruleset(
            anchor_config.classification_rules,
            anchor_config.classification_overrides);
    anchor_config.classification_overrides.clear();
  }
  anchor_config.structural_symbol_rules =
      cli.symbol_rules_path.empty()
          ? traceloom::load_default_structural_symbol_ruleset(
                executable_path)
          : traceloom::load_structural_symbol_ruleset(
                cli.symbol_rules_path);
  if (!cli.extend_symbol_rules_path.empty()) {
    anchor_config.structural_symbol_rules =
        traceloom::extend_structural_symbol_ruleset(
            anchor_config.structural_symbol_rules,
            traceloom::load_structural_symbol_ruleset(
                cli.extend_symbol_rules_path));
  }
  anchor_config.event_reconciliation_rules =
      cli.event_reconciliation_rules_path.empty()
          ? traceloom::load_default_event_reconciliation_ruleset(
                executable_path)
          : traceloom::load_event_reconciliation_ruleset(
                cli.event_reconciliation_rules_path);
  if (!cli.extend_event_reconciliation_rules_path.empty()) {
    anchor_config.event_reconciliation_rules =
        traceloom::overlay_event_reconciliation_ruleset(
            anchor_config.event_reconciliation_rules,
            traceloom::load_event_reconciliation_ruleset(
                cli.extend_event_reconciliation_rules_path));
  }
}
}  // namespace traceloom::tools
