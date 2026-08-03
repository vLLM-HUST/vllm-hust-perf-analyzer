#pragma once

#include <optional>
#include <string>
#include <vector>

#include "traceloom/analysis/idle_evidence_semantic_rules.h"
#include "traceloom/core/ids.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom {

// Derives a classification input for one TaskRow by joining the raw symbol
// fields (op name, op type, compute task type, comm name, task type, raw
// event label) into the matchable `blob`.
SemanticTaskClassificationInput build_semantic_classification_input(
    const NativeIr& ir, const TaskRow& task);

// Derived per-task classification. TaskRow remains the raw imported fact;
// this row is the conclusion produced by a specific ruleset version.
struct SemanticTaskClassificationRow {
  TaskId task_id;
  TraceEventId trace_event_id;
  SemanticTaskRole role = SemanticTaskRole::kUnknown;
  std::optional<std::string> matched_rule_id;
};

struct SemanticTaskClassificationResult {
  std::vector<SemanticTaskClassificationRow> rows;
  std::string semantic_rules_version;
  std::string semantic_rules_sha256;
};

// Classifies every TaskRow in the IR. Row order follows TaskTable order so
// index i corresponds to task i.
SemanticTaskClassificationResult classify_semantic_tasks(
    const NativeIr& ir, const SemanticTaskRuleset& ruleset);

}  // namespace traceloom
