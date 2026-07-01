#include "traceloom/adapters/aclgraph_fixture_adapter.h"
#include "traceloom/adapters/aclgraph_fixture_reader.h"
#include "traceloom/compat/aclgraph_graph_replay_rows.h"
#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/ir/native_ir.h"
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

void require_node_coverage_invariants(const std::string& db_path) {
  traceloom::testing::require(run_scalar_int(
                                  db_path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_viz_node_anchor na "
                                  "LEFT JOIN traceloom_viz_node n ON "
                                  "n.node_id = na.node_id "
                                  "LEFT JOIN traceloom_anchor a ON "
                                  "a.anchor_id = na.anchor_id "
                                  "WHERE n.node_id IS NULL OR "
                                  "a.anchor_id IS NULL") == 0);
  traceloom::testing::require(
      run_scalar_int(db_path,
                     "SELECT COUNT(*) FROM traceloom_viz_edge e "
                     "LEFT JOIN traceloom_viz_node parent ON "
                     "parent.node_id = e.parent_node_id "
                     "LEFT JOIN traceloom_viz_node child ON "
                     "child.node_id = e.child_node_id "
                     "WHERE parent.node_id IS NULL OR child.node_id IS NULL") ==
      0);
  traceloom::testing::require(
      run_scalar_int(db_path,
                     "SELECT COUNT(*) FROM traceloom_loop_node l "
                     "LEFT JOIN traceloom_viz_node n ON n.node_id = l.node_id "
                     "WHERE n.node_id IS NULL") == 0);
  traceloom::testing::require(run_scalar_int(
                                  db_path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_anchor_primary_node ap "
                                  "LEFT JOIN traceloom_anchor a ON "
                                  "a.anchor_id = ap.anchor_id "
                                  "LEFT JOIN traceloom_viz_node n ON "
                                  "n.node_id = ap.node_id "
                                  "WHERE a.anchor_id IS NULL OR "
                                  "n.node_id IS NULL") == 0);
}

void require_all_anchors_have_primary_node_coverage(
    const std::string& db_path) {
  traceloom::testing::require(run_scalar_int(
                                  db_path,
                                  "SELECT COUNT(*) FROM traceloom_anchor a "
                                  "LEFT JOIN traceloom_viz_node_anchor na ON "
                                  "na.anchor_id = a.anchor_id "
                                  "WHERE na.anchor_id IS NULL") == 0);
  traceloom::testing::require(run_scalar_int(
                                  db_path,
                                  "SELECT COUNT(*) FROM ("
                                  "SELECT a.anchor_id, "
                                  "COUNT(ap.anchor_id) AS primary_count "
                                  "FROM traceloom_anchor a "
                                  "LEFT JOIN traceloom_anchor_primary_node ap "
                                  "ON ap.anchor_id = a.anchor_id "
                                  "GROUP BY a.anchor_id "
                                  "HAVING primary_count != 1"
                                  ")") == 0);
}

void require_semantic_tree_invariants(const std::string& db_path) {
  traceloom::testing::require(
      run_scalar_int(db_path,
                     "SELECT COUNT(*) FROM traceloom_semantic_node n "
                     "LEFT JOIN traceloom_semantic_tree t ON "
                     "t.tree_id = n.tree_id "
                     "WHERE t.tree_id IS NULL") == 0);
  traceloom::testing::require(run_scalar_int(
                                  db_path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_semantic_tree t "
                                  "LEFT JOIN traceloom_semantic_node root ON "
                                  "root.node_id = t.root_node_id "
                                  "WHERE t.root_node_id IS NOT NULL AND "
                                  "root.node_id IS NULL") == 0);
  traceloom::testing::require(run_scalar_int(
                                  db_path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_semantic_edge e "
                                  "LEFT JOIN traceloom_semantic_tree t ON "
                                  "t.tree_id = e.tree_id "
                                  "LEFT JOIN traceloom_semantic_node parent "
                                  "ON parent.node_id = e.parent_node_id "
                                  "LEFT JOIN traceloom_semantic_node child ON "
                                  "child.node_id = e.child_node_id "
                                  "WHERE t.tree_id IS NULL OR "
                                  "parent.node_id IS NULL OR "
                                  "child.node_id IS NULL") == 0);
}

void require_anchor_cost_rows_link_to_anchors(const std::string& db_path) {
  traceloom::testing::require(
      run_scalar_int(db_path,
                     "SELECT COUNT(*) FROM "
                     "traceloom_anchor_cost_breakdown c "
                     "LEFT JOIN traceloom_anchor a ON "
                     "a.anchor_idx = c.anchor_idx "
                     "WHERE a.anchor_idx IS NULL") == 0);
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
  traceloom::compat::write_aclgraph_fixture_compatibility_sidecar(
      db_path, fixture, ir, breakdown, options);
}

