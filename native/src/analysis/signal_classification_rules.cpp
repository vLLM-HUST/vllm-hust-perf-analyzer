#include "traceloom/analysis/signal_classification_rules.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#ifndef TRACELOOM_SOURCE_DEFAULT_RULESET_PATH
#define TRACELOOM_SOURCE_DEFAULT_RULESET_PATH ""
#endif

#ifndef TRACELOOM_INSTALL_DEFAULT_RULESET_PATH
#define TRACELOOM_INSTALL_DEFAULT_RULESET_PATH ""
#endif

namespace traceloom {
namespace {

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

std::string lower_ascii(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

std::string normalize_task_type(std::string value) {
  for (char& ch : value) {
    if (std::isalnum(static_cast<unsigned char>(ch))) {
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    } else {
      ch = '_';
    }
  }
  while (value.find("__") != std::string::npos) {
    value.replace(value.find("__"), 2, "_");
  }
  return trim(value);
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

std::string rule_key(const SignalClassificationRule& rule) {
  return rule.source_domain + "\n" + std::to_string(static_cast<int>(rule.field)) +
         "\n" + std::to_string(static_cast<int>(rule.match)) + "\n" +
         rule.pattern + "\n" + std::to_string(rule.priority);
}

void validate_rules(const std::vector<SignalClassificationRule>& rules) {
  std::unordered_set<std::string> keys;
  for (const SignalClassificationRule& rule : rules) {
    if (rule.source_domain != "task") {
      throw std::invalid_argument(
          "unsupported signal rule source_domain at line " +
          std::to_string(rule.source_line) + ": " + rule.source_domain);
    }
    if (rule.pattern.empty()) {
      throw std::invalid_argument("empty signal rule pattern at line " +
                                  std::to_string(rule.source_line));
    }
    if (!keys.insert(rule_key(rule)).second) {
      throw std::invalid_argument(
          "conflicting signal rules with equal precedence at line " +
          std::to_string(rule.source_line));
    }
  }
}

SignalMatchField parse_field(const std::string& value, std::size_t line) {
  if (value == "task_type") {
    return SignalMatchField::kTaskType;
  }
  if (value == "blob") {
    return SignalMatchField::kBlob;
  }
  throw std::invalid_argument("unknown signal rule field at line " +
                              std::to_string(line) + ": " + value);
}

SignalMatchKind parse_match(const std::string& value, std::size_t line) {
  if (value == "exact") {
    return SignalMatchKind::kExact;
  }
  if (value == "contains") {
    return SignalMatchKind::kContains;
  }
  throw std::invalid_argument("unknown signal rule match at line " +
                              std::to_string(line) + ": " + value);
}

SignalRole parse_role(const std::string& value, std::size_t line) {
  if (value == "anchor") {
    return SignalRole::kAnchor;
  }
  if (value == "ignore") {
    return SignalRole::kIgnore;
  }
  throw std::invalid_argument("unknown signal rule role at line " +
                              std::to_string(line) + ": " + value);
}

bool matches(const SignalClassificationRule& rule,
             const SignalClassificationInput& input) {
  std::string value;
  std::string pattern = rule.pattern;
  if (rule.field == SignalMatchField::kTaskType) {
    value = normalize_task_type(input.task_type);
    pattern = normalize_task_type(pattern);
  } else {
    value = lower_ascii(input.blob);
    pattern = lower_ascii(pattern);
  }
  if (rule.match == SignalMatchKind::kExact) {
    return value == pattern;
  }
  return value.find(pattern) != std::string::npos;
}

}  // namespace

SignalClassificationRuleset::SignalClassificationRuleset(
    std::vector<SignalClassificationRule> rules)
    : rules_(std::move(rules)) {
  validate_rules(rules_);
  std::stable_sort(
      rules_.begin(), rules_.end(),
      [](const SignalClassificationRule& lhs,
         const SignalClassificationRule& rhs) {
        return lhs.priority > rhs.priority;
      });
}

std::optional<SignalRole> SignalClassificationRuleset::classify(
    const SignalClassificationInput& input) const {
  for (const SignalClassificationRule& rule : rules_) {
    if (rule.source_domain == input.source_domain && matches(rule, input)) {
      return rule.role;
    }
  }
  return std::nullopt;
}

SignalClassificationRuleset load_signal_classification_ruleset(
    const std::string& path) {
  std::ifstream stream(path);
  if (!stream) {
    throw std::invalid_argument("cannot open signal classification ruleset: " +
                                path);
  }
  std::vector<SignalClassificationRule> rules;
  std::string line;
  std::size_t line_number = 0;
  bool saw_header = false;
  while (std::getline(stream, line)) {
    ++line_number;
    if (trim(line).empty() || trim(line).front() == '#') {
      continue;
    }
    const std::vector<std::string> fields = split_tsv(line);
    if (!saw_header) {
      const std::vector<std::string> expected{
          "priority", "source_domain", "field", "match", "pattern", "role",
          "note"};
      if (fields != expected) {
        throw std::invalid_argument(
            "invalid signal ruleset header; expected tab-separated: " +
            std::string("priority, source_domain, field, match, pattern, role, note"));
      }
      saw_header = true;
      continue;
    }
    if (fields.size() != 7) {
      throw std::invalid_argument("signal rule at line " +
                                  std::to_string(line_number) +
                                  " must contain seven tab-separated fields");
    }
    std::size_t consumed = 0;
    const long priority = std::stol(fields[0], &consumed, 10);
    if (consumed != fields[0].size()) {
      throw std::invalid_argument("invalid signal rule priority at line " +
                                  std::to_string(line_number));
    }
    rules.push_back(SignalClassificationRule{
        static_cast<std::int32_t>(priority), fields[1],
        parse_field(fields[2], line_number), parse_match(fields[3], line_number),
        fields[4], parse_role(fields[5], line_number), fields[6], line_number});
  }
  if (!saw_header) {
    throw std::invalid_argument("signal classification ruleset is empty: " + path);
  }
  return SignalClassificationRuleset(std::move(rules));
}

SignalClassificationRuleset load_default_signal_classification_ruleset(
    const std::string& executable_path) {
  const char* override_path = std::getenv("TRACELOOM_CLASSIFICATION_RULES");
  if (override_path != nullptr && *override_path != '\0') {
    return load_signal_classification_ruleset(override_path);
  }
  std::vector<std::string> candidates;
  if (!executable_path.empty()) {
    const std::filesystem::path executable =
        std::filesystem::absolute(executable_path);
    candidates.push_back(
        (executable.parent_path().parent_path() / "share" / "traceloom" /
         "default_signal_classification_rules.tsv")
            .string());
  }
  candidates.push_back(TRACELOOM_SOURCE_DEFAULT_RULESET_PATH);
  candidates.push_back(TRACELOOM_INSTALL_DEFAULT_RULESET_PATH);
  candidates.push_back(
      "/usr/share/traceloom/default_signal_classification_rules.tsv");
  candidates.push_back(
      "/usr/local/share/traceloom/default_signal_classification_rules.tsv");
  for (const std::string& path : candidates) {
    if (path.empty()) {
      continue;
    }
    std::ifstream probe(path);
    if (probe.good()) {
      return load_signal_classification_ruleset(path);
    }
  }
  throw std::invalid_argument(
      "default signal classification ruleset not found; set "
      "TRACELOOM_CLASSIFICATION_RULES or pass --classification-rules");
}

SignalClassificationRuleset extend_signal_classification_ruleset(
    const SignalClassificationRuleset& base,
    const SignalClassificationRuleset& extension) {
  std::vector<SignalClassificationRule> merged = base.rules();
  merged.insert(merged.end(), extension.rules().begin(), extension.rules().end());
  return SignalClassificationRuleset(std::move(merged));
}

}  // namespace traceloom
