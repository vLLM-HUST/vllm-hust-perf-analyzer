#include "traceloom/config/yaml_document.h"
#include <fstream>
#include <stdexcept>

namespace traceloom::config {
std::string read_config_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::invalid_argument("cannot open configuration: " + path);
  std::string text;
  char buffer[4096];
  while (in.read(buffer, sizeof buffer) || in.gcount()) {
    text.append(buffer, static_cast<std::size_t>(in.gcount()));
    if (text.size() > 1024 * 1024)
      throw std::invalid_argument("configuration exceeds 1 MiB: " + path);
  }
  if (in.bad()) throw std::invalid_argument("cannot read configuration: " + path);
  return text;
}
YamlDocument::YamlDocument(const std::string& text) {
  if (text.size() > 1024 * 1024)
    throw std::invalid_argument("configuration exceeds 1 MiB");
  yaml_parser_t parser;
  if (!yaml_parser_initialize(&parser)) throw std::runtime_error("cannot initialize YAML parser");
  yaml_parser_set_input_string(&parser,
      reinterpret_cast<const unsigned char*>(text.data()), text.size());
  if (!yaml_parser_load(&parser, &document_)) {
    const std::string error = parser.problem ? parser.problem : "parse error";
    yaml_parser_delete(&parser);
    throw std::invalid_argument("configuration YAML: " + error);
  }
  yaml_document_t trailing;
  const bool loaded = yaml_parser_load(&parser, &trailing);
  const bool extra = loaded && yaml_document_get_root_node(&trailing);
  if (loaded) yaml_document_delete(&trailing);
  yaml_parser_delete(&parser);
  if (!loaded || extra) {
    yaml_document_delete(&document_);
    throw std::invalid_argument("configuration must contain one valid YAML document");
  }
}
YamlDocument::~YamlDocument() { yaml_document_delete(&document_); }
yaml_node_t* YamlDocument::root() { return yaml_document_get_root_node(&document_); }
std::string YamlDocument::scalar(yaml_node_t* node) {
  if (!node || node->type != YAML_SCALAR_NODE)
    throw std::invalid_argument("configuration: expected a scalar");
  const std::string tag(reinterpret_cast<char*>(node->tag));
  if (tag != YAML_STR_TAG && tag != YAML_INT_TAG && tag != YAML_FLOAT_TAG && tag != YAML_BOOL_TAG)
    throw std::invalid_argument("configuration: unsupported scalar tag");
  std::string value(reinterpret_cast<char*>(node->data.scalar.value), node->data.scalar.length);
  if (value.find('\0') != std::string::npos)
    throw std::invalid_argument("configuration: NUL is not allowed");
  return value;
}
YamlDocument::Fields YamlDocument::fields(yaml_node_t* node, const std::set<std::string>& allowed) {
  if (!node || node->type != YAML_MAPPING_NODE || std::string(reinterpret_cast<char*>(node->tag)) != YAML_MAP_TAG)
    throw std::invalid_argument("configuration: expected a mapping");
  Fields out;
  for (auto p = node->data.mapping.pairs.start; p != node->data.mapping.pairs.top; ++p) {
    auto key = scalar(yaml_document_get_node(&document_, p->key));
    if (!allowed.count(key) || !out.emplace(key, yaml_document_get_node(&document_, p->value)).second)
      throw std::invalid_argument("configuration: unknown or duplicate key: " + key);
  }
  return out;
}
std::vector<yaml_node_t*> YamlDocument::sequence(yaml_node_t* node) {
  if (!node || node->type != YAML_SEQUENCE_NODE || std::string(reinterpret_cast<char*>(node->tag)) != YAML_SEQ_TAG)
    throw std::invalid_argument("configuration: expected a sequence");
  std::vector<yaml_node_t*> out;
  for (auto p = node->data.sequence.items.start; p != node->data.sequence.items.top; ++p)
    out.push_back(yaml_document_get_node(&document_, *p));
  return out;
}
yaml_node_t* YamlDocument::required(const Fields& fields, const std::string& key) {
  const auto p = fields.find(key);
  if (p == fields.end()) throw std::invalid_argument("configuration: missing " + key);
  return p->second;
}
}  // namespace traceloom::config
