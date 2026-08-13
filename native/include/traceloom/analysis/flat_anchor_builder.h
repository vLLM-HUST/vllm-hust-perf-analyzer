#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "traceloom/analysis/event_reconciliation.h"
#include "traceloom/analysis/signal_classification_rules.h"
#include "traceloom/analysis/structural_symbol_normalization.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom {

struct FlatAnchorBuildConfig {
  std::vector<std::string> skipped_task_type_symbols;
  bool skip_tasks_covered_by_communication_ops = false;
  bool skip_tasks_covered_by_replay_units = false;
  bool skip_events_covered_by_replay_units = false;
  bool filter_auxiliary_task_anchors = false;
  SignalClassificationRuleset classification_rules;
  std::vector<SignalClassificationOverride> classification_overrides;
  StructuralSymbolNormalizationRuleset structural_symbol_rules;
  EventReconciliationRuleset event_reconciliation_rules;
};

struct FlatAnchorBuildStats {
  std::string projection_kind = "raw_event_bootstrap";
  std::string classification_policy_id;
  std::string classification_policy_version;
  std::string classification_manifest_sha256;
  std::string event_reconciliation_policy_id;
  std::string event_reconciliation_policy_version;
  std::string event_reconciliation_manifest_sha256;
  std::size_t event_reconciliation_decisions = 0;
  std::size_t reconciled_event_groups = 0;
  std::size_t reconciled_event_members = 0;
  std::size_t suppressed_duplicate_observations = 0;
  std::size_t device_event_anchors = 0;
  std::size_t communication_anchors = 0;
  std::size_t skipped_task_events = 0;
  std::size_t auxiliary_task_events = 0;
  std::size_t transparent_task_events = 0;
  std::size_t unknown_anchor_task_events = 0;
  // Backward-compatible name for unknown_anchor_task_events.
  std::size_t preserved_unclassified_task_events = 0;
  std::size_t tokens = 0;
};

// Builds the exact normalized policy input used by structural projection.
// Database materializers reuse this function so audit rows cannot drift from
// the executable classifier through a second provider-specific reconstruction.
SignalClassificationInput signal_classification_input_for_task(
    const NativeIr& ir,
    const TaskRow& task);

FlatAnchorBuildStats build_flat_anchors(
    NativeIr& ir,
    FlatAnchorBuildConfig config = FlatAnchorBuildConfig{});

}  // namespace traceloom
