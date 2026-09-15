#include "traceloom/config/rule_manifest.h"
#include "traceloom/core/sha256.h"
#include <filesystem>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace traceloom::config {
namespace {
const std::map<std::string, std::vector<std::string>> columns{
  {"classification", {"rule_id", "priority", "provider_scope", "source_domain", "field", "match", "pattern", "role", "required_fields", "structural_participation", "cost_treatment", "context_treatment", "provenance_treatment", "missing_evidence_behavior", "concrete_identity_behavior", "note"}},
  {"symbol_normalization", {"priority", "rule_id", "provider_scope", "source_domain", "field", "match", "pattern", "structural_symbol", "note"}},
  {"event_reconciliation", {"priority", "rule_id", "provider_scope", "source_domain", "task_type", "generic_context_id", "concrete_context_id", "min_contained_fraction", "task_op_type", "communication_op_name_prefix", "identity_policy", "note"}}
};
const std::map<std::string, std::set<std::string>> metadata_keys{
  {"classification", {"manifest_schema", "policy_id", "policy_version", "provider_scopes", "fallback_identity_role", "fallback_cost_treatment", "fallback_context_treatment", "fallback_provenance_treatment", "missing_evidence_behavior"}},
  {"symbol_normalization", {"policy_id", "policy_version"}},
  {"event_reconciliation", {"manifest_schema", "policy_id", "policy_version", "unmatched_behavior"}}
};
std::string cell(yaml_node_t* node) {
  auto value = YamlDocument::scalar(node);
  if (value.find_first_of("\t\r\n") != std::string::npos ||
      (!value.empty() && (std::isspace(static_cast<unsigned char>(value.front())) || std::isspace(static_cast<unsigned char>(value.back())))))
    throw std::invalid_argument("policy values must be single-line without surrounding whitespace");
  return value;
}
std::string join(const std::vector<std::string>& values) {
  std::string out;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i) out += '\t';
    out += values[i];
  }
  return out;
}
}
RuleManifest yaml_rule_manifest(YamlDocument& document, yaml_node_t* metadata,
    yaml_node_t* rules, const std::string& kind, const std::string& path,
    const std::string& source_bytes) {
  RuleManifest out{{}, {}, path, sha256_hex(source_bytes), "yaml"};
  const auto fields = document.fields(metadata, metadata_keys.at(kind));
  for (const auto& key : metadata_keys.at(kind)) {
    auto node = YamlDocument::required(fields, key);
    out.lines.push_back("# " + key + "=" + cell(node));
    out.source_lines.push_back(node->start_mark.line + 1);
  }
  // YAML uses the current identity policy contract only; legacy TSV v1 is still readable.
  if (kind == "event_reconciliation" && cell(fields.at("manifest_schema")) != "traceloom.event-reconciliation-policy/v2")
    throw std::invalid_argument("YAML reconciliation requires policy/v2");
  const auto& header = columns.at(kind);
  out.lines.push_back(join(header));
  out.source_lines.push_back(rules->start_mark.line + 1);
  for (auto node : document.sequence(rules)) {
    auto row = document.fields(node, {header.begin(), header.end()});
    std::vector<std::string> values;
    for (const auto& key : header) {
      auto p = row.find(key);
      // Optional descriptive/identity fields have the same empty meaning as TSV.
      const bool optional = key == "note" || key == "task_type" || key == "task_op_type" ||
          key == "communication_op_name_prefix" || key == "required_fields";
      values.push_back(p == row.end() && optional ? "" : cell(YamlDocument::required(row, key)));
    }
    if (!values.front().empty() && values.front().front() == '#')
      throw std::invalid_argument("policy first field cannot begin with #");
    out.lines.push_back(join(values));
    out.source_lines.push_back(node->start_mark.line + 1);
  }
  return out;
}
RuleManifest load_rule_manifest(const std::string& path, const std::string& kind) {
  const auto bytes = read_config_file(path);
  auto extension = std::filesystem::path(path).extension().string();
  if (extension == ".yaml" || extension == ".yml") {
    YamlDocument document(bytes);
    auto root = document.fields(document.root(), {"schema", "kind", "metadata", "rules"});
    if (YamlDocument::scalar(YamlDocument::required(root, "schema")) != "traceloom-policy-v1" ||
        YamlDocument::scalar(YamlDocument::required(root, "kind")) != kind)
      throw std::invalid_argument("unsupported policy schema or wrong policy kind: " + path);
    return yaml_rule_manifest(document, YamlDocument::required(root, "metadata"),
        YamlDocument::required(root, "rules"), kind, path, bytes);
  }
  RuleManifest out{{}, {}, path, sha256_hex(bytes), "flat_tsv"};
  std::istringstream stream(bytes);
  std::string line;
  while (std::getline(stream, line)) {
    out.lines.push_back(line);
    out.source_lines.push_back(out.lines.size());
  }
  return out;
}
}  // namespace traceloom::config
