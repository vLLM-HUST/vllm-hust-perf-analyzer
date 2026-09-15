#pragma once
#include <string>
#include <vector>
namespace traceloom {
struct OrderedMarkerRule { std::string id, before, after; };
struct MacroMatchRules {
  std::vector<OrderedMarkerRule> ordered_markers;
  std::string source_path, source_yaml;
};
MacroMatchRules load_macro_match_rules(const std::string& path);
}  // namespace traceloom
