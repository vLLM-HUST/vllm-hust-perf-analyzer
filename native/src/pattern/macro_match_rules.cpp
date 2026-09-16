#include "traceloom/pattern/macro_match_rules.h"
#include <yaml.h>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace traceloom {
namespace {
std::string scalar(yaml_node_t* n) {
  if (!n || n->type != YAML_SCALAR_NODE)
    throw std::invalid_argument("match rules: expected a scalar");
  return std::string(reinterpret_cast<char*>(n->data.scalar.value), n->data.scalar.length);
}
using Fields = std::map<std::string, yaml_node_t*>;
Fields fields(yaml_document_t& doc, yaml_node_t* n, const std::set<std::string>& allowed) {
  if (!n || n->type != YAML_MAPPING_NODE)
    throw std::invalid_argument("match rules: expected a mapping");
  Fields out;
  for (auto p=n->data.mapping.pairs.start; p!=n->data.mapping.pairs.top; ++p) {
    const auto key=scalar(yaml_document_get_node(&doc,p->key));
    if (!allowed.count(key) || !out.emplace(key,yaml_document_get_node(&doc,p->value)).second)
      throw std::invalid_argument("match rules: unknown or duplicate key: "+key);
  }
  return out;
}
std::string required(const Fields& f, const std::string& key) {
  auto p=f.find(key);
  if (p==f.end()) throw std::invalid_argument("match rules: missing "+key);
  auto s=scalar(p->second);
  if (s.empty()) throw std::invalid_argument("match rules: empty "+key);
  return s;
}
}
MacroMatchRules load_macro_match_rules(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::invalid_argument("cannot open match rules: "+path);
  MacroMatchRules rules;
  rules.source_path=path;
  char buffer[4096];
  while (in.read(buffer,sizeof buffer) || in.gcount()) {
    rules.source_yaml.append(buffer,static_cast<std::size_t>(in.gcount()));
    if (rules.source_yaml.size()>1024*1024)
      throw std::invalid_argument("match rules YAML exceeds 1 MiB");
  }
  yaml_parser_t parser;
  if (!yaml_parser_initialize(&parser)) throw std::runtime_error("cannot initialize YAML parser");
  yaml_parser_set_input_string(&parser,
      reinterpret_cast<const unsigned char*>(rules.source_yaml.data()), rules.source_yaml.size());
  yaml_document_t doc;
  bool loaded=false;
  try {
    if (!yaml_parser_load(&parser,&doc))
      throw std::invalid_argument("match rules YAML: "+std::string(parser.problem ? parser.problem : "parse error"));
    loaded=true;
    auto root=fields(doc,yaml_document_get_root_node(&doc),{"schema","ordered_markers","suffix_markers","partition_by"});
    if (required(root,"schema")!="traceloom-match-rules-v1")
      throw std::invalid_argument("unsupported match rules schema");
    if (root.count("partition_by")) {
      rules.partition_by=required(root,"partition_by");
      if (rules.partition_by!="scheduler_step")
        throw std::invalid_argument("match rules: partition_by must be scheduler_step");
    }
    auto list=root.find("ordered_markers");
    if (list!=root.end() && list->second->type!=YAML_SEQUENCE_NODE)
      throw std::invalid_argument("match rules: ordered_markers must be a sequence");
    std::set<std::string> ids;
    if (list!=root.end())
    for (auto i=list->second->data.sequence.items.start; i!=list->second->data.sequence.items.top; ++i) {
      auto f=fields(doc,yaml_document_get_node(&doc,*i),{"id","before","after"});
      OrderedMarkerRule rule{required(f,"id"),required(f,"before"),required(f,"after")};
      if (rule.before==rule.after || !ids.insert(rule.id).second)
        throw std::invalid_argument("match rules: identical markers or duplicate rule id");
      rules.ordered_markers.push_back(std::move(rule));
    }
    auto suffix=root.find("suffix_markers");
    if (suffix!=root.end()) {
      if (suffix->second->type!=YAML_SEQUENCE_NODE)
        throw std::invalid_argument("match rules: suffix_markers must be a sequence");
      for (auto i=suffix->second->data.sequence.items.start; i!=suffix->second->data.sequence.items.top; ++i) {
        auto f=fields(doc,yaml_document_get_node(&doc,*i),{"id","marker"});
        SuffixMarkerRule rule{required(f,"id"),required(f,"marker")};
        if (!ids.insert(rule.id).second)
          throw std::invalid_argument("match rules: duplicate rule id");
        rules.suffix_markers.push_back(std::move(rule));
      }
    }
    yaml_document_delete(&doc); loaded=false;
    if (!yaml_parser_load(&parser,&doc))
      throw std::invalid_argument("match rules YAML: invalid trailing document");
    loaded=true;
    if (yaml_document_get_root_node(&doc))
      throw std::invalid_argument("match rules: only one YAML document is allowed");
    yaml_document_delete(&doc); loaded=false;
    yaml_parser_delete(&parser);
  } catch (...) {
    if (loaded) yaml_document_delete(&doc);
    yaml_parser_delete(&parser);
    throw;
  }
  return rules;
}
}  // namespace traceloom
