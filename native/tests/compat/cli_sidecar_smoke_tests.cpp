#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

int run_scalar_int(const std::string& path, const std::string& sql) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);

  sqlite3_stmt* raw_stmt = nullptr;
  rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw_stmt, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);
  rc = sqlite3_step(raw_stmt);
  traceloom::testing::require(rc == SQLITE_ROW);
  const int value = sqlite3_column_int(raw_stmt, 0);
  rc = sqlite3_step(raw_stmt);
  traceloom::testing::require(rc == SQLITE_DONE);
  sqlite3_finalize(raw_stmt);
  sqlite3_close(db);
  return value;
}

void require_basic_sidecar(const std::string& path) {
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM traceloom_metadata "
                                  "WHERE key = "
                                  "'native_compatibility_materializer' "
                                  "AND value = 'basic_native_ir_v1'") == 1);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM traceloom_metadata "
                                  "WHERE key = 'evidence_role_policy_id' "
                                  "AND length(value) > 0") == 1);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM traceloom_metadata "
                                  "WHERE key = "
                                  "'evidence_role_manifest_sha256' "
                                  "AND length(value) = 64") == 1);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM sqlite_master "
                                  "WHERE type = 'view' AND "
                                  "name = 'traceloom_v_tree_node'") == 1);
  traceloom::testing::require(
      run_scalar_int(path, "SELECT COUNT(*) FROM traceloom_event") > 0);
  traceloom::testing::require(
      run_scalar_int(path, "SELECT COUNT(*) FROM traceloom_anchor") > 0);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_evidence_role_policy WHERE "
                                  "input_format = 'flat_tsv'") == 1);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_evidence_role_decision") ==
                              run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM traceloom_event"));
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_evidence_role_decision d "
                                  "LEFT JOIN traceloom_evidence_role_rule r "
                                  "ON r.policy_id = d.policy_id AND "
                                  "r.rule_id = d.rule_id "
                                  "WHERE r.rule_id IS NULL") == 0);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM sqlite_master "
                                  "WHERE type = 'table' AND name = "
                                  "'traceloom_collective_global_link'") == 1);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_event_source s "
                                  "LEFT JOIN traceloom_event e ON "
                                  "e.event_id = s.event_id "
                                  "WHERE e.event_id IS NULL") == 0);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM traceloom_anchor a "
                                  "LEFT JOIN traceloom_event e ON "
                                  "e.event_id = a.event_id "
                                  "WHERE e.event_id IS NULL") == 0);
  traceloom::testing::require(
      run_scalar_int(path,
                     "SELECT COUNT(*) FROM "
                     "traceloom_anchor_cost_breakdown c "
                     "LEFT JOIN traceloom_anchor a ON "
                     "a.anchor_idx = c.anchor_idx "
                     "WHERE a.anchor_idx IS NULL") == 0);
}

void require_aclgraph_sidecar(const std::string& path) {
  traceloom::testing::require(
      run_scalar_int(path,
                     "SELECT COUNT(*) FROM traceloom_cuda_graph_replay") > 0);
  traceloom::testing::require(
      run_scalar_int(path,
                     "SELECT COUNT(*) FROM traceloom_cuda_graph_envelope") > 0);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_cuda_graph_replay WHERE "
                                  "graph_provider != 'aclgraph' OR "
                                  "NOT json_valid(raw_json)") == 0);
  traceloom::testing::require(
      run_scalar_int(path,
                     "SELECT COUNT(*) FROM traceloom_cuda_graph_envelope ge "
                     "LEFT JOIN traceloom_event e ON "
                     "e.event_id = ge.child_event_id "
                     "WHERE e.event_id IS NULL") == 0);
}

