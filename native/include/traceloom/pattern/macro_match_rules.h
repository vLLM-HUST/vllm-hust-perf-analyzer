#pragma once
#include <string>
#include <vector>
namespace traceloom {
struct OrderedMarkerRule { std::string id, before, after; };
struct SuffixMarkerRule { std::string id, marker; };
struct MacroMatchRules {
  std::vector<OrderedMarkerRule> ordered_markers;
  std::string source_path, source_yaml;
  std::vector<SuffixMarkerRule> suffix_markers;
};
MacroMatchRules load_macro_match_rules(const std::string& path);
}  // namespace traceloom
