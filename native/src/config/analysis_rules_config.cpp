#include "traceloom/config/analysis_rules_config.h"
#include "traceloom/config/rule_manifest.h"
#include <set>
#include <stdexcept>

namespace traceloom::config {
namespace {
// Explicit override is intentionally different from an extension's ambiguous
// duplicate ID. It replaces a complete rule, never an implicit field patch.
template<class Rule>
std::vector<Rule> retained_rules(const std::vector<Rule>& base,
    const std::vector<Rule>& supplied, const std::string& mode) {
  std::set<std::string> existing, incoming;
  for (const auto& rule : base) existing.insert(rule.rule_id);
  for (const auto& rule : supplied) {
    incoming.insert(rule.rule_id);
    if (mode == "extend" && existing.count(rule.rule_id))
      throw std::invalid_argument("extend duplicates rule ID; use override: " + rule.rule_id);
    if (mode == "override" && !existing.count(rule.rule_id))
      throw std::invalid_argument("override references unknown rule ID: " + rule.rule_id);
  }
  std::vector<Rule> out;
  for (const auto& rule : base)
    if (mode != "override" || !incoming.count(rule.rule_id)) out.push_back(rule);
  return out;
}
std::string mode_of(const YamlDocument::Fields& fields) {
  auto mode = YamlDocument::scalar(YamlDocument::required(fields, "mode"));
  if (mode != "extend" && mode != "override" && mode != "replace")
    throw std::invalid_argument("policy mode must be extend, override, or replace");
  return mode;
}
}
void AnalysisRulesConfig::apply(FlatAnchorBuildConfig& config) const {
  if (classification) config.classification_rules = *classification;
  if (symbols) config.structural_symbol_rules = *symbols;
  if (reconciliation) config.event_reconciliation_rules = *reconciliation;
}
AnalysisRulesConfig load_analysis_rules_config(const std::string& path,
    const std::string& executable_path) {
  AnalysisRulesConfig out;
  out.source_path = path;
  out.source_yaml = read_config_file(path);
  YamlDocument document(out.source_yaml);
  const auto root = document.fields(document.root(),
      {"schema", "classification", "symbol_normalization", "event_reconciliation", "macro_matching", "structure", "replay_structure"});
  if (YamlDocument::scalar(YamlDocument::required(root, "schema")) != "traceloom-analysis-rules-v1")
    throw std::invalid_argument("unsupported analysis rules schema");
  for (const auto& kind : {"classification", "symbol_normalization", "event_reconciliation"}) {
    auto p = root.find(kind);
    if (p == root.end()) continue;
    auto fields = document.fields(p->second, {"mode", "metadata", "rules"});
    auto mode = mode_of(fields);
    auto manifest = yaml_rule_manifest(document, YamlDocument::required(fields, "metadata"),
        YamlDocument::required(fields, "rules"), kind, path, out.source_yaml);
    if (std::string(kind) == "classification") {
      auto supplied = parse_signal_classification_ruleset(manifest);
      if (mode == "replace") { out.classification = std::move(supplied); continue; }
      auto base = load_default_signal_classification_ruleset(executable_path);
      auto retained = retained_rules(base.rules(), supplied.rules(), mode);
      out.classification = extend_signal_classification_ruleset(
          SignalClassificationRuleset(base.metadata(), std::move(retained)), supplied);
    } else if (std::string(kind) == "symbol_normalization") {
      auto supplied = parse_structural_symbol_ruleset(manifest);
      if (mode == "replace") { out.symbols = std::move(supplied); continue; }
      auto base = load_default_structural_symbol_ruleset(executable_path);
      auto retained = retained_rules(base.rules(), supplied.rules(), mode);
      out.symbols = extend_structural_symbol_ruleset(
          StructuralSymbolNormalizationRuleset(base.policy_id(), base.policy_version(),
              base.source_manifest(), base.manifest_sha256(), std::move(retained)), supplied);
    } else {
      auto supplied = parse_event_reconciliation_ruleset(manifest);
      if (mode == "replace") { out.reconciliation = std::move(supplied); continue; }
      auto base = load_default_event_reconciliation_ruleset(executable_path);
      retained_rules(base.rules(), supplied.rules(), mode); // validate explicit mode before legacy overlay
      out.reconciliation = overlay_event_reconciliation_ruleset(base, supplied);
    }
  }
  if (auto p = root.find("macro_matching"); p != root.end()) {
    auto fields = document.fields(p->second, {"ordered_markers", "suffix_markers", "partition_by"});
    if (auto partition = fields.find("partition_by"); partition != fields.end()) {
      out.macro_matching.partition_by = YamlDocument::scalar(partition->second);
      if (out.macro_matching.partition_by != "scheduler_step")
        throw std::invalid_argument("macro_matching.partition_by must be scheduler_step");
    }
    std::set<std::string> ids;
    if (auto list = fields.find("ordered_markers"); list != fields.end()) {
      for (auto node : document.sequence(list->second)) {
        auto rule = document.fields(node, {"id", "before", "after"});
        OrderedMarkerRule marker{YamlDocument::scalar(YamlDocument::required(rule, "id")),
            YamlDocument::scalar(YamlDocument::required(rule, "before")),
            YamlDocument::scalar(YamlDocument::required(rule, "after"))};
        if (marker.id.empty() || marker.before.empty() || marker.after.empty() ||
            marker.before == marker.after || !ids.insert(marker.id).second)
          throw std::invalid_argument("invalid or duplicate ordered marker rule");
        out.macro_matching.ordered_markers.push_back(std::move(marker));
      }
    }
    if (auto list = fields.find("suffix_markers"); list != fields.end()) {
      for (auto node : document.sequence(list->second)) {
        auto rule = document.fields(node, {"id", "marker"});
        SuffixMarkerRule marker{YamlDocument::scalar(YamlDocument::required(rule, "id")),
            YamlDocument::scalar(YamlDocument::required(rule, "marker"))};
        if (marker.id.empty() || marker.marker.empty() || !ids.insert(marker.id).second)
          throw std::invalid_argument("invalid or duplicate suffix marker rule");
        out.macro_matching.suffix_markers.push_back(std::move(marker));
      }
    }
    out.macro_matching.source_path = path;
    out.macro_matching.source_yaml = out.source_yaml;
  }
  for (const auto& key : {"structure", "replay_structure"}) {
    auto p = root.find(key);
    if (p == root.end()) continue;
    auto& structure = std::string(key) == "structure" ? out.structure : out.replay_structure;
    auto fields = document.fields(p->second, {"unit", "compositions"});
    auto unit = document.fields(YamlDocument::required(fields,"unit"), {"id","begin","end","labels","mode","end_predecessor","end_sequence","label","boundary_label","name_normalization","end_predecessor_any"});
    auto required_string = [](const auto& f,const std::string& key) {
      auto value=YamlDocument::scalar(YamlDocument::required(f,key));
      if(value.empty()) throw std::invalid_argument("empty structure field: "+key);
      return value;
    };
    structure.id=required_string(unit,"id");
    if (unit.count("name_normalization")) {
      structure.name_normalization=required_string(unit,"name_normalization");
      if (structure.name_normalization!="exact" && structure.name_normalization!="ascend_decorated_kernel")
        throw std::invalid_argument("unsupported model name_normalization");
    }
    if (unit.count("end_predecessor_any")) {
      if (unit.count("end_predecessor") || !unit.count("mode") || required_string(unit,"mode")!="end_delimited")
        throw std::invalid_argument("end_predecessor_any requires end_delimited and excludes end_predecessor");
      for (auto item:document.sequence(unit.at("end_predecessor_any"))) {
        auto name=YamlDocument::scalar(item);
        if (name.empty()) throw std::invalid_argument("empty predecessor alternative");
        structure.end_predecessor_any.push_back(name);
      }
      if (structure.end_predecessor_any.empty()) throw std::invalid_argument("empty predecessor alternatives");
    }
    if (auto mode=unit.find("mode"); mode!=unit.end())
      structure.mode=required_string(unit,"mode");
    if (structure.mode!="paired" && structure.mode!="end_delimited" && structure.mode!="cycle_end")
      throw std::invalid_argument("unsupported structure boundary mode");
    if (unit.count("boundary_label")) {
      if (structure.mode!="end_delimited")
        throw std::invalid_argument("boundary_label requires end_delimited mode");
      structure.boundary_label=required_string(unit,"boundary_label");
      if (structure.boundary_label=="ambiguous" || structure.boundary_label=="unclassified")
        throw std::invalid_argument("reserved boundary label");
    }
    if (structure.mode=="cycle_end") {
      if (std::string(key)=="replay_structure" || unit.count("end") ||
          unit.count("labels") || unit.count("end_predecessor") || fields.count("compositions"))
        throw std::invalid_argument("cycle_end requires an outer end sequence and label, with optional begin");
      structure.cycle_label=required_string(unit,"label");
      if (structure.cycle_label=="ambiguous" || structure.cycle_label=="unclassified")
        throw std::invalid_argument("reserved cycle label");
      for (auto symbol:document.sequence(YamlDocument::required(unit,"end_sequence"))) {
        auto name=YamlDocument::scalar(symbol);
        if (name.empty()) throw std::invalid_argument("empty cycle end symbol");
        structure.end_sequence.push_back(name);
      }
      if (structure.end_sequence.empty()) throw std::invalid_argument("empty cycle end sequence");
      if (unit.count("begin")) {
        structure.begin=required_string(unit,"begin");
        for (const auto& symbol:structure.end_sequence)
          if (symbol==structure.begin) throw std::invalid_argument("cycle begin overlaps end sequence");
      }
      continue;
    }
    if (unit.count("end_sequence") || unit.count("label"))
      throw std::invalid_argument("end_sequence and label require cycle_end mode");
    if (unit.count("end_predecessor")) {
      if (structure.mode!="end_delimited")
        throw std::invalid_argument("end_predecessor requires end_delimited mode");
      structure.end_predecessor=required_string(unit,"end_predecessor");
    }
    structure.begin=required_string(unit,"begin");
    structure.end=required_string(unit,"end");
    if(structure.begin==structure.end) throw std::invalid_argument("identical unit markers");
    std::set<std::string> labels;
    if (!structure.boundary_label.empty()) labels.insert(structure.boundary_label);
    for(auto node:document.sequence(YamlDocument::required(unit,"labels"))) {
      auto f=document.fields(node,{"label","contains_any"});
      UnitLabelRule rule;rule.label=required_string(f,"label");
      if(rule.label=="ambiguous" || rule.label=="unclassified" || !labels.insert(rule.label).second)
        throw std::invalid_argument("reserved or duplicate unit label");
      for(auto symbol:document.sequence(YamlDocument::required(f,"contains_any"))) {
        auto name=YamlDocument::scalar(symbol);
        if(name.empty()) throw std::invalid_argument("empty unit classifier symbol");
        rule.contains_any.push_back(name);
      }
      if(rule.contains_any.empty()) throw std::invalid_argument("empty unit classifier");
      structure.labels.push_back(std::move(rule));
    }
    if(structure.labels.empty()) throw std::invalid_argument("unit labels must not be empty");
    std::set<std::string> composition_labels;
    std::set<std::vector<std::string>> signatures;
    if(auto p=fields.find("compositions");p!=fields.end())
    for(auto node:document.sequence(p->second)) {
      auto f=document.fields(node,{"label","sequence"});
      UnitCompositionRule rule;rule.label=required_string(f,"label");
      if(labels.count(rule.label) || rule.label=="ambiguous" || rule.label=="unclassified" ||
         !composition_labels.insert(rule.label).second)
        throw std::invalid_argument("conflicting composition label");
      for(auto member:document.sequence(YamlDocument::required(f,"sequence"))) {
        auto label=YamlDocument::scalar(member);
        if(!labels.count(label)) throw std::invalid_argument("composition references unknown unit label");
        rule.sequence.push_back(label);
      }
      if(rule.sequence.size()<2 || !signatures.insert(rule.sequence).second)
        throw std::invalid_argument("short or duplicate composition sequence");
      structure.compositions.push_back(std::move(rule));
    }
  }
  if (!out.classification) out.classification = load_default_signal_classification_ruleset(executable_path);
  if (!out.symbols) out.symbols = load_default_structural_symbol_ruleset(executable_path);
  if (!out.reconciliation) out.reconciliation = load_default_event_reconciliation_ruleset(executable_path);
  return out;
}
}  // namespace traceloom::config
