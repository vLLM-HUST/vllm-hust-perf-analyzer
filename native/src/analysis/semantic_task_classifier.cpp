#include "traceloom/analysis/semantic_task_classifier.h"

#include <stdexcept>
#include <string>

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
      "task", symbol_text(ir, task.task_type_symbol_id), std::move(blob)};
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
    result.rows.push_back(std::move(row));
  }
  return result;
}

}  // namespace traceloom
