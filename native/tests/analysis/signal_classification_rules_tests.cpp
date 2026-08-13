#include "traceloom/analysis/signal_classification_rules.h"
#include "traceloom/testing/test_util.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace {

std::filesystem::path temp_path(const std::string& suffix) {
  return std::filesystem::temp_directory_path() /
         ("traceloom-rules-test-" + suffix + ".tsv");
}

void write_manifest_preamble(std::ostream& out,
                             const std::string& policy_id) {
  out << "# manifest_schema=traceloom.evidence-role-policy/v1\n"
      << "# policy_id=" << policy_id << "\n"
      << "# policy_version=1\n"
      << "# provider_scopes=any\n"
      << "# fallback_identity_role=unknown_anchor\n"
      << "# fallback_cost_treatment=retained_for_attribution\n"
      << "# fallback_context_treatment=retained\n"
      << "# fallback_provenance_treatment=retained\n"
      << "# missing_evidence_behavior=continue_or_fallback\n"
      << "rule_id\tpriority\tprovider_scope\tsource_domain\tfield\tmatch\t"
         "pattern\trole\trequired_fields\tstructural_participation\t"
         "cost_treatment\tcontext_treatment\tprovenance_treatment\t"
         "missing_evidence_behavior\t"
         "concrete_identity_behavior\tnote\n";
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  const SignalClassificationRuleset defaults =
      load_default_signal_classification_ruleset();
  require(defaults.metadata().manifest_schema ==
          "traceloom.evidence-role-policy/v1");
  require(defaults.metadata().policy_id ==
          "traceloom.default.accelerator-task-projection");
  require(defaults.metadata().policy_version == "1");
  require(defaults.metadata().provider_scopes == "ascend,cuda,hygon");
  require(defaults.metadata().manifest_sha256.size() == 64);

  std::unordered_set<std::string> rule_ids;
  for (const SignalClassificationRule& rule : defaults.rules()) {
    require(!rule.rule_id.empty());
    require(rule_ids.insert(rule.rule_id).second);
    require(!rule.required_fields.empty());
    require(rule.provenance_treatment ==
            SignalProvenanceTreatment::kRetained);
  }

  const SignalClassificationDecision transparent = defaults.decide(
      {"task", "AI_CORE", "MatMulV2", "", false});
  require(transparent.matched_rule);
  require(transparent.role == SignalRole::kTransparent);
  require(transparent.structural_participation ==
          SignalStructuralParticipation::kExcluded);
  require(transparent.cost_treatment ==
          SignalCostTreatment::kRetainedForAttribution);
  require(transparent.context_treatment == SignalContextTreatment::kRetained);
  require(transparent.rule_id.find("ascend.transparent") == 0);

  const SignalClassificationDecision concrete_ai_core = defaults.decide(
      {"task", "AI_CORE", "FutureFusedKernel", "FutureFusedKernel", true});
  require(!concrete_ai_core.matched_rule);
  require(concrete_ai_core.role == SignalRole::kUnknownAnchor);
  require(concrete_ai_core.reason_code ==
          "unmatched_observation_unknown_first");

  const SignalClassificationDecision anchor = defaults.decide(
      {"task", "AICORE_KERNEL", "MatMulV2", "MatMulV2", true});
  require(anchor.matched_rule);
  require(anchor.role == SignalRole::kAnchor);
  require(anchor.structural_participation ==
          SignalStructuralParticipation::kIdentity);
  require(anchor.required_fields == "blob");

  const SignalClassificationDecision cuda_unknown = defaults.decide(
      {"task", "CUDA_KERNEL", "unknown_kernel", "unknown_kernel", true});
  require(cuda_unknown.matched_rule);
  require(cuda_unknown.role == SignalRole::kAnchor);
  require(cuda_unknown.provider_scope == "cuda");

  const SignalClassificationDecision explicit_operator = defaults.decide(
      {"task", "AI_CORE", "DispatchFFNCombineBF16",
       "DispatchFFNCombineBF16", true});
  require(explicit_operator.matched_rule);
  require(explicit_operator.role == SignalRole::kAnchor);
  require(explicit_operator.required_fields == "operator");

  const SignalClassificationDecision auxiliary = defaults.decide(
      {"task", "EVENT_WAIT", "event_wait", "", false});
  require(auxiliary.matched_rule);
  require(auxiliary.role == SignalRole::kAuxiliary);
  require(auxiliary.cost_treatment ==
          SignalCostTreatment::kRetainedForAttribution);

  const SignalClassificationDecision wrong_provider = defaults.decide(
      {"task", "EVENT_WAIT", "event_wait", "", false, "cuda"});
  require(!wrong_provider.matched_rule);
  require(wrong_provider.role == SignalRole::kUnknownAnchor);
  require(wrong_provider.provider_scope == "cuda");
  const SignalClassificationDecision matching_provider = defaults.decide(
      {"task", "EVENT_WAIT", "event_wait", "", false, "ascend"});
  require(matching_provider.matched_rule);
  require(matching_provider.provider_scope == "ascend");

  const SignalClassificationDecision unknown = defaults.decide(
      {"task", "UNKNOWN", "future_fused_kernel", "FutureFusedKernel", true});
  require(!unknown.matched_rule);
  require(unknown.role == SignalRole::kUnknownAnchor);
  require(unknown.structural_participation ==
          SignalStructuralParticipation::kIdentity);
  require(unknown.rule_id == "fallback.unknown_observation");
  require(unknown.reason_code == "unmatched_observation_unknown_first");
  require(unknown.policy_id == defaults.metadata().policy_id);
  require(!defaults.classify({"task", "UNKNOWN", "future_fused_kernel",
                              "FutureFusedKernel", true})
               .has_value());

  const SignalClassificationDecision unidentified = defaults.decide(
      {"task", "UNKNOWN", "unknown", "", false});
  require(!unidentified.matched_rule);
  require(unidentified.role == SignalRole::kUnknownAnchor);
  require(unidentified.structural_participation ==
          SignalStructuralParticipation::kIdentity);
  require(unidentified.rule_id == "fallback.unknown_observation");
  require(unidentified.reason_code == "unmatched_observation_unknown_first");

  require(std::string(signal_role_name(SignalRole::kUnknownAnchor)) ==
          "unknown_anchor");
  require(std::string(signal_structural_participation_name(
              SignalStructuralParticipation::kIdentity)) == "identity");
  require(std::string(signal_cost_treatment_name(
              SignalCostTreatment::kRetainedForAttribution)) ==
          "retained_for_attribution");
  require(std::string(signal_context_treatment_name(
              SignalContextTreatment::kRetained)) == "retained");

  const std::filesystem::path extension_path = temp_path("extension");
  {
    std::ofstream out(extension_path);
    write_manifest_preamble(out, "test.custom-extension");
    out << "test.anchor.custom\t200\tany\ttask\tblob\tcontains\t"
           "custom_kernel\tanchor\tblob\tidentity\tretained_for_attribution\tretained\t"
           "retained\tcontinue_or_fallback\tapply\ttest override\n"
        << "test.aux.capability\t300\tany\ttask\tblob\tcontains\t"
           "capability_kernel\tauxiliary\tblob,operator\texcluded\t"
           "retained_for_attribution\tretained\tretained\t"
           "continue_or_fallback\tapply\trequires operator evidence\n"
        << "test.anchor.capability\t200\tany\ttask\tblob\tcontains\t"
           "capability_kernel\tanchor\tblob\tidentity\t"
           "retained_for_attribution\tretained\tretained\t"
           "continue_or_fallback\tapply\tlower capability fallback\n";
  }
  const SignalClassificationRuleset extension =
      load_signal_classification_ruleset(extension_path.string());
  const SignalClassificationRuleset merged =
      extend_signal_classification_ruleset(defaults, extension);
  const SignalClassificationDecision custom = merged.decide(
      {"task", "UNKNOWN", "custom_kernel", "custom_kernel", true});
  require(custom.matched_rule);
  require(custom.role == SignalRole::kAnchor);
  require(custom.rule_id == "test.anchor.custom");
  require(merged.metadata().policy_id.find("test.custom-extension") !=
          std::string::npos);
  require(merged.metadata().manifest_sha256.size() == 64);
  const SignalClassificationDecision capability_present = merged.decide(
      {"task", "UNKNOWN", "capability_kernel", "capability_kernel", true});
  require(capability_present.rule_id == "test.aux.capability");
  const SignalClassificationDecision capability_missing = merged.decide(
      {"task", "UNKNOWN", "capability_kernel", "", true});
  require(capability_missing.rule_id == "test.anchor.capability");
  std::filesystem::remove(extension_path);

  // Legacy seven-column extension files remain accepted, but receive a
  // content-addressed policy version and deterministic synthetic rule IDs.
  const std::filesystem::path legacy_path = temp_path("legacy");
  {
    std::ofstream out(legacy_path);
    out << "priority\tsource_domain\tfield\tmatch\tpattern\trole\tnote\n"
        << "200\ttask\tblob\tcontains\tlegacy_kernel\tanchor\tlegacy\n";
  }
  const SignalClassificationRuleset legacy =
      load_signal_classification_ruleset(legacy_path.string());
  require(legacy.metadata().policy_id == "traceloom.legacy-signal-rules");
  require(legacy.metadata().policy_version.find("1+sha256:") == 0);
  require(legacy.rules().front().rule_id.find("legacy.") == 0);
  std::filesystem::remove(legacy_path);

  const std::filesystem::path invalid_field = temp_path("invalid-field");
  {
    std::ofstream out(invalid_field);
    out << "priority\tsource_domain\tfield\tmatch\tpattern\trole\tnote\n"
        << "10\ttask\tunknown\texact\tx\tanchor\tbad field\n";
  }
  bool rejected = false;
  try {
    (void)load_signal_classification_ruleset(invalid_field.string());
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected);
  std::filesystem::remove(invalid_field);

  const std::filesystem::path missing_metadata = temp_path("missing-metadata");
  {
    std::ofstream out(missing_metadata);
    out << "rule_id\tpriority\tprovider_scope\tsource_domain\tfield\tmatch\t"
           "pattern\trole\trequired_fields\tstructural_participation\t"
           "cost_treatment\tcontext_treatment\tprovenance_treatment\t"
         "missing_evidence_behavior\t"
           "concrete_identity_behavior\tnote\n"
        << "test.anchor\t1\tany\ttask\tblob\tcontains\tx\tanchor\tblob\t"
           "identity\tretained_for_attribution\tretained\tretained\tcontinue_or_fallback\t"
           "apply\tmissing metadata\n";
  }
  rejected = false;
  try {
    (void)load_signal_classification_ruleset(missing_metadata.string());
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected);
  std::filesystem::remove(missing_metadata);

  const std::filesystem::path conflicting = temp_path("conflicting");
  {
    std::ofstream out(conflicting);
    write_manifest_preamble(out, "test.conflict");
    out << "test.anchor.x\t10\tany\ttask\tblob\texact\tx\tanchor\tblob\t"
           "identity\tretained_for_attribution\tretained\tretained\tcontinue_or_fallback\t"
           "apply\tfirst\n"
        << "test.aux.x\t10\tany\ttask\tblob\texact\tx\tauxiliary\tblob\t"
           "excluded\tretained_for_attribution\tretained\tretained\t"
           "continue_or_fallback\tapply\tconflict\n";
  }
  rejected = false;
  try {
    (void)load_signal_classification_ruleset(conflicting.string());
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected);
  std::filesystem::remove(conflicting);

  const std::filesystem::path omitted_required = temp_path("required-field");
  {
    std::ofstream out(omitted_required);
    write_manifest_preamble(out, "test.required-field");
    out << "test.anchor.x\t10\tany\ttask\tblob\texact\tx\tanchor\toperator\t"
           "identity\tretained_for_attribution\tretained\tretained\tcontinue_or_fallback\t"
           "apply\twrong required field\n";
  }
  rejected = false;
  try {
    (void)load_signal_classification_ruleset(omitted_required.string());
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected);
  std::filesystem::remove(omitted_required);

  return 0;
}
