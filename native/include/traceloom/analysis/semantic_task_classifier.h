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
  std::optional<SignalMatchField> matched_field;
  std::optional<SignalMatchKind> matched_kind;
};

struct SemanticTaskClassificationResult {
  std::vector<SemanticTaskClassificationRow> rows;
  std::string semantic_rules_version;
  std::string semantic_rules_sha256;
};

struct UnregisteredOperatorSummaryRow {
  std::string operator_name;
  std::string task_type;
  std::string semantic_role;
  std::string matched_rule_id;
  std::uint64_t occurrence_count = 0;
  std::uint64_t total_duration_ns = 0;
  std::uint64_t graph_body_member_count = 0;
};

struct SemanticOperatorCoverageSummary {
  std::uint64_t task_count = 0;
  std::uint64_t unknown_task_count = 0;
  std::uint64_t unregistered_operator_occurrence_count = 0;
  std::vector<UnregisteredOperatorSummaryRow> unregistered_operators;
};

// Classifies every TaskRow in the IR. Row order follows TaskTable order so
// index i corresponds to task i.
SemanticTaskClassificationResult classify_semantic_tasks(
    const NativeIr& ir, const SemanticTaskRuleset& ruleset);

// Summarizes operator-bearing tasks that no exact operator-identity rule
// registered. A fuzzy family match may still supply a useful semantic role,
// but it does not make a previously unseen raw operator name disappear from
// the audit. graph_body_member_count makes such work inside captured graph
// bodies explicit rather than letting replay protection hide it.
SemanticOperatorCoverageSummary summarize_semantic_operator_coverage(
    const NativeIr& ir,
    const SemanticTaskClassificationResult& classification);

}  // namespace traceloom
