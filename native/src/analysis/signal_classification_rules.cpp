#include "traceloom/analysis/signal_classification_rules.h"

#include "signal_classification_rules_internal.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace traceloom {
namespace signal_classification_detail {

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

}  // namespace signal_classification_detail

namespace {

using signal_classification_detail::trim;

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

}  // namespace

namespace signal_classification_detail {

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

}  // namespace signal_classification_detail

namespace {

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

}  // namespace

namespace signal_classification_detail {

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

}  // namespace signal_classification_detail

namespace {

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
                             signal_match_field_name(rule.field))) {
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
    if (!keys.insert(signal_classification_detail::canonical_rule_key(rule))
             .second) {
      throw std::invalid_argument(
          "conflicting signal rules with equal precedence at line " +
          std::to_string(rule.source_line));
    }
  }
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

std::string join_values(const std::set<std::string>& values) {
  std::string out;
  for (const std::string& value : values) {
    if (!out.empty()) {
      out += ',';
    }
    out += value;
  }
  return out;
}

std::string available_fields(const SignalClassificationInput& input) {
  std::set<std::string> fields;
  if (!input.source_domain.empty()) {
    fields.insert("source_domain");
  }
  if (!input.provider_scope.empty()) {
    fields.insert("provider_scope");
  }
  if (!input.task_type.empty()) {
    fields.insert("task_type");
  }
  if (!input.blob.empty()) {
    fields.insert("blob");
  }
  if (!input.operator_name.empty()) {
    fields.insert("operator");
  }
  return join_values(fields);
}

std::set<std::string> missing_required_evidence(
    const SignalClassificationRule& rule,
    const SignalClassificationInput& input) {
  std::set<std::string> missing;
  for (const std::string& field : comma_list_values(rule.required_fields)) {
    if ((field == "source_domain" && input.source_domain.empty()) ||
        (field == "provider_scope" && input.provider_scope.empty()) ||
        (field == "task_type" && input.task_type.empty()) ||
        (field == "blob" && input.blob.empty()) ||
        (field == "operator" && input.operator_name.empty())) {
      missing.insert(field);
    }
  }
  return missing;
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

const char* signal_match_field_name(SignalMatchField field) noexcept {
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

const char* signal_match_kind_name(SignalMatchKind match) noexcept {
  switch (match) {
    case SignalMatchKind::kExact:
      return "exact";
    case SignalMatchKind::kContains:
      return "contains";
  }
  return "unknown";
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
  signal_classification_detail::validate_metadata(metadata_);
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
  std::set<std::string> missing_fields;
  std::set<std::string> missing_rule_ids;
  for (const SignalClassificationRule& rule : rules_) {
    const bool provider_matches =
        input.provider_scope == "any" || rule.provider_scope == "any" ||
        rule.provider_scope == input.provider_scope;
    if (!provider_matches || rule.source_domain != input.source_domain ||
        !matches(rule, input)) {
      continue;
    }
    if (!has_required_evidence(rule, input)) {
      const std::set<std::string> rule_missing =
          missing_required_evidence(rule, input);
      missing_fields.insert(rule_missing.begin(), rule_missing.end());
      missing_rule_ids.insert(rule.rule_id);
      continue;
    }
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
    decision.available_fields = available_fields(input);
    decision.missing_required_fields = join_values(missing_fields);
    decision.missing_capability_rule_ids = join_values(missing_rule_ids);
    decision.support_state = missing_fields.empty()
                                 ? "supported"
                                 : "supported_after_missing_capability";
    decision.reason_code = missing_fields.empty()
                               ? "matched_positive_rule"
                               : "matched_rule_after_missing_capability";
    decision.priority = rule.priority;
    decision.declaration_order = rule.declaration_order;
    return decision;
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
  decision.available_fields = available_fields(input);
  decision.missing_required_fields = join_values(missing_fields);
  decision.missing_capability_rule_ids = join_values(missing_rule_ids);
  decision.support_state = missing_fields.empty() ? "supported_unknown_first"
                                                   : "missing_capability";
  decision.reason_code = missing_fields.empty()
                             ? "unmatched_observation_unknown_first"
                             : "missing_capability_unknown_first";
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

}  // namespace traceloom
