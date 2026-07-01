#include "traceloom/adapters/aclgraph_fixture_adapter.h"
#include "traceloom/adapters/aclgraph_fixture_reader.h"
#include "traceloom/compat/aclgraph_graph_replay_rows.h"
#include "traceloom/compat/anchor_cost_breakdown_rows.h"
#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/report/anchor_internal_cost_breakdown.h"
#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string temp_db_path() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("traceloom_native_generated_sidecar_report_" + std::to_string(now) +
       ".db");
  return path.string();
}

std::filesystem::path workspace_root() {
  return std::filesystem::path(TRACELOOM_WORKSPACE_ROOT);
}

std::filesystem::path report_sql_path(const std::string& filename) {
  return workspace_root() / "traceloom" / "docs" / "report-sql" / filename;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  traceloom::testing::require(input.good());
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

int run_scalar_int(const std::string& db_path, const std::string& sql) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
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

int run_report_sql_row_count(const std::string& db_path,
                             const std::string& filename) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);

  const std::string sql = read_file(report_sql_path(filename));
  sqlite3_stmt* raw_stmt = nullptr;
  rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw_stmt, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);

  int row_count = 0;
  while ((rc = sqlite3_step(raw_stmt)) == SQLITE_ROW) {
    ++row_count;
  }
  traceloom::testing::require(rc == SQLITE_DONE);
  sqlite3_finalize(raw_stmt);
  sqlite3_close(db);
  return row_count;
}

void materialize_aclgraph_fixture_sidecar(const std::string& db_path) {
  const std::filesystem::path fixture_path =
      workspace_root() / "drafts" / "refactor" / "80_tests_fixtures" /
      "fixtures" / "aclgraph" / "aclgraph_python_minimal_assets.json";
  const traceloom::AclGraphSemanticFixture fixture =
      traceloom::load_aclgraph_semantic_fixture(fixture_path.string());
  const traceloom::NativeIr ir = traceloom::AclGraphFixtureAdapter(fixture).load();
  const traceloom::AnchorInternalCostBreakdown breakdown =
      traceloom::build_aclgraph_fixture_anchor_cost_breakdown(fixture, ir);

  traceloom::compat::NativeCompatibilitySidecarOptions options;
  options.source_kind = "aclgraph_semantic_fixture";
  options.source_path = fixture_path.string();
  traceloom::compat::write_basic_native_compatibility_sidecar(db_path, ir,
                                                              options);
  traceloom::compat::replace_anchor_cost_breakdown_rows(
      db_path, traceloom::compat::build_anchor_cost_breakdown_sql_rows(
                   breakdown));
  traceloom::compat::replace_graph_replay_rows(
      db_path,
      traceloom::compat::build_aclgraph_fixture_graph_replay_sql_rows(fixture,
                                                                      ir));
}

}  // namespace

int main() {
  using traceloom::testing::require;

  const std::string db_path = temp_db_path();
  materialize_aclgraph_fixture_sidecar(db_path);

  require(run_report_sql_row_count(db_path, "cuda-graph-envelope.sql") > 0);
  require(run_report_sql_row_count(db_path, "anchor-cost-breakdown.sql") > 0);
  require(run_report_sql_row_count(db_path, "node-cost-breakdown.sql") > 0);
  require(run_report_sql_row_count(db_path, "node-events.sql") > 0);
  require(run_report_sql_row_count(db_path, "node-occurrences.sql") > 0);
  require(run_report_sql_row_count(db_path, "tree-map.sql") > 0);
  require(run_report_sql_row_count(db_path, "semantic-tree-readable.sql") > 0);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM traceloom_tree_node_anchor na "
              "JOIN traceloom_anchor a ON a.anchor_id = na.anchor_id "
              "JOIN traceloom_event e ON e.event_id = a.event_id") > 0);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM traceloom_cuda_graph_envelope ge "
              "LEFT JOIN traceloom_event e ON e.event_id = ge.child_event_id "
              "WHERE e.event_id IS NULL") == 0);

  std::remove(db_path.c_str());
  return 0;
}
