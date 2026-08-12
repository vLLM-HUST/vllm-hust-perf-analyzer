#include <algorithm>
#include <string>
#include <vector>

#include "traceloom/analysis/graph_body_performance_comparison.h"
#include "traceloom/testing/test_util.h"

namespace {

traceloom::GraphBodyProfileSample sample(
    std::uint32_t device_id,
    std::size_t count,
    std::uint64_t envelope_ns,
    std::uint64_t busy_union_ns,
    std::uint64_t task_sum_ns,
    const std::string& launch_identity_prefix = "launch-") {
  traceloom::GraphBodyProfileSample result;
  result.device_id = device_id;
  result.replay_body_template_id = traceloom::ReplayBodyTemplateId(0);
  result.stream_count = 2;
  result.compute_task_count = 10;
  result.communication_task_count = 2;
  result.replay_unit_launch_count = 1;
  for (std::size_t index = 0; index < count; ++index) {
    result.launch_identity.push_back(launch_identity_prefix +
                                     std::to_string(index));
  }
  result.envelope_ns.assign(count, envelope_ns);
  result.busy_union_ns.assign(count, busy_union_ns);
  result.task_sum_ns.assign(count, task_sum_ns);
  result.compute_ns.assign(count, task_sum_ns - 10);
  result.communication_ns.assign(count, 10);
  result.data_move_ns.assign(count, 0);
  return result;
}

const traceloom::GraphBodyMetricComparison& metric(
    const traceloom::GraphBodyPerformanceComparison& result,
    const std::string& name) {
  const auto found = std::find_if(
      result.metrics.begin(), result.metrics.end(),
      [&](const traceloom::GraphBodyMetricComparison& row) {
        return row.metric == name;
      });
  traceloom::testing::require(found != result.metrics.end(),
                              "comparison metric exists");
  return *found;
}

bool has_reason(const traceloom::GraphBodyPerformanceComparison& result,
                const std::string& reason) {
  return std::find(result.reason_codes.begin(), result.reason_codes.end(),
                   reason) != result.reason_codes.end();
}

traceloom::NativeIr extraction_fixture(bool multi_launch_unit) {
  using namespace traceloom;
  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "memory", "TASK", 0);
  const SymbolId op = ir.symbols.intern("Compute");
  const TraceEventId late_event =
      ir.trace_events.append(source, 1, 0, 1, 20, 30, op);
  const TraceEventId early_event =
      ir.trace_events.append(source, 2, 0, 1, 0, 5, op);
  const TaskId late_task = ir.tasks.append(
      source, late_event, 1, 1, -1, op, SymbolId::invalid(), op,
      SymbolId::invalid(), SymbolId::invalid());
  const TaskId early_task = ir.tasks.append(
      source, early_event, 2, 2, -1, op, SymbolId::invalid(), op,
      SymbolId::invalid(), SymbolId::invalid());
  const ReplayBodyTemplateId body_template = ir.replay_body_templates.append(
      source, 123, op, 1, 0, 1,
      ReplayBodyTopologyPolicy::kCapturedStreamSetUnordered);
  const auto append_launch = [&](std::int64_t start, std::int64_t end) {
    return ir.graph_launch_occurrences.append(
        source, source, 0, 1, 1, 1, 1, StreamId::invalid(),
        StreamId::invalid(), CapturedGraphInstanceId::invalid(),
        TaskId::invalid(), TaskId::invalid(), TaskId::invalid(), start, end,
        0, GraphLaunchMatchPolicy::kNotifyCompletionAdjacent,
        GraphLaunchInstanceAssociationPolicy::kRecordModelId);
  };
  const GraphLaunchOccurrenceId late_launch = append_launch(20, 30);
  const GraphLaunchOccurrenceId early_launch = append_launch(0, 5);
  const GraphLaunchBodyId late_body = ir.graph_launch_bodies.append(
      late_launch, body_template, late_task, late_task, 1, 0, 1);
  const GraphLaunchBodyId early_body = ir.graph_launch_bodies.append(
      early_launch, body_template, early_task, early_task, 1, 0, 1);
  ir.graph_launch_body_members.append(
      late_body, late_task, 0, 0,
      GraphLaunchBodyMemberRow::Kind::kCompute);
  ir.graph_launch_body_members.append(
      early_body, early_task, 0, 0,
      GraphLaunchBodyMemberRow::Kind::kCompute);
  ir.replay_unit_launch_members.append(
      ReplayUnitId(0), 0, late_launch, ReplayCompositionSlotId(0));
  ir.replay_unit_launch_members.append(
      multi_launch_unit ? ReplayUnitId(0) : ReplayUnitId(1),
      multi_launch_unit ? 1 : 0, early_launch, ReplayCompositionSlotId(0));
  return ir;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  GraphBodyPerformanceComparisonConfig config;
  config.same_workload_attested = true;
  config.bootstrap_iterations = 1000;
  config.minimum_sample_count = 8;
  config.minimum_effect_pct = 1.0;

