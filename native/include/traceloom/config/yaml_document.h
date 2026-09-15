#pragma once
#include <yaml.h>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace traceloom::config {
// A bounded, single-document YAML reader. It interprets no custom tags/includes.
std::string read_config_file(const std::string& path);
class YamlDocument {
 public:
  explicit YamlDocument(const std::string& text);
  ~YamlDocument();
  YamlDocument(const YamlDocument&) = delete;
  YamlDocument& operator=(const YamlDocument&) = delete;
  using Fields = std::map<std::string, yaml_node_t*>;
  yaml_node_t* root();
  Fields fields(yaml_node_t* node, const std::set<std::string>& allowed);
  std::vector<yaml_node_t*> sequence(yaml_node_t* node);
  static std::string scalar(yaml_node_t* node);
  static yaml_node_t* required(const Fields& fields, const std::string& key);
 private:
  yaml_document_t document_{};
};
}  // namespace traceloom::config
