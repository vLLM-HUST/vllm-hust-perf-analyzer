#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace traceloom {

struct AclGraphCaptureSlotFixtureRow {
  std::string capture_slot_id;
  std::uint32_t capture_slot_idx = 0;
  std::uint32_t capture_group_idx = 0;
  std::uint32_t capture_group_size = 0;
  std::uint32_t capture_slot_in_group = 0;
  std::string slot_kind;
  std::string slot_symbol;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::string body_match_signature;
};

struct AclGraphCaptureDictionaryFixtureRow {
  std::string capture_dictionary_id;
  std::uint32_t dictionary_idx = 0;
  std::string slot_kind;
  std::string slot_symbol;
  std::vector<std::string> capture_slot_ids;
  std::uint32_t capture_slot_count = 0;
  std::uint32_t unique_match_signature_count = 0;
  std::string variation_summary;
};

struct AclGraphReplayActivityFixtureRow {
  std::string replay_activity_id;
  std::uint32_t activity_idx = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::vector<std::uint32_t> stream_ids;
  std::uint32_t raw_child_task_count = 0;
};

struct AclGraphReplayUnitBoundaryFixtureRow {
  std::string boundary_set_id;
  std::string replay_activity_id;
  std::uint32_t expected_unit_count = 0;
  std::uint32_t effective_unit_count = 0;
  std::string unit_source;
  std::string split_source;
  std::string confidence;
  std::vector<std::int64_t> boundary_ns;
};

struct AclGraphReplayUnitFixtureRow {
  std::string replay_unit_id;
  std::string replay_activity_id;
  std::string boundary_set_id;
  std::uint32_t unit_idx_global = 0;
  std::uint32_t unit_idx_in_activity = 0;
  std::uint32_t unit_count_in_activity = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

struct AclGraphReplayTilingFixtureRow {
  std::string replay_tiling_id;
  std::string replay_unit_id;
  std::string policy;
  std::uint32_t subslot_count = 0;
  std::string sequence;
  std::uint32_t matched_count = 0;
  std::uint32_t unmatched_count = 0;
  std::string coverage;
  std::string top_mismatches;
};

struct AclGraphReplaySubslotFixtureRow {
  std::string subslot_id;
  std::string replay_tiling_id;
  std::uint32_t subslot_idx = 0;
  std::string slot_kind;
  std::string slot_symbol;
  bool matched = false;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::uint32_t stream_id = 0;
};

struct AclGraphHltAnchorSeedFixtureRow {
  std::string anchor_seed_id;
  std::string replay_unit_id;
  std::string subslot_id;
  std::string launch_activity_id;
  std::string symbol;
  std::string slot_symbol;
  std::string semantic_role;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
};

struct AclGraphFixtureGolden {
  std::uint32_t capture_slot_count = 0;
  std::uint32_t capture_group_count = 0;
  std::uint32_t capture_group_size = 0;
  std::uint32_t capture_dictionary_count = 0;
  std::string dictionary_sequence;
  std::uint32_t replay_activity_count = 0;
  std::uint32_t replay_unit_count = 0;
  std::uint32_t boundary_effective_unit_count = 0;
  std::uint32_t hlt_anchor_count = 0;
  std::string flat_hlt_sequence;
  std::string normal_flat_hlt_sequence;
  std::uint32_t launch_anchor_count = 0;
  bool launch_boundary_used_as_anchor = false;
  std::uint32_t unique_launch_activity_ids_in_anchors = 0;
  std::uint32_t unique_replay_unit_ids_in_anchors = 0;
  std::uint32_t replay_tiling_subslot_count = 0;
  std::uint32_t replay_tiling_matched_count = 0;
  std::uint32_t replay_tiling_unmatched_count = 0;
  std::uint32_t layer_unique_match_signature_count = 0;
  std::map<std::string, std::string> split_confidence;
  std::map<std::string, std::uint32_t> diagnostic_codes;
};

struct AclGraphSemanticFixture {
  std::string fixture_id;
  std::string description;
  std::vector<AclGraphCaptureSlotFixtureRow> capture_slots;
  std::vector<AclGraphCaptureDictionaryFixtureRow> capture_dictionary;
  std::vector<AclGraphReplayActivityFixtureRow> replay_activities;
  std::vector<AclGraphReplayUnitBoundaryFixtureRow> replay_unit_boundaries;
  std::vector<AclGraphReplayUnitFixtureRow> replay_units;
  std::vector<AclGraphReplayTilingFixtureRow> replay_tilings;
  std::vector<AclGraphReplaySubslotFixtureRow> replay_subslots;
  std::vector<AclGraphHltAnchorSeedFixtureRow> hlt_anchor_seeds;
  AclGraphFixtureGolden golden;
};

AclGraphSemanticFixture load_aclgraph_semantic_fixture(
    const std::string& path);

std::string flat_hlt_anchor_sequence(const AclGraphSemanticFixture& fixture);

std::map<std::string, std::uint32_t> derive_aclgraph_diagnostic_counts(
    const AclGraphSemanticFixture& fixture);

}  // namespace traceloom
