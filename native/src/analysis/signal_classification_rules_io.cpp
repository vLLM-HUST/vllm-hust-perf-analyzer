#include "traceloom/analysis/signal_classification_rules.h"

#include "signal_classification_rules_internal.h"
#include "traceloom/core/sha256.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifndef TRACELOOM_SOURCE_DEFAULT_RULESET_PATH
#define TRACELOOM_SOURCE_DEFAULT_RULESET_PATH ""
#endif

#ifndef TRACELOOM_INSTALL_DEFAULT_RULESET_PATH
#define TRACELOOM_INSTALL_DEFAULT_RULESET_PATH ""
#endif

namespace traceloom {
namespace {

std::vector<std::string> split_tsv(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (true) {
    const std::size_t tab = line.find('\t', start);
    fields.push_back(signal_classification_detail::trim(
        line.substr(start, tab - start)));
    if (tab == std::string::npos) {
      return fields;
    }
    start = tab + 1;
  }
}

std::string legacy_rule_id(const SignalClassificationRule& rule) {
  return "legacy." +
         sha256_hex(signal_classification_detail::canonical_rule_key(rule))
             .substr(0, 16);
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

std::unordered_map<std::string, std::string> parse_manifest_metadata(
    const std::vector<std::string>& lines) {
  const std::unordered_set<std::string> allowed{
      "manifest_schema", "policy_id", "policy_version", "provider_scopes",
      "fallback_identity_role", "fallback_cost_treatment",
      "fallback_context_treatment", "fallback_provenance_treatment",
      "missing_evidence_behavior"};
  std::unordered_map<std::string, std::string> metadata;
  for (const std::string& raw_line : lines) {
    const std::string line = signal_classification_detail::trim(raw_line);
    if (line.empty() || line.front() != '#') {
      continue;
    }
    const std::string body = signal_classification_detail::trim(line.substr(1));
    const std::size_t equal = body.find('=');
    if (equal == std::string::npos) {
      continue;
    }
    const std::string key =
        signal_classification_detail::trim(body.substr(0, equal));
    const std::string value =
        signal_classification_detail::trim(body.substr(equal + 1));
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
  metadata.effective_config_sha256 = manifest_sha256;
  signal_classification_detail::validate_metadata(metadata);
  return metadata;
}

SignalClassificationPolicyMetadata legacy_metadata(
    const std::string& manifest_sha256) {
  SignalClassificationPolicyMetadata metadata;
  metadata.manifest_schema = signal_classification_detail::kManifestSchema;
  metadata.policy_id = "traceloom.legacy-signal-rules";
  metadata.policy_version = "1+sha256:" + manifest_sha256.substr(0, 16);
  metadata.provider_scopes = "any";
  metadata.manifest_sha256 = manifest_sha256;
  metadata.effective_config_sha256 = manifest_sha256;
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
  rule.required_fields = signal_match_field_name(rule.field);
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

void apply_override(SignalClassificationRule& rule,
                    const SignalClassificationOverride& override) {
  if (override.field == "priority") {
    rule.priority = parse_priority(override.value, 0);
  } else if (override.field == "provider_scope") {
    rule.provider_scope = override.value;
  } else if (override.field == "source_domain") {
    rule.source_domain = override.value;
  } else if (override.field == "field") {
    rule.field = parse_field(override.value, 0);
  } else if (override.field == "match") {
    rule.match = parse_match(override.value, 0);
  } else if (override.field == "pattern") {
    rule.pattern = override.value;
  } else if (override.field == "role") {
    rule.role = parse_role(override.value, 0);
  } else if (override.field == "required_fields") {
    rule.required_fields = override.value;
  } else if (override.field == "structural_participation") {
    rule.structural_participation =
        parse_structural_participation(override.value, 0);
  } else if (override.field == "cost_treatment") {
    rule.cost_treatment = parse_cost_treatment(override.value, 0);
  } else if (override.field == "context_treatment") {
    rule.context_treatment = parse_context_treatment(override.value, 0);
  } else if (override.field == "provenance_treatment") {
    rule.provenance_treatment =
        parse_provenance_treatment(override.value, 0);
  } else if (override.field == "missing_evidence_behavior") {
    rule.missing_evidence_behavior =
        parse_missing_evidence_behavior(override.value, 0);
  } else if (override.field == "concrete_identity_behavior") {
    rule.concrete_identity_behavior =
        parse_concrete_identity_behavior(override.value, 0);
  } else if (override.field == "note") {
    rule.note = override.value;
  } else {
    throw std::invalid_argument("unsupported signal classification override "
                                "field: " +
                                override.field);
  }
}

}  // namespace

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
    const std::string stripped = signal_classification_detail::trim(lines[index]);
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
  metadata.manifest_source_path =
      std::filesystem::absolute(path).lexically_normal().string();
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
  metadata.effective_config_sha256 = metadata.manifest_sha256;
  metadata.manifest_format = "flat_tsv";
  metadata.manifest_source_path = base.metadata().manifest_source_path + ";" +
                                  extension.metadata().manifest_source_path;
  return SignalClassificationRuleset(std::move(metadata), std::move(merged));
}

SignalClassificationOverride parse_signal_classification_override(
    const std::string& specification) {
  const std::size_t equal = specification.find('=');
  const std::size_t dot = specification.rfind('.', equal);
  if (equal == std::string::npos || dot == std::string::npos || dot == 0 ||
      dot + 1 == equal || equal + 1 >= specification.size()) {
    throw std::invalid_argument(
        "classification override must be RULE_ID.FIELD=VALUE: " +
        specification);
  }
  return SignalClassificationOverride{specification.substr(0, dot),
                                      specification.substr(dot + 1,
                                                           equal - dot - 1),
                                      specification.substr(equal + 1)};
}

SignalClassificationRuleset override_signal_classification_ruleset(
    const SignalClassificationRuleset& base,
    const std::vector<SignalClassificationOverride>& overrides) {
  if (overrides.empty()) {
    return base;
  }
  std::vector<SignalClassificationRule> rules = base.rules();
  std::unordered_map<std::string, std::size_t> rule_indexes;
  for (std::size_t index = 0; index < rules.size(); ++index) {
    rule_indexes.emplace(rules[index].rule_id, index);
  }

  std::map<std::pair<std::string, std::string>, std::string> canonical;
  for (const SignalClassificationOverride& override : overrides) {
    const auto rule = rule_indexes.find(override.rule_id);
    if (rule == rule_indexes.end()) {
      throw std::invalid_argument(
          "classification override references unknown rule_id: " +
          override.rule_id);
    }
    const auto inserted = canonical.emplace(
        std::make_pair(override.rule_id, override.field), override.value);
    if (!inserted.second) {
      throw std::invalid_argument(
          "duplicate classification override: " + override.rule_id + "." +
          override.field);
    }
    apply_override(rules[rule->second], override);
  }

  std::string canonical_text;
  for (const auto& item : canonical) {
    if (!canonical_text.empty()) {
      canonical_text += ';';
    }
    canonical_text += item.first.first + "." + item.first.second + "=" +
                      item.second;
  }
  const std::string config_digest =
      sha256_hex(base.metadata().manifest_sha256 + "\n" + canonical_text);
  SignalClassificationPolicyMetadata metadata = base.metadata();
  metadata.policy_id += "+config-override:" + config_digest.substr(0, 16);
  metadata.policy_version += "+config:" + config_digest.substr(0, 16);
  metadata.effective_config_sha256 = config_digest;
  metadata.config_overrides = canonical_text;
  return SignalClassificationRuleset(std::move(metadata), std::move(rules));
}

}  // namespace traceloom
