#include "traceloom/analysis/idle_evidence_semantic_rules.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "traceloom/core/sha256.h"

#ifndef TRACELOOM_SOURCE_DEFAULT_IDLE_RULESET_PATH
#define TRACELOOM_SOURCE_DEFAULT_IDLE_RULESET_PATH ""
#endif

#ifndef TRACELOOM_INSTALL_DEFAULT_IDLE_RULESET_PATH
#define TRACELOOM_INSTALL_DEFAULT_IDLE_RULESET_PATH ""
#endif

namespace traceloom {
namespace {

const char* kRulesetVersionPrefix = "# ruleset_version:";

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

SemanticTaskRole parse_role(const std::string& value, std::size_t line) {
  if (value == "productive_compute") {
    return SemanticTaskRole::kProductiveCompute;
  }
  if (value == "productive_comm") {
    return SemanticTaskRole::kProductiveComm;
  }
  if (value == "productive_data_move") {
    return SemanticTaskRole::kProductiveDataMove;
  }
  if (value == "visible_wait") {
    return SemanticTaskRole::kVisibleWait;
  }
  if (value == "capture_control") {
    return SemanticTaskRole::kCaptureControl;
  }
  if (value == "record") {
    return SemanticTaskRole::kRecord;
  }
  if (value == "runtime_control") {
    return SemanticTaskRole::kRuntimeControl;
  }
  if (value == "unknown") {
    return SemanticTaskRole::kUnknown;
  }
  throw std::invalid_argument("unknown idle evidence rule role at line " +
                              std::to_string(line) + ": " + value);
}

SignalMatchField parse_field(const std::string& value, std::size_t line) {
  if (value == "task_type") {
    return SignalMatchField::kTaskType;
  }
  if (value == "blob") {
    return SignalMatchField::kBlob;
  }
  throw std::invalid_argument("unknown idle evidence rule field at line " +
                              std::to_string(line) + ": " + value);
}

SignalMatchKind parse_match(const std::string& value, std::size_t line) {
  if (value == "exact") {
    return SignalMatchKind::kExact;
  }
  if (value == "contains") {
    return SignalMatchKind::kContains;
  }
  throw std::invalid_argument("unknown idle evidence rule match at line " +
                              std::to_string(line) + ": " + value);
}

bool matches(const SemanticTaskRule& rule,
             const SemanticTaskClassificationInput& input) {
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

std::string_view semantic_task_role_name(SemanticTaskRole role) {
  switch (role) {
    case SemanticTaskRole::kProductiveCompute:
      return "productive_compute";
    case SemanticTaskRole::kProductiveComm:
      return "productive_comm";
    case SemanticTaskRole::kProductiveDataMove:
      return "productive_data_move";
    case SemanticTaskRole::kVisibleWait:
      return "visible_wait";
    case SemanticTaskRole::kCaptureControl:
      return "capture_control";
    case SemanticTaskRole::kRecord:
      return "record";
    case SemanticTaskRole::kRuntimeControl:
      return "runtime_control";
    case SemanticTaskRole::kUnknown:
      return "unknown";
  }
  return "unknown";
}

SemanticTaskRuleset::SemanticTaskRuleset(std::vector<SemanticTaskRule> rules,
                                         std::string version,
                                         std::string sha256)
    : rules_(std::move(rules)),
      version_(std::move(version)),
      sha256_(std::move(sha256)) {
  if (version_.empty()) {
    throw std::invalid_argument(
        "idle evidence semantic ruleset requires a ruleset_version");
  }
  std::unordered_set<std::string> ids;
  std::unordered_set<std::string> keys;
  for (const SemanticTaskRule& rule : rules_) {
    if (rule.rule_id.empty()) {
      throw std::invalid_argument(
          "idle evidence rule at line " +
          std::to_string(rule.source_line) + " has an empty rule_id");
    }
    if (!ids.insert(rule.rule_id).second) {
      throw std::invalid_argument(
          "duplicate idle evidence rule_id at line " +
          std::to_string(rule.source_line) + ": " + rule.rule_id);
    }
    if (rule.source_domain != "task") {
      throw std::invalid_argument(
          "unsupported idle evidence rule source_domain at line " +
          std::to_string(rule.source_line) + ": " + rule.source_domain);
    }
    if (rule.pattern.empty()) {
      throw std::invalid_argument(
          "empty idle evidence rule pattern at line " +
          std::to_string(rule.source_line));
    }
    // Conflict keys use the normalized pattern so that equivalent
    // task_type patterns (e.g. EVENT-WAIT vs EVENT_WAIT) are recognized as
    // duplicates at load time instead of at classification time.
    std::string key_pattern = rule.pattern;
    if (rule.field == SignalMatchField::kTaskType) {
      key_pattern = normalize_task_type(key_pattern);
    } else {
      key_pattern = lower_ascii(key_pattern);
    }
    const std::string key =
        rule.source_domain + "\n" +
        std::to_string(static_cast<int>(rule.field)) + "\n" +
        std::to_string(static_cast<int>(rule.match)) + "\n" + key_pattern +
        "\n" + std::to_string(rule.priority);
    if (!keys.insert(key).second) {
      throw std::invalid_argument(
          "conflicting idle evidence rules with equal precedence at line " +
          std::to_string(rule.source_line));
    }
  }
  std::stable_sort(
      rules_.begin(), rules_.end(),
      [](const SemanticTaskRule& lhs, const SemanticTaskRule& rhs) {
        return lhs.priority > rhs.priority;
      });
}

SemanticTaskMatch SemanticTaskRuleset::classify(
    const SemanticTaskClassificationInput& input) const {
  SemanticTaskMatch best;
  std::int32_t best_priority = 0;
  bool have_match = false;
  bool conflict = false;
  for (const SemanticTaskRule& rule : rules_) {
    if (rule.source_domain != input.source_domain) {
      continue;
    }
    if (!matches(rule, input)) {
      continue;
    }
    if (!have_match || rule.priority > best_priority) {
      have_match = true;
      best_priority = rule.priority;
      best.role = rule.role;
      best.matched_rule_id = rule.rule_id;
      best.matched_priority = rule.priority;
      conflict = false;
      continue;
    }
    if (rule.priority == best_priority && rule.role != best.role) {
      conflict = true;
    }
  }
  if (!have_match) {
    return SemanticTaskMatch{};
  }
  if (conflict) {
    throw std::invalid_argument(
        "conflicting idle evidence rules at the same priority for input: " +
        input.source_domain + " / " + input.task_type + " / " + input.blob);
  }
  return best;
}

SemanticTaskRuleset load_idle_evidence_semantic_ruleset(
    const std::string& path) {
  const std::string file_sha = sha256_file_hex(path);
  std::ifstream stream(path);
  if (!stream) {
    throw std::invalid_argument(
        "cannot open idle evidence semantic ruleset: " + path);
  }
  std::vector<SemanticTaskRule> rules;
  std::string version;
  std::string line;
  std::size_t line_number = 0;
  bool saw_header = false;
  while (std::getline(stream, line)) {
    ++line_number;
    const std::string trimmed = trim(line);
    if (trimmed.empty()) {
      continue;
    }
    if (trimmed.front() == '#') {
      // The ruleset version MUST appear exactly once, with the exact
      // prefix "# ruleset_version:", before the TSV header, and must be
      // non-empty.
      if (trimmed.rfind(kRulesetVersionPrefix, 0) == 0) {
        const std::string value =
            trim(trimmed.substr(std::strlen(kRulesetVersionPrefix)));
        if (value.empty()) {
          throw std::invalid_argument(
              "empty ruleset_version at line " + std::to_string(line_number));
        }
        if (saw_header) {
          throw std::invalid_argument(
              "ruleset_version must precede the TSV header (line " +
              std::to_string(line_number) + ")");
        }
        if (!version.empty()) {
          throw std::invalid_argument(
              "duplicate ruleset_version at line " +
              std::to_string(line_number));
        }
        version = value;
      }
      continue;
    }
    const std::vector<std::string> fields = split_tsv(line);
    if (!saw_header) {
      const std::vector<std::string> expected{
          "rule_id", "priority", "source_domain", "field",
          "match",   "pattern",  "role",           "note"};
      if (fields != expected) {
        throw std::invalid_argument(
            "invalid idle evidence ruleset header; expected tab-separated: "
            "rule_id, priority, source_domain, field, match, pattern, role, "
            "note");
      }
      saw_header = true;
      continue;
    }
    if (fields.size() != 8) {
      throw std::invalid_argument(
          "idle evidence rule at line " + std::to_string(line_number) +
          " must contain eight tab-separated fields");
    }
    std::size_t consumed = 0;
    long priority = 0;
    try {
      priority = std::stol(fields[1], &consumed, 10);
    } catch (const std::out_of_range&) {
      throw std::invalid_argument(
          "invalid idle evidence rule priority at line " +
          std::to_string(line_number));
    }
    if (consumed != fields[1].size() ||
        priority < std::numeric_limits<std::int32_t>::min() ||
        priority > std::numeric_limits<std::int32_t>::max()) {
      throw std::invalid_argument(
          "invalid idle evidence rule priority at line " +
          std::to_string(line_number));
    }
    SemanticTaskRule rule;
    rule.rule_id = fields[0];
    rule.priority = static_cast<std::int32_t>(priority);
    rule.source_domain = fields[2];
    rule.field = parse_field(fields[3], line_number);
    rule.match = parse_match(fields[4], line_number);
    rule.pattern = fields[5];
    rule.role = parse_role(fields[6], line_number);
    rule.note = fields[7];
    rule.source_line = line_number;
    rules.push_back(std::move(rule));
  }
  if (!saw_header) {
    throw std::invalid_argument(
        "idle evidence semantic ruleset is empty: " + path);
  }
  return SemanticTaskRuleset(std::move(rules), std::move(version), file_sha);
}

SemanticTaskRuleset load_default_idle_evidence_semantic_ruleset(
    const std::string& executable_path) {
  const char* override_path = std::getenv("TRACELOOM_IDLE_EVIDENCE_RULES");
  if (override_path != nullptr && *override_path != '\0') {
    return load_idle_evidence_semantic_ruleset(override_path);
  }
  std::vector<std::string> candidates;
  if (!executable_path.empty()) {
    const std::filesystem::path executable =
        std::filesystem::absolute(executable_path);
    candidates.push_back(
        (executable.parent_path().parent_path() / "share" / "traceloom" /
         "idle_evidence_semantic_rules.tsv")
            .string());
  }
  candidates.push_back(TRACELOOM_SOURCE_DEFAULT_IDLE_RULESET_PATH);
  candidates.push_back(TRACELOOM_INSTALL_DEFAULT_IDLE_RULESET_PATH);
  candidates.push_back(
      "/usr/share/traceloom/idle_evidence_semantic_rules.tsv");
  candidates.push_back(
      "/usr/local/share/traceloom/idle_evidence_semantic_rules.tsv");
  for (const std::string& path : candidates) {
    if (path.empty()) {
      continue;
    }
    std::ifstream probe(path);
    if (probe.good()) {
      return load_idle_evidence_semantic_ruleset(path);
    }
  }
  throw std::invalid_argument(
      "default idle evidence semantic ruleset not found; set "
      "TRACELOOM_IDLE_EVIDENCE_RULES or pass --idle-evidence-rules");
}

}  // namespace traceloom
