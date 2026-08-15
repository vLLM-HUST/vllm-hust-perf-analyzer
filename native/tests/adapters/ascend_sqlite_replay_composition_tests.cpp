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

  const std::filesystem::path body_mismatch_dir =
      temp_ascend_profile_dir("_body_mismatch");
  std::filesystem::create_directories(body_mismatch_dir);
  const std::string body_mismatch_path =
      (body_mismatch_dir / "msprof.db").string();
  materialize_ascend_graph_fixture(body_mismatch_dir, "body_mismatch");
  apply_ascend_fixture_mutation(body_mismatch_path, "body_mismatch",
                                "body_sequence_mismatch.sql");
  NativeIr body_mismatch_ir =
      AscendSQLiteAdapter(body_mismatch_path, "graph_body_mismatch").load();
  require(body_mismatch_ir.replay_composition_candidates.size() == 1 &&
              body_mismatch_ir.replay_composition_slots.size() == 1 &&
              body_mismatch_ir.replay_composition_regions.size() == 7 &&
              body_mismatch_ir.replay_composition_region_members.size() == 7,
          "body mismatch profile lost exact composition membership");
  const ReplayCompositionCandidateRow& body_mismatch_candidate =
      body_mismatch_ir.replay_composition_candidates.row(
          ReplayCompositionCandidateId(0));
  require(body_mismatch_candidate.shape_policy ==
                  ReplayCompositionShapePolicy::kUnclassified &&
              replay_composition_candidate_has_exact_structure(
                  body_mismatch_candidate) &&
              body_mismatch_ir.replay_composition_slots
                      .row(ReplayCompositionSlotId(0))
                      .role == ReplayCompositionSlotRole::kGeneric,
          "generic periodic composition retained an H/L/T dependency");
  std::size_t recognized_regions = 0;
  std::size_t mismatched_regions = 0;
  for (const ReplayCompositionRegionRow& region :
       body_mismatch_ir.replay_composition_regions.rows()) {
    if (region.status ==
        ReplayCompositionRegionStatus::kRecognizedCompletePattern) {
      ++recognized_regions;
    } else if (region.status ==
               ReplayCompositionRegionStatus::kUnrecognizedBodyMismatch) {
      ++mismatched_regions;
    }
  }
  require(recognized_regions == 6 && mismatched_regions == 1 &&
              body_mismatch_ir.replay_composition_regions.row(
                  ReplayCompositionRegionId(3))
                      .status == ReplayCompositionRegionStatus::
                                     kUnrecognizedBodyMismatch,
          "repeated graph identity silently accepted a changed compute body");
  require(body_mismatch_ir.replay_units.size() == 6 &&
              body_mismatch_ir.graph_templates.size() == 1 &&
              body_mismatch_ir.replay_unit_launch_members.size() == 6,
          "generic periodic composition did not promote matching bodies");
  for (const ReplayUnitRow& unit : body_mismatch_ir.replay_units.rows()) {
    require(unit.replay_composition_region_id.valid(),
            "generic exact unit fell back to legacy reconstruction");
  }

  FlatAnchorBuildConfig generic_anchor_config;
  generic_anchor_config.filter_auxiliary_task_anchors = true;
  generic_anchor_config.skip_events_covered_by_replay_units = true;
  const FlatAnchorBuildStats generic_anchor_stats =
      build_flat_anchors(body_mismatch_ir, generic_anchor_config);
  require(generic_anchor_stats.device_event_anchors == 7 &&
              body_mismatch_ir.protected_intervals.size() == 6,
          "generic exact units did not project to protected anchors");
  std::size_t graph_anchor_count = 0;
  std::size_t changed_body_anchor_count = 0;
  for (const AnchorRow& anchor : body_mismatch_ir.anchors.rows()) {
    const std::string symbol = body_mismatch_ir.symbols.value(anchor.symbol_id);
    if (anchor.kind == AnchorKind::kGraphReplayUnit && symbol == "ACLG") {
      ++graph_anchor_count;
    } else if (anchor.kind == AnchorKind::kDeviceEvent && symbol == "Sub") {
      ++changed_body_anchor_count;
    } else {
      require(false,
              "generic exact unit projected an unexpected semantic anchor");
    }
  }
  require(graph_anchor_count == 6 && changed_body_anchor_count == 1,
          "changed-body evidence was lost or normalized into the exact family");

  const std::filesystem::path exact_hlt_dir =
      temp_ascend_profile_dir("_exact_hlt");
  std::filesystem::create_directories(exact_hlt_dir);
  const std::string exact_hlt_path =
      (exact_hlt_dir / "msprof.db").string();
  materialize_ascend_graph_fixture(exact_hlt_dir, "exact_hlt");
  NativeIr exact_hlt_ir =
      AscendSQLiteAdapter(exact_hlt_path, "graph_exact_hlt").load();
  require(exact_hlt_ir.replay_composition_candidates.size() == 2 &&
              exact_hlt_ir.replay_composition_slots.size() == 6,
          "exact HLT profile did not preserve prefill/decode compositions");
  require(exact_hlt_ir.replay_composition_candidates
                      .row(ReplayCompositionCandidateId(0))
                      .boundary_policy ==
                  ReplayCompositionBoundaryPolicy::
                      kExactOneShotLeadingComposition &&
              exact_hlt_ir.replay_composition_candidates
                      .row(ReplayCompositionCandidateId(1))
                      .boundary_policy ==
                  ReplayCompositionBoundaryPolicy::kExactPeriodicSuffix,
          "exact HLT prefill/decode boundary policies mismatch");
  require(exact_hlt_ir.replay_composition_regions.size() == 5 &&
              exact_hlt_ir.replay_composition_region_members.size() == 14,
          "exact HLT profile lost prefill/decode/tail membership");
  require(exact_hlt_ir.replay_units.size() == 4 &&
              exact_hlt_ir.graph_templates.size() == 2 &&
              exact_hlt_ir.replay_unit_launch_members.size() == 12,
          "exact HLT prefill/decode regions did not cut over to units");
  require(exact_hlt_ir.replay_composition_regions
                  .row(ReplayCompositionRegionId(4))
                  .status == ReplayCompositionRegionStatus::
                                 kUnrecognizedIncompleteTail,
          "exact HLT prefix tail should remain explicitly unrecognized");
  for (const ReplayUnitRow& unit : exact_hlt_ir.replay_units.rows()) {
    require(unit.replay_composition_region_id.valid(),
            "exact HLT replay unit lost its composition region link");
  }
  require(exact_hlt_ir.captured_graph_instances.size() == 6 &&
              exact_hlt_ir.captured_graph_streams.size() == 12 &&
              exact_hlt_ir.graph_launch_bodies.size() == 14,
          "exact HLT fixture lost its multi-stream body evidence");
  for (const GraphLaunchBodyRow& body :
       exact_hlt_ir.graph_launch_bodies.rows()) {
    const ReplayBodyTemplateRow& body_template =
        exact_hlt_ir.replay_body_templates.row(body.replay_body_template_id);
    require(body.compute_task_count == 2 &&
                body.communication_task_count == 0 &&
                body.stream_count == 2 &&
                body_template.stream_count == 2 &&
                body_template.topology_policy ==
                    ReplayBodyTopologyPolicy::kCapturedStreamSetUnordered,
            "exact HLT projection did not retain both graph body lanes");
  }
  for (std::size_t index = 0;
       index < exact_hlt_ir.replay_unit_launch_members.size(); ++index) {
    const ReplayUnitLaunchMemberRow& member =
        exact_hlt_ir.replay_unit_launch_members.row(
            ReplayUnitLaunchMemberId(index));
    require(member.member_order == index % 3,
            "exact HLT replay membership order is not unit-local");
  }

  const std::filesystem::path split_exact_hlt_dir =
      exact_hlt_dir / "split_profile";
  materialize_ascend_graph_split_fixture(split_exact_hlt_dir, "exact_hlt");
  const NativeIr split_exact_hlt_ir =
      AscendSQLiteAdapter(split_exact_hlt_dir.string(),
                          "graph_exact_hlt_split")
          .load();
  require(split_exact_hlt_ir.replay_composition_candidates.size() == 2 &&
              split_exact_hlt_ir.replay_composition_slots.size() == 6 &&
              split_exact_hlt_ir.replay_composition_regions.size() == 5 &&
              split_exact_hlt_ir.replay_composition_region_members.size() ==
                  14 &&
              split_exact_hlt_ir.replay_units.size() == 4 &&
              split_exact_hlt_ir.replay_unit_launch_members.size() == 12,
          "split exact HLT reconstruction differs from monolithic");
  require(split_exact_hlt_ir.replay_composition_regions
                  .row(ReplayCompositionRegionId(4))
                  .status == ReplayCompositionRegionStatus::
                                 kUnrecognizedIncompleteTail,
          "split exact HLT tail did not remain unrecognized");
  std::vector<std::uint64_t> exact_hlt_body_hashes;
  std::vector<std::uint64_t> split_exact_hlt_body_hashes;
  for (const ReplayBodyTemplateRow& body :
       exact_hlt_ir.replay_body_templates.rows()) {
    exact_hlt_body_hashes.push_back(body.exact_sequence_hash);
  }
  for (const ReplayBodyTemplateRow& body :
       split_exact_hlt_ir.replay_body_templates.rows()) {
    split_exact_hlt_body_hashes.push_back(body.exact_sequence_hash);
  }
  std::sort(exact_hlt_body_hashes.begin(), exact_hlt_body_hashes.end());
  std::sort(split_exact_hlt_body_hashes.begin(),
            split_exact_hlt_body_hashes.end());
  require(split_exact_hlt_body_hashes == exact_hlt_body_hashes,
          "split exact HLT body identities differ from monolithic");

  FlatAnchorBuildConfig exact_anchor_config;
  exact_anchor_config.filter_auxiliary_task_anchors = true;
  exact_anchor_config.skip_events_covered_by_replay_units = true;
  const FlatAnchorBuildStats exact_anchor_stats =
      build_flat_anchors(exact_hlt_ir, exact_anchor_config);
  require(exact_anchor_stats.device_event_anchors == 16 &&
              exact_anchor_stats.communication_anchors == 0 &&
              exact_anchor_stats.preserved_unclassified_task_events == 4 &&
              exact_hlt_ir.protected_intervals.size() == 4,
          "exact HLT projection did not preserve complete units and unknown operators");
  std::size_t head_anchors = 0;
  std::size_t layer_anchors = 0;
  std::size_t tail_anchors = 0;
  std::size_t raw_anchors = 0;
  for (const AnchorRow& anchor : exact_hlt_ir.anchors.rows()) {
    switch (anchor.kind) {
      case AnchorKind::kGraphH:
        ++head_anchors;
        break;
      case AnchorKind::kGraphL:
        ++layer_anchors;
        break;
      case AnchorKind::kGraphT:
        ++tail_anchors;
        break;
      case AnchorKind::kDeviceEvent:
        ++raw_anchors;
        break;
      default:
        break;
    }
  }
  require(head_anchors == 4 && layer_anchors == 4 && tail_anchors == 4 &&
              raw_anchors == 4,
          "exact HLT anchor roles mismatch");
  for (std::size_t index = 0;
       index < exact_hlt_ir.protected_intervals.size(); ++index) {
    const ProtectedIntervalRow& interval =
        exact_hlt_ir.protected_intervals.row(ProtectedIntervalId(index));
    require(interval.first_token_id.value() == index * 3 &&
                interval.last_token_id.value() == index * 3 + 2 &&
                interval.boundary_policy == BoundaryPolicy::kNoCross,
            "unknown operator was folded into an exact H/L/T interval");
  }

  const std::filesystem::path missing_body_dir =
      temp_ascend_profile_dir("_missing_body");
  std::filesystem::create_directories(missing_body_dir);
  const std::string missing_body_path =
      (missing_body_dir / "msprof.db").string();
  materialize_ascend_graph_fixture(missing_body_dir, "exact_hlt");
  apply_ascend_fixture_mutation(missing_body_path, "exact_hlt",
                                "missing_body.sql");
  const NativeIr missing_body_ir =
      AscendSQLiteAdapter(missing_body_path, "graph_missing_body").load();
  std::size_t missing_body_regions = 0;
  for (const ReplayCompositionRegionRow& region :
       missing_body_ir.replay_composition_regions.rows()) {
    if (region.status == ReplayCompositionRegionStatus::
                             kUnrecognizedMissingBodyEvidence) {
      ++missing_body_regions;
    }
  }
  require(missing_body_ir.replay_composition_candidates.size() == 2 &&
              missing_body_ir.replay_composition_regions.size() == 5 &&
              missing_body_regions == 1 &&
              missing_body_ir.graph_launch_bodies.size() == 13 &&
              missing_body_ir.replay_units.size() == 3 &&
              missing_body_ir.replay_unit_launch_members.size() == 9,
          "missing graph body evidence did not stay typed and unpromoted");

  const std::filesystem::path truncated_launch_dir =
      temp_ascend_profile_dir("_truncated_launch");
  std::filesystem::create_directories(truncated_launch_dir);
  const std::string truncated_launch_path =
      (truncated_launch_dir / "msprof.db").string();
  materialize_ascend_graph_fixture(truncated_launch_dir, "exact_hlt");
  apply_ascend_fixture_mutation(truncated_launch_path, "exact_hlt",
                                "truncated_completion.sql");
  const NativeIr truncated_launch_ir =
      AscendSQLiteAdapter(truncated_launch_path, "graph_truncated_launch")
          .load();
  std::size_t missing_completion_regions = 0;
  for (const ReplayCompositionRegionRow& region :
       truncated_launch_ir.replay_composition_regions.rows()) {
    if (region.status == ReplayCompositionRegionStatus::
                             kUnrecognizedMissingCompletionEvidence) {
      ++missing_completion_regions;
      const ReplayCompositionCandidateRow& candidate =
          truncated_launch_ir.replay_composition_candidates.row(
              region.replay_composition_candidate_id);
      require(candidate.identity_policy ==
                  ReplayCompositionIdentityPolicy::kUnavailable &&
                  candidate.boundary_policy ==
                      ReplayCompositionBoundaryPolicy::
                          kIncompleteLaunchEvidence &&
                  region.observed_launch_count == 1 &&
                  region.expected_launch_count == 1,
              "truncated launch region lost its explicit evidence policy");
    }
  }
  require(truncated_launch_ir.graph_launch_occurrences.size() == 14 &&
              truncated_launch_ir.graph_launch_bodies.size() == 13 &&
              truncated_launch_ir.replay_composition_candidates.size() == 3 &&
              truncated_launch_ir.replay_composition_regions.size() == 6 &&
              truncated_launch_ir.replay_composition_region_members.size() ==
                  14 &&
              missing_completion_regions == 1 &&
              truncated_launch_ir.replay_units.size() == 4 &&
              truncated_launch_ir.replay_unit_launch_members.size() == 12,
          "truncated completion evidence disappeared or changed exact units");
  const GraphLaunchOccurrenceRow& truncated_launch =
      truncated_launch_ir.graph_launch_occurrences.row(
          GraphLaunchOccurrenceId(13));
  require(truncated_launch.match_policy == GraphLaunchMatchPolicy::kUnmatched &&
              !truncated_launch.notify_record_task_id.valid() &&
              truncated_launch.raw_graph_connection_id < 0,
          "truncated launch unexpectedly acquired completion identity");

  const auto require_missing_body_capability = [](const NativeIr& candidate,
                                                  const char* message) {
    std::size_t capability_regions = 0;
    for (const ReplayCompositionRegionRow& region :
         candidate.replay_composition_regions.rows()) {
      if (region.status == ReplayCompositionRegionStatus::
                               kUnrecognizedMissingBodyCapability) {
        ++capability_regions;
      }
    }
    require(candidate.graph_launch_occurrences.size() == 14 &&
                candidate.replay_composition_candidates.size() == 1 &&
                candidate.replay_composition_regions.size() == 5 &&
                candidate.replay_composition_region_members.size() == 14 &&
                capability_regions == 5 && candidate.replay_units.empty() &&
                candidate.replay_unit_launch_members.empty(),
            message);
  };

  const std::filesystem::path incomplete_compute_dir =
      temp_ascend_profile_dir("_incomplete_compute_schema");
  std::filesystem::create_directories(incomplete_compute_dir);
  const std::string incomplete_compute_path =
      (incomplete_compute_dir / "msprof.db").string();
  materialize_ascend_graph_fixture(incomplete_compute_dir, "exact_hlt");
  apply_ascend_fixture_mutation(incomplete_compute_path, "exact_hlt",
                                "incomplete_compute_schema.sql");
  require_missing_body_capability(
      AscendSQLiteAdapter(incomplete_compute_path,
                          "graph_incomplete_compute_schema")
          .load(),
      "incompatible compute schema was promoted or disappeared");

  const std::filesystem::path missing_communication_dir =
      temp_ascend_profile_dir("_missing_communication_schema");
  std::filesystem::create_directories(missing_communication_dir);
  const std::string missing_communication_path =
      (missing_communication_dir / "msprof.db").string();
  materialize_ascend_graph_fixture(missing_communication_dir, "exact_hlt");
  apply_ascend_fixture_mutation(missing_communication_path, "exact_hlt",
                                "missing_communication_schema.sql");
  require_missing_body_capability(
      AscendSQLiteAdapter(missing_communication_path,
                          "graph_missing_communication_schema")
          .load(),
      "missing communication schema was treated as an observed empty body");

  const std::filesystem::path missing_capture_dir =
      temp_ascend_profile_dir("_missing_capture_schema");
  std::filesystem::create_directories(missing_capture_dir);
  const std::string missing_capture_path =
      (missing_capture_dir / "msprof.db").string();
  materialize_ascend_graph_fixture(missing_capture_dir, "exact_hlt");
  require(std::filesystem::remove(
              missing_capture_dir / "host" / "sqlite" / "stream_info.db"),
          "failed to remove capture schema fixture DB");
  require_missing_body_capability(
      AscendSQLiteAdapter(missing_capture_path,
                          "graph_missing_capture_schema")
          .load(),
      "missing capture stream schema was treated as a complete stream set");

  const std::filesystem::path incomplete_split_dir =
      exact_hlt_dir / "split_incomplete_task_info";
  materialize_ascend_graph_split_fixture(incomplete_split_dir, "exact_hlt");
  const std::string incomplete_split_task_info_path =
      (incomplete_split_dir / "host" / "sqlite" / "ge_info.db").string();
  apply_ascend_fixture_mutation(incomplete_split_task_info_path, "exact_hlt",
                                "incomplete_split_task_info.sql");
  require_missing_body_capability(
      AscendSQLiteAdapter(incomplete_split_dir.string(),
                          "graph_incomplete_split_task_info")
          .load(),
      "incomplete split TaskInfo schema was promoted or rejected");

  const std::filesystem::path device_order_dir =
      temp_ascend_profile_dir("_device_order_without_host_api");
  std::filesystem::create_directories(device_order_dir);
  const std::string device_order_path =
      (device_order_dir / "msprof.db").string();
  materialize_ascend_graph_fixture(device_order_dir, "exact_hlt");
  apply_ascend_fixture_mutation(device_order_path, "exact_hlt",
                                "device_order_without_host_api.sql");
  const NativeIr device_order_ir =
      AscendSQLiteAdapter(device_order_path, "graph_device_order").load();
  require(device_order_ir.replay_composition_candidates.size() == 2 &&
              device_order_ir.replay_units.size() == 4 &&
              device_order_ir.replay_unit_launch_members.size() == 12 &&
              std::all_of(
                  device_order_ir.replay_units.rows().begin(),
                  device_order_ir.replay_units.rows().end(),
                  [](const ReplayUnitRow& unit) {
                    return unit.replay_composition_region_id.valid();
                  }) &&
              std::all_of(
                  device_order_ir.replay_composition_candidates.rows().begin(),
                  device_order_ir.replay_composition_candidates.rows().end(),
                  [](const ReplayCompositionCandidateRow& candidate) {
                    return candidate.order_policy ==
                           ReplayCompositionOrderPolicy::
                               kDeviceExecutionOrder;
                  }),
          "missing host API should retain exact device-order reconstruction");

  std::filesystem::remove_all(body_mismatch_dir);
  std::filesystem::remove_all(exact_hlt_dir);
  std::filesystem::remove_all(missing_body_dir);
  std::filesystem::remove_all(truncated_launch_dir);
  std::filesystem::remove_all(incomplete_compute_dir);
  std::filesystem::remove_all(missing_communication_dir);
  std::filesystem::remove_all(missing_capture_dir);
  std::filesystem::remove_all(device_order_dir);
  return 0;
}
