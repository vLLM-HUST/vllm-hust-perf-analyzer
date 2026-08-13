#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace traceloom {

// Evidence roles describe participation in structural identity recovery. They
// do not classify workload phases or claim that omitted observations are
// physically irrelevant.
enum class SignalRole {
  kAnchor,
  kAuxiliary,
  kTransparent,
  kUnknownAnchor,
};

enum class SignalMatchField {
  kTaskType,
  kBlob,
  kOperator,
};

enum class SignalMatchKind {
  kExact,
  kContains,
};

enum class SignalStructuralParticipation {
  kIdentity,
  kExcluded,
};

enum class SignalCostTreatment {
  kRetainedForAttribution,
  kRetainedAsEvidence,
};

enum class SignalContextTreatment {
  kRetained,
};

enum class SignalProvenanceTreatment {
  kRetained,
};

enum class SignalMissingEvidenceBehavior {
  kContinueOrFallback,
};

enum class SignalConcreteIdentityBehavior {
  kApply,
  kDeferToIdentityRules,
};

struct SignalClassificationPolicyMetadata {
  std::string manifest_schema;
  std::string policy_id;
  std::string policy_version;
  std::string provider_scopes;
  SignalRole fallback_identity_role = SignalRole::kUnknownAnchor;
  SignalCostTreatment fallback_cost_treatment =
      SignalCostTreatment::kRetainedForAttribution;
  SignalContextTreatment fallback_context_treatment =
      SignalContextTreatment::kRetained;
  SignalProvenanceTreatment fallback_provenance_treatment =
      SignalProvenanceTreatment::kRetained;
  SignalMissingEvidenceBehavior missing_evidence_behavior =
      SignalMissingEvidenceBehavior::kContinueOrFallback;
  std::string manifest_sha256;
};

struct SignalClassificationRule {
  std::string rule_id;
  std::int32_t priority = 0;
  std::string provider_scope;
  std::string source_domain;
  SignalMatchField field = SignalMatchField::kBlob;
  SignalMatchKind match = SignalMatchKind::kContains;
  std::string pattern;
  SignalRole role = SignalRole::kAuxiliary;
  std::string required_fields;
  SignalStructuralParticipation structural_participation =
      SignalStructuralParticipation::kExcluded;
  SignalCostTreatment cost_treatment =
      SignalCostTreatment::kRetainedForAttribution;
  SignalContextTreatment context_treatment = SignalContextTreatment::kRetained;
  SignalProvenanceTreatment provenance_treatment =
      SignalProvenanceTreatment::kRetained;
  SignalMissingEvidenceBehavior missing_evidence_behavior =
      SignalMissingEvidenceBehavior::kContinueOrFallback;
  SignalConcreteIdentityBehavior concrete_identity_behavior =
      SignalConcreteIdentityBehavior::kApply;
  std::string note;
  std::size_t source_line = 0;
  std::size_t declaration_order = 0;
};

struct SignalClassificationInput {
  std::string source_domain;
  std::string task_type;
  std::string blob;
  std::string operator_name;
  bool has_concrete_identity = true;
  // "any" keeps fixture and legacy callers provider-neutral. Production
  // adapters pass a concrete scope inferred from the source reference.
  std::string provider_scope = "any";
};

struct SignalClassificationDecision {
  SignalRole role = SignalRole::kUnknownAnchor;
  SignalStructuralParticipation structural_participation =
      SignalStructuralParticipation::kIdentity;
  SignalCostTreatment cost_treatment =
      SignalCostTreatment::kRetainedForAttribution;
  SignalContextTreatment context_treatment = SignalContextTreatment::kRetained;
  SignalProvenanceTreatment provenance_treatment =
      SignalProvenanceTreatment::kRetained;
  SignalMissingEvidenceBehavior missing_evidence_behavior =
      SignalMissingEvidenceBehavior::kContinueOrFallback;
  SignalConcreteIdentityBehavior concrete_identity_behavior =
      SignalConcreteIdentityBehavior::kApply;
  bool matched_rule = false;
  std::string policy_id;
  std::string policy_version;
  std::string manifest_sha256;
  std::string rule_id;
  std::string provider_scope;
  std::string required_fields;
  std::string reason_code;
  std::int32_t priority = 0;
  std::size_t declaration_order = 0;
};

class SignalClassificationRuleset {
 public:
  SignalClassificationRuleset() = default;
  SignalClassificationRuleset(SignalClassificationPolicyMetadata metadata,
                              std::vector<SignalClassificationRule> rules);

  const SignalClassificationPolicyMetadata& metadata() const {
    return metadata_;
  }
  const std::vector<SignalClassificationRule>& rules() const { return rules_; }

  SignalClassificationDecision decide(
      const SignalClassificationInput& input) const;

  // Compatibility helper for callers that only care about explicit rules.
  // Unlike decide(), an unmatched observation returns nullopt here.
  std::optional<SignalRole> classify(
      const SignalClassificationInput& input) const;

 private:
  SignalClassificationPolicyMetadata metadata_;
  std::vector<SignalClassificationRule> rules_;
};

const char* signal_role_name(SignalRole role) noexcept;
const char* signal_structural_participation_name(
    SignalStructuralParticipation participation) noexcept;
const char* signal_cost_treatment_name(SignalCostTreatment treatment) noexcept;
const char* signal_context_treatment_name(
    SignalContextTreatment treatment) noexcept;
const char* signal_provenance_treatment_name(
    SignalProvenanceTreatment treatment) noexcept;
const char* signal_missing_evidence_behavior_name(
    SignalMissingEvidenceBehavior behavior) noexcept;
const char* signal_concrete_identity_behavior_name(
    SignalConcreteIdentityBehavior behavior) noexcept;

SignalClassificationRuleset load_signal_classification_ruleset(
    const std::string& path);
SignalClassificationRuleset load_default_signal_classification_ruleset(
    const std::string& executable_path = "");
SignalClassificationRuleset extend_signal_classification_ruleset(
    const SignalClassificationRuleset& base,
    const SignalClassificationRuleset& extension);

}  // namespace traceloom