void materialize_aux_fixture_sidecar(const std::string& db_path) {
  traceloom::NativeIr ir;
  const traceloom::SourceRefId source =
      ir.source_refs.append("fixture", "memory", "TASK", 0);
  const traceloom::SymbolId memcpy = ir.symbols.intern("Memcpy");
  const traceloom::SymbolId matmul = ir.symbols.intern("MatMul");

  ir.trace_events.append(source, 1, 0, 7, 1000, 1500, memcpy);
  const traceloom::TraceEventId anchor_event =
      ir.trace_events.append(source, 2, 0, 7, 2000, 3200, matmul);
  ir.anchors.append(source, anchor_event, traceloom::ReplayUnitId::invalid(),
                    traceloom::AnchorKind::kDeviceEvent, matmul, 0, 7, 2000,
                    3200);

  traceloom::compat::NativeCompatibilitySidecarOptions options;
  options.source_kind = "native_aux_fixture";
  options.source_path = "memory";
  traceloom::compat::write_basic_native_compatibility_sidecar(db_path, ir,
                                                              options);
}

void materialize_repeat_fixture_sidecar(const std::string& db_path) {
  traceloom::NativeIr ir;
  const traceloom::SourceRefId source =
      ir.source_refs.append("fixture", "memory", "TASK", 0);
  const traceloom::SymbolId memcpy = ir.symbols.intern("Memcpy");
  const traceloom::SymbolId matmul = ir.symbols.intern("MatMul");

  ir.trace_events.append(source, 0, 0, 3, 500, 800, memcpy);
  const traceloom::TraceEventId event0 =
      ir.trace_events.append(source, 1, 0, 3, 1000, 2000, matmul);
  const traceloom::TraceEventId event1 =
      ir.trace_events.append(source, 2, 0, 3, 2500, 4500, matmul);
  const traceloom::AnchorId anchor0 =
      ir.anchors.append(source, event0, traceloom::ReplayUnitId::invalid(),
                        traceloom::AnchorKind::kDeviceEvent, matmul, 0, 3,
                        1000, 2000);
  const traceloom::AnchorId anchor1 =
      ir.anchors.append(source, event1, traceloom::ReplayUnitId::invalid(),
                        traceloom::AnchorKind::kDeviceEvent, matmul, 0, 3,
                        2500, 4500);
  ir.tokens.append(anchor0, matmul, 0, 0, 1000, 2000);
  ir.tokens.append(anchor1, matmul, 0, 1, 2500, 4500);

  traceloom::compat::NativeCompatibilitySidecarOptions options;
  options.source_kind = "native_repeat_fixture";
  options.source_path = "memory";
  traceloom::compat::write_basic_native_compatibility_sidecar(db_path, ir,
                                                              options);
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
  require_node_coverage_invariants(db_path);
  require_all_anchors_have_primary_node_coverage(db_path);
  require_semantic_tree_invariants(db_path);
  require_anchor_cost_rows_link_to_anchors(db_path);

  std::remove(db_path.c_str());

  const std::string aux_db_path = temp_db_path();
  materialize_aux_fixture_sidecar(aux_db_path);

  require(run_report_sql_row_count(aux_db_path, "anchor-aux.sql") > 0);
  require(run_report_sql_row_count(aux_db_path,
                                   "anchor-cost-breakdown.sql") > 0);
  require(run_scalar_int(
              aux_db_path,
              "SELECT COUNT(*) FROM traceloom_anchor_cost_breakdown "
              "WHERE aux_us > 0") > 0);
  require(run_scalar_int(
              aux_db_path,
              "SELECT COUNT(*) FROM traceloom_aux_link al "
              "JOIN traceloom_anchor a ON a.anchor_id = al.anchor_id "
              "JOIN traceloom_event e ON e.event_id = al.aux_event_id") > 0);
  require(run_scalar_int(
              aux_db_path,
              "SELECT COUNT(*) FROM traceloom_aux_link al "
              "LEFT JOIN traceloom_anchor a ON a.anchor_id = al.anchor_id "
              "LEFT JOIN traceloom_event e ON e.event_id = al.aux_event_id "
              "WHERE a.anchor_id IS NULL OR e.event_id IS NULL") == 0);
  require_anchor_cost_rows_link_to_anchors(aux_db_path);

  std::remove(aux_db_path.c_str());

  const std::string repeat_db_path = temp_db_path();
  materialize_repeat_fixture_sidecar(repeat_db_path);

  require(run_report_sql_row_count(repeat_db_path, "repeat-overview.sql") > 0);
  require(run_report_sql_row_count(repeat_db_path, "repeat-children.sql") > 0);
  require(run_scalar_int(
              repeat_db_path,
              "SELECT COUNT(*) FROM traceloom_viz_node "
              "WHERE repeat_count IS NOT NULL") > 0);
  require(run_scalar_int(
              repeat_db_path,
              "SELECT COUNT(*) FROM traceloom_viz_node "
              "WHERE aux_events > 0 AND aux_us > 0") > 0);
  require(run_scalar_int(
              repeat_db_path,
              "SELECT COUNT(*) FROM traceloom_semantic_node "
              "WHERE aux_event_count > 0 AND aux_us > 0") > 0);
  require_node_coverage_invariants(repeat_db_path);
  require_all_anchors_have_primary_node_coverage(repeat_db_path);
  require_semantic_tree_invariants(repeat_db_path);
  require_anchor_cost_rows_link_to_anchors(repeat_db_path);

  std::remove(repeat_db_path.c_str());
  return 0;
}
