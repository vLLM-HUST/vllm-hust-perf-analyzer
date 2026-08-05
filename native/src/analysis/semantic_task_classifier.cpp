#include "traceloom/analysis/semantic_task_classifier.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>

namespace traceloom {
namespace {

std::string symbol_text(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

SymbolId choose_task_symbol(const TaskRow& task) {
  if (task.op_type_symbol_id.valid()) {
    return task.op_type_symbol_id;
  }
  if (task.op_name_symbol_id.valid()) {
    return task.op_name_symbol_id;
  }
  if (task.comm_name_symbol_id.valid()) {
    return task.comm_name_symbol_id;
  }
  return task.task_type_symbol_id;
}

}  // namespace

SemanticTaskClassificationInput build_semantic_classification_input(
    const NativeIr& ir, const TaskRow& task) {
  // Corrupted task rows must not be silently classified with partial input.
  if (!task.trace_event_id.valid() ||
      task.trace_event_id.value() >= ir.trace_events.size()) {
    throw std::invalid_argument("TaskRow trace_event_id is out of range");
  }
  std::string blob;
  blob += symbol_text(ir, choose_task_symbol(task));
  blob += " ";
  blob += symbol_text(ir, task.op_name_symbol_id);
  blob += " ";
  blob += symbol_text(ir, task.op_type_symbol_id);
  blob += " ";
  blob += symbol_text(ir, task.compute_task_type_symbol_id);
  blob += " ";
  blob += symbol_text(ir, task.comm_name_symbol_id);
  blob += " ";
  blob += symbol_text(ir, task.task_type_symbol_id);
  if (task.trace_event_id.valid() &&
      task.trace_event_id.value() < ir.trace_events.size()) {
    blob += " ";
    blob += symbol_text(
        ir, ir.trace_events.row(task.trace_event_id).raw_name_symbol_id);
  }
  return SemanticTaskClassificationInput{
      "task", symbol_text(ir, task.task_type_symbol_id), std::move(blob),
      symbol_text(ir, choose_task_symbol(task))};
}

SemanticTaskClassificationResult classify_semantic_tasks(
    const NativeIr& ir, const SemanticTaskRuleset& ruleset) {
  SemanticTaskClassificationResult result;
  result.semantic_rules_version = ruleset.version();
  result.semantic_rules_sha256 = ruleset.sha256();
  result.rows.reserve(ir.tasks.size());
  for (const TaskRow& task : ir.tasks.rows()) {
    const SemanticTaskMatch match =
        ruleset.classify(build_semantic_classification_input(ir, task));
    SemanticTaskClassificationRow row;
    row.task_id = task.id;
    row.trace_event_id = task.trace_event_id;
    row.role = match.role;
    row.matched_rule_id = match.matched_rule_id;
    row.matched_field = match.matched_field;
    row.matched_kind = match.matched_kind;
    result.rows.push_back(std::move(row));
  }
  return result;
}

SemanticOperatorCoverageSummary summarize_semantic_operator_coverage(
    const NativeIr& ir,
    const SemanticTaskClassificationResult& classification) {
  if (classification.rows.size() != ir.tasks.size()) {
    throw std::invalid_argument(
        "semantic classification row count does not match TaskTable");
  }
  std::unordered_set<TaskId::value_type> graph_body_task_ids;
  graph_body_task_ids.reserve(ir.graph_launch_body_members.size());
  for (const GraphLaunchBodyMemberRow& member :
       ir.graph_launch_body_members.rows()) {
    graph_body_task_ids.insert(member.task_id.value());
  }

  SemanticOperatorCoverageSummary summary;
  summary.task_count = ir.tasks.size();
  using CoverageKey =
      std::tuple<std::string, std::string, std::string, std::string>;
  std::map<CoverageKey, UnregisteredOperatorSummaryRow> aggregates;
  for (std::size_t index = 0; index < ir.tasks.size(); ++index) {
    const SemanticTaskClassificationRow& classified =
        classification.rows[index];
    if (classified.role == SemanticTaskRole::kUnknown) {
      ++summary.unknown_task_count;
    }
    const TaskRow& task = ir.tasks.row(TaskId(index));
    SymbolId operator_symbol = choose_task_symbol(task);
    const bool has_operator = task.op_type_symbol_id.valid() ||
                              task.op_name_symbol_id.valid() ||
                              task.comm_name_symbol_id.valid();
    if (!has_operator || !operator_symbol.valid()) {
      continue;
    }
    const bool exact_operator_registration =
        classified.matched_field.has_value() &&
        *classified.matched_field == SignalMatchField::kOperator &&
        classified.matched_kind.has_value() &&
        *classified.matched_kind == SignalMatchKind::kExact;
    if (exact_operator_registration) {
      continue;
    }
    ++summary.unregistered_operator_occurrence_count;
    const std::string operator_name = symbol_text(ir, operator_symbol);
    const std::string task_type =
        symbol_text(ir, task.task_type_symbol_id);
    const std::string semantic_role(
        semantic_task_role_name(classified.role));
    const std::string matched_rule_id =
        classified.matched_rule_id.value_or("");
    UnregisteredOperatorSummaryRow& row =
        aggregates[{operator_name, task_type, semantic_role,
                    matched_rule_id}];
    row.operator_name = operator_name;
    row.task_type = task_type;
    row.semantic_role = semantic_role;
    row.matched_rule_id = matched_rule_id;
    const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
    ++row.occurrence_count;
    row.total_duration_ns += static_cast<std::uint64_t>(
        std::max<std::int64_t>(0, event.end_ns - event.start_ns));
    if (graph_body_task_ids.find(task.id.value()) !=
        graph_body_task_ids.end()) {
      ++row.graph_body_member_count;
    }
  }
  summary.unregistered_operators.reserve(aggregates.size());
  for (auto& item : aggregates) {
    summary.unregistered_operators.push_back(std::move(item.second));
  }
  std::stable_sort(
      summary.unregistered_operators.begin(),
      summary.unregistered_operators.end(),
      [](const UnregisteredOperatorSummaryRow& lhs,
         const UnregisteredOperatorSummaryRow& rhs) {
        if (lhs.occurrence_count != rhs.occurrence_count) {
          return lhs.occurrence_count > rhs.occurrence_count;
        }
        if (lhs.graph_body_member_count != rhs.graph_body_member_count) {
          return lhs.graph_body_member_count > rhs.graph_body_member_count;
        }
        return std::tie(lhs.operator_name, lhs.task_type) <
               std::tie(rhs.operator_name, rhs.task_type);
      });
  return summary;
}

}  // namespace traceloom
