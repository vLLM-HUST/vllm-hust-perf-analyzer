#include "traceloom/analysis/structural_symbol_normalization.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "traceloom/core/sha256.h"
#include "traceloom/ir/native_ir.h"

#ifndef TRACELOOM_SOURCE_DEFAULT_SYMBOL_RULESET_PATH
#define TRACELOOM_SOURCE_DEFAULT_SYMBOL_RULESET_PATH ""
#endif

#ifndef TRACELOOM_INSTALL_DEFAULT_SYMBOL_RULESET_PATH
#define TRACELOOM_INSTALL_DEFAULT_SYMBOL_RULESET_PATH ""
#endif

namespace traceloom {
namespace {

struct ObservedSymbol {
  SymbolId symbol_id;
  StructuralSymbolSource source = StructuralSymbolSource::kUnknown;
};

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

std::vector<std::string> split(const std::string& value, char delimiter) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (true) {
    const std::size_t end = value.find(delimiter, start);
    fields.push_back(trim(value.substr(start, end - start)));
    if (end == std::string::npos) {
      return fields;
    }
    start = end + 1;
  }
}

std::vector<std::string> split_tsv(const std::string& line) {
  return split(line, '\t');
}

std::string symbol_text(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

bool is_opaque_numeric_instance(const std::string& value) {
  if (value.empty() ||
      !std::isdigit(static_cast<unsigned char>(value.front()))) {
    return false;
  }
  bool saw_underscore = false;
  for (char ch : value) {
    if (ch == '_') {
      saw_underscore = true;
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  return saw_underscore;
}

StructuralSymbolField parse_field(const std::string& value,
                                  std::size_t line) {
  if (value == "selected") return StructuralSymbolField::kSelected;
  if (value == "any_identity") return StructuralSymbolField::kAnyIdentity;
  if (value == "op_name") return StructuralSymbolField::kOpName;
  if (value == "op_type") return StructuralSymbolField::kOpType;
  if (value == "task_type") return StructuralSymbolField::kTaskType;
  if (value == "compute_task_type")
    return StructuralSymbolField::kComputeTaskType;
  if (value == "comm_name") return StructuralSymbolField::kCommName;
  if (value == "linked_task_name")
    return StructuralSymbolField::kLinkedTaskName;
  if (value == "linked_task_type")
    return StructuralSymbolField::kLinkedTaskType;
  throw std::invalid_argument("unknown structural-symbol field at line " +
                              std::to_string(line) + ": " + value);
}

StructuralSymbolMatch parse_match(const std::string& value,
                                  std::size_t line) {
  if (value == "exact") return StructuralSymbolMatch::kExact;
  if (value == "exact_any") return StructuralSymbolMatch::kExactAny;
  if (value == "contains_ci")
    return StructuralSymbolMatch::kContainsCaseInsensitive;
  if (value == "contains_any_ci")
    return StructuralSymbolMatch::kContainsAnyCaseInsensitive;
  if (value == "opaque_numeric_instance")
    return StructuralSymbolMatch::kOpaqueNumericInstance;
  throw std::invalid_argument("unknown structural-symbol match at line " +
                              std::to_string(line) + ": " + value);
}

std::string rule_key(const StructuralSymbolNormalizationRule& rule) {
  return lower_ascii(rule.provider_scope) + "\n" +
         lower_ascii(rule.source_domain) + "\n" +
         std::to_string(static_cast<int>(rule.field)) + "\n" +
         std::to_string(static_cast<int>(rule.match)) + "\n" +
         lower_ascii(rule.pattern) + "\n" + std::to_string(rule.priority);
}

void validate_rules(const std::vector<StructuralSymbolNormalizationRule>& rules) {
  std::unordered_set<std::string> ids;
  std::unordered_set<std::string> keys;
  for (const auto& rule : rules) {
    if (rule.rule_id.empty() || rule.provider_scope.empty() ||
        rule.source_domain.empty() || rule.pattern.empty() ||
        rule.structural_symbol.empty()) {
      throw std::invalid_argument(
          "structural-symbol rule has an empty required field at line " +
          std::to_string(rule.source_line));
    }
    if (rule.source_domain != "task" && rule.source_domain != "communication") {
      throw std::invalid_argument(
          "unsupported structural-symbol source_domain at line " +
          std::to_string(rule.source_line) + ": " + rule.source_domain);
    }
    if (!ids.insert(rule.rule_id).second) {
      throw std::invalid_argument("duplicate structural-symbol rule_id: " +
                                  rule.rule_id);
    }
    if (!keys.insert(rule_key(rule)).second) {
      throw std::invalid_argument(
          "conflicting structural-symbol rules with equal precedence at line " +
          std::to_string(rule.source_line));
    }
  }
}

ObservedSymbol selected_task_symbol(const TaskRow& task) {
  if (task.op_type_symbol_id.valid())
    return {task.op_type_symbol_id, StructuralSymbolSource::kTaskOpType};
  if (task.op_name_symbol_id.valid())
    return {task.op_name_symbol_id, StructuralSymbolSource::kTaskOpName};
  if (task.comm_name_symbol_id.valid())
    return {task.comm_name_symbol_id, StructuralSymbolSource::kTaskCommName};
  if (task.task_type_symbol_id.valid())
    return {task.task_type_symbol_id, StructuralSymbolSource::kTaskType};
  return {};
}

ObservedSymbol communication_identity_symbol(
    const CommunicationOpRow& communication) {
  if (communication.op_type_symbol_id.valid())
    return {communication.op_type_symbol_id,
            StructuralSymbolSource::kCommunicationOpType};
  if (communication.linked_task_type_symbol_id.valid())
    return {communication.linked_task_type_symbol_id,
            StructuralSymbolSource::kCommunicationLinkedTaskType};
  if (communication.linked_task_name_symbol_id.valid())
    return {communication.linked_task_name_symbol_id,
            StructuralSymbolSource::kCommunicationLinkedTaskName};
  if (communication.op_name_symbol_id.valid())
    return {communication.op_name_symbol_id,
            StructuralSymbolSource::kCommunicationOpName};
  return {};
}

std::vector<ObservedSymbol> task_fields(const TaskRow& task,
                                        StructuralSymbolField field) {
  switch (field) {
    case StructuralSymbolField::kSelected:
      return {selected_task_symbol(task)};
    case StructuralSymbolField::kAnyIdentity:
      return {{task.op_name_symbol_id, StructuralSymbolSource::kTaskOpName},
              {task.op_type_symbol_id, StructuralSymbolSource::kTaskOpType},
              {task.compute_task_type_symbol_id,
               StructuralSymbolSource::kTaskComputeType},
              {task.comm_name_symbol_id,
               StructuralSymbolSource::kTaskCommName},
              {task.task_type_symbol_id,
               StructuralSymbolSource::kTaskType}};
    case StructuralSymbolField::kOpName:
      return {{task.op_name_symbol_id, StructuralSymbolSource::kTaskOpName}};
    case StructuralSymbolField::kOpType:
      return {{task.op_type_symbol_id, StructuralSymbolSource::kTaskOpType}};
    case StructuralSymbolField::kTaskType:
      return {{task.task_type_symbol_id, StructuralSymbolSource::kTaskType}};
    case StructuralSymbolField::kComputeTaskType:
      return {{task.compute_task_type_symbol_id,
               StructuralSymbolSource::kTaskComputeType}};
    case StructuralSymbolField::kCommName:
      return {{task.comm_name_symbol_id,
               StructuralSymbolSource::kTaskCommName}};
    case StructuralSymbolField::kLinkedTaskName:
    case StructuralSymbolField::kLinkedTaskType:
      return {};
  }
  return {};
}

std::vector<ObservedSymbol> communication_fields(
    const CommunicationOpRow& communication,
    StructuralSymbolField field) {
  switch (field) {
    case StructuralSymbolField::kSelected:
      return {communication_identity_symbol(communication)};
    case StructuralSymbolField::kAnyIdentity:
      return {{communication.op_name_symbol_id,
               StructuralSymbolSource::kCommunicationOpName},
              {communication.op_type_symbol_id,
               StructuralSymbolSource::kCommunicationOpType},
              {communication.linked_task_name_symbol_id,
               StructuralSymbolSource::kCommunicationLinkedTaskName},
              {communication.linked_task_type_symbol_id,
               StructuralSymbolSource::kCommunicationLinkedTaskType}};
    case StructuralSymbolField::kOpName:
      return {{communication.op_name_symbol_id,
               StructuralSymbolSource::kCommunicationOpName}};
    case StructuralSymbolField::kOpType:
      return {{communication.op_type_symbol_id,
               StructuralSymbolSource::kCommunicationOpType}};
    case StructuralSymbolField::kLinkedTaskName:
      return {{communication.linked_task_name_symbol_id,
               StructuralSymbolSource::kCommunicationLinkedTaskName}};
    case StructuralSymbolField::kLinkedTaskType:
      return {{communication.linked_task_type_symbol_id,
               StructuralSymbolSource::kCommunicationLinkedTaskType}};
    case StructuralSymbolField::kTaskType:
    case StructuralSymbolField::kComputeTaskType:
    case StructuralSymbolField::kCommName:
      return {};
  }
  return {};
}

bool rule_matches(const StructuralSymbolNormalizationRule& rule,
                  const std::string& value) {
  const std::vector<std::string> patterns = split(rule.pattern, '|');
  switch (rule.match) {
    case StructuralSymbolMatch::kExact:
      return value == rule.pattern;
    case StructuralSymbolMatch::kExactAny:
      return std::find(patterns.begin(), patterns.end(), value) !=
             patterns.end();
    case StructuralSymbolMatch::kContainsCaseInsensitive:
      return lower_ascii(value).find(lower_ascii(rule.pattern)) !=
             std::string::npos;
    case StructuralSymbolMatch::kContainsAnyCaseInsensitive: {
      const std::string lower_value = lower_ascii(value);
      return std::any_of(patterns.begin(), patterns.end(),
                         [&lower_value](const std::string& pattern) {
                           return lower_value.find(lower_ascii(pattern)) !=
                                  std::string::npos;
                         });
    }
    case StructuralSymbolMatch::kOpaqueNumericInstance:
      return is_opaque_numeric_instance(value);
  }
  return false;
}

ResolvedStructuralSymbol resolve(
    NativeIr& ir,
    const std::string& provider,
    const std::string& source_domain,
    const std::vector<std::pair<const StructuralSymbolNormalizationRule*,
                                std::vector<ObservedSymbol>>>& candidates,
    ObservedSymbol fallback) {
  std::int32_t matched_priority = 0;
  bool saw_match = false;
  std::vector<std::pair<const StructuralSymbolNormalizationRule*,
                        ObservedSymbol>> matched_rules;
  for (const auto& candidate : candidates) {
    const StructuralSymbolNormalizationRule& rule = *candidate.first;
    // Rules are sorted by descending priority.  Once a higher-priority match
    // exists, lower-priority rules cannot participate in the decision.
    if (saw_match && rule.priority < matched_priority) break;
    if (rule.source_domain != source_domain) continue;
    if (rule.provider_scope != "provider-neutral" &&
        lower_ascii(provider).find(lower_ascii(rule.provider_scope)) ==
            std::string::npos) {
      continue;
    }
    for (const ObservedSymbol& observed : candidate.second) {
      if (observed.symbol_id.valid() &&
          rule_matches(rule, symbol_text(ir, observed.symbol_id))) {
        if (!saw_match) {
          matched_priority = rule.priority;
          saw_match = true;
        }
        matched_rules.push_back({&rule, observed});
        break;
      }
    }
  }
  if (matched_rules.size() == 1) {
    const auto& match = matched_rules.front();
    return {ir.symbols.intern(match.first->structural_symbol),
            {match.second.symbol_id, match.second.source,
             match.first->rule_id, match.first->rule_id,
             StructuralSymbolOutcome::kCanonicalized}};
  }
  if (matched_rules.size() > 1) {
    std::string candidate_ids;
    for (const auto& match : matched_rules) {
      if (!candidate_ids.empty()) candidate_ids += "|";
      candidate_ids += match.first->rule_id;
    }
    return {fallback.symbol_id,
            {fallback.symbol_id, fallback.source, "fallback.rule-conflict",
             candidate_ids, StructuralSymbolOutcome::kConflict}};
  }
  return preserve_structural_symbol(fallback.symbol_id, fallback.source);
}

}  // namespace

StructuralSymbolNormalizationRuleset::StructuralSymbolNormalizationRuleset(
    std::string policy_id,
    std::string policy_version,
    std::string source_manifest,
    std::string manifest_sha256,
    std::vector<StructuralSymbolNormalizationRule> rules)
    : policy_id_(std::move(policy_id)),
      policy_version_(std::move(policy_version)),
      source_manifest_(std::move(source_manifest)),
      manifest_sha256_(std::move(manifest_sha256)),
      rules_(std::move(rules)) {
  if (policy_id_.empty() || policy_version_.empty()) {
    throw std::invalid_argument(
        "structural-symbol manifest requires policy_id and policy_version");
  }
  validate_rules(rules_);
  std::stable_sort(rules_.begin(), rules_.end(),
                   [](const auto& lhs, const auto& rhs) {
                     return lhs.priority > rhs.priority;
                   });
}

StructuralSymbolPolicySnapshot
StructuralSymbolNormalizationRuleset::snapshot() const {
  StructuralSymbolPolicySnapshot out;
  out.policy_id = policy_id_;
  out.policy_version = policy_version_;
  out.policy_kind = "explicit_aliases_with_identity_fallback";
  out.source_manifest = source_manifest_;
  out.manifest_sha256 = manifest_sha256_;
  for (const auto& rule : rules_) {
    out.rules.push_back(
        {rule.priority,
         rule.rule_id,
         rule.provider_scope,
         rule.source_domain,
         structural_symbol_field_name(rule.field),
         structural_symbol_match_name(rule.match),
         rule.pattern,
         rule.structural_symbol,
         structural_symbol_field_name(rule.field),
         rule.note,
         rule.rule_origin,
         rule.rule_origin_sha256,
         rule.source_line});
  }
  return out;
}

const char* structural_symbol_source_name(StructuralSymbolSource source) {
  switch (source) {
    case StructuralSymbolSource::kUnknown: return "unknown";
    case StructuralSymbolSource::kTraceEventRawName: return "trace_event.raw_name";
    case StructuralSymbolSource::kTaskOpType: return "task.op_type";
    case StructuralSymbolSource::kTaskOpName: return "task.op_name";
    case StructuralSymbolSource::kTaskCommName: return "task.comm_name";
    case StructuralSymbolSource::kTaskType: return "task.task_type";
    case StructuralSymbolSource::kTaskComputeType: return "task.compute_task_type";
    case StructuralSymbolSource::kCommunicationOpName: return "communication.op_name";
    case StructuralSymbolSource::kCommunicationOpType: return "communication.op_type";
    case StructuralSymbolSource::kCommunicationLinkedTaskName: return "communication.linked_task_name";
    case StructuralSymbolSource::kCommunicationLinkedTaskType: return "communication.linked_task_type";
    case StructuralSymbolSource::kReplayCompositionSlot: return "analysis.replay_composition_slot";
  }
  return "unknown";
}

const char* structural_symbol_outcome_name(StructuralSymbolOutcome outcome) {
  switch (outcome) {
    case StructuralSymbolOutcome::kUnsupported: return "unsupported";
    case StructuralSymbolOutcome::kIdentity: return "identity";
    case StructuralSymbolOutcome::kCanonicalized: return "canonicalized";
    case StructuralSymbolOutcome::kConflict: return "conflict";
    case StructuralSymbolOutcome::kSynthetic: return "synthetic";
  }
  return "unsupported";
}

const char* structural_symbol_reason_code(StructuralSymbolOutcome outcome) {
  switch (outcome) {
    case StructuralSymbolOutcome::kUnsupported: return "missing_observed_symbol";
    case StructuralSymbolOutcome::kIdentity: return "no_explicit_rule_identity_preserved";
    case StructuralSymbolOutcome::kCanonicalized: return "explicit_rule_match";
    case StructuralSymbolOutcome::kConflict: return "equal_precedence_rule_conflict";
    case StructuralSymbolOutcome::kSynthetic: return "analysis_generated_structural_symbol";
  }
  return "missing_observed_symbol";
}

const char* structural_symbol_field_name(StructuralSymbolField field) {
  switch (field) {
    case StructuralSymbolField::kSelected: return "selected";
    case StructuralSymbolField::kAnyIdentity: return "any_identity";
    case StructuralSymbolField::kOpName: return "op_name";
    case StructuralSymbolField::kOpType: return "op_type";
    case StructuralSymbolField::kTaskType: return "task_type";
    case StructuralSymbolField::kComputeTaskType: return "compute_task_type";
    case StructuralSymbolField::kCommName: return "comm_name";
    case StructuralSymbolField::kLinkedTaskName: return "linked_task_name";
    case StructuralSymbolField::kLinkedTaskType: return "linked_task_type";
  }
  return "selected";
}

const char* structural_symbol_match_name(StructuralSymbolMatch match) {
  switch (match) {
    case StructuralSymbolMatch::kExact: return "exact";
    case StructuralSymbolMatch::kExactAny: return "exact_any";
    case StructuralSymbolMatch::kContainsCaseInsensitive: return "contains_ci";
    case StructuralSymbolMatch::kContainsAnyCaseInsensitive: return "contains_any_ci";
    case StructuralSymbolMatch::kOpaqueNumericInstance: return "opaque_numeric_instance";
  }
  return "exact";
}

StructuralSymbolNormalizationRuleset load_structural_symbol_ruleset(
    const std::string& path) {
  std::ifstream stream(path);
  if (!stream) {
    throw std::invalid_argument(
        "cannot open structural-symbol normalization manifest: " + path);
  }
  std::string policy_id;
  std::string policy_version;
  std::vector<StructuralSymbolNormalizationRule> rules;
  std::string line;
  std::size_t line_number = 0;
  bool saw_header = false;
  while (std::getline(stream, line)) {
    ++line_number;
    const std::string stripped = trim(line);
    if (stripped.empty()) continue;
    if (stripped.front() == '#') {
      const std::string prefix = "# policy_id=";
      const std::string version_prefix = "# policy_version=";
      if (stripped.rfind(prefix, 0) == 0) policy_id = trim(stripped.substr(prefix.size()));
      if (stripped.rfind(version_prefix, 0) == 0) policy_version = trim(stripped.substr(version_prefix.size()));
      continue;
    }
    const auto fields = split_tsv(line);
    if (!saw_header) {
      const std::vector<std::string> expected{
          "priority", "rule_id", "provider_scope", "source_domain",
          "field", "match", "pattern", "structural_symbol", "note"};
      if (fields != expected) {
        throw std::invalid_argument(
            "invalid structural-symbol manifest header at line " +
            std::to_string(line_number));
      }
      saw_header = true;
      continue;
    }
    if (fields.size() != 9) {
      throw std::invalid_argument(
          "structural-symbol rule at line " + std::to_string(line_number) +
          " must contain nine tab-separated fields");
    }
    std::size_t consumed = 0;
    const long priority = std::stol(fields[0], &consumed, 10);
    if (consumed != fields[0].size() ||
        priority < std::numeric_limits<std::int32_t>::min() ||
        priority > std::numeric_limits<std::int32_t>::max()) {
      throw std::invalid_argument("invalid structural-symbol priority at line " +
                                  std::to_string(line_number));
    }
    rules.push_back(
        {static_cast<std::int32_t>(priority), fields[1], fields[2], fields[3],
         parse_field(fields[4], line_number), parse_match(fields[5], line_number),
         fields[6], fields[7], fields[8], path, "", line_number});
  }
  if (!saw_header) {
    throw std::invalid_argument(
        "structural-symbol normalization manifest is empty: " + path);
  }
  const std::string manifest_sha256 = sha256_file_hex(path);
  for (auto& rule : rules) rule.rule_origin_sha256 = manifest_sha256;
  return {policy_id, policy_version, path, manifest_sha256, std::move(rules)};
}

StructuralSymbolNormalizationRuleset load_default_structural_symbol_ruleset(
    const std::string& executable_path) {
  const char* override_path = std::getenv("TRACELOOM_SYMBOL_RULES");
  if (override_path != nullptr && *override_path != '\0') {
    return load_structural_symbol_ruleset(override_path);
  }
  std::vector<std::string> candidates;
  if (!executable_path.empty()) {
    const auto executable = std::filesystem::absolute(executable_path);
    candidates.push_back((executable.parent_path().parent_path() / "share" /
                          "traceloom" /
                          "default_structural_symbol_rules.tsv").string());
  }
  candidates.push_back(TRACELOOM_SOURCE_DEFAULT_SYMBOL_RULESET_PATH);
  candidates.push_back(TRACELOOM_INSTALL_DEFAULT_SYMBOL_RULESET_PATH);
  candidates.push_back("/usr/share/traceloom/default_structural_symbol_rules.tsv");
  candidates.push_back("/usr/local/share/traceloom/default_structural_symbol_rules.tsv");
  for (const std::string& candidate : candidates) {
    if (candidate.empty()) continue;
    std::ifstream probe(candidate);
    if (probe.good()) return load_structural_symbol_ruleset(candidate);
  }
  throw std::invalid_argument(
      "default structural-symbol manifest not found; set "
      "TRACELOOM_SYMBOL_RULES or pass --symbol-rules");
}

StructuralSymbolNormalizationRuleset extend_structural_symbol_ruleset(
    const StructuralSymbolNormalizationRuleset& base,
    const StructuralSymbolNormalizationRuleset& extension) {
  std::vector<StructuralSymbolNormalizationRule> merged = base.rules();
  merged.insert(merged.end(), extension.rules().begin(), extension.rules().end());
  const std::string composite_hash = sha256_hex(
      base.manifest_sha256() + ":" + extension.manifest_sha256());
  return {base.policy_id() + "+" + extension.policy_id(),
          base.policy_version() + "+" + extension.policy_version(),
          base.source_manifest() + ";" + extension.source_manifest(),
          composite_hash, std::move(merged)};
}

ResolvedStructuralSymbol preserve_structural_symbol(
    SymbolId observed_symbol_id, StructuralSymbolSource source) {
  if (!observed_symbol_id.valid()) {
    return {SymbolId::invalid(),
            {SymbolId::invalid(), source, "fallback.missing-observed-symbol",
             "",
             StructuralSymbolOutcome::kUnsupported}};
  }
  return {observed_symbol_id,
          {observed_symbol_id, source, "fallback.identity-preserve",
           "",
           StructuralSymbolOutcome::kIdentity}};
}

ResolvedStructuralSymbol synthesize_structural_symbol(
    SymbolId structural_symbol_id, StructuralSymbolSource source) {
  return {structural_symbol_id,
          {structural_symbol_id, source, "analysis.replay-composition-slot",
           "",
           StructuralSymbolOutcome::kSynthetic}};
}

ResolvedStructuralSymbol normalize_task_structural_symbol(
    NativeIr& ir, const TaskRow& task,
    const StructuralSymbolNormalizationRuleset& ruleset) {
  std::vector<std::pair<const StructuralSymbolNormalizationRule*,
                        std::vector<ObservedSymbol>>> candidates;
  candidates.reserve(ruleset.rules().size());
  for (const auto& rule : ruleset.rules()) {
    candidates.push_back({&rule, task_fields(task, rule.field)});
  }
  const std::string provider = task.source_ref_id.valid()
                                   ? ir.source_refs.row(task.source_ref_id)
                                         .source_kind
                                   : std::string();
  return resolve(ir, provider, "task", candidates,
                 selected_task_symbol(task));
}

ResolvedStructuralSymbol normalize_communication_structural_symbol(
    NativeIr& ir, const CommunicationOpRow& communication,
    const StructuralSymbolNormalizationRuleset& ruleset) {
  std::vector<std::pair<const StructuralSymbolNormalizationRule*,
                        std::vector<ObservedSymbol>>> candidates;
  candidates.reserve(ruleset.rules().size());
  for (const auto& rule : ruleset.rules()) {
    candidates.push_back({&rule, communication_fields(communication, rule.field)});
  }
  const std::string provider = communication.source_ref_id.valid()
                                   ? ir.source_refs
                                         .row(communication.source_ref_id)
                                         .source_kind
                                   : std::string();
  return resolve(ir, provider, "communication", candidates,
                 communication_identity_symbol(communication));
}

}  // namespace traceloom
