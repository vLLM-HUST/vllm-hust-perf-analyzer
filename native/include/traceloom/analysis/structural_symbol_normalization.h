#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/ir/communication_op_table.h"
#include "traceloom/ir/structural_symbol.h"
#include "traceloom/ir/task_table.h"

namespace traceloom {

struct NativeIr;

enum class StructuralSymbolField {
  kSelected,
  kAnyIdentity,
  kOpName,
  kOpType,
  kTaskType,
  kComputeTaskType,
  kCommName,
  kLinkedTaskName,
  kLinkedTaskType,
};

enum class StructuralSymbolMatch {
  kExact,
  kExactAny,
  kContainsCaseInsensitive,
  kContainsAnyCaseInsensitive,
  kOpaqueNumericInstance,
  kAscendKernelBaseExactAny,
  kAscendDecoratedKernel,
};

struct StructuralSymbolNormalizationRule {
  std::int32_t priority = 0;
  std::string rule_id;
  std::string provider_scope;
  std::string source_domain;
  StructuralSymbolField field = StructuralSymbolField::kSelected;
  StructuralSymbolMatch match = StructuralSymbolMatch::kExact;
  std::string pattern;
  std::string structural_symbol;
  std::string note;
  std::string rule_origin;
  std::string rule_origin_sha256;
  std::size_t source_line = 0;
};

struct ResolvedStructuralSymbol {
  SymbolId structural_symbol_id;
  StructuralSymbolDecision decision;
};

class StructuralSymbolNormalizationRuleset {
 public:
  StructuralSymbolNormalizationRuleset() = default;
  StructuralSymbolNormalizationRuleset(
      std::string policy_id,
      std::string policy_version,
      std::string source_manifest,
      std::string manifest_sha256,
      std::vector<StructuralSymbolNormalizationRule> rules);

  // A policy with zero explicit aliases is still a valid replacement input:
  // it means identity-preserve every observed symbol.  Only an uninitialized
  // ruleset asks the caller to load TraceLoom's default manifest.
  bool empty() const noexcept { return policy_id_.empty(); }
  const std::string& policy_id() const noexcept { return policy_id_; }
  const std::string& policy_version() const noexcept { return policy_version_; }
  const std::string& source_manifest() const noexcept {
    return source_manifest_;
  }
  const std::string& manifest_sha256() const noexcept {
    return manifest_sha256_;
  }
  const std::vector<StructuralSymbolNormalizationRule>& rules() const {
    return rules_;
  }

  StructuralSymbolPolicySnapshot snapshot() const;

 private:
  std::string policy_id_;
  std::string policy_version_;
  std::string source_manifest_;
  std::string manifest_sha256_;
  std::vector<StructuralSymbolNormalizationRule> rules_;
};

const char* structural_symbol_source_name(StructuralSymbolSource source);
const char* structural_symbol_outcome_name(StructuralSymbolOutcome outcome);
const char* structural_symbol_reason_code(StructuralSymbolOutcome outcome);
const char* structural_symbol_field_name(StructuralSymbolField field);
const char* structural_symbol_match_name(StructuralSymbolMatch match);

StructuralSymbolNormalizationRuleset load_structural_symbol_ruleset(
    const std::string& path);
StructuralSymbolNormalizationRuleset load_default_structural_symbol_ruleset(
    const std::string& executable_path = "");
StructuralSymbolNormalizationRuleset extend_structural_symbol_ruleset(
    const StructuralSymbolNormalizationRuleset& base,
    const StructuralSymbolNormalizationRuleset& extension);

ResolvedStructuralSymbol normalize_task_structural_symbol(
    NativeIr& ir,
    const TaskRow& task,
    const StructuralSymbolNormalizationRuleset& ruleset);
ResolvedStructuralSymbol normalize_communication_structural_symbol(
    NativeIr& ir,
    const CommunicationOpRow& communication,
    const StructuralSymbolNormalizationRuleset& ruleset);
ResolvedStructuralSymbol preserve_structural_symbol(
    SymbolId observed_symbol_id,
    StructuralSymbolSource source);
ResolvedStructuralSymbol synthesize_structural_symbol(
    SymbolId structural_symbol_id,
    StructuralSymbolSource source =
        StructuralSymbolSource::kReplayCompositionSlot);

}  // namespace traceloom
