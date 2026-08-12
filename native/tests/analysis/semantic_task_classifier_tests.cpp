#include "traceloom/analysis/semantic_task_classifier.h"
#include "traceloom/analysis/idle_evidence_semantic_rules.h"
#include "traceloom/testing/test_util.h"

#include <algorithm>
#include <string>

namespace {

traceloom::NativeIr make_ir() {
  using namespace traceloom;

  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "memory", "TASK", 0);
  const SymbolId matmul = ir.symbols.intern("MatMulV2");
  const SymbolId event_wait = ir.symbols.intern("EVENT_WAIT");
  const SymbolId capture_wait = ir.symbols.intern("CAPTURE_WAIT");
  const SymbolId unknown_task = ir.symbols.intern("UNKNOWN_FUTURE_TASK");
  const SymbolId aicore = ir.symbols.intern("AI_CORE");
  const SymbolId label = ir.symbols.intern("raw_label");
  const SymbolId future_op = ir.symbols.intern("FutureFusedKernelV1");
  const SymbolId compute_task_only =
      ir.symbols.intern("ComputeTaskOnlyFutureKernel");

  const TraceEventId e0 = ir.trace_events.append(source, 1, 0, 0, 0, 10, label);
  const TraceEventId e1 = ir.trace_events.append(source, 2, 0, 0, 10, 20, label);
  const TraceEventId e2 = ir.trace_events.append(source, 3, 0, 0, 20, 30, label);
  const TraceEventId e3 = ir.trace_events.append(source, 4, 0, 0, 30, 40, label);
  const TraceEventId e4 = ir.trace_events.append(source, 5, 0, 0, 40, 55, label);
  const TraceEventId e5 = ir.trace_events.append(source, 6, 0, 0, 55, 75, label);

  // AI_CORE with MatMul metadata -> productive_compute via blob rule.
  ir.tasks.append(source, e0, 1, 1001, -1, aicore, matmul, matmul,
                  SymbolId::invalid(), SymbolId::invalid());
  // Pure EVENT_WAIT -> visible_wait via exact task_type rule.
  ir.tasks.append(source, e1, 2, 1002, -1, event_wait, SymbolId::invalid(),
                  SymbolId::invalid(), SymbolId::invalid(),
                  SymbolId::invalid());
  // CAPTURE_WAIT -> capture_control.
  ir.tasks.append(source, e2, 3, 1003, -1, capture_wait, SymbolId::invalid(),
                  SymbolId::invalid(), SymbolId::invalid(),
                  SymbolId::invalid());
  // Unknown task type with no matching keywords -> unknown.
  ir.tasks.append(source, e3, 4, 1004, -1, unknown_task, SymbolId::invalid(),
                  SymbolId::invalid(), SymbolId::invalid(),
                  SymbolId::invalid());
  ir.tasks.append(source, e4, 5, 1005, -1, aicore, SymbolId::invalid(),
                  future_op, SymbolId::invalid(), SymbolId::invalid());
  // CANN variants may expose only compute_task_type as concrete operator
  // identity. It must be selected and audited instead of falling back to the
  // generic AI_CORE task type.
  ir.tasks.append(source, e5, 6, 1006, -1, aicore, SymbolId::invalid(),
                  SymbolId::invalid(), compute_task_only, SymbolId::invalid());
  ir.graph_launch_body_members.append(
      GraphLaunchBodyId(0), TaskId(4), 0, 0,
      GraphLaunchBodyMemberRow::Kind::kCompute);
  return ir;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  const NativeIr ir = make_ir();
  const SemanticTaskRuleset ruleset =
      load_default_idle_evidence_semantic_ruleset();
  const SemanticTaskClassificationResult result =
      classify_semantic_tasks(ir, ruleset);

  require(result.rows.size() == ir.tasks.size(),
          "one classification row per task");
  require(result.semantic_rules_version == "idle-evidence-semantic-v1",
          "result carries ruleset version");
  require(result.semantic_rules_sha256 == ruleset.sha256(),
          "result carries ruleset sha256");

