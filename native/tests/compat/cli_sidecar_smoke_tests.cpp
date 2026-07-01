#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

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
                                  "SELECT COUNT(*) FROM sqlite_master "
                                  "WHERE type = 'view' AND "
                                  "name = 'traceloom_v_tree_node'") == 1);
  traceloom::testing::require(
      run_scalar_int(path, "SELECT COUNT(*) FROM traceloom_event") > 0);
  traceloom::testing::require(
      run_scalar_int(path, "SELECT COUNT(*) FROM traceloom_anchor") > 0);
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
  traceloom::testing::require(
      run_scalar_int(path,
                     "SELECT COUNT(*) FROM traceloom_cuda_graph_envelope ge "
                     "LEFT JOIN traceloom_event e ON "
                     "e.event_id = ge.child_event_id "
                     "WHERE e.event_id IS NULL") == 0);
}

}  // namespace

int main(int argc, char** argv) {
  traceloom::testing::require(argc == 3,
                              "usage: cli_sidecar_smoke_tests DB MODE");
  const std::string path = argv[1];
  const std::string mode = argv[2];
  require_basic_sidecar(path);
  if (mode == "aclgraph") {
    require_aclgraph_sidecar(path);
  } else {
    traceloom::testing::require(mode == "fixture");
  }
  return 0;
}
