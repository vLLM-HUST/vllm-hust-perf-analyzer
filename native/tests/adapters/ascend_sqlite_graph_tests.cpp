#include "traceloom/adapters/ascend_sqlite_adapter.h"
#include "support/ascend_sqlite_fixture.h"

#include "traceloom/analysis/flat_anchor_builder.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  using namespace traceloom;
  using namespace traceloom::test;

  const std::filesystem::path graph_dir = temp_ascend_profile_dir("_graph");
  materialize_ascend_graph_fixture(graph_dir, "aclgraph_smoke");
  const AscendSQLiteAdapter graph_adapter(
      AscendSQLiteAdapterOptions{(graph_dir / "msprof.db").string(),
                                 "ascend_graph_smoke"});
  NativeIr graph_ir = graph_adapter.load();
  require(graph_ir.runtime_calls.size() == 10,
          "CANN_API rows were not retained as host-side runtime calls");
  require(graph_ir.runtime_calls.row(RuntimeCallId(0)).raw_correlation_id ==
              9000 &&
              graph_ir.runtime_calls.row(RuntimeCallId(0)).raw_global_tid == 1,
          "Ascend runtime-call correlation/thread identity mismatch");
  require(graph_ir.replay_units.size() == 2,
          "ACLGraph replay units were not reconstructed");
  require(graph_ir.graph_templates.size() == 1,
          "ACLGraph equivalent units should share one template");
  require(graph_ir.graph_templates.row(GraphTemplateId(0)).slot_count == 2,
          "ACLGraph template should retain its capture group size");
  require(graph_ir.trace_events
                  .row(graph_ir.replay_units.row(ReplayUnitId(0))
                           .launch_trace_event_id)
                  .start_ns == 100 &&
              graph_ir.trace_events
                      .row(graph_ir.replay_units.row(ReplayUnitId(0))
                               .launch_trace_event_id)
                      .end_ns == 130,
          "ACLGraph replay window should not absorb the inter-wave gap");

  FlatAnchorBuildConfig anchor_config;
  anchor_config.filter_auxiliary_task_anchors = true;
  anchor_config.skip_events_covered_by_replay_units = true;
  const FlatAnchorBuildStats graph_stats =
      build_flat_anchors(graph_ir, anchor_config);
  require(graph_stats.device_event_anchors == 2,
          "ACLGraph replay units should become device anchors");
  require(graph_stats.communication_anchors == 1,
          "only communication outside graph replay units should remain");
  require(graph_ir.tokens.size() == 3,
          "covered graph events should be replaced by replay-unit tokens");
  require(graph_ir.anchors.row(AnchorId(0)).kind == AnchorKind::kGraphReplayUnit,
          "first graph anchor kind mismatch");
  require(graph_ir.anchors.row(AnchorId(1)).kind == AnchorKind::kGraphReplayUnit,
          "second graph anchor kind mismatch");
  require(graph_ir.anchors.row(AnchorId(2)).kind == AnchorKind::kCommunication,
          "outside communication anchor should remain");
  require(graph_ir.protected_intervals.empty(),
          "single-token GraphReplayUnit anchors should remain grammar-compressible");

  const std::string current_stream_db_path =
      (graph_dir / "host" / "sqlite" / "stream_info.db").string();
  apply_ascend_fixture_mutation(current_stream_db_path, "aclgraph_smoke",
                                "current_schema.sql");

  const AscendSQLiteAdapter current_graph_adapter(
      AscendSQLiteAdapterOptions{(graph_dir / "msprof.db").string(),
                                 "ascend_graph_current_schema_smoke"});
  const NativeIr current_graph_ir = current_graph_adapter.load();
  require(current_graph_ir.replay_units.size() == 2,
          "ACLGraph replay reconstruction missed current stream_id schema");
  require(current_graph_ir.graph_templates.size() == 1,
          "current ACLGraph stream_id schema changed graph identity");

  apply_ascend_fixture_mutation(current_stream_db_path, "aclgraph_smoke",
                                "single_slot.sql");
  const AscendSQLiteAdapter single_slot_graph_adapter(
      AscendSQLiteAdapterOptions{(graph_dir / "msprof.db").string(),
                                 "ascend_graph_single_slot_smoke"});
  const NativeIr single_slot_graph_ir = single_slot_graph_adapter.load();
  require(single_slot_graph_ir.replay_units.size() == 4,
          "single-slot ACLGraph launches should reconstruct one unit each");
  require(single_slot_graph_ir.graph_templates.row(GraphTemplateId(0))
              .slot_count == 1,
          "single-slot ACLGraph template should retain slot count one");
  const TraceEventRow& single_slot_second = single_slot_graph_ir.trace_events.row(
      single_slot_graph_ir.replay_units.row(ReplayUnitId(1))
          .launch_trace_event_id);
  require(single_slot_second.start_ns == 120 &&
              single_slot_second.end_ns == 130,
          "single-slot replay window should use launch/body evidence only");

  const std::filesystem::path launch_identity_dir =
      temp_ascend_profile_dir("_launch_identity");
  std::filesystem::create_directories(launch_identity_dir);
  const std::string launch_identity_path =
      (launch_identity_dir / "msprof.db").string();
  materialize_ascend_graph_fixture(launch_identity_dir, "launch_identity");
  const NativeIr launch_identity_ir =
      AscendSQLiteAdapter(launch_identity_path, "graph_launch_identity").load();
  require(launch_identity_ir.graph_launch_occurrences.size() == 4,
          "graph launch occurrence count mismatch");
  const GraphLaunchOccurrenceRow& launch0 =
      launch_identity_ir.graph_launch_occurrences.row(
          GraphLaunchOccurrenceId(0));
  const GraphLaunchOccurrenceRow& launch1 =
      launch_identity_ir.graph_launch_occurrences.row(
          GraphLaunchOccurrenceId(1));
  const GraphLaunchOccurrenceRow& launch2 =
      launch_identity_ir.graph_launch_occurrences.row(
          GraphLaunchOccurrenceId(2));
  const GraphLaunchOccurrenceRow& launch3 =
      launch_identity_ir.graph_launch_occurrences.row(
          GraphLaunchOccurrenceId(3));
  require(launch0.match_policy ==
                  GraphLaunchMatchPolicy::kNotifyCompletionAdjacent &&
              launch1.match_policy ==
                  GraphLaunchMatchPolicy::kNotifyCompletionAdjacent,
          "completion-adjacent launch match policy mismatch");
  require(launch2.match_policy ==
              GraphLaunchMatchPolicy::kNotifyOrderedFallback,
          "ordered fallback launch match policy mismatch");
  require(launch3.match_policy == GraphLaunchMatchPolicy::kUnmatched,
          "incomplete launch should remain unmatched");
  require(launch0.raw_launch_connection_id == 100 &&
              launch0.raw_graph_connection_id == 9001 &&
              launch0.raw_model_id == 7,
          "first graph launch identity mismatch");
  require(launch1.raw_graph_connection_id == 9002 &&
              launch1.raw_model_id == 8,
          "second graph launch identity mismatch");
  require(launch2.raw_graph_connection_id == 9001 &&
              launch2.raw_model_id == -1,
          "stream-associated graph launch raw identity mismatch");
  require(launch3.raw_graph_connection_id == -1 &&
              launch3.raw_model_id == -1 &&
              !launch3.notify_wait_task_id.valid() &&
              !launch3.notify_record_task_id.valid(),
          "unmatched graph launch should not invent identity evidence");
  require(launch0.raw_host_api_row_id == 9 &&
              launch1.raw_host_api_row_id == 10 &&
              launch2.raw_host_api_row_id == 11 &&
              launch3.raw_host_api_row_id == 12,
          "host graph execute provenance mismatch");
  require(launch_identity_ir.source_refs
                  .row(launch0.host_api_source_ref_id)
                  .table_name == "CANN_API",
          "host graph execute source table mismatch");
  require(launch0.wait_record_end_delta_ns == 0 &&
              launch2.wait_record_end_delta_ns == -699680,
          "wait-record completion delta mismatch");
  require(launch0.execute_stream_id.valid() &&
              launch0.model_stream_id.valid() &&
              launch0.execute_stream_id != launch0.model_stream_id,
          "graph launch stream identities were not normalized");
  require(launch_identity_ir.captured_graph_instances.size() == 2 &&
              launch_identity_ir.captured_graph_streams.size() == 3,
          "capture model groups were not preserved");
  require(launch_identity_ir.graph_slot_templates.size() == 2,
          "distinct capture signatures should form two slot templates");
  require(launch_identity_ir.replay_body_templates.size() == 2,
          "distinct replay compute bodies should form two body templates");
  require(launch_identity_ir.graph_launch_bodies.size() == 3,
          "matched launches with compute work should materialize bodies");
  const GraphLaunchBodyRow& body0 =
      launch_identity_ir.graph_launch_bodies.row(GraphLaunchBodyId(0));
  const GraphLaunchBodyRow& body1 =
      launch_identity_ir.graph_launch_bodies.row(GraphLaunchBodyId(1));
  const GraphLaunchBodyRow& body2 =
      launch_identity_ir.graph_launch_bodies.row(GraphLaunchBodyId(2));
  require(body0.graph_launch_occurrence_id == GraphLaunchOccurrenceId(0) &&
              body1.graph_launch_occurrence_id == GraphLaunchOccurrenceId(1) &&
              body2.graph_launch_occurrence_id == GraphLaunchOccurrenceId(2),
          "graph launch bodies lost launch occurrence order");
  require(body0.replay_body_template_id == body2.replay_body_template_id &&
              body0.replay_body_template_id != body1.replay_body_template_id,
          "repeated replay compute bodies lost exact template identity");
  require(body0.compute_task_count == 1 &&
              body0.communication_task_count == 1 &&
              body1.compute_task_count == 1 &&
              body1.communication_task_count == 0 &&
              body2.compute_task_count == 1 &&
              body2.communication_task_count == 1 &&
              body0.stream_count == 2 && body1.stream_count == 1 &&
              body2.stream_count == 2,
          "graph launch body compute task counts mismatch");
  require(launch_identity_ir.graph_launch_body_members.size() == 5,
          "graph launch body members lost exact task provenance");
  require(launch_identity_ir.graph_launch_body_members
                  .row(GraphLaunchBodyMemberId(0))
                  .graph_launch_body_id == body0.id &&
              launch_identity_ir.graph_launch_body_members
                      .row(GraphLaunchBodyMemberId(0))
                      .lane_ordinal == 0,
          "graph launch body member linkage mismatch");
  require(launch_identity_ir.replay_body_templates
                  .row(body0.replay_body_template_id)
                  .topology_policy ==
              ReplayBodyTopologyPolicy::kCapturedStreamSetUnordered &&
              launch_identity_ir.replay_body_templates
                      .row(body0.replay_body_template_id)
                      .stream_count == 2,
          "multi-stream graph body lost its captured stream topology");
  require(launch_identity_ir.symbols
                  .value(launch_identity_ir.replay_body_templates
                             .row(body0.replay_body_template_id)
                             .op_sequence_symbol_id)
                  .find("comm:hcom_allReduce/Reduce_Inline") !=
              std::string::npos,
          "multi-stream graph body lost normalized communication topology");
  require(launch0.captured_graph_instance_id.valid() &&
              launch1.captured_graph_instance_id.valid() &&
              launch2.captured_graph_instance_id ==
                  launch0.captured_graph_instance_id &&
              !launch3.captured_graph_instance_id.valid(),
          "launches did not link to captured graph instances by direct record "
          "identity");
  require(launch0.instance_association_policy ==
                  GraphLaunchInstanceAssociationPolicy::kRecordModelId &&
              launch1.instance_association_policy ==
                  GraphLaunchInstanceAssociationPolicy::kRecordModelId &&
              launch2.instance_association_policy ==
                  GraphLaunchInstanceAssociationPolicy::kRecordModelStream &&
              launch3.instance_association_policy ==
                  GraphLaunchInstanceAssociationPolicy::kNone,
          "graph launch instance association policy mismatch");
  require(launch_identity_ir.captured_graph_instances
                  .row(launch0.captured_graph_instance_id)
                  .slot_template_id !=
              launch_identity_ir.captured_graph_instances
                  .row(launch1.captured_graph_instance_id)
                  .slot_template_id,
          "same-profile graph instances lost their slot-template identity");

  apply_ascend_fixture_mutation(launch_identity_path, "launch_identity",
                                "mc2_nested_controls.sql");
  const NativeIr nested_control_ir =
      AscendSQLiteAdapter(launch_identity_path, "graph_nested_controls")
          .load();
  require(nested_control_ir.graph_launch_occurrences.size() == 4,
          "nested same-connection controls changed graph launch count");
  const GraphLaunchOccurrenceRow& nested_launch =
      nested_control_ir.graph_launch_occurrences.row(
          GraphLaunchOccurrenceId(0));
  const TaskRow& nested_wait =
      nested_control_ir.tasks.row(nested_launch.notify_wait_task_id);
  const TaskRow& nested_record =
      nested_control_ir.tasks.row(nested_launch.notify_record_task_id);
  const TraceEventRow& nested_wait_event =
      nested_control_ir.trace_events.row(nested_wait.trace_event_id);
  const TraceEventRow& nested_record_event =
      nested_control_ir.trace_events.row(nested_record.trace_event_id);
  require(nested_wait_event.stream_id == 3 &&
              nested_wait_event.start_ns == 115,
          "graph matcher selected a pre-execute or nested-stream notify wait");
  require(nested_record_event.stream_id == 36 &&
              nested_record.raw_connection_id == 9001,
          "graph matcher selected a same-connection nested notify record");
  require(nested_launch.instance_association_policy ==
              GraphLaunchInstanceAssociationPolicy::kRecordModelId &&
              nested_launch.captured_graph_instance_id.valid(),
          "nested controls displaced capture-backed graph identity");
  require(nested_control_ir.replay_units.size() ==
              launch_identity_ir.replay_units.size() &&
              nested_control_ir.graph_launch_bodies.size() ==
                  launch_identity_ir.graph_launch_bodies.size(),
          "nested controls changed exact replay recovery");

  require(launch_identity_ir.graph_launch_activities.size() == 2 &&
              launch_identity_ir.graph_launch_activity_members.size() == 4,
          "host blocking sync boundaries did not preserve launch activities");
  const GraphLaunchActivityRow& activity0 =
      launch_identity_ir.graph_launch_activities.row(GraphLaunchActivityId(0));
  const GraphLaunchActivityRow& activity1 =
      launch_identity_ir.graph_launch_activities.row(GraphLaunchActivityId(1));
  require(activity0.host_execute_count == 2 &&
              activity0.matched_launch_count == 2 &&
              activity0.boundary_host_api_row_id == 13 &&
              activity1.host_execute_count == 2 &&
              activity1.matched_launch_count == 2 &&
              activity1.boundary_host_api_row_id == 14,
          "graph launch activity cardinality/provenance mismatch");
  require(activity0.boundary_policy ==
                  GraphLaunchActivityBoundaryPolicy::kHostBlockingSync &&
              activity1.boundary_policy ==
                  GraphLaunchActivityBoundaryPolicy::kHostBlockingSync,
          "graph launch activity boundary policy mismatch");

  const std::filesystem::path split_graph_dir =
      launch_identity_dir / "split_profile";
  materialize_ascend_graph_split_fixture(split_graph_dir, "launch_identity");
  const NativeIr split_graph_ir =
      AscendSQLiteAdapter(split_graph_dir.string(),
                          "graph_launch_identity_split")
          .load();
  require(split_graph_ir.graph_launch_occurrences.size() ==
                  launch_identity_ir.graph_launch_occurrences.size() &&
              split_graph_ir.captured_graph_instances.size() ==
                  launch_identity_ir.captured_graph_instances.size() &&
              split_graph_ir.captured_graph_streams.size() ==
                  launch_identity_ir.captured_graph_streams.size() &&
              split_graph_ir.graph_launch_bodies.size() ==
                  launch_identity_ir.graph_launch_bodies.size() &&
              split_graph_ir.graph_launch_activities.size() ==
                  launch_identity_ir.graph_launch_activities.size() &&
              split_graph_ir.graph_launch_activity_members.size() ==
                  launch_identity_ir.graph_launch_activity_members.size(),
          "split ACLGraph evidence cardinality differs from monolithic");
  for (std::uint32_t index = 0;
       index < split_graph_ir.graph_launch_occurrences.size(); ++index) {
    const GraphLaunchOccurrenceRow& monolithic =
        launch_identity_ir.graph_launch_occurrences.row(
            GraphLaunchOccurrenceId(index));
    const GraphLaunchOccurrenceRow& split =
        split_graph_ir.graph_launch_occurrences.row(
            GraphLaunchOccurrenceId(index));
    require(split.raw_host_api_row_id == monolithic.raw_host_api_row_id &&
                split.raw_launch_connection_id ==
                    monolithic.raw_launch_connection_id &&
                split.raw_graph_connection_id ==
                    monolithic.raw_graph_connection_id &&
                split.raw_model_id == monolithic.raw_model_id &&
                split.instance_association_policy ==
                    monolithic.instance_association_policy &&
                split.match_policy == monolithic.match_policy,
            "split ACLGraph launch identity differs from monolithic");
    require(split_graph_ir.source_refs.row(split.source_ref_id).table_name ==
                "AscendTask",
            "split graph launch provenance did not retain AscendTask");
  }
  std::vector<std::uint64_t> monolithic_body_hashes;
  std::vector<std::uint64_t> split_body_hashes;
  for (const ReplayBodyTemplateRow& body :
       launch_identity_ir.replay_body_templates.rows()) {
    monolithic_body_hashes.push_back(body.exact_sequence_hash);
  }
  for (const ReplayBodyTemplateRow& body :
       split_graph_ir.replay_body_templates.rows()) {
    split_body_hashes.push_back(body.exact_sequence_hash);
  }
  std::sort(monolithic_body_hashes.begin(), monolithic_body_hashes.end());
  std::sort(split_body_hashes.begin(), split_body_hashes.end());
  require(split_body_hashes == monolithic_body_hashes,
          "split ACLGraph body templates differ from monolithic");
  require(split_graph_ir.graph_launch_bodies.row(GraphLaunchBodyId(0))
                  .communication_task_count == 1 &&
              split_graph_ir.symbols
                      .value(split_graph_ir.replay_body_templates
                                 .row(split_graph_ir.graph_launch_bodies
                                          .row(GraphLaunchBodyId(0))
                                          .replay_body_template_id)
                                 .op_sequence_symbol_id)
                      .find("comm:hcom_allReduce/Reduce_Inline") !=
                  std::string::npos,
          "split ACLGraph body lost HCCL task identity");

  std::filesystem::remove_all(graph_dir);
  std::filesystem::remove_all(launch_identity_dir);
  return 0;
}
