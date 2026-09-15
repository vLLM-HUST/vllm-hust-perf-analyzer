#include "traceloom/config/analysis_rules_config.h"
#include "traceloom/config/rule_manifest.h"
#include "traceloom/core/sha256.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/testing/test_util.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <functional>
#include <chrono>

using namespace traceloom;
using namespace traceloom::config;
using traceloom::testing::require;
namespace {
std::filesystem::path temporary;
std::string write(const std::string& name, const std::string& text) {
  auto path = temporary / name;
  std::ofstream out(path); out << text; out.close();
  return path.string();
}
void rejected(const std::function<void()>& f) {
  bool failed = false;
  try { f(); } catch (const std::exception&) { failed = true; }
  require(failed);
}
const std::string schema = "schema: traceloom-analysis-rules-v1\n";
std::string stage(const std::string& source, const std::string& name,
    const std::string& mode, bool empty = false) {
  auto text = read_config_file(source);
  text = text.substr(text.find("metadata:"));
  if (empty) text = text.substr(0, text.find("rules:")) + "rules: []\n";
  std::istringstream in(text);
  std::string out = name + ":\n  mode: " + mode + "\n", line;
  while (std::getline(in, line)) out += "  " + line + "\n";
  return out;
}
const std::string symbol_extension = R"(symbol_normalization:
  mode: extend
  metadata: {policy_id: test.symbols, policy_version: "1"}
  rules:
    - rule_id: test.alias
      priority: 200
      provider_scope: ascend
      source_domain: task
      field: selected
      match: exact
      pattern: "Custom: kernel # text"
      structural_symbol: Custom
)";
}
int main() {
  temporary = std::filesystem::temp_directory_path() / ("traceloom-config-tests-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(temporary);
  const auto c = load_default_signal_classification_ruleset();
  const auto s = load_default_structural_symbol_ruleset();
  const auto r = load_default_event_reconciliation_ruleset();
  require(c.metadata().manifest_format == "yaml");
  require(c.metadata().manifest_sha256 == sha256_file_hex(c.metadata().manifest_source_path));
  require(s.rules().front().source_line > 5);
  require(r.rules().front().rule_origin_sha256 == sha256_file_hex(r.source_manifest()));

  // The shared YAML frontend preserves typed legacy policy semantics.
  auto table = load_rule_manifest(s.source_manifest(), "symbol_normalization");
  std::string tsv;
  for (const auto& line : table.lines) tsv += line + "\n";
  auto legacy = load_structural_symbol_ruleset(write("legacy.tsv", tsv));
  require(legacy.rules().size() == s.rules().size());
  for (std::size_t i = 0; i < s.rules().size(); ++i) {
    require(legacy.rules()[i].rule_id == s.rules()[i].rule_id);
    require(legacy.rules()[i].match == s.rules()[i].match);
    require(legacy.rules()[i].pattern == s.rules()[i].pattern);
  }
  auto config = load_analysis_rules_config(write("empty.yaml", schema));
  require(config.classification->metadata().manifest_sha256 == c.metadata().manifest_sha256);
  require(config.symbols->manifest_sha256() == s.manifest_sha256());
  require(config.reconciliation->manifest_sha256() == r.manifest_sha256());
  require(config.macro_matching.ordered_markers.empty());

  for (const auto& mode : {"replace", "override"}) {
    auto yaml = schema + stage(c.metadata().manifest_source_path, "classification", mode) +
        stage(s.source_manifest(), "symbol_normalization", mode) +
        stage(r.source_manifest(), "event_reconciliation", mode);
    auto resolved = load_analysis_rules_config(write("all.yaml", yaml));
    require(resolved.classification->rules().size() == c.rules().size());
    require(resolved.symbols->rules().size() == s.rules().size());
    require(resolved.reconciliation->rules().size() == r.rules().size());
  }
  for (const auto& item : {std::make_pair(c.metadata().manifest_source_path, "classification"),
      std::make_pair(s.source_manifest(), "symbol_normalization"),
      std::make_pair(r.source_manifest(), "event_reconciliation")}) {
    rejected([&] { load_analysis_rules_config(write("duplicate.yaml", schema + stage(item.first, item.second, "extend"))); });
  }
  auto extended = load_analysis_rules_config(write("extension.yml", schema + symbol_extension));
  require(extended.symbols->rules().size() == s.rules().size() + 1);
  require(extended.symbols->rules().front().pattern == "Custom: kernel # text");
  require(extended.symbols->rules().front().source_line == 6);
  auto changed = symbol_extension;
  changed.replace(changed.find("extend"), 6, "override");
  rejected([&] { load_analysis_rules_config(write("unknown-override.yaml", schema + changed)); });
  changed.replace(changed.find("test.alias"), 10, "ascend.task.matmul-backend-variant");
  auto overridden = load_analysis_rules_config(write("override.yaml", schema + changed));
  require(overridden.symbols->rules().size() == s.rules().size());
  require(overridden.symbols->rules().front().structural_symbol == "Custom");

  auto empty = load_analysis_rules_config(write("replace-empty.yaml", schema +
      stage(c.metadata().manifest_source_path, "classification", "replace", true) +
      stage(s.source_manifest(), "symbol_normalization", "replace", true) +
      stage(r.source_manifest(), "event_reconciliation", "replace", true)));
  FlatAnchorBuildConfig anchors; empty.apply(anchors);
  NativeIr ir;
  auto stats = build_flat_anchors(ir, anchors);
  require(stats.classification_manifest_sha256 == empty.classification->metadata().manifest_sha256);
  require(ir.structural_symbol_policy.rules.empty());
  require(ir.event_reconciliation.policy.rules.empty());

  const auto macros = schema + "macro_matching:\n  ordered_markers:\n    - {id: hc, before: HcPre, after: HcPost}\n";
  auto hinted = load_analysis_rules_config(write("hints.yaml", macros));
  require(hinted.macro_matching.ordered_markers.size() == 1);
  require(hinted.macro_matching.source_yaml == macros);
  auto suffix = load_analysis_rules_config(write("suffix.yaml", schema +
      "macro_matching: {suffix_markers: [{id: end, marker: S}]}\n"));
  require(suffix.macro_matching.suffix_markers.size() == 1);
  require(suffix.macro_matching.suffix_markers[0].marker == "S");
  for (auto bad : {"suffix_markers: {}", "suffix_markers: [{id: end}]",
                  "suffix_markers: [{id: end, marker: S}, {id: end, marker: T}]",
                  "ordered_markers: [{id: x, before: P, after: Q}], suffix_markers: [{id: x, marker: S}]"})
    rejected([&] { load_analysis_rules_config(write("suffix-invalid.yaml",
        schema + "macro_matching: {" + bad + "}\n")); });
  for (auto invalid : {schema + "unknown: {}\n", schema + "macro_matching: []\n",
      schema + "macro_matching: {ordered_markers: [{id: x, before: P, after: P}]}\n",
      schema + "schema: traceloom-analysis-rules-v1\n", schema + "---\n{}\n",
      schema + "classification: {mode: extend}\n", schema + "symbol_normalization: {mode: silently-merge}\n",
      schema + "macro_matching: &x {ordered_markers: [*x]}\n"}) {
    rejected([&] { load_analysis_rules_config(write("invalid.yaml", invalid)); });
  }
  rejected([&] { load_structural_symbol_ruleset(c.metadata().manifest_source_path); });
  rejected([&] { YamlDocument huge(std::string(1024 * 1024 + 1, 'x')); });
  // Temp directory contains only these tests' explicitly-created files.
  for (const auto& file : std::filesystem::directory_iterator(temporary)) std::filesystem::remove(file.path());
  std::filesystem::remove(temporary);
}