  require(result.rows[0].task_id == ir.tasks.row(TaskId(0)).id,
          "row 0 task id linkage");
  require(result.rows[0].role == SemanticTaskRole::kProductiveCompute,
          "AI_CORE+MatMul -> productive_compute");
  require(result.rows[0].matched_rule_id.has_value() &&
              *result.rows[0].matched_rule_id == "compute.matmul",
          "AI_CORE+MatMul matched compute.matmul");

  require(result.rows[1].role == SemanticTaskRole::kVisibleWait,
          "EVENT_WAIT -> visible_wait");
  require(result.rows[1].matched_rule_id.has_value() &&
              *result.rows[1].matched_rule_id == "wait.event_wait",
          "EVENT_WAIT matched wait.event_wait");

  require(result.rows[2].role == SemanticTaskRole::kCaptureControl,
          "CAPTURE_WAIT -> capture_control");
  require(result.rows[2].matched_rule_id.has_value() &&
              *result.rows[2].matched_rule_id == "capture.capture_wait",
          "CAPTURE_WAIT matched capture.capture_wait");

  require(result.rows[3].role == SemanticTaskRole::kUnknown,
          "unknown task -> unknown");
  require(!result.rows[3].matched_rule_id.has_value(),
          "unknown task has no matched rule");
  require(result.rows[4].role == SemanticTaskRole::kUnknown &&
              !result.rows[4].matched_rule_id.has_value(),
          "unknown operator remains unknown");

  const SemanticOperatorCoverageSummary coverage =
      summarize_semantic_operator_coverage(ir, result);
  require(coverage.task_count == 6 && coverage.unknown_task_count == 3,
          "operator coverage task counts");
  require(coverage.unregistered_operator_occurrence_count == 3 &&
              coverage.unregistered_operators.size() == 3,
          "operator coverage keeps concrete unknowns separate");
  const auto future = std::find_if(
      coverage.unregistered_operators.begin(),
      coverage.unregistered_operators.end(),
      [](const UnregisteredOperatorSummaryRow& row) {
        return row.operator_name == "FutureFusedKernelV1";
      });
  require(future != coverage.unregistered_operators.end() &&
              future->semantic_role == "unknown" &&
              future->total_duration_ns == 15 &&
              future->graph_body_member_count == 1,
          "unknown graph-body operator evidence summary");
  const auto compute_only = std::find_if(
      coverage.unregistered_operators.begin(),
      coverage.unregistered_operators.end(),
      [](const UnregisteredOperatorSummaryRow& row) {
        return row.operator_name == "ComputeTaskOnlyFutureKernel";
      });
  require(compute_only != coverage.unregistered_operators.end() &&
              compute_only->task_type == "AI_CORE" &&
              compute_only->semantic_role == "unknown" &&
              compute_only->total_duration_ns == 20,
          "compute-task-type-only operator uses concrete identity in audit");

  // Classification input composition: blob includes op and task fields.
  const SemanticTaskClassificationInput input =
      build_semantic_classification_input(ir, ir.tasks.row(TaskId(0)));
  require(input.source_domain == "task", "input source_domain is task");
  require(input.task_type == "AI_CORE", "input task_type resolved");
  require(input.blob.find("MatMulV2") != std::string::npos,
          "blob carries op metadata");
  require(input.blob.find("AI_CORE") != std::string::npos,
          "blob carries task type");

  // Corrupted task rows with an out-of-range trace_event_id must be
  // rejected, not classified with partial input.
  {
    NativeIr broken;
    const SourceRefId source =
        broken.source_refs.append("fixture", "memory", "TASK", 0);
    const SymbolId label = broken.symbols.intern("label");
    const TraceEventId event =
        broken.trace_events.append(source, 1, 0, 0, 0, 10, label);
    broken.tasks.append(source, event, 1, 1001, -1, label,
                        SymbolId::invalid(), SymbolId::invalid(),
                        SymbolId::invalid(), SymbolId::invalid());
    // Append a task whose trace_event_id is out of range.
    broken.tasks.append(source, TraceEventId(99), 2, 1002, -1, label,
                        SymbolId::invalid(), SymbolId::invalid(),
                        SymbolId::invalid(), SymbolId::invalid());
    bool rejected = false;
    try {
      (void)classify_semantic_tasks(broken, ruleset);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "out-of-range trace_event_id rejected");
  }
  return 0;
}
