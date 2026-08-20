#include "traceloom/analysis/event_reconciliation.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/testing/test_util.h"

#include <string>
#include <vector>

namespace {

traceloom::EventReconciliationRuleset ruleset(
    std::int32_t priority = 100,
    const std::string& task_type = "KERNEL_MIX_AIV") {
  traceloom::EventReconciliationRule rule;
  rule.priority = priority;
  rule.rule_id = "ascend.task.mix-aiv.context-detail";
  rule.provider_scope = "ascend";
  rule.source_domain = "task";
  rule.task_type = task_type;
  rule.generic_context_id = 4294967295LL;
  rule.concrete_context_id = 0;
  rule.min_contained_fraction = 0.99;
  rule.note = "test";
  rule.rule_origin = "memory";
  rule.rule_origin_sha256 = "test-sha";
  rule.source_line = 1;
  return {"traceloom.event-reconciliation-policy/v1",
          "test.event-reconciliation", "1", "memory", "test-sha",
          "independent", {rule}};
}

traceloom::EventReconciliationRuleset task_communication_ruleset() {
  traceloom::EventReconciliationRule rule;
  rule.priority = 90;
  rule.rule_id = "ascend.task-communication.mc2-fused";
  rule.provider_scope = "ascend";
  rule.source_domain = "task+communication_op";
  rule.task_op_type = "MatmulAllReduce";
  rule.communication_op_name_prefix = "MatmulAllReduceMc2AicpuKernel_";
  rule.identity_policy = "same_input,device,unique_containment";
  rule.min_contained_fraction = 1.0;
  rule.note = "test";
  rule.rule_origin = "memory";
  rule.rule_origin_sha256 = "test-sha";
  rule.source_line = 1;
  return {"traceloom.event-reconciliation-policy/v2",
          "test.task-communication-reconciliation", "1", "memory",
          "test-sha", "independent", {rule}};
}

void append_task(traceloom::NativeIr& ir,
                 traceloom::SourceRefId source,
                 std::uint64_t source_row,
                 std::int64_t start_ns,
                 std::int64_t end_ns,
                 std::uint64_t raw_task_id,
                 std::int64_t global_task_id,
                 std::int64_t connection_id,
                 std::int64_t context_id,
                 bool semantic_detail) {
  const traceloom::SymbolId task_type =
      ir.symbols.intern("KERNEL_MIX_AIV");
  const traceloom::SymbolId reduce_all =
      semantic_detail ? ir.symbols.intern("ReduceAll")
                      : traceloom::SymbolId::invalid();
  const traceloom::TraceEventId event = ir.trace_events.append(
      source, source_row, 0, 46, start_ns, end_ns, task_type);
  ir.tasks.append(source, event, raw_task_id, global_task_id, connection_id,
                  task_type, traceloom::SymbolId::invalid(), reduce_all,
                  traceloom::SymbolId::invalid(),
                  traceloom::SymbolId::invalid(), -1,
                  traceloom::SymbolId::invalid(), context_id);
}

void append_fused_task(traceloom::NativeIr& ir,
                       traceloom::SourceRefId source,
                       std::uint64_t source_row,
                       std::int64_t start_ns,
                       std::int64_t end_ns) {
  const traceloom::SymbolId task_type =
      ir.symbols.intern("KERNEL_MIX_AIC");
  const traceloom::SymbolId op_type =
      ir.symbols.intern("MatmulAllReduce");
  const traceloom::TraceEventId event = ir.trace_events.append(
      source, source_row, 0, 47, start_ns, end_ns, task_type);
  ir.tasks.append(source, event, source_row, source_row, source_row, task_type,
                  op_type, op_type, traceloom::SymbolId::invalid(),
                  traceloom::SymbolId::invalid());
}

void append_mc2_communication(traceloom::NativeIr& ir,
                              traceloom::SourceRefId source,
                              std::uint64_t source_row,
                              std::int64_t start_ns,
                              std::int64_t end_ns) {
  const traceloom::SymbolId name =
      ir.symbols.intern("MatmulAllReduceMc2AicpuKernel_fixture");
  const traceloom::TraceEventId event = ir.trace_events.append(
      source, source_row, 0, 116, start_ns, end_ns, name);
  ir.communication_ops.append(source, event, source_row, source_row, 1, 1,
                              name);
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  NativeIr exact;
  const SourceRefId exact_source =
      exact.source_refs.append("ascend", "memory", "TASK", 0);
  append_task(exact, exact_source, 1, 100, 140, 7, 31, 500,
              4294967295LL, false);
  append_task(exact, exact_source, 2, 105, 135, 7, 9, 500, 0, true);
  const EventReconciliationState exact_state =
      reconcile_event_observations(exact, ruleset());
  require(exact_state.decisions.size() == 1);
  require(exact_state.members.size() == 2);
  const EventReconciliationDecisionRow& decision = exact_state.decisions[0];
  require(decision.status == EventReconciliationStatus::kReconciled);
  require(decision.reason_code ==
          "unique_identity_pair_with_contained_detail");
  require(decision.canonical_event_id == TraceEventId(1));
  require(decision.envelope_event_id == TraceEventId(0));
  require(decision.canonical_start_ns == 100);
  require(decision.canonical_end_ns == 140);
  require(decision.contained_fraction == 1.0);
  require(exact_state.members[0].role ==
          EventReconciliationMemberRole::kTimingEnvelope);
  require(exact_state.members[0].contributes_timing);
  require(exact_state.members[0].contributes_cost);
  require(!exact_state.members[0].contributes_symbol);
  require(exact_state.members[1].role ==
          EventReconciliationMemberRole::kSemanticDetail);
  require(exact_state.members[1].contributes_symbol);
  require(!exact_state.members[1].contributes_cost);

  NativeIr missing;
  const SourceRefId missing_source =
      missing.source_refs.append("ascend", "memory", "TASK", 0);
  append_task(missing, missing_source, 1, 100, 140, 7, 31, 500,
              4294967295LL, false);
  const EventReconciliationState missing_state =
      reconcile_event_observations(missing, ruleset());
  require(missing_state.decisions.size() == 1);
  require(missing_state.decisions[0].status ==
          EventReconciliationStatus::kIndependent);
  require(missing_state.members.size() == 1);
  require(missing_state.members[0].role ==
          EventReconciliationMemberRole::kIndependentCandidate);

  NativeIr ambiguous;
  const SourceRefId ambiguous_source =
      ambiguous.source_refs.append("ascend", "memory", "TASK", 0);
  append_task(ambiguous, ambiguous_source, 1, 100, 140, 7, 31, 500,
              4294967295LL, false);
  append_task(ambiguous, ambiguous_source, 2, 105, 135, 7, 9, 500, 0, true);
  append_task(ambiguous, ambiguous_source, 3, 106, 134, 7, 10, 500, 0,
              true);
  const EventReconciliationState ambiguous_state =
      reconcile_event_observations(ambiguous, ruleset());
  require(ambiguous_state.decisions.size() == 1);
  require(ambiguous_state.decisions[0].status ==
          EventReconciliationStatus::kAmbiguous);
  require(ambiguous_state.members.size() == 3);

  NativeIr conflicting;
  const SourceRefId conflict_source =
      conflicting.source_refs.append("ascend", "memory", "TASK", 0);
  append_task(conflicting, conflict_source, 1, 100, 120, 7, 31, 500,
              4294967295LL, false);
  append_task(conflicting, conflict_source, 2, 110, 150, 7, 9, 500, 0,
              true);
  const EventReconciliationState conflict_state =
      reconcile_event_observations(conflicting, ruleset());
  require(conflict_state.decisions.size() == 1);
  require(conflict_state.decisions[0].status ==
          EventReconciliationStatus::kConflict);
  require(conflict_state.decisions[0].reason_code ==
          "insufficient_interval_containment");

  NativeIr cross_provider;
  const SourceRefId cross_task_source =
      cross_provider.source_refs.append(
          "ascend", "profile/device_0/sqlite/ascend_task.db", "TASK", 0);
  const SourceRefId cross_comm_source = cross_provider.source_refs.append(
      "ascend", "profile/device_0/sqlite/hccl_single_device.db",
      "COMMUNICATION_OP", 0);
  append_fused_task(cross_provider, cross_task_source, 1, 100, 200);
  append_mc2_communication(cross_provider, cross_comm_source, 2, 120, 180);
  const EventReconciliationState cross_state =
      reconcile_event_observations(cross_provider,
                                   task_communication_ruleset());
  require(cross_state.decisions.size() == 1);
  require(cross_state.decisions[0].status ==
          EventReconciliationStatus::kReconciled);
  require(cross_state.decisions[0].reason_code ==
          "unique_provider_observation_with_containing_task");
  require(cross_state.decisions[0].canonical_event_id == TraceEventId(0));
  require(cross_state.members.size() == 2);
  require(cross_state.members[0].role ==
          EventReconciliationMemberRole::kProviderDetail);
  require(cross_state.members[0].communication_op_id ==
          CommunicationOpId(0));
  require(cross_state.members[1].contributes_timing &&
          cross_state.members[1].contributes_symbol &&
          cross_state.members[1].contributes_cost);

  NativeIr cross_ambiguous;
  const SourceRefId ambiguous_task_source = cross_ambiguous.source_refs.append(
      "ascend", "profile.db", "TASK", 0);
  const SourceRefId ambiguous_comm_source =
      cross_ambiguous.source_refs.append(
          "ascend", "profile.db", "COMMUNICATION_OP", 0);
  append_fused_task(cross_ambiguous, ambiguous_task_source, 1, 100, 200);
  append_fused_task(cross_ambiguous, ambiguous_task_source, 2, 90, 210);
  append_mc2_communication(cross_ambiguous, ambiguous_comm_source, 3, 120,
                           180);
  const EventReconciliationState cross_ambiguous_state =
      reconcile_event_observations(cross_ambiguous,
                                   task_communication_ruleset());
  require(cross_ambiguous_state.decisions.size() == 1);
  require(cross_ambiguous_state.decisions[0].status ==
          EventReconciliationStatus::kAmbiguous);
  require(cross_ambiguous_state.members.size() == 3);

  const EventReconciliationRuleset overlaid =
      overlay_event_reconciliation_ruleset(ruleset(100),
                                           ruleset(200, "REPLACEMENT"));
  require(overlaid.rules().size() == 1);
  require(overlaid.rules()[0].priority == 200);
  require(overlaid.rules()[0].task_type == "REPLACEMENT");
  require(overlaid.policy_id().find("+overlay:") != std::string::npos);
  require(overlaid.manifest_sha256().size() == 64);

  return 0;
}
