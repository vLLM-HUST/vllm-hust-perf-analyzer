#include "traceloom/analysis/host_api_rules.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

#include "traceloom/core/sha256.h"

#ifndef TRACELOOM_SOURCE_DEFAULT_HOST_API_RULESET_PATH
#define TRACELOOM_SOURCE_DEFAULT_HOST_API_RULESET_PATH ""
#endif

#ifndef TRACELOOM_INSTALL_DEFAULT_HOST_API_RULESET_PATH
#define TRACELOOM_INSTALL_DEFAULT_HOST_API_RULESET_PATH ""
#endif

namespace traceloom {
namespace {

const char* kVersionPrefix = "# ruleset_version:";

std::string trim(std::string value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  return value;
}

std::vector<std::string> split_tsv(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (true) {
    const std::size_t tab = line.find('\t', start);
    fields.push_back(trim(line.substr(start, tab - start)));
    if (tab == std::string::npos) {
      return fields;
    }
    start = tab + 1;
  }
}

bool glob_match(std::string_view pattern, std::string_view value) {
  // Deterministic '*' wildcard matching; no locale, regex, or hidden
  // normalization. API names are contract spelling-sensitive.
  std::size_t pattern_index = 0;
  std::size_t value_index = 0;
  std::size_t star = std::string_view::npos;
  std::size_t star_value = 0;
  while (value_index < value.size()) {
    if (pattern_index < pattern.size() &&
        pattern[pattern_index] == value[value_index]) {
      ++pattern_index;
      ++value_index;
    } else if (pattern_index < pattern.size() &&
               pattern[pattern_index] == '*') {
      star = pattern_index++;
      star_value = value_index;
    } else if (star != std::string_view::npos) {
      pattern_index = star + 1;
      value_index = ++star_value;
    } else {
      return false;
    }
  }
  while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
    ++pattern_index;
  }
  return pattern_index == pattern.size();
}

HostApiFamily parse_family(const std::string& value, std::size_t line) {
  if (value == "host_sync") {
    return HostApiFamily::kHostSync;
  }
  if (value == "enqueue") {
    return HostApiFamily::kEnqueue;
  }
  throw std::invalid_argument("unknown host API family at line " +
                              std::to_string(line) + ": " + value);
}

}  // namespace

std::string_view host_api_family_name(HostApiFamily family) {
  switch (family) {
    case HostApiFamily::kHostSync:
      return "host_sync";
    case HostApiFamily::kEnqueue:
      return "enqueue";
  }
  return "host_sync";
}

HostApiRuleset::HostApiRuleset(std::vector<HostApiRule> rules,
                               std::string version,
                               std::string sha256)
    : rules_(std::move(rules)),
      version_(std::move(version)),
      sha256_(std::move(sha256)) {
  if (version_.empty()) {
    throw std::invalid_argument(
        "host API ruleset requires a ruleset_version");
  }
  std::unordered_set<std::string> patterns;
  for (const HostApiRule& rule : rules_) {
    if (rule.api_pattern.empty()) {
      throw std::invalid_argument("empty host API pattern at line " +
                                  std::to_string(rule.source_line));
    }
    if (!patterns.insert(rule.api_pattern).second) {
      throw std::invalid_argument("duplicate host API pattern at line " +
                                  std::to_string(rule.source_line));
    }
  }
}

std::optional<HostApiMatch> HostApiRuleset::classify(
    std::string_view api_name) const {
  const HostApiRule* match = nullptr;
  std::size_t specificity = 0;
  for (const HostApiRule& rule : rules_) {
    if (!glob_match(rule.api_pattern, api_name)) {
      continue;
    }
    const std::size_t current_specificity =
        static_cast<std::size_t>(std::count_if(
            rule.api_pattern.begin(), rule.api_pattern.end(),
            [](char ch) { return ch != '*'; }));
    if (match == nullptr || current_specificity > specificity) {
      match = &rule;
      specificity = current_specificity;
    } else if (current_specificity == specificity &&
               match->family != rule.family) {
      throw std::invalid_argument(
          "host API name matches conflicting equally-specific rules: " +
          std::string(api_name));
    }
  }
  if (match == nullptr) {
    return std::nullopt;
  }
  return HostApiMatch{match->family, match->api_pattern};
}

HostApiRuleset load_idle_evidence_host_api_ruleset(const std::string& path) {
  const std::string file_sha = sha256_file_hex(path);
  std::ifstream input(path);
  if (!input) {
    throw std::invalid_argument("cannot open host API ruleset: " + path);
  }
  std::vector<HostApiRule> rules;
  std::string version;
  std::string line;
  std::size_t line_number = 0;
  bool saw_header = false;
  while (std::getline(input, line)) {
    ++line_number;
    const std::string cleaned = trim(line);
    if (cleaned.empty()) {
      continue;
    }
    if (cleaned.front() == '#') {
      if (cleaned.rfind(kVersionPrefix, 0) == 0) {
        if (saw_header || !version.empty()) {
          throw std::invalid_argument(
              "misplaced or duplicate host API ruleset_version at line " +
              std::to_string(line_number));
        }
        version = trim(cleaned.substr(std::strlen(kVersionPrefix)));
        if (version.empty()) {
          throw std::invalid_argument("empty host API ruleset_version");
        }
      }
      continue;
    }
    const std::vector<std::string> fields = split_tsv(line);
    if (!saw_header) {
      if (fields !=
          std::vector<std::string>{"api_pattern", "family", "note"}) {
        throw std::invalid_argument("invalid host API ruleset header");
      }
      saw_header = true;
      continue;
    }
    if (fields.size() != 3) {
      throw std::invalid_argument("host API rule at line " +
                                  std::to_string(line_number) +
                                  " must contain three fields");
    }
    rules.push_back(HostApiRule{fields[0], parse_family(fields[1], line_number),
                                fields[2], line_number});
  }
  if (!saw_header || version.empty()) {
    throw std::invalid_argument("host API ruleset is incomplete: " + path);
  }
  return HostApiRuleset(std::move(rules), std::move(version), file_sha);
}

HostApiRuleset load_default_idle_evidence_host_api_ruleset(
    const std::string& executable_path) {
  const char* override_path = std::getenv("TRACELOOM_HOST_API_RULES");
  if (override_path != nullptr && *override_path != '\0') {
    return load_idle_evidence_host_api_ruleset(override_path);
  }
  std::vector<std::string> candidates;
  if (!executable_path.empty()) {
    const std::filesystem::path executable =
        std::filesystem::absolute(executable_path);
    candidates.push_back(
        (executable.parent_path().parent_path() / "share" / "traceloom" /
         "idle_evidence_host_api_rules.tsv")
            .string());
  }
  candidates.push_back(TRACELOOM_SOURCE_DEFAULT_HOST_API_RULESET_PATH);
  candidates.push_back(TRACELOOM_INSTALL_DEFAULT_HOST_API_RULESET_PATH);
  candidates.push_back("/usr/share/traceloom/idle_evidence_host_api_rules.tsv");
  candidates.push_back(
      "/usr/local/share/traceloom/idle_evidence_host_api_rules.tsv");
  for (const std::string& path : candidates) {
    if (!path.empty() && std::ifstream(path).good()) {
      return load_idle_evidence_host_api_ruleset(path);
    }
  }
  throw std::invalid_argument(
      "default host API ruleset not found; set TRACELOOM_HOST_API_RULES or "
      "pass --host-api-rules");
}

}  // namespace traceloom
