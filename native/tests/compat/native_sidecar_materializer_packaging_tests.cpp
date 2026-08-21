#include "native_sidecar_materializer_test_support.h"

#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/core/sha256.h"
#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace traceloom::testing::sidecar_materializer {
namespace {

using namespace traceloom;

void require_analysis_surface_queries_prepare(const std::string& path) {
  sqlite3* db = nullptr;
  traceloom::testing::require(
      sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) ==
      SQLITE_OK);
  sqlite3_stmt* catalog = nullptr;
  traceloom::testing::require(
      sqlite3_prepare_v2(db,
                         "SELECT surface_name, example_sql FROM "
                         "traceloom_analysis_surface ORDER BY surface_name",
                         -1, &catalog, nullptr) == SQLITE_OK);
  while (sqlite3_step(catalog) == SQLITE_ROW) {
    const unsigned char* surface_text = sqlite3_column_text(catalog, 0);
    const unsigned char* sql_text = sqlite3_column_text(catalog, 1);
    traceloom::testing::require(surface_text != nullptr && sql_text != nullptr);
    sqlite3_stmt* example = nullptr;
    const int rc = sqlite3_prepare_v2(
        db, reinterpret_cast<const char*>(sql_text), -1, &example, nullptr);
    if (example != nullptr) {
      sqlite3_finalize(example);
    }
    const std::string error_message =
        "analysis surface example SQL did not prepare: " +
        std::string(reinterpret_cast<const char*>(surface_text)) + ": " +
        sqlite3_errmsg(db);
    traceloom::testing::require(rc == SQLITE_OK, error_message.c_str());
  }
  sqlite3_finalize(catalog);

  traceloom::testing::require(
      sqlite3_prepare_v2(
          db,
          "SELECT projection_name, parameter_name, selection_relation, "
          "selection_column FROM traceloom_projection_parameter ORDER BY "
          "projection_name, parameter_order",
          -1, &catalog, nullptr) == SQLITE_OK);
  while (sqlite3_step(catalog) == SQLITE_ROW) {
    const unsigned char* projection_text = sqlite3_column_text(catalog, 0);
    const unsigned char* parameter_text = sqlite3_column_text(catalog, 1);
    const unsigned char* relation_text = sqlite3_column_text(catalog, 2);
    const unsigned char* column_text = sqlite3_column_text(catalog, 3);
    traceloom::testing::require(
        projection_text != nullptr && parameter_text != nullptr &&
        relation_text != nullptr && column_text != nullptr);
    const std::string selection_sql =
        "SELECT " + std::string(reinterpret_cast<const char*>(column_text)) +
        " FROM " + std::string(reinterpret_cast<const char*>(relation_text)) +
        " LIMIT 0";
    sqlite3_stmt* selection = nullptr;
    const int rc = sqlite3_prepare_v2(db, selection_sql.c_str(), -1,
                                      &selection, nullptr);
    if (selection != nullptr) {
      sqlite3_finalize(selection);
    }
    const std::string error_message =
        "projection parameter selection coordinate is invalid: " +
        std::string(reinterpret_cast<const char*>(projection_text)) + "." +
        std::string(reinterpret_cast<const char*>(parameter_text)) + ": " +
        sqlite3_errmsg(db);
    traceloom::testing::require(rc == SQLITE_OK, error_message.c_str());
  }
  sqlite3_finalize(catalog);

  traceloom::testing::require(
      sqlite3_prepare_v2(db,
                         "SELECT projection_name, example_sql FROM "
                         "traceloom_projection_recipe ORDER BY display_order",
                         -1, &catalog, nullptr) == SQLITE_OK);
  while (sqlite3_step(catalog) == SQLITE_ROW) {
    const unsigned char* projection_text = sqlite3_column_text(catalog, 0);
    const unsigned char* sql_text = sqlite3_column_text(catalog, 1);
    traceloom::testing::require(projection_text != nullptr && sql_text != nullptr);
    sqlite3_stmt* example = nullptr;
    const int rc = sqlite3_prepare_v2(
        db, reinterpret_cast<const char*>(sql_text), -1, &example, nullptr);
    if (example != nullptr) {
      sqlite3_finalize(example);
    }
    const std::string error_message =
        "projection recipe example SQL did not prepare: " +
        std::string(reinterpret_cast<const char*>(projection_text)) + ": " +
        sqlite3_errmsg(db);
    traceloom::testing::require(rc == SQLITE_OK, error_message.c_str());
  }
  sqlite3_finalize(catalog);

