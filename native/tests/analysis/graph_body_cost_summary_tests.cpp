#include "traceloom/analysis/graph_body_cost_summary.h"
#include "traceloom/testing/test_util.h"

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "memory", "TASK", 0);
  const SymbolId compute = ir.symbols.intern("Compute");
  const SymbolId communication = ir.symbols.intern("AllReduce");
  const TraceEventId e0 =
      ir.trace_events.append(source, 1, 0, 1, 0, 10, compute);
  const TraceEventId e1 =
      ir.trace_events.append(source, 2, 0, 2, 5, 15, communication);
  const TraceEventId e2 =
      ir.trace_events.append(source, 3, 0, 1, 20, 32, compute);
  const TraceEventId e3 =
      ir.trace_events.append(source, 4, 0, 2, 33, 38, communication);
  const TaskId t0 = ir.tasks.append(
      source, e0, 1, 1, -1, compute, SymbolId::invalid(), compute,
      SymbolId::invalid(), SymbolId::invalid());
  const TaskId t1 = ir.tasks.append(
      source, e1, 2, 2, -1, communication, SymbolId::invalid(),
      SymbolId::invalid(), SymbolId::invalid(), communication);
  const TaskId t2 = ir.tasks.append(
      source, e2, 3, 3, -1, compute, SymbolId::invalid(), compute,
      SymbolId::invalid(), SymbolId::invalid());
  const TaskId t3 = ir.tasks.append(
      source, e3, 4, 4, -1, communication, SymbolId::invalid(),
      SymbolId::invalid(), SymbolId::invalid(), communication);
  const ReplayBodyTemplateId body_template = ir.replay_body_templates.append(
      source, 123, ir.symbols.intern("Compute\nAllReduce"), 1, 1, 2,
      ReplayBodyTopologyPolicy::kCapturedStreamSetUnordered);
  const auto append_launch = [&](std::int64_t start, std::int64_t end) {
    return ir.graph_launch_occurrences.append(
        source, source, 0, 1, 1, 1, 1, StreamId::invalid(),
        StreamId::invalid(), CapturedGraphInstanceId::invalid(),
        TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), start, end,
        0, GraphLaunchMatchPolicy::kNotifyCompletionAdjacent,
        GraphLaunchInstanceAssociationPolicy::kRecordModelId);
  };
  const GraphLaunchOccurrenceId launch0 = append_launch(0, 15);
  const GraphLaunchOccurrenceId launch1 = append_launch(20, 38);
  const GraphLaunchBodyId body0 = ir.graph_launch_bodies.append(
      launch0, body_template, t0, t1, 1, 1, 2);
  const GraphLaunchBodyId body1 = ir.graph_launch_bodies.append(
      launch1, body_template, t2, t3, 1, 1, 2);
  ir.graph_launch_body_members.append(
      body0, t0, 0, 0, GraphLaunchBodyMemberRow::Kind::kCompute);
  ir.graph_launch_body_members.append(
      body0, t1, 1, 0, GraphLaunchBodyMemberRow::Kind::kCommunication);
  ir.graph_launch_body_members.append(
      body1, t2, 0, 0, GraphLaunchBodyMemberRow::Kind::kCompute);
  ir.graph_launch_body_members.append(
      body1, t3, 1, 0, GraphLaunchBodyMemberRow::Kind::kCommunication);
  ir.replay_unit_launch_members.append(
      ReplayUnitId(0), 0, launch1, ReplayCompositionSlotId(0));

  const GraphBodyCostSummary summary =
      build_graph_body_cost_summary(ir);
  require(summary.occurrences.size() == 2, "two body costs");
  require(summary.occurrences[0].task_sum_ns == 20 &&
              summary.occurrences[0].busy_union_ns == 15 &&
              summary.occurrences[0].envelope_ns == 15,
          "overlap-aware first body costs");
  require(summary.occurrences[1].task_sum_ns == 17 &&
              summary.occurrences[1].busy_union_ns == 17 &&
              summary.occurrences[1].envelope_ns == 18 &&
              summary.occurrences[1].exact_replay_unit,
          "exact second body costs");
  require(summary.distributions.size() == 2,
          "all and exact distributions");
  require(summary.distributions[0].scope ==
                  GraphBodyCostScope::kAllObservedBodies &&
              summary.distributions[0].occurrence_count == 2 &&
              summary.distributions[0].task_sum_p25_ns == 17 &&
              summary.distributions[0].task_sum_median_ns == 18 &&
              summary.distributions[0].task_sum_p75_ns == 20 &&
              summary.distributions[0].busy_union_median_ns == 16,
          "all-body distribution");
  require(summary.distributions[1].scope ==
                  GraphBodyCostScope::kExactReplayUnits &&
              summary.distributions[1].occurrence_count == 1 &&
              summary.distributions[1].task_sum_median_ns == 17 &&
              summary.distributions[1].communication_median_ns == 5,
          "exact-body distribution");

  // Fail-closed malformed membership: body members with invalid body ids,
  // invalid task ids, or invalid trace-event references are skipped (never
  // dereferenced, never thrown); valid-IR rows are unaffected.
  NativeIr malformed;
  const SourceRefId m_source =
      malformed.source_refs.append("fixture", "memory", "TASK", 0);
  const SymbolId m_compute = malformed.symbols.intern("Compute");
  const TraceEventId m_e0 =
      malformed.trace_events.append(m_source, 1, 0, 1, 0, 10, m_compute);
  const TaskId m_t0 = malformed.tasks.append(
      m_source, m_e0, 1, 1, -1, m_compute, SymbolId::invalid(), m_compute,
      SymbolId::invalid(), SymbolId::invalid());
  const TaskId m_bad_event_task = malformed.tasks.append(
      m_source, TraceEventId::invalid(), 2, 2, -1, m_compute,
      SymbolId::invalid(), m_compute, SymbolId::invalid(),
      SymbolId::invalid());
  const ReplayBodyTemplateId m_template =
      malformed.replay_body_templates.append(
          m_source, 1, malformed.symbols.intern("Compute"), 1, 0, 1,
          ReplayBodyTopologyPolicy::kSingleModelStream);
  const GraphLaunchOccurrenceId m_launch =
      malformed.graph_launch_occurrences.append(
          m_source, m_source, 0, 1, 1, 1, 1, StreamId::invalid(),
          StreamId::invalid(), CapturedGraphInstanceId::invalid(),
          TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), 0, 10, 0,
          GraphLaunchMatchPolicy::kNotifyCompletionAdjacent,
          GraphLaunchInstanceAssociationPolicy::kRecordModelId);
  const GraphLaunchBodyId m_body = malformed.graph_launch_bodies.append(
      m_launch, m_template, m_t0, m_t0, 1, 0, 1);
  malformed.graph_launch_body_members.append(
      m_body, m_t0, 0, 0, GraphLaunchBodyMemberRow::Kind::kCompute);
  malformed.graph_launch_body_members.append(
      GraphLaunchBodyId(99), m_t0, 0, 0,
      GraphLaunchBodyMemberRow::Kind::kCompute);
  malformed.graph_launch_body_members.append(
      m_body, TaskId::invalid(), 0, 0,
      GraphLaunchBodyMemberRow::Kind::kCompute);
  malformed.graph_launch_body_members.append(
      m_body, m_bad_event_task, 0, 0,
      GraphLaunchBodyMemberRow::Kind::kCompute);

  const GraphBodyCostSummary malformed_summary =
      build_graph_body_cost_summary(malformed);
  require(malformed_summary.occurrences.size() == 1,
          "malformed membership still yields one body row");
  require(malformed_summary.occurrences[0].member_count == 1 &&
              malformed_summary.occurrences[0].task_sum_ns == 10 &&
              malformed_summary.occurrences[0].compute_ns == 10 &&
              malformed_summary.occurrences[0].busy_union_ns == 10 &&
              malformed_summary.occurrences[0].envelope_ns == 10,
          "only the valid member contributes cost evidence");
  return 0;
}