void require_cuda_sidecar(const std::string& path) {
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM traceloom_metadata "
                                  "WHERE key = 'source_kind' AND "
                                  "value = 'cuda_nsys_sqlite'") == 1);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM traceloom_viz_node "
                                  "WHERE view_name = "
                                  "'native_report_tree'") > 0);
  // A single profiler DB may contain several devices. The report path must
  // materialize one tree per device with its true device_id instead of a
  // combined tree stamped with device 0.
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(DISTINCT device_id) FROM "
                                  "traceloom_viz_node WHERE view_name = "
                                  "'native_report_tree'") == 2);
  traceloom::testing::require(
      run_scalar_int(path,
                     "SELECT COUNT(*) FROM traceloom_viz_node "
                     "WHERE view_name = 'native_report_tree' AND "
                     "device_id = 0 AND kind = 'seq'") == 1);
  traceloom::testing::require(
      run_scalar_int(path,
                     "SELECT COUNT(*) FROM traceloom_viz_node "
                     "WHERE view_name = 'native_report_tree' AND "
                     "device_id = 1 AND kind = 'seq'") == 1);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM traceloom_semantic_tree "
                                  "WHERE device_id = 0 AND tree_id = "
                                  "'native-report-tree-d0'") == 1);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM traceloom_semantic_tree "
                                  "WHERE device_id = 1 AND tree_id = "
                                  "'native-report-tree-d1'") == 1);
  traceloom::testing::require(
      run_scalar_int(path,
                     "SELECT COUNT(*) FROM traceloom_cuda_graph_replay") > 0);
  traceloom::testing::require(
      run_scalar_int(path,
                     "SELECT COUNT(*) FROM traceloom_cuda_graph_envelope") > 0);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_cuda_graph_replay WHERE "
                                  "graph_provider != 'cuda' OR "
                                  "NOT json_valid(raw_json)") == 0);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_cuda_graph_envelope ge "
                                  "LEFT JOIN traceloom_event e ON "
                                  "e.event_id = ge.child_event_id "
                                  "WHERE e.event_id IS NULL") == 0);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM traceloom_event "
                                  "WHERE task_type LIKE 'CUDA_%_AUX' "
                                  "AND role != 'aux'") == 0);
}

void require_augmented_database(const std::string& path) {
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM traceloom_metadata "
                                  "WHERE key = 'artifact_kind' AND value = "
                                  "'queryable_database_timeline'") == 1);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_raw_source_database") == 1);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_analysis_surface") >= 8);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_projection_recipe") == 18);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_projection_parameter") == 26);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_projection_coordinate") == 58);
  const int replay_unit_count = run_scalar_int(
      path, "SELECT COUNT(*) FROM traceloom_replay_cost_unit");
  if (replay_unit_count == 0) {
    traceloom::testing::require(run_scalar_int(
                                    path,
                                    "SELECT COUNT(*) FROM "
                                    "traceloom_replay_cost_issue WHERE code = "
                                    "'no_replay_units' AND replay_unit_id = -1 "
                                    "AND launch_id IS NULL") == 1);
  }
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM traceloom_metadata "
                                  "WHERE key = 'analytical_projection_contract' "
                                  "AND value = "
                                  "'scope_population_resolution_domain_lens_"
                                  "coordinates_v2'") ==
                              1);
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM sqlite_master WHERE "
                                  "type = 'table' AND name = "
                                  "'CUPTI_ACTIVITY_KIND_KERNEL'") == 1);
  traceloom::testing::require(
      !std::filesystem::exists(std::filesystem::path(path).parent_path() /
                               "loop_tree_v2.md"),
      "default augmented-DB UX unexpectedly emitted Markdown");
  traceloom::testing::require(run_scalar_int(
                                  path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_evidence_role_policy WHERE "
                                  "length(effective_config_sha256) = 64 AND "
                                  "config_overrides LIKE "
                                  "'%cuda.anchor.task.type.cuda.kernel."
                                  "51cc7472.priority=96%'") == 1);
}

void require_cuda_markdown(const std::string& path) {
  std::ifstream input(path);
  traceloom::testing::require(input.good(),
                              "CUDA loop tree Markdown was not written");
  std::ostringstream contents;
  contents << input.rdbuf();
  const std::string text = contents.str();
  traceloom::testing::require(
      text.find("report_view: `native_report_tree`") != std::string::npos);
  traceloom::testing::require(
      text.find("renderer: `native_loop_tree_markdown_v0`") !=
      std::string::npos);
  traceloom::testing::require(
      text.find("source_kind: `cuda_nsys_sqlite`") != std::string::npos);
}

}  // namespace

int main(int argc, char** argv) {
  traceloom::testing::require(
      argc == 3 || argc == 4,
      "usage: cli_sidecar_smoke_tests DB MODE [LOOP_TREE_MD]");
  const std::string path = argv[1];
  const std::string mode = argv[2];
  require_basic_sidecar(path);
  if (mode == "aclgraph") {
    require_aclgraph_sidecar(path);
  } else if (mode == "cuda") {
    traceloom::testing::require(
        argc == 4, "CUDA sidecar smoke test requires Loop Tree Markdown");
    require_cuda_sidecar(path);
    require_cuda_markdown(argv[3]);
  } else if (mode == "augmented") {
    traceloom::testing::require(argc == 3);
    require_cuda_sidecar(path);
    require_augmented_database(path);
  } else {
    traceloom::testing::require(argc == 3);
    traceloom::testing::require(mode == "fixture");
  }
  return 0;
}
