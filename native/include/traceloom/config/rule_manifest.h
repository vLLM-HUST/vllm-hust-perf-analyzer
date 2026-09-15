#pragma once
#include "traceloom/config/yaml_document.h"
#include <cstddef>

namespace traceloom::config {
// YAML is lowered to the existing validated policy table in memory, not to a
// temporary file. Source bytes and original YAML line numbers remain authoritative.
struct RuleManifest {
  std::vector<std::string> lines;
  std::vector<std::size_t> source_lines;
  std::string path;
  std::string digest;
  std::string format;
};
RuleManifest load_rule_manifest(const std::string& path, const std::string& kind);
RuleManifest yaml_rule_manifest(YamlDocument& document, yaml_node_t* metadata,
    yaml_node_t* rules, const std::string& kind, const std::string& path,
    const std::string& source_bytes);
}  // namespace traceloom::config
