#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

enum class EventReconciliationStatus {
  kReconciled,
  kIndependent,
  kAmbiguous,
  kConflict,
};

enum class EventReconciliationMemberRole {
  kTimingEnvelope,
  kSemanticDetail,
  kProviderDetail,
  kIndependentCandidate,
  kConflictingCandidate,
};

struct EventReconciliationRuleSnapshot {
  std::string rule_id;
  std::int32_t priority = 0;
  std::string provider_scope;
  std::string source_domain;
  std::string task_type;
  std::int64_t generic_context_id = -1;
  std::int64_t concrete_context_id = -1;
  double min_contained_fraction = 1.0;
  std::string task_op_type;
  std::string communication_op_name_prefix;
  std::string identity_policy;
  std::string rule_origin;
  std::string rule_origin_sha256;
  std::uint64_t source_line = 0;
  std::string note;
};

struct EventReconciliationPolicySnapshot {
  std::string manifest_schema;
  std::string policy_id;
  std::string policy_version;
  std::string source_manifest;
  std::string manifest_sha256;
  std::string unmatched_behavior = "independent";
  std::vector<EventReconciliationRuleSnapshot> rules;
};

struct EventReconciliationDecisionRow {
  EventReconciliationDecisionId id;
  std::string rule_id;
  EventReconciliationStatus status =
      EventReconciliationStatus::kIndependent;
  std::string reason_code;
  TaskId canonical_task_id;
  TraceEventId canonical_event_id;
  TraceEventId envelope_event_id;
  std::int64_t canonical_start_ns = 0;
  std::int64_t canonical_end_ns = 0;
  double contained_fraction = 0.0;
};

struct EventReconciliationMemberRow {
  EventReconciliationDecisionId decision_id;
  TaskId task_id;
  TraceEventId event_id;
  EventReconciliationMemberRole role =
      EventReconciliationMemberRole::kIndependentCandidate;
  bool contributes_timing = false;
  bool contributes_symbol = false;
  bool contributes_cost = false;
  bool retained_as_normalized_evidence = true;
  CommunicationOpId communication_op_id;
};

struct EventReconciliationState {
  EventReconciliationPolicySnapshot policy;
  std::vector<EventReconciliationDecisionRow> decisions;
  std::vector<EventReconciliationMemberRow> members;

  bool initialized() const noexcept { return !policy.policy_id.empty(); }
};

const char* event_reconciliation_status_name(EventReconciliationStatus status);
const char* event_reconciliation_member_role_name(
    EventReconciliationMemberRole role);

}  // namespace traceloom
