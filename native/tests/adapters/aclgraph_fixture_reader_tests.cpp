#include "traceloom/adapters/aclgraph_fixture_reader.h"
#include "traceloom/testing/test_util.h"

#include <map>
#include <set>
#include <string>

namespace {

std::string fixture_path(const std::string& name) {
  return std::string(TRACELOOM_WORKSPACE_ROOT) +
         "/drafts/refactor/80_tests_fixtures/fixtures/aclgraph/" + name +
         ".json";
}

std::set<std::string> unique_replay_unit_ids(
    const traceloom::AclGraphSemanticFixture& fixture) {
  std::set<std::string> ids;
  for (const traceloom::AclGraphHltAnchorSeedFixtureRow& seed :
       fixture.hlt_anchor_seeds) {
    ids.insert(seed.replay_unit_id);
  }
  return ids;
}

std::set<std::string> unique_launch_activity_ids(
    const traceloom::AclGraphSemanticFixture& fixture) {
  std::set<std::string> ids;
  for (const traceloom::AclGraphHltAnchorSeedFixtureRow& seed :
       fixture.hlt_anchor_seeds) {
    if (!seed.launch_activity_id.empty()) {
      ids.insert(seed.launch_activity_id);
    }
  }
  return ids;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  {
    const AclGraphSemanticFixture fixture =
        load_aclgraph_semantic_fixture(
            fixture_path("aclgraph_hlt_single_unit"));
    require(fixture.fixture_id == "aclgraph_hlt_single_unit");
    require(fixture.capture_slots.size() == fixture.golden.capture_slot_count);
    require(fixture.capture_dictionary.size() == 3);
    require(fixture.replay_activities.size() ==
            fixture.golden.replay_activity_count);
    require(fixture.replay_units.size() == fixture.golden.replay_unit_count);
    require(fixture.hlt_anchor_seeds.size() == fixture.golden.hlt_anchor_count);
    require(flat_hlt_anchor_sequence(fixture) ==
            fixture.golden.flat_hlt_sequence);
    require(fixture.golden.launch_anchor_count == 0);
    require(derive_aclgraph_diagnostic_counts(fixture).empty());
  }

  {
    const AclGraphSemanticFixture fixture =
        load_aclgraph_semantic_fixture(
            fixture_path("aclgraph_hlt_multi_unit"));
    require(fixture.replay_activities.size() == 1);
    require(fixture.replay_units.size() == 2);
    require(fixture.replay_unit_boundaries.size() == 1);
    require(fixture.replay_unit_boundaries[0].effective_unit_count ==
            fixture.golden.boundary_effective_unit_count);
    require(flat_hlt_anchor_sequence(fixture) ==
            fixture.golden.flat_hlt_sequence);
    require(unique_replay_unit_ids(fixture).size() ==
            fixture.golden.unique_replay_unit_ids_in_anchors);
  }

  {
    const AclGraphSemanticFixture fixture =
        load_aclgraph_semantic_fixture(
            fixture_path("aclgraph_hlt_launch_metadata"));
    require(fixture.replay_activities.size() == 2);
    require(fixture.replay_units.size() == 1);
    require(flat_hlt_anchor_sequence(fixture) ==
            fixture.golden.flat_hlt_sequence);
    require(!fixture.golden.launch_boundary_used_as_anchor);
    require(unique_launch_activity_ids(fixture).size() == 2);
    require(unique_replay_unit_ids(fixture).size() == 1);
  }

  {
    const AclGraphSemanticFixture fixture =
        load_aclgraph_semantic_fixture(
            fixture_path("aclgraph_hlt_partial_coverage"));
    require(fixture.replay_subslots.size() ==
            fixture.golden.replay_tiling_subslot_count);
    require(fixture.replay_tilings.size() == 1);
    require(fixture.replay_tilings[0].matched_count ==
            fixture.golden.replay_tiling_matched_count);
    require(fixture.replay_tilings[0].unmatched_count ==
            fixture.golden.replay_tiling_unmatched_count);
    require(flat_hlt_anchor_sequence(fixture) ==
            fixture.golden.normal_flat_hlt_sequence);
    require(derive_aclgraph_diagnostic_counts(fixture)
                .at("replay_tiling_partial_coverage") == 1);
  }

  {
    const AclGraphSemanticFixture fixture =
        load_aclgraph_semantic_fixture(
            fixture_path("aclgraph_hlt_capture_variation"));
    require(fixture.capture_slots.size() == 6);
    require(fixture.capture_dictionary.size() ==
            fixture.golden.capture_dictionary_count);
    require(fixture.capture_dictionary[1].slot_symbol == "L");
    require(fixture.capture_dictionary[1].unique_match_signature_count ==
            fixture.golden.layer_unique_match_signature_count);
    require(derive_aclgraph_diagnostic_counts(fixture)
                .at("capture_dictionary_variation") == 1);
  }

  {
    const AclGraphSemanticFixture fixture =
        load_aclgraph_semantic_fixture(
            fixture_path("aclgraph_hlt_split_confidence"));
    const std::map<std::string, std::uint32_t> diagnostics =
        derive_aclgraph_diagnostic_counts(fixture);
    require(diagnostics.at("replay_unit_unsplit") == 1);
    for (const AclGraphReplayUnitBoundaryFixtureRow& boundary :
         fixture.replay_unit_boundaries) {
      const auto found =
          fixture.golden.split_confidence.find(boundary.split_source);
      require(found != fixture.golden.split_confidence.end());
      require(found->second == boundary.confidence);
    }
  }

  {
    const AclGraphSemanticFixture fixture =
        load_aclgraph_semantic_fixture(
            fixture_path("aclgraph_python_minimal_assets"));
    require(fixture.fixture_id == "aclgraph_python_minimal_assets");
    require(fixture.capture_slots.size() == fixture.golden.capture_slot_count);
    require(fixture.capture_dictionary.size() ==
            fixture.golden.capture_dictionary_count);
    require(fixture.replay_activities.size() ==
            fixture.golden.replay_activity_count);
    require(fixture.replay_units.size() == fixture.golden.replay_unit_count);
    require(fixture.replay_subslots.size() == 6);
    require(fixture.hlt_anchor_seeds.size() == fixture.golden.hlt_anchor_count);
    require(flat_hlt_anchor_sequence(fixture) ==
            fixture.golden.flat_hlt_sequence);
    require(derive_aclgraph_diagnostic_counts(fixture)
                .at("replay_tiling_partial_coverage") == 1);
  }

  return 0;
}
