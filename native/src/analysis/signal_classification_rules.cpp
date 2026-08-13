#include "traceloom/analysis/signal_classification_rules.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "traceloom/core/sha256.h"

#ifndef TRACELOOM_SOURCE_DEFAULT_RULESET_PATH
#define TRACELOOM_SOURCE_DEFAULT_RULESET_PATH ""
#endif

#ifndef TRACELOOM_INSTALL_DEFAULT_RULESET_PATH
#define TRACELOOM_INSTALL_DEFAULT_RULESET_PATH ""
#endif

namespace traceloom {
namespace {

constexpr const char* kManifestSchema = "traceloom.evidence-role-policy/v1";

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

std::string canonical_rule_key(const SignalClassificationRule& rule) {
  std::string pattern = rule.pattern;
  if (rule.field == SignalMatchField::kTaskType) {
    pattern = normalize_task_type(pattern);
  } else {
    pattern = lower_ascii(pattern);
  }
  return rule.source_domain + "\n" +
         rule.provider_scope + "\n" +
         std::to_string(static_cast<int>(rule.field)) + "\n" +
         std::to_string(static_cast<int>(rule.match)) + "\n" + pattern +
         "\n" + std::to_string(rule.priority);
}

std::string legacy_rule_id(const SignalClassificationRule& rule) {
  return "legacy." + sha256_hex(canonical_rule_key(rule)).substr(0, 16);
}

bool comma_list_contains(const std::string& list, const std::string& value) {
  std::size_t start = 0;
  while (start <= list.size()) {
    const std::size_t comma = list.find(',', start);
    if (trim(list.substr(start, comma - start)) == value) {
      return true;
    }
    if (comma == std::string::npos) {
      return false;
    }
    start = comma + 1;
  }
  return false;
}

std::vector<std::string> comma_list_values(const std::string& list) {
  std::vector<std::string> values;
  std::size_t start = 0;
  while (start <= list.size()) {
    const std::size_t comma = list.find(',', start);
    const std::string value = trim(list.substr(start, comma - start));
    if (!value.empty()) {
      values.push_back(value);
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  return values;
}

const char* match_field_name(SignalMatchField field) {
  switch (field) {
    case SignalMatchField::kTaskType:
      return "task_type";
    case SignalMatchField::kBlob:
      return "blob";
    case SignalMatchField::kOperator:
      return "operator";
  }
  return "unknown";
}

void validate_metadata(const SignalClassificationPolicyMetadata& metadata) {
  if (metadata.manifest_schema != kManifestSchema) {
    throw std::invalid_argument(
        "unsupported signal policy manifest_schema: " +
        metadata.manifest_schema);
  }
  if (metadata.policy_id.empty() || metadata.policy_version.empty()) {
    throw std::invalid_argument(
        "signal policy requires policy_id and policy_version");
  }
  if (metadata.provider_scopes.empty()) {
    throw std::invalid_argument("signal policy requires provider_scopes");
  }
  const std::vector<std::string> scopes =
      comma_list_values(metadata.provider_scopes);
  if (scopes.empty()) {
    throw std::invalid_argument("signal policy requires provider_scopes");
  }
  for (const std::string& scope : scopes) {
    if (scope != "any" && scope != "ascend" && scope != "cuda" &&
        scope != "hygon") {
      throw std::invalid_argument("unsupported signal policy provider scope: " +
                                  scope);
    }
  }
  if (metadata.fallback_identity_role != SignalRole::kUnknownAnchor) {
    throw std::invalid_argument(
        "signal policy fallback_identity_role must be unknown_anchor");
  }
}

void validate_rules(const std::vector<SignalClassificationRule>& rules) {
  std::unordered_set<std::string> keys;
  std::unordered_set<std::string> rule_ids;
  for (const SignalClassificationRule& rule : rules) {
    if (rule.rule_id.empty()) {
      throw std::invalid_argument("empty signal rule id at line " +
                                  std::to_string(rule.source_line));
    }
    if (!rule_ids.insert(rule.rule_id).second) {
      throw std::invalid_argument("duplicate signal rule id at line " +
                                  std::to_string(rule.source_line) + ": " +
                                  rule.rule_id);
    }
    if (rule.provider_scope != "any" && rule.provider_scope != "ascend" &&
        rule.provider_scope != "cuda" && rule.provider_scope != "hygon") {
      throw std::invalid_argument(
          "unsupported signal rule provider_scope at line " +
          std::to_string(rule.source_line) + ": " + rule.provider_scope);
    }
    if (rule.source_domain != "task") {
      throw std::invalid_argument(
          "unsupported signal rule source_domain at line " +
          std::to_string(rule.source_line) + ": " + rule.source_domain);
    }
    if (rule.pattern.empty()) {
      throw std::invalid_argument("empty signal rule pattern at line " +
                                  std::to_string(rule.source_line));
    }
    if (!comma_list_contains(rule.required_fields,
                             match_field_name(rule.field))) {
      throw std::invalid_argument(
          "signal rule required_fields omits match field at line " +
          std::to_string(rule.source_line));
    }
    for (const std::string& field : comma_list_values(rule.required_fields)) {
      if (field != "source_domain" && field != "provider_scope" &&
          field != "task_type" && field != "blob" && field != "operator") {
        throw std::invalid_argument(
            "unknown signal rule required field at line " +
            std::to_string(rule.source_line) + ": " + field);
      }
    }
    const bool identity_role = rule.role == SignalRole::kAnchor;
    if (identity_role != (rule.structural_participation ==
                          SignalStructuralParticipation::kIdentity)) {
      throw std::invalid_argument(
          "signal role and structural_participation disagree at line " +
          std::to_string(rule.source_line));
    }
    if (rule.role == SignalRole::kUnknownAnchor) {
      throw std::invalid_argument(
          "unknown_anchor is a fallback role, not a positive rule, at line " +
          std::to_string(rule.source_line));
    }
    if (!keys.insert(canonical_rule_key(rule)).second) {
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
  if (value == "operator") {
    return SignalMatchField::kOperator;
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

SignalRole parse_role(const std::string& value, std::size_t line,
                      bool legacy_ignore_allowed = false) {
  if (value == "anchor") {
    return SignalRole::kAnchor;
  }
  if (value == "auxiliary" ||
      (legacy_ignore_allowed && value == "ignore")) {
    return SignalRole::kAuxiliary;
  }
  if (value == "transparent") {
    return SignalRole::kTransparent;
  }
  if (value == "unknown_anchor") {
    return SignalRole::kUnknownAnchor;
  }
  throw std::invalid_argument("unknown signal rule role at line " +
                              std::to_string(line) + ": " + value);
}

SignalStructuralParticipation parse_structural_participation(
    const std::string& value, std::size_t line) {
  if (value == "identity") {
    return SignalStructuralParticipation::kIdentity;
  }
  if (value == "excluded") {
    return SignalStructuralParticipation::kExcluded;
  }
  throw std::invalid_argument(
      "unknown signal structural participation at line " +
      std::to_string(line) + ": " + value);
}

SignalCostTreatment parse_cost_treatment(const std::string& value,
                                         std::size_t line) {
  if (value == "retained_for_attribution") {
    return SignalCostTreatment::kRetainedForAttribution;
  }
  if (value == "retained_as_evidence") {
    return SignalCostTreatment::kRetainedAsEvidence;
  }
  throw std::invalid_argument("unknown signal cost treatment at line " +
                              std::to_string(line) + ": " + value);
}

SignalContextTreatment parse_context_treatment(const std::string& value,
                                                std::size_t line) {
  if (value == "retained") {
    return SignalContextTreatment::kRetained;
  }
  throw std::invalid_argument("unknown signal context treatment at line " +
                              std::to_string(line) + ": " + value);
}

SignalProvenanceTreatment parse_provenance_treatment(
    const std::string& value, std::size_t line) {
  if (value == "retained") {
    return SignalProvenanceTreatment::kRetained;
  }
  throw std::invalid_argument("unknown signal provenance treatment at line " +
                              std::to_string(line) + ": " + value);
}

SignalMissingEvidenceBehavior parse_missing_evidence_behavior(
    const std::string& value, std::size_t line) {
  if (value == "continue_or_fallback") {
    return SignalMissingEvidenceBehavior::kContinueOrFallback;
  }
  throw std::invalid_argument(
      "unknown signal missing-evidence behavior at line " +
      std::to_string(line) + ": " + value);
}

SignalConcreteIdentityBehavior parse_concrete_identity_behavior(
    const std::string& value, std::size_t line) {
  if (value == "apply") {
    return SignalConcreteIdentityBehavior::kApply;
  }
  if (value == "defer_to_identity_rules") {
    return SignalConcreteIdentityBehavior::kDeferToIdentityRules;
  }
  throw std::invalid_argument(
      "unknown signal concrete-identity behavior at line " +
      std::to_string(line) + ": " + value);
}

bool matches(const SignalClassificationRule& rule,
             const SignalClassificationInput& input) {
  std::string value;
  std::string pattern = rule.pattern;
  if (rule.field == SignalMatchField::kTaskType) {
    value = normalize_task_type(input.task_type);
    pattern = normalize_task_type(pattern);
  } else if (rule.field == SignalMatchField::kBlob) {
    value = lower_ascii(input.blob);
    pattern = lower_ascii(pattern);
  } else {
    value = lower_ascii(input.operator_name);
    pattern = lower_ascii(pattern);
  }
  if (value.empty()) {
    return false;
  }
  if (rule.match == SignalMatchKind::kExact) {
    return value == pattern;
  }
  return value.find(pattern) != std::string::npos;
}

bool has_required_evidence(const SignalClassificationRule& rule,
                           const SignalClassificationInput& input) {
  for (const std::string& field : comma_list_values(rule.required_fields)) {
    if ((field == "source_domain" && input.source_domain.empty()) ||
        (field == "provider_scope" && input.provider_scope.empty()) ||
        (field == "task_type" && input.task_type.empty()) ||
        (field == "blob" && input.blob.empty()) ||
        (field == "operator" && input.operator_name.empty())) {
      return false;
    }
  }
  return true;
}

std::unordered_map<std::string, std::string> parse_manifest_metadata(
    const std::vector<std::string>& lines) {
  const std::unordered_set<std::string> allowed{
      "manifest_schema", "policy_id", "policy_version", "provider_scopes",
      "fallback_identity_role", "fallback_cost_treatment",
      "fallback_context_treatment", "fallback_provenance_treatment",
      "missing_evidence_behavior"};
  std::unordered_map<std::string, std::string> metadata;
  for (const std::string& raw_line : lines) {
    const std::string line = trim(raw_line);
    if (line.empty() || line.front() != '#') {
      continue;
    }
    const std::string body = trim(line.substr(1));
    const std::size_t equal = body.find('=');
    if (equal == std::string::npos) {
      continue;
    }
    const std::string key = trim(body.substr(0, equal));
    const std::string value = trim(body.substr(equal + 1));
    if (allowed.find(key) == allowed.end()) {
      throw std::invalid_argument("unknown signal policy metadata: " + key);
    }
    if (key.empty() || value.empty() || !metadata.emplace(key, value).second) {
      throw std::invalid_argument("invalid or duplicate signal policy metadata: " +
                                  key);
    }
  }
  return metadata;
}

std::string require_metadata(
    const std::unordered_map<std::string, std::string>& metadata,
    const std::string& key) {
  const auto it = metadata.find(key);
  if (it == metadata.end()) {
    throw std::invalid_argument("signal policy manifest missing metadata: " +
                                key);
  }
  return it->second;
}

SignalClassificationPolicyMetadata parse_policy_metadata(
    const std::vector<std::string>& lines, const std::string& manifest_sha256) {
  const std::unordered_map<std::string, std::string> values =
      parse_manifest_metadata(lines);
  SignalClassificationPolicyMetadata metadata;
  metadata.manifest_schema = require_metadata(values, "manifest_schema");
  metadata.policy_id = require_metadata(values, "policy_id");
  metadata.policy_version = require_metadata(values, "policy_version");
  metadata.provider_scopes = require_metadata(values, "provider_scopes");
  metadata.fallback_identity_role = parse_role(
      require_metadata(values, "fallback_identity_role"), 0);
  metadata.fallback_cost_treatment = parse_cost_treatment(
      require_metadata(values, "fallback_cost_treatment"), 0);
  metadata.fallback_context_treatment = parse_context_treatment(
      require_metadata(values, "fallback_context_treatment"), 0);
  metadata.fallback_provenance_treatment = parse_provenance_treatment(
      require_metadata(values, "fallback_provenance_treatment"), 0);
  metadata.missing_evidence_behavior = parse_missing_evidence_behavior(
      require_metadata(values, "missing_evidence_behavior"), 0);
  metadata.manifest_sha256 = manifest_sha256;
  validate_metadata(metadata);
  return metadata;
}

SignalClassificationPolicyMetadata legacy_metadata(
    const std::string& manifest_sha256) {
  SignalClassificationPolicyMetadata metadata;
  metadata.manifest_schema = kManifestSchema;
  metadata.policy_id = "traceloom.legacy-signal-rules";
  metadata.policy_version = "1+sha256:" + manifest_sha256.substr(0, 16);
  metadata.provider_scopes = "any";
  metadata.manifest_sha256 = manifest_sha256;
  return metadata;
}

std::int32_t parse_priority(const std::string& value, std::size_t line) {
  std::size_t consumed = 0;
  const long priority = std::stol(value, &consumed, 10);
  if (consumed != value.size() ||
      priority < std::numeric_limits<std::int32_t>::min() ||
      priority > std::numeric_limits<std::int32_t>::max()) {
    throw std::invalid_argument("invalid signal rule priority at line " +
                                std::to_string(line));
  }
  return static_cast<std::int32_t>(priority);
}

SignalClassificationRule parse_legacy_rule(
    const std::vector<std::string>& fields, std::size_t line,
    std::size_t declaration_order) {
  SignalClassificationRule rule;
  rule.priority = parse_priority(fields[0], line);
  rule.provider_scope = "any";
  rule.source_domain = fields[1];
  rule.field = parse_field(fields[2], line);
  rule.match = parse_match(fields[3], line);
  rule.pattern = fields[4];
  rule.role = parse_role(fields[5], line, true);
  rule.required_fields = match_field_name(rule.field);
  rule.structural_participation =
      rule.role == SignalRole::kAnchor
          ? SignalStructuralParticipation::kIdentity
          : SignalStructuralParticipation::kExcluded;
  rule.cost_treatment = SignalCostTreatment::kRetainedForAttribution;
  rule.note = fields[6];
  rule.source_line = line;
  rule.declaration_order = declaration_order;
  rule.rule_id = legacy_rule_id(rule);
  rule.concrete_identity_behavior = SignalConcreteIdentityBehavior::kApply;
  return rule;
}

SignalClassificationRule parse_manifest_rule(
    const std::vector<std::string>& fields, std::size_t line,
    std::size_t declaration_order) {
  SignalClassificationRule rule;
  rule.rule_id = fields[0];
  rule.priority = parse_priority(fields[1], line);
  rule.provider_scope = fields[2];
  rule.source_domain = fields[3];
  rule.field = parse_field(fields[4], line);
  rule.match = parse_match(fields[5], line);
  rule.pattern = fields[6];
  rule.role = parse_role(fields[7], line);
  rule.required_fields = fields[8];
  rule.structural_participation =
      parse_structural_participation(fields[9], line);
  rule.cost_treatment = parse_cost_treatment(fields[10], line);
  rule.context_treatment = parse_context_treatment(fields[11], line);
  rule.provenance_treatment = parse_provenance_treatment(fields[12], line);
  rule.missing_evidence_behavior =
      parse_missing_evidence_behavior(fields[13], line);
  rule.concrete_identity_behavior =
      parse_concrete_identity_behavior(fields[14], line);
  rule.note = fields[15];
  rule.source_line = line;
  rule.declaration_order = declaration_order;
  return rule;
}

std::string composite_manifest_digest(
    const SignalClassificationRuleset& base,
    const SignalClassificationRuleset& extension) {
  return sha256_hex(base.metadata().manifest_sha256 + "\n" +
                    extension.metadata().manifest_sha256);
}

}  // namespace

const char* signal_role_name(SignalRole role) noexcept {
  switch (role) {
    case SignalRole::kAnchor:
      return "anchor";
    case SignalRole::kAuxiliary:
      return "auxiliary";
    case SignalRole::kTransparent:
      return "transparent";
    case SignalRole::kUnknownAnchor:
      return "unknown_anchor";
  }
  return "unknown_anchor";
}

const char* signal_structural_participation_name(
    SignalStructuralParticipation participation) noexcept {
  switch (participation) {
    case SignalStructuralParticipation::kIdentity:
      return "identity";
    case SignalStructuralParticipation::kExcluded:
      return "excluded";
  }
  return "excluded";
}

const char* signal_cost_treatment_name(
    SignalCostTreatment treatment) noexcept {
  switch (treatment) {
    case SignalCostTreatment::kRetainedForAttribution:
      return "retained_for_attribution";
    case SignalCostTreatment::kRetainedAsEvidence:
      return "retained_as_evidence";
  }
  return "retained_as_evidence";
}

const char* signal_context_treatment_name(
    SignalContextTreatment treatment) noexcept {
  switch (treatment) {
    case SignalContextTreatment::kRetained:
      return "retained";
  }
  return "retained";
}

const char* signal_provenance_treatment_name(
    SignalProvenanceTreatment treatment) noexcept {
  switch (treatment) {
    case SignalProvenanceTreatment::kRetained:
      return "retained";
  }
  return "retained";
}

const char* signal_missing_evidence_behavior_name(
    SignalMissingEvidenceBehavior behavior) noexcept {
  switch (behavior) {
    case SignalMissingEvidenceBehavior::kContinueOrFallback:
      return "continue_or_fallback";
  }
  return "continue_or_fallback";
}

const char* signal_concrete_identity_behavior_name(
    SignalConcreteIdentityBehavior behavior) noexcept {
  switch (behavior) {
    case SignalConcreteIdentityBehavior::kApply:
      return "apply";
    case SignalConcreteIdentityBehavior::kDeferToIdentityRules:
      return "defer_to_identity_rules";
  }
  return "apply";
}

SignalClassificationRuleset::SignalClassificationRuleset(
    SignalClassificationPolicyMetadata metadata,
    std::vector<SignalClassificationRule> rules)
    : metadata_(std::move(metadata)), rules_(std::move(rules)) {
  validate_metadata(metadata_);
  validate_rules(rules_);
  for (const SignalClassificationRule& rule : rules_) {
    if (rule.provider_scope != "any" &&
        !comma_list_contains(metadata_.provider_scopes, "any") &&
        !comma_list_contains(metadata_.provider_scopes,
                             rule.provider_scope)) {
      throw std::invalid_argument(
          "signal rule provider_scope is not declared by policy " +
          metadata_.policy_id + ": " + rule.provider_scope);
    }
  }
  std::stable_sort(
      rules_.begin(), rules_.end(),
      [](const SignalClassificationRule& lhs,
         const SignalClassificationRule& rhs) {
        if (lhs.priority != rhs.priority) {
          return lhs.priority > rhs.priority;
        }
        return lhs.declaration_order < rhs.declaration_order;
      });
}

SignalClassificationDecision SignalClassificationRuleset::decide(
    const SignalClassificationInput& input) const {
  if (input.provider_scope != "any" && input.provider_scope != "ascend" &&
      input.provider_scope != "cuda" && input.provider_scope != "hygon") {
    throw std::invalid_argument("unsupported classification provider scope: " +
                                input.provider_scope);
  }
  for (const SignalClassificationRule& rule : rules_) {
    const bool provider_matches =
        input.provider_scope == "any" || rule.provider_scope == "any" ||
        rule.provider_scope == input.provider_scope;
    if (provider_matches && rule.source_domain == input.source_domain &&
        has_required_evidence(rule, input) && matches(rule, input)) {
      if (input.has_concrete_identity &&
          rule.concrete_identity_behavior ==
              SignalConcreteIdentityBehavior::kDeferToIdentityRules) {
        continue;
      }
      SignalClassificationDecision decision;
      decision.role = rule.role;
      decision.structural_participation = rule.structural_participation;
      decision.cost_treatment = rule.cost_treatment;
      decision.context_treatment = rule.context_treatment;
      decision.provenance_treatment = rule.provenance_treatment;
      decision.missing_evidence_behavior = rule.missing_evidence_behavior;
      decision.concrete_identity_behavior = rule.concrete_identity_behavior;
      decision.matched_rule = true;
      decision.policy_id = metadata_.policy_id;
      decision.policy_version = metadata_.policy_version;
      decision.manifest_sha256 = metadata_.manifest_sha256;
      decision.rule_id = rule.rule_id;
      decision.provider_scope = rule.provider_scope;
      decision.required_fields = rule.required_fields;
      decision.reason_code = "matched_positive_rule";
      decision.priority = rule.priority;
      decision.declaration_order = rule.declaration_order;
      return decision;
    }
  }

  SignalClassificationDecision decision;
  decision.role = metadata_.fallback_identity_role;
  decision.structural_participation =
      SignalStructuralParticipation::kIdentity;
  decision.cost_treatment = metadata_.fallback_cost_treatment;
  decision.context_treatment = metadata_.fallback_context_treatment;
  decision.provenance_treatment = metadata_.fallback_provenance_treatment;
  decision.missing_evidence_behavior = metadata_.missing_evidence_behavior;
  decision.concrete_identity_behavior = SignalConcreteIdentityBehavior::kApply;
  decision.policy_id = metadata_.policy_id;
  decision.policy_version = metadata_.policy_version;
  decision.manifest_sha256 = metadata_.manifest_sha256;
  decision.rule_id = "fallback.unknown_observation";
  decision.provider_scope = input.provider_scope;
  decision.reason_code = "unmatched_observation_unknown_first";
  return decision;
}

std::optional<SignalRole> SignalClassificationRuleset::classify(
    const SignalClassificationInput& input) const {
  const SignalClassificationDecision decision = decide(input);
  if (!decision.matched_rule) {
    return std::nullopt;
  }
  return decision.role;
}

SignalClassificationRuleset load_signal_classification_ruleset(
    const std::string& path) {
  std::ifstream stream(path);
  if (!stream) {
    throw std::invalid_argument("cannot open signal classification ruleset: " +
                                path);
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }
  const std::string manifest_sha256 = sha256_file_hex(path);

  const std::vector<std::string> legacy_header{
      "priority", "source_domain", "field", "match", "pattern", "role",
      "note"};
  const std::vector<std::string> manifest_header{
      "rule_id", "priority", "provider_scope", "source_domain", "field",
      "match", "pattern", "role", "required_fields",
      "structural_participation", "cost_treatment", "context_treatment",
      "provenance_treatment", "missing_evidence_behavior",
      "concrete_identity_behavior", "note"};

  std::vector<SignalClassificationRule> rules;
  std::vector<std::string> header;
  std::size_t declaration_order = 0;
  for (std::size_t index = 0; index < lines.size(); ++index) {
    const std::size_t line_number = index + 1;
    const std::string stripped = trim(lines[index]);
    if (stripped.empty() || stripped.front() == '#') {
      continue;
    }
    const std::vector<std::string> fields = split_tsv(lines[index]);
    if (header.empty()) {
      if (fields != legacy_header && fields != manifest_header) {
        throw std::invalid_argument(
            "invalid signal ruleset header at line " +
            std::to_string(line_number));
      }
      header = fields;
      continue;
    }
    if (fields.size() != header.size()) {
      throw std::invalid_argument("signal rule at line " +
                                  std::to_string(line_number) + " must contain " +
                                  std::to_string(header.size()) +
                                  " tab-separated fields");
    }
    if (header == legacy_header) {
      rules.push_back(
          parse_legacy_rule(fields, line_number, declaration_order++));
    } else {
      rules.push_back(
          parse_manifest_rule(fields, line_number, declaration_order++));
    }
  }
  if (header.empty()) {
    throw std::invalid_argument("signal classification ruleset is empty: " +
                                path);
  }
  SignalClassificationPolicyMetadata metadata =
      header == legacy_header
          ? legacy_metadata(manifest_sha256)
          : parse_policy_metadata(lines, manifest_sha256);
  return SignalClassificationRuleset(std::move(metadata), std::move(rules));
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
  std::size_t order = merged.size();
  for (SignalClassificationRule rule : extension.rules()) {
    rule.declaration_order = order++;
    merged.push_back(std::move(rule));
  }
  SignalClassificationPolicyMetadata metadata = base.metadata();
  metadata.policy_id = base.metadata().policy_id + "+" +
                       extension.metadata().policy_id;
  metadata.policy_version = base.metadata().policy_version + "+" +
                            extension.metadata().policy_version;
  metadata.provider_scopes = base.metadata().provider_scopes + "," +
                             extension.metadata().provider_scopes;
  metadata.manifest_sha256 = composite_manifest_digest(base, extension);
  return SignalClassificationRuleset(std::move(metadata), std::move(merged));
}

}  // namespace traceloom
