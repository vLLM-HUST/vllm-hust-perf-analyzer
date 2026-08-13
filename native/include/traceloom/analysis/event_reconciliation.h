#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/ir/event_reconciliation.h"

namespace traceloom {

struct NativeIr;

struct EventReconciliationRule {
  std::int32_t priority = 0;
  std::string rule_id;
  std::string provider_scope;
  std::string source_domain;
  std::string task_type;
  std::int64_t generic_context_id = -1;
  std::int64_t concrete_context_id = -1;
  double min_contained_fraction = 1.0;
  std::string note;
  std::string rule_origin;
  std::string rule_origin_sha256;
  std::uint64_t source_line = 0;
};

class EventReconciliationRuleset {
 public:
  EventReconciliationRuleset() = default;
  EventReconciliationRuleset(
      std::string manifest_schema,
      std::string policy_id,
      std::string policy_version,
      std::string source_manifest,
      std::string manifest_sha256,
      std::string unmatched_behavior,
      std::vector<EventReconciliationRule> rules);

  bool empty() const noexcept { return policy_id_.empty(); }
  const std::string& manifest_schema() const noexcept {
    return manifest_schema_;
  }
  const std::string& policy_id() const noexcept { return policy_id_; }
  const std::string& policy_version() const noexcept {
    return policy_version_;
  }
  const std::string& source_manifest() const noexcept {
    return source_manifest_;
  }
  const std::string& manifest_sha256() const noexcept {
    return manifest_sha256_;
  }
  const std::string& unmatched_behavior() const noexcept {
    return unmatched_behavior_;
  }
  const std::vector<EventReconciliationRule>& rules() const noexcept {
    return rules_;
  }
  EventReconciliationPolicySnapshot snapshot() const;

 private:
  std::string manifest_schema_;
  std::string policy_id_;
  std::string policy_version_;
  std::string source_manifest_;
  std::string manifest_sha256_;
  std::string unmatched_behavior_ = "independent";
  std::vector<EventReconciliationRule> rules_;
};

EventReconciliationRuleset load_event_reconciliation_ruleset(
    const std::string& path);
EventReconciliationRuleset load_default_event_reconciliation_ruleset(
    const std::string& executable_path = "");

// Overlay rules replace base rules with the same stable rule_id and add new
// rules otherwise.  The returned policy has a composite digest and retains
// each rule's source manifest/hash for database audit.
EventReconciliationRuleset overlay_event_reconciliation_ruleset(
    const EventReconciliationRuleset& base,
    const EventReconciliationRuleset& overlay);

EventReconciliationState reconcile_event_observations(
    const NativeIr& ir,
    const EventReconciliationRuleset& ruleset);

}  // namespace traceloom