  traceloom::testing::require(
      sqlite3_prepare_v2(
          db,
          "SELECT c.projection_name, c.result_column, r.example_sql FROM "
          "traceloom_projection_coordinate c JOIN "
          "traceloom_projection_recipe r USING (projection_name) ORDER BY "
          "c.projection_name, c.coordinate_order",
          -1, &catalog, nullptr) == SQLITE_OK);
  while (sqlite3_step(catalog) == SQLITE_ROW) {
    const unsigned char* projection_text = sqlite3_column_text(catalog, 0);
    const unsigned char* column_text = sqlite3_column_text(catalog, 1);
    const unsigned char* sql_text = sqlite3_column_text(catalog, 2);
    traceloom::testing::require(projection_text != nullptr &&
                                column_text != nullptr && sql_text != nullptr);
    sqlite3_stmt* example = nullptr;
    const int rc = sqlite3_prepare_v2(
        db, reinterpret_cast<const char*>(sql_text), -1, &example, nullptr);
    traceloom::testing::require(rc == SQLITE_OK && example != nullptr);
    bool found = false;
    for (int column_idx = 0; column_idx < sqlite3_column_count(example);
         ++column_idx) {
      const char* result_name = sqlite3_column_name(example, column_idx);
      if (result_name != nullptr &&
          std::string(result_name) ==
              reinterpret_cast<const char*>(column_text)) {
        found = true;
        break;
      }
    }
    const std::string error_message =
        "projection coordinate is absent from recipe result: " +
        std::string(reinterpret_cast<const char*>(projection_text)) + "." +
        std::string(reinterpret_cast<const char*>(column_text));
    sqlite3_finalize(example);
    traceloom::testing::require(found, error_message.c_str());
  }
  sqlite3_finalize(catalog);
  sqlite3_close(db);
}

NativeIr build_source_locator_ir(const std::string& source_path,
                                 std::uint64_t source_row_id) {
  NativeIr ir;
  const SourceRefId source = ir.source_refs.append(
      "fixture_sqlite", source_path, "RAW_SENTINEL", 0);
  const SymbolId symbol = ir.symbols.intern("SourceLinkedWork");
  const TraceEventId event = ir.trace_events.append(
      source, source_row_id, 0, 1, 1000, 2000, symbol);
  const AnchorId anchor = ir.anchors.append(
      source, event, ReplayUnitId::invalid(), AnchorKind::kDeviceEvent,
      symbol, 0, 1, 1000, 2000);
  ir.tokens.append(anchor, symbol, 0, 0, 1000, 2000);
  return ir;
}

NativeIr build_symbol_variant_repeat_ir(const std::string& source_path) {
  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("ascend", source_path, "TASK", 0);
  const SymbolId ai_core = ir.symbols.intern("AI_CORE");
  const SymbolId matmul_v2 = ir.symbols.intern("MatMulV2");
  const SymbolId matmul_v3 = ir.symbols.intern("MatMulV3");
  const SymbolId relu = ir.symbols.intern("Relu");
  for (std::uint32_t idx = 0; idx < 8; ++idx) {
    const bool matmul = idx % 2 == 0;
    const SymbolId symbol =
        matmul ? ((idx / 2) % 2 == 0 ? matmul_v2 : matmul_v3) : relu;
    const std::int64_t start_ns = 1000 + idx * 1000;
    const TraceEventId event = ir.trace_events.append(
        source, idx + 1, 0, 5, start_ns,
        start_ns + (matmul ? 600 : 200), symbol);
    ir.tasks.append(source, event, idx + 1, 9000 + idx, -1, ai_core,
                    SymbolId::invalid(), symbol, SymbolId::invalid(),
                    SymbolId::invalid());
  }
  const FlatAnchorBuildStats stats = build_flat_anchors(ir);
  testing::require(stats.tokens == 8);
  return ir;
}

}  // namespace