  const GraphBodyProfileSampleResult extracted =
      extract_graph_body_profile_sample(extraction_fixture(false), "fixture");
  require(extracted.supported && extracted.sample.envelope_ns.size() == 2 &&
              extracted.sample.envelope_ns[0] == 5 &&
              extracted.sample.envelope_ns[1] == 10,
          "exact body samples are selected and ordered chronologically");
  const GraphBodyProfileSampleResult multi_launch =
      extract_graph_body_profile_sample(extraction_fixture(true), "fixture");
  require(!multi_launch.supported &&
              multi_launch.reason_code ==
                  "multi_launch_replay_unit_requires_selector",
          "multi-launch units fail closed without a body selector");

  const GraphBodyPerformanceComparison unequal_positive =
      compare_graph_body_performance(
          {sample(0, 12, 100, 95, 90)},
          {sample(0, 9, 80, 75, 70)}, config);
  require(unequal_positive.verdict == GraphBodyPerformanceVerdict::kFaster,
          "unequal independent samples can support faster");
  require(metric(unequal_positive, "envelope_ns").baseline_count == 12 &&
              metric(unequal_positive, "envelope_ns").candidate_count == 9 &&
              metric(unequal_positive, "envelope_ns").improvement_pct == 20.0,
          "unequal sample evidence is retained");
  require(metric(unequal_positive, "data_move_ns").verdict ==
              GraphBodyPerformanceVerdict::kEquivalent,
          "jointly absent component is equivalent rather than undefined");

  const GraphBodyPerformanceComparison negative =
      compare_graph_body_performance(
          {sample(0, 12, 100, 95, 90)},
          {sample(0, 9, 120, 115, 110)}, config);
  require(negative.verdict == GraphBodyPerformanceVerdict::kSlower,
          "constant regression supports slower");

  const GraphBodyPerformanceComparison equivalent =
      compare_graph_body_performance(
          {sample(0, 12, 100, 95, 90)},
          {sample(0, 9, 100, 95, 90)}, config);
  require(equivalent.verdict == GraphBodyPerformanceVerdict::kEquivalent,
          "constant zero delta supports equivalent");

  const GraphBodyPerformanceComparison unattested =
      compare_graph_body_performance(
          {sample(0, 12, 100, 95, 90)},
          {sample(0, 9, 80, 75, 70)});
  require(unattested.verdict ==
                  GraphBodyPerformanceVerdict::kInconclusive &&
              has_reason(unattested, "workload_identity_not_attested"),
          "workload identity is an explicit verdict gate");

  const GraphBodyPerformanceComparison contradictory =
      compare_graph_body_performance(
          {sample(0, 12, 100, 95, 90)},
          {sample(0, 9, 80, 120, 120)}, config);
  require(contradictory.verdict ==
                  GraphBodyPerformanceVerdict::kInconclusive &&
              has_reason(contradictory,
                         "secondary_cost_contradicts_envelope"),
          "contradictory cost views fail closed");

  const GraphBodyPerformanceComparison rank_critical =
      compare_graph_body_performance(
          {sample(0, 12, 100, 95, 90), sample(1, 12, 110, 105, 100)},
          {sample(0, 9, 90, 85, 80), sample(1, 9, 95, 90, 85)}, config);
  require(rank_critical.verdict == GraphBodyPerformanceVerdict::kFaster &&
              metric(rank_critical, "envelope_ns").baseline_median_ns ==
                  110.0 &&
              metric(rank_critical, "envelope_ns").candidate_median_ns ==
                  95.0,
          "multi-rank samples use ordinal critical maxima");

  GraphBodyProfileSample mismatched_rank = sample(1, 11, 110, 105, 100);
  const GraphBodyPerformanceComparison invalid_rank_shape =
      compare_graph_body_performance(
          {sample(0, 12, 100, 95, 90), mismatched_rank},
          {sample(0, 9, 90, 85, 80), sample(1, 9, 95, 90, 85)}, config);
  require(invalid_rank_shape.verdict ==
                  GraphBodyPerformanceVerdict::kInconclusive &&
              has_reason(invalid_rank_shape,
                         "baseline_rank_sample_count_mismatch"),
          "rank alignment mismatch remains inconclusive");

  const GraphBodyPerformanceComparison unverified_rank_pairing =
      compare_graph_body_performance(
          {sample(0, 12, 100, 95, 90),
           sample(1, 12, 110, 105, 100, "shifted-launch-")},
          {sample(0, 9, 90, 85, 80), sample(1, 9, 95, 90, 85)}, config);
  require(unverified_rank_pairing.verdict ==
                  GraphBodyPerformanceVerdict::kInconclusive &&
              has_reason(unverified_rank_pairing,
                         "baseline_rank_launch_identity_mismatch"),
          "equal rank sample counts do not prove launch identity");

  GraphBodyProfileSample no_identity_rank = sample(1, 12, 110, 105, 100);
  no_identity_rank.launch_identity.clear();
  const GraphBodyPerformanceComparison missing_rank_identity =
      compare_graph_body_performance(
          {sample(0, 12, 100, 95, 90), no_identity_rank},
          {sample(0, 9, 90, 85, 80), sample(1, 9, 95, 90, 85)}, config);
  require(missing_rank_identity.verdict ==
                  GraphBodyPerformanceVerdict::kInconclusive &&
              has_reason(missing_rank_identity,
                         "baseline_rank_launch_identity_invalid"),
          "missing cross-rank launch identity fails closed");
  return 0;
}
