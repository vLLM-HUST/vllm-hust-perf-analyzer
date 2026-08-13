#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

enum class StructuralSymbolSource {
  kUnknown,
  kTraceEventRawName,
  kTaskOpType,
  kTaskOpName,
  kTaskCommName,
  kTaskType,
  kTaskComputeType,
  kCommunicationOpName,
  kCommunicationOpType,
  kCommunicationLinkedTaskName,
  kCommunicationLinkedTaskType,
  kReplayCompositionSlot,
};

enum class StructuralSymbolOutcome {
  kUnsupported,
  kIdentity,
  kCanonicalized,
  kConflict,
  kSynthetic,
};

struct StructuralSymbolDecision {
  SymbolId observed_symbol_id;
  StructuralSymbolSource observed_source = StructuralSymbolSource::kUnknown;
  std::string rule_id = "fallback.missing-observed-symbol";
  std::string candidate_rule_ids;
  StructuralSymbolOutcome outcome = StructuralSymbolOutcome::kUnsupported;
};

struct StructuralSymbolRuleSnapshot {
  std::int32_t priority = 0;
  std::string rule_id;
  std::string provider_scope;
  std::string source_domain;
  std::string field;
  std::string match_mode;
  std::string pattern;
  std::string structural_symbol;
  std::string required_fields;
  std::string note;
  std::string rule_origin;
  std::string rule_origin_sha256;
  std::size_t source_line = 0;
};

struct StructuralSymbolPolicySnapshot {
  std::string policy_id;
  std::string policy_version;
  std::string policy_kind;
  std::string source_manifest;
  std::string manifest_sha256;
  std::vector<StructuralSymbolRuleSnapshot> rules;

  bool empty() const noexcept { return policy_id.empty(); }
};

}  // namespace traceloom
