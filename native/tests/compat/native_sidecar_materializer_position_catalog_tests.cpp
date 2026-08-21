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
              "SELECT COUNT(*) FROM traceloom_analysis_surface WHERE "
              "surface_name IN ('execution_tree_edge', "
              "'execution_tree_edge_role', 'execution_tree_edge_cost')") ==
          3);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_projection_recipe WHERE "
              "projection_name IN ('tree_edge_roles', 'tree_edges', "
              "'equivalent_tree_edges')") == 3);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_projection_recipe WHERE "
              "projection_name IN ('occurrence_host_windows', "
              "'occurrence_host_context')") == 2);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_projection_recipe WHERE "
              "projection_name IN ('edge_role_bubble_summary', "
              "'edge_role_bubbles')") == 2);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_projection_recipe WHERE "
              "projection_name IN ('scope_catalog','scope_occurrences',"
              "'scope_hierarchy','scope_members','position_population',"
              "'position_occurrences','scope_host_windows',"
              "'scope_host_context') AND display_order >= 100 AND "
              "purpose LIKE 'compatibility:%'") == 8);
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
              "WHERE source_projection='hpo_positions' AND "
              "target_projection='tree_edge_roles' AND "
              "source_column='position_id'") == 1);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection='hpo_occurrences' AND "
              "target_projection='tree_edges' AND "
              "source_column='occurrence_id'") == 1);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection='tree_edge_roles' AND "
              "target_projection='equivalent_tree_edges' AND "
              "source_column='edge_role_id'") == 1);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection='tree_edges' AND "
              "target_projection='equivalent_tree_edges' AND "
              "source_column='edge_role_id'") == 1);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection='equivalent_tree_edges' AND "
              "target_projection IN ('occurrence_host_windows', "
              "'occurrence_host_context') AND "
              "source_column='child_occurrence_id'") == 2);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection='tree_edge_roles' AND "
              "target_projection IN ('edge_role_bubble_summary', "
              "'edge_role_bubbles') AND source_column='edge_role_id'") == 2);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection='edge_role_bubble_summary' AND "
              "target_projection='edge_role_bubbles' AND "
              "source_column='edge_role_id'") == 1);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection='edge_role_bubbles' AND "
              "target_projection IN ('occurrence_host_windows', "
              "'occurrence_host_context') AND "
              "source_column='child_occurrence_id'") == 2);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection='edge_role_bubbles' AND "
              "target_projection='host_window_calls' AND "
              "source_column='host_interval_id'") == 1);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_projection_recipe WHERE "
              "projection_name IN ('edge_role_bubble_summary', "
              "'edge_role_bubbles') AND example_sql LIKE "
              "'%selected_role AS MATERIALIZED%' AND example_sql LIKE "
              "'%selected_bubble AS MATERIALIZED%' AND example_sql LIKE "
              "'%CROSS JOIN traceloom_structure_bubble_occurrence%'") == 2);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection IN ('occurrence_host_windows', "
              "'occurrence_host_context') AND "
              "target_projection='host_window_calls' AND "
              "source_column='interval_id'") == 2);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_projection_recipe WHERE "
              "projection_name IN ('occurrence_host_windows', "
              "'occurrence_host_context') AND "
              "example_sql LIKE '%tree.semantic_projection%' AND "
              "example_sql NOT LIKE '%:node_id%'") == 2);
  require(run_scalar_int(
              path,
              "SELECT COUNT(*) FROM traceloom_projection_recipe WHERE "
              "projection_name IN ('tree_edges','equivalent_tree_edges') "
              "AND example_sql NOT LIKE '%slot_ordinal%' AND "
              "example_sql NOT LIKE '%member_order%'") == 2);
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