void run_packaging_materializer_tests() {
  using namespace traceloom;
  using traceloom::testing::require;

  // The analysis database is a new snapshot, not a modified sidecar. It must
  // retain arbitrary raw profiler relations after the source is gone.
  const std::string raw_source_path = temp_db_path();
  const std::string augmented_path = temp_db_path();
  run_sql(raw_source_path,
          "CREATE TABLE RAW_SENTINEL(id INTEGER, payload TEXT);"
          "INSERT INTO RAW_SENTINEL VALUES(7, 'retained profiler evidence')");
  const std::string source_hash_before = sha256_file_hex(raw_source_path);
  compat::NativeCompatibilitySidecarOptions augmented_options;
  augmented_options.source_kind = "cuda_nsys_sqlite";
  augmented_options.evidence_role_config.classification_rules =
      load_default_signal_classification_ruleset();
  augmented_options.evidence_role_config.skip_events_covered_by_replay_units =
      true;
  augmented_options.evidence_role_config.filter_auxiliary_task_anchors = true;
  compat::write_queryable_database_timeline(
      augmented_path, raw_source_path, build_exact_cuda_graph_replay_ir(),
      augmented_options);
  require(sha256_file_hex(raw_source_path) == source_hash_before,
          "augmented DB construction modified the input profiler DB");
  require(run_scalar_text(augmented_path,
                          "SELECT value FROM traceloom_metadata WHERE key = "
                          "'artifact_kind'") ==
          "queryable_database_timeline");
  require(run_scalar_text(augmented_path,
                          "SELECT value FROM traceloom_metadata WHERE key = "
                          "'source_sha256'") == source_hash_before);
  std::remove(raw_source_path.c_str());
  require(run_scalar_text(augmented_path,
                          "SELECT payload FROM RAW_SENTINEL WHERE id = 7") ==
          "retained profiler evidence");
  require(run_scalar_int(augmented_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_v_node_replay_cost_member") == 3);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_evidence_role_policy WHERE "
              "input_format = 'flat_tsv' AND length(manifest_sha256) = 64") ==
          1);
  require(run_scalar_int(augmented_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_evidence_role_rule") > 60);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_evidence_role_rule WHERE "
              "rule_id = 'system.event_reconciliation' AND rule_class = "
              "'provider_relation' AND role = 'anchor' AND required_fields "
              "= 'event_reconciliation_membership' AND "
              "structural_participation = 'identity'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_evidence_role_decision") ==
          run_scalar_int(augmented_path,
                         "SELECT COUNT(*) FROM traceloom_event"));
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_evidence_role_decision d "
              "LEFT JOIN traceloom_event e ON e.event_id = d.event_id AND "
              "e.db_idx = d.db_idx WHERE e.event_id IS NULL") == 0);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_evidence_role_decision d "
              "LEFT JOIN traceloom_evidence_role_rule r ON "
              "r.policy_id = d.policy_id AND r.rule_id = d.rule_id "
              "WHERE r.rule_id IS NULL") == 0);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_evidence_role_decision "
              "WHERE final_role = 'protected_boundary'") == 4);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_protected_interval WHERE "
              "kind = 'graph_replay_unit' AND boundary_policy = 'no_cross' "
              "AND support_state = 'supported'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM "
              "traceloom_v_exact_replay_partition_status WHERE "
              "exact_replay_count = 1 AND unsupported_interval_count = 0 "
              "AND invalid_bound_count = 0 AND overlap_count = 0 AND "
              "support_state = 'supported' AND "
              "reason_code = 'ordered_disjoint_exact_replays'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_exact_replay_partition") ==
          3);
  require(run_scalar_text(
              augmented_path,
              "SELECT group_concat(segment_label, ',') FROM "
              "(SELECT segment_label FROM traceloom_v_exact_replay_partition "
              "ORDER BY segment_order)") == "X1,R1,X2");
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_exact_replay_partition "
              "WHERE support_state != 'supported' OR anchor_count < 0") ==
          0);
  require(run_scalar_int(
              augmented_path,
              "SELECT ABS((SELECT SUM(total_us) FROM "
              "traceloom_v_exact_replay_partition) - (SELECT total_us FROM "
              "traceloom_v_tree_node WHERE parent_node_id IS NULL)) < 1e-9") ==
          1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_evidence_role_placement p "
              "LEFT JOIN traceloom_protected_interval i ON "
              "i.protected_interval_id = p.placement_id "
              "WHERE p.placement_kind = 'protected_interval' AND "
              "i.protected_interval_id IS NULL") == 0);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_evidence_role_placement "
              "WHERE placement_kind = 'protected_interval'") == 4);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_evidence_role_placement "
              "WHERE placement_kind = 'graph_body_member'") == 3);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_evidence_role_placement "
              "WHERE placement_id = 'graph-body-member-0'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT SUM(event_count) FROM "
              "traceloom_v_evidence_role_cost_coverage") == 4);
  require(run_scalar_int(augmented_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_analysis_surface") >= 8);
  require(run_scalar_text(augmented_path,
                          "SELECT value FROM traceloom_metadata "
                          "WHERE key = 'operator_audit_status'") ==
          "observed_inventory");
  require(run_scalar_int(augmented_path,
                         "SELECT COUNT(*) FROM traceloom_operator_audit") ==
          2);
  require(run_scalar_int(
              augmented_path,
              "SELECT occurrence_count FROM traceloom_operator_audit "
              "WHERE operator_name = 'graph_a_gemm' AND "
              "task_type = 'AI_CORE'") == 2);
  require(run_scalar_int(
              augmented_path,
              "SELECT total_duration_ns FROM traceloom_operator_audit "
              "WHERE operator_name = 'graph_a_gemm'") == 20);
  require(run_scalar_int(
              augmented_path,
              "SELECT graph_body_member_count FROM "
              "traceloom_operator_audit WHERE "
              "operator_name = 'graph_a_gemm'") == 2);
  require(run_scalar_int(
              augmented_path,
              "SELECT SUM(anchor_event_count) FROM "
              "traceloom_operator_audit") ==
          0);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_analysis_surface "
              "WHERE surface_name = 'composable_projection' AND "
              "relation_name = 'traceloom_projection_recipe'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_analysis_surface "
              "WHERE surface_name = 'projection_parameter' AND "
              "relation_name = 'traceloom_projection_parameter'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_projection_recipe WHERE "
              "scope_kind != '' AND population_mode != '' AND "
              "resolution != '' AND observation_domain != '' AND "
              "measure_lens != ''") == 40);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_projection_parameter WHERE "
              "parameter_name != '' AND sqlite_type IN ('TEXT', 'INTEGER') "
              "AND coordinate_kind != '' AND selection_relation != '' AND "
              "selection_column != ''") == 46);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_projection_parameter p LEFT "
              "JOIN traceloom_projection_recipe r USING (projection_name) "
              "WHERE r.projection_name IS NULL") == 0);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_projection_coordinate") ==
          140);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_projection_coordinate WHERE "
              "projection_name = 'exact_replay_partition'") == 5);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_projection_coordinate WHERE "
              "projection_name = 'scope_catalog' AND result_column = "
              "'parent_node_id' AND coordinate_kind = "
              "'structural_node_id'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_projection_coordinate c LEFT "
              "JOIN traceloom_projection_recipe r USING (projection_name) "
              "WHERE r.projection_name IS NULL") == 0);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection = 'scope_hierarchy' AND "
              "target_projection = 'scope_occurrences' AND source_column = "
              "'child_node_id' AND target_parameter = 'node_id'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection = 'position_population' AND "
              "target_projection = 'position_occurrences' AND "
              "required_coordinate_count = 2") == 2);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection = 'bubble_occurrences' AND "
              "target_projection = 'host_window_calls' AND source_column = "
              "'host_interval_id'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection = 'host_window_calls' AND "
              "target_projection = 'runtime_call_audit' AND source_column = "
              "'runtime_call_id'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_projection_recipe WHERE "
              "projection_name IN ('replay_cost_units', "
              "'replay_cost_launches', 'replay_cost_members', "
              "'replay_structural_placements')") == 4);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_analysis_surface WHERE "
              "surface_name = 'replay_position_realization_member' AND "
              "relation_name = "
              "'traceloom_v_replay_position_realization_member'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_projection_recipe WHERE "
              "projection_name IN ('scope_exact_replay_members', "
              "'replay_cost_members') AND resolution = "
              "'observed_member_plane' AND example_sql LIKE "
              "'%ORDER BY%observed_order%' AND example_sql NOT LIKE "
              "'%ORDER BY lane_ordinal%'") == 2);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_projection_recipe WHERE "
              "projection_name = 'scope_exact_replay_members' AND "
              "example_sql LIKE "
              "'%position.launch_id = member.node_launch_id%'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection = 'scope_exact_replay_members' AND "
              "target_projection = 'replay_cost_launches' AND "
              "source_column = 'cost_unit_id' AND target_parameter = "
              "'cost_unit_id' AND coordinate_kind = "
              "'replay_cost_unit_id'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection = 'replay_cost_units' AND "
              "target_projection = 'replay_cost_launches' AND "
              "source_column = 'cost_unit_id' AND target_parameter = "
              "'cost_unit_id'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection = 'replay_cost_launches' AND "
              "target_projection = 'replay_cost_members' AND "
              "source_column = 'launch_id' AND target_parameter = "
              "'launch_id'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection = 'replay_cost_members' AND "
              "target_projection = 'event_audit' AND source_column = "
              "'event_id' AND target_parameter = 'event_id'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection = 'replay_cost_units' AND "
              "target_projection = 'replay_structural_placements' AND "
              "source_column = 'replay_unit_id' AND target_parameter = "
              "'replay_unit_id'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection = 'replay_structural_placements' "
              "AND target_projection = 'scope_host_context' AND "
              "source_column IN ('node_id', 'occurrence_idx')") == 2);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection = 'replay_body_domains' AND "
              "target_projection = 'replay_body_patterns' AND "
              "source_column = 'domain_id' AND target_parameter = "
              "'domain_id'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection = 'replay_body_patterns' AND "
              "target_projection = 'replay_body_pattern_occurrences' AND "
              "source_column = 'pattern_id' AND target_parameter = "
              "'pattern_id'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection = 'replay_body_pattern_occurrences' "
              "AND target_projection IN ('replay_body_pattern_positions', "
              "'replay_body_pattern_members') AND source_column = "
              "'occurrence_id' AND target_parameter = 'occurrence_id'") ==
          2);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection = 'scope_catalog' AND "
              "target_projection = 'position_occurrences'") == 0);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection = 'scope_catalog' AND "
              "target_projection IN ('bubble_occurrences', "
              "'bubble_host_context')") == 0);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_projection_continuation "
              "WHERE source_projection = 'bubble_hotspots' AND "
              "target_projection = 'bubble_occurrences' AND source_column = "
              "'structural_position_id' AND target_parameter = "
              "'structural_position_id' AND coordinate_kind = "
              "'structural_position_id'") == 1);
  require(run_scalar_text(
              augmented_path,
              "SELECT selector_parameters FROM traceloom_projection_recipe "
              "WHERE projection_name = 'scope_occurrences'") ==
          ":node_id, :occurrence_idx (NULL selects all)");
  require(run_scalar_text(
              augmented_path,
              "SELECT value FROM traceloom_metadata WHERE key = "
              "'analytical_projection_contract'") ==
          "scope_population_resolution_domain_lens_coordinates_v2");
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_analysis_surface "
              "WHERE surface_name = 'operator_audit' AND "
              "relation_name = 'traceloom_operator_audit'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_analysis_surface "
              "WHERE surface_name = 'synchronization_action' AND "
              "relation_name = 'traceloom_v_sync_runtime_call'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_analysis_surface "
              "WHERE surface_name = 'evidence_role_decision' AND "
              "relation_name = 'traceloom_v_evidence_role_decision'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_analysis_surface WHERE "
              "surface_name = 'event_reconciliation_policy' AND "
              "relation_name = 'traceloom_event_reconciliation_policy'") ==
          1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_analysis_surface WHERE "
              "surface_name = 'event_reconciliation_rule' AND "
              "relation_name = 'traceloom_event_reconciliation_rule'") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_analysis_surface WHERE "
              "surface_name = 'event_reconciliation_decision' AND "
              "relation_name = 'traceloom_event_reconciliation_decision'") ==
          1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_analysis_surface WHERE "
              "surface_name = 'event_reconciliation_member' AND "
              "relation_name = 'traceloom_event_reconciliation_member'") ==
          1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM "
              "traceloom_symbol_normalization_policy") == 1);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM "
              "traceloom_symbol_normalization_rule") >= 10);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM "
              "traceloom_anchor_symbol_normalization") ==
          run_scalar_int(augmented_path,
                         "SELECT COUNT(*) FROM traceloom_anchor"));
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_analysis_surface WHERE "
              "surface_name = 'anchor_symbol_lineage' AND relation_name = "
              "'traceloom_v_anchor_symbol_lineage'") == 1);
  require_analysis_surface_queries_prepare(augmented_path);
  run_position_projection_catalog_tests(augmented_path);
  run_host_window_query_tests(augmented_path);
  require(run_scalar_text(augmented_path,
                          "SELECT embedded_table_name FROM "
                          "traceloom_raw_table WHERE source_table = "
                          "'RAW_SENTINEL'") == "RAW_SENTINEL");
  std::remove(augmented_path.c_str());

  // A published `embedded_raw` locator is a literal row promise. The
  // materializer accepts an exact provider row and rejects a stale key before
  // atomically publishing the augmented database.
  const std::string locator_raw_path = temp_db_path();
  const std::string locator_augmented_path = temp_db_path();
  run_sql(locator_raw_path,
          "CREATE TABLE RAW_SENTINEL(payload TEXT);"
          "INSERT INTO RAW_SENTINEL VALUES('literal provider row')");
  compat::write_queryable_database_timeline(
      locator_augmented_path, locator_raw_path,
      build_source_locator_ir(locator_raw_path, 1), augmented_options);
  require(run_scalar_text(
              locator_augmented_path,
              "SELECT resolution_status FROM "
              "traceloom_v_event_source_locator WHERE event_id='event-0'") ==
          "embedded_raw");
  require(run_scalar_text(locator_augmented_path,
                          "SELECT payload FROM RAW_SENTINEL WHERE rowid=1") ==
          "literal provider row");
  std::remove(locator_augmented_path.c_str());

  bool rejected_stale_source_key = false;
  try {
    compat::write_queryable_database_timeline(
        locator_augmented_path, locator_raw_path,
        build_source_locator_ir(locator_raw_path, 2), augmented_options);
  } catch (const std::runtime_error&) {
    rejected_stale_source_key = true;
  }
  require(rejected_stale_source_key,
          "augmented DB accepted a source locator for a missing raw row");
  require(!std::filesystem::exists(locator_augmented_path),
          "failed locator validation published a partial augmented DB");
  std::remove(locator_raw_path.c_str());

  // Backend lowering labels share a structural symbol only through an
  // explicit, queryable rule. The reverse path retains both concrete labels
  // at the same recovered structural position.
  const std::string variant_source = temp_db_path();
  const std::string variant_augmented = temp_db_path();
  run_sql(variant_source,
          "CREATE TABLE TASK(opName TEXT);"
          "INSERT INTO TASK VALUES('MatMulV2'),('Relu'),('MatMulV3'),"
          "('Relu'),('MatMulV2'),('Relu'),('MatMulV3'),('Relu')");
  compat::NativeCompatibilitySidecarOptions variant_options;
  variant_options.source_kind = "ascend";
  compat::write_queryable_database_timeline(
      variant_augmented, variant_source,
      build_symbol_variant_repeat_ir(variant_source), variant_options);
  require(run_scalar_int(
              variant_augmented,
              "SELECT COUNT(*) FROM traceloom_anchor_symbol_normalization "
              "WHERE structural_symbol = 'MatMul' AND outcome = "
              "'canonicalized' AND rule_id = "
              "'ascend.task.matmul-backend-variant'") == 4);
  require(run_scalar_int(
              variant_augmented,
              "SELECT COUNT(DISTINCT observed_symbol) FROM "
              "traceloom_anchor_symbol_normalization WHERE "
              "structural_symbol = 'MatMul'") == 2);
  require(run_scalar_int(
              variant_augmented,
              "SELECT COUNT(DISTINCT observed_symbol) FROM "
              "traceloom_v_symbol_normalization_placement WHERE "
              "structural_symbol = 'MatMul' AND coverage_kind = 'self'") ==
          2);
  require(run_scalar_int(
              variant_augmented,
              "SELECT COUNT(*) FROM "
              "traceloom_v_exact_replay_partition_status WHERE "
              "exact_replay_count = 0 AND support_state = 'unsupported' AND "
              "reason_code = 'no_exact_replays'") == 1);
  require(run_scalar_int(
              variant_augmented,
              "SELECT COUNT(*) FROM traceloom_v_exact_replay_partition") ==
          0);
  require(run_scalar_int(
              variant_augmented,
              "SELECT COUNT(DISTINCT observed_symbol) FROM "
              "traceloom_v_symbol_variant_cost WHERE structural_symbol = "
              "'MatMul'") == 2);
  require(run_scalar_int(
              variant_augmented,
              "SELECT COUNT(*) FROM traceloom_v_anchor_symbol_lineage d "
              "JOIN traceloom_raw_table rt ON rt.source_path = "
              "d.source_path AND rt.source_table = d.source_table WHERE "
              "d.structural_symbol = 'MatMul'") == 4);
  require_analysis_surface_queries_prepare(variant_augmented);
  std::remove(variant_source.c_str());
  std::remove(variant_augmented.c_str());

  const std::string deterministic_raw_source = temp_db_path();
  run_sql(deterministic_raw_source,
          "CREATE TABLE RAW_SENTINEL(id INTEGER, payload TEXT);"
          "INSERT INTO RAW_SENTINEL VALUES(7, 'retained profiler evidence')");
  const std::string collective_augmented_a = temp_db_path();
  const std::string collective_augmented_b = temp_db_path();
  const std::string collective_augmented_parallel = temp_db_path();
  compat::NativeCompatibilitySidecarOptions deterministic_options;
  deterministic_options.source_kind = "fixture";
  deterministic_options.source_path = "/stable/logical/profile";
  deterministic_options.collective_expected_world_size = 1;
  compat::write_queryable_database_timeline(
      collective_augmented_a, deterministic_raw_source,
      build_collective_repeat_ir(), deterministic_options);
  compat::write_queryable_database_timeline(
      collective_augmented_b, deterministic_raw_source,
      build_collective_repeat_ir(), deterministic_options);
  compat::NativeCompatibilitySidecarOptions parallel_options =
      deterministic_options;
  parallel_options.grammar_worker_count = 4;
  parallel_options.grammar_target_nodes_per_chunk = 2;
  compat::write_queryable_database_timeline(
      collective_augmented_parallel, deterministic_raw_source,
      build_collective_repeat_ir(), parallel_options);
  require(sha256_file_hex(collective_augmented_a) ==
              sha256_file_hex(collective_augmented_b),
          "augmented DB bytes depend on output/temp path");
  require(sha256_file_hex(collective_augmented_a) ==
              sha256_file_hex(collective_augmented_parallel),
          "augmented DB relations depend on grammar worker/chunk count");
  require(run_scalar_text(collective_augmented_a,
                          "SELECT DISTINCT db_name FROM "
                          "traceloom_collective_global_link") ==
          "profile.traceloom.db");
  std::remove(collective_augmented_a.c_str());
  std::remove(collective_augmented_b.c_str());
  std::remove(collective_augmented_parallel.c_str());
  std::remove(deterministic_raw_source.c_str());

  // Split layouts package every raw DB in one portable artifact. Identical
  // vendor table names must remain distinct and preserve the original rowid
  // as an explicit queryable column.
  const std::string split_source_a = temp_db_path();
  const std::string split_source_b = temp_db_path();
  const std::string split_augmented = temp_db_path();
  run_sql(split_source_a,
          "CREATE TABLE RAW_SHARED(id INTEGER, payload TEXT);"
          "INSERT INTO RAW_SHARED VALUES(1, 'source-a')");
  run_sql(split_source_b,
          "CREATE TABLE RAW_SHARED(id INTEGER, payload TEXT);"
          "INSERT INTO RAW_SHARED VALUES(2, 'source-b')");
  const std::string split_source_a_hash = sha256_file_hex(split_source_a);
  const std::string split_source_b_hash = sha256_file_hex(split_source_b);
  compat::NativeCompatibilitySidecarOptions split_options;
  split_options.source_kind = "ascend_sqlite_split";
  split_options.source_path = "/portable/original/split-profile";
  compat::write_queryable_database_timeline(
      split_augmented,
      std::vector<std::string>{split_source_b, split_source_a},
      build_exact_cuda_graph_replay_ir(), split_options);
  require(sha256_file_hex(split_source_a) == split_source_a_hash &&
              sha256_file_hex(split_source_b) == split_source_b_hash,
          "multi-source augmented DB construction modified a raw input");
  require(run_scalar_int(split_augmented,
                         "SELECT COUNT(*) FROM "
                         "traceloom_raw_source_database") == 2);
  require(run_scalar_int(split_augmented,
                         "SELECT COUNT(*) FROM traceloom_raw_table WHERE "
                         "source_table = 'RAW_SHARED'") == 2);
  require(run_scalar_int(split_augmented,
                         "SELECT COUNT(*) FROM "
                         "traceloom_raw_000__RAW_SHARED") == 1);
  require(run_scalar_int(split_augmented,
                         "SELECT COUNT(*) FROM "
                         "traceloom_raw_001__RAW_SHARED") == 1);
  require(run_scalar_text(split_augmented,
                          "SELECT source_rowid_column FROM "
                          "traceloom_raw_table WHERE source_id = "
                          "'raw-source-000' AND source_table = "
                          "'RAW_SHARED'") ==
          "__traceloom_source_rowid__");
  require(run_scalar_int(split_augmented,
                         "SELECT __traceloom_source_rowid__ FROM "
                         "traceloom_raw_000__RAW_SHARED") == 1);
  std::remove(split_source_a.c_str());
  std::remove(split_source_b.c_str());
  require(run_scalar_int(split_augmented,
                         "SELECT sum(id) FROM (SELECT id FROM "
                         "traceloom_raw_000__RAW_SHARED UNION ALL SELECT id "
                         "FROM traceloom_raw_001__RAW_SHARED)") == 3);
  std::remove(split_augmented.c_str());


}

}  // namespace traceloom::testing::sidecar_materializer
