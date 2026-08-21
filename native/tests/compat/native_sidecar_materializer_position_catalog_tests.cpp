#include "native_sidecar_materializer_test_support.h"

#include "traceloom/testing/test_util.h"

namespace traceloom::testing::sidecar_materializer {

void run_position_projection_catalog_tests(const std::string& path) {
  using traceloom::testing::require;
  require(run_scalar_text(
              path,
              "SELECT value FROM traceloom_metadata WHERE key="
              "'structural_coordinate_model'") ==
          "hierarchical_position_occurrence_v1");
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_analysis_surface WHERE "
              "surface_name IN ('hierarchical_position', "
              "'position_refinement', 'position_occurrence', "
              "'position_direct_member', 'replay_hierarchical_position', "
              "'replay_position_direct_member')") == 6);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_projection_recipe WHERE "
              "projection_name LIKE 'hpo_%' OR projection_name LIKE "
              "'replay_hpo_%'") == 8);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection='hpo_positions' AND "
              "target_projection IN ('hpo_refinements','hpo_occurrences') "
              "AND source_column='position_id'") == 2);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection='hpo_occurrences' AND "
              "target_projection='hpo_members' AND "
              "source_column='occurrence_id'") == 1);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection='hpo_members' AND "
              "target_projection='event_audit' AND source_column='event_id'") ==
          1);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection='replay_hpo_positions' AND "
              "target_projection IN ('replay_hpo_refinements', "
              "'replay_hpo_occurrences') AND source_column='position_id'") ==
          2);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection='replay_hpo_occurrences' AND "
              "target_projection='replay_hpo_members' AND "
              "source_column='occurrence_id'") == 1);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_projection_recipe WHERE "
              "projection_name IN ('hpo_members','replay_hpo_members') AND "
              "example_sql LIKE '%ORDER BY slot_ordinal, member_order%'") ==
          2);
}

}  // namespace traceloom::testing::sidecar_materializer
