#include "traceloom/compat/schema.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

struct QueryCase {
  std::string filename;
  std::vector<std::string> expected_columns;
  int expected_min_rows = 0;
};

struct QueryResult {
  std::vector<std::string> columns;
  int row_count = 0;
  std::vector<std::string> first_row;
};

std::string temp_db_path() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("traceloom_report_sql_compat_" + std::to_string(now) + ".db");
  return path.string();
}

std::filesystem::path report_sql_path(const std::string& filename) {
  return std::filesystem::path(TRACELOOM_REPO_ROOT) / "docs" / "report-sql" /
         filename;
}

std::filesystem::path report_sql_dir() {
  return std::filesystem::path(TRACELOOM_REPO_ROOT) / "docs" / "report-sql";
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  traceloom::testing::require(input.good());
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

std::string sqlite_text(sqlite3_stmt* stmt, int column) {
  const unsigned char* value = sqlite3_column_text(stmt, column);
  return value == nullptr ? "" : reinterpret_cast<const char*>(value);
}

QueryResult run_query(const std::string& db_path, const QueryCase& query_case) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);

  const std::string sql = read_file(report_sql_path(query_case.filename));
  sqlite3_stmt* raw_stmt = nullptr;
  rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw_stmt, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);

  QueryResult result;
  const int column_count = sqlite3_column_count(raw_stmt);
  for (int index = 0; index < column_count; ++index) {
    result.columns.push_back(sqlite3_column_name(raw_stmt, index));
  }

  while ((rc = sqlite3_step(raw_stmt)) == SQLITE_ROW) {
    ++result.row_count;
    if (result.first_row.empty()) {
      for (int index = 0; index < column_count; ++index) {
        result.first_row.push_back(sqlite_text(raw_stmt, index));
      }
    }
  }
  traceloom::testing::require(rc == SQLITE_DONE);
  sqlite3_finalize(raw_stmt);
  sqlite3_close(db);
  return result;
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

void execute_sql(const std::string& db_path, const std::string& sql) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE,
                           nullptr);
  traceloom::testing::require(rc == SQLITE_OK);
  char* error = nullptr;
  rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
  if (error != nullptr) {
    sqlite3_free(error);
  }
  traceloom::testing::require(rc == SQLITE_OK);
  sqlite3_close(db);
}

void require_event_source_invariants(const std::string& db_path) {
  traceloom::testing::require(run_scalar_int(
                                  db_path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_event_source s "
                                  "LEFT JOIN traceloom_event e ON "
                                  "e.event_id = s.event_id "
                                  "WHERE e.event_id IS NULL") == 0);
}

void require_anchor_aux_invariants(const std::string& db_path) {
  require_event_source_invariants(db_path);
  traceloom::testing::require(run_scalar_int(
                                  db_path,
                                  "SELECT COUNT(*) FROM traceloom_anchor a "
                                  "LEFT JOIN traceloom_event e ON "
                                  "e.event_id = a.event_id "
                                  "WHERE e.event_id IS NULL") == 0);
  traceloom::testing::require(run_scalar_int(
                                  db_path,
                                  "SELECT COUNT(*) FROM traceloom_aux_link al "
                                  "LEFT JOIN traceloom_anchor a ON "
                                  "a.anchor_id = al.anchor_id "
                                  "WHERE a.anchor_id IS NULL") == 0);
  traceloom::testing::require(run_scalar_int(
                                  db_path,
                                  "SELECT COUNT(*) FROM traceloom_aux_link al "
                                  "LEFT JOIN traceloom_event e ON "
                                  "e.event_id = al.aux_event_id "
                                  "WHERE e.event_id IS NULL") == 0);
}

void require_node_coverage_invariants(const std::string& db_path) {
  traceloom::testing::require(
      run_scalar_int(db_path,
                     "SELECT COUNT(*) FROM traceloom_viz_node_anchor na "
                     "LEFT JOIN traceloom_viz_node n ON n.node_id = na.node_id "
                     "WHERE n.node_id IS NULL") == 0);
  traceloom::testing::require(run_scalar_int(
                                  db_path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_viz_node_anchor na "
                                  "LEFT JOIN traceloom_anchor a ON "
                                  "a.anchor_id = na.anchor_id "
                                  "WHERE a.anchor_id IS NULL") == 0);
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
  traceloom::testing::require(
      run_scalar_int(db_path,
                     "SELECT COUNT(*) FROM traceloom_anchor_primary_node ap "
                     "LEFT JOIN traceloom_anchor a ON "
                     "a.anchor_id = ap.anchor_id "
                     "LEFT JOIN traceloom_viz_node n ON n.node_id = ap.node_id "
                     "WHERE a.anchor_id IS NULL OR n.node_id IS NULL") == 0);
}

void require_graph_replay_invariants(const std::string& db_path) {
  require_event_source_invariants(db_path);
  traceloom::testing::require(
      run_scalar_int(db_path,
                     "SELECT COUNT(*) FROM traceloom_cuda_graph_replay g "
                     "LEFT JOIN traceloom_event e ON e.event_id = g.event_id "
                     "WHERE e.event_id IS NULL") == 0);
  traceloom::testing::require(
      run_scalar_int(db_path,
                     "SELECT COUNT(*) FROM traceloom_cuda_graph_envelope ge "
                     "LEFT JOIN traceloom_cuda_graph_replay g ON "
                     "g.graph_event_id = ge.graph_event_id "
                     "WHERE g.graph_event_id IS NULL") == 0);
  traceloom::testing::require(
      run_scalar_int(db_path,
                     "SELECT COUNT(*) FROM traceloom_cuda_graph_envelope ge "
                     "LEFT JOIN traceloom_event e ON "
                     "e.event_id = ge.child_event_id "
                     "WHERE e.event_id IS NULL") == 0);
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
                                  "LEFT JOIN traceloom_semantic_node n ON "
                                  "n.node_id = t.root_node_id "
                                  "WHERE t.root_node_id IS NOT NULL AND "
                                  "n.node_id IS NULL") == 0);
  traceloom::testing::require(run_scalar_int(
                                  db_path,
                                  "SELECT COUNT(*) FROM "
                                  "traceloom_semantic_edge e "
                                  "LEFT JOIN traceloom_semantic_tree t ON "
                                  "t.tree_id = e.tree_id "
                                  "WHERE t.tree_id IS NULL") == 0);
  traceloom::testing::require(
      run_scalar_int(db_path,
                     "SELECT COUNT(*) FROM traceloom_semantic_edge e "
                     "LEFT JOIN traceloom_semantic_node parent ON "
                     "parent.node_id = e.parent_node_id "
                     "LEFT JOIN traceloom_semantic_node child ON "
                     "child.node_id = e.child_node_id "
                     "WHERE parent.node_id IS NULL OR child.node_id IS NULL") ==
      0);
}

void write_anchor_aux_fixture_via_asset_writers(
    const std::string& db_path,
    const traceloom::compat::AnchorAuxSqlRows& rows) {
  traceloom::compat::replace_timeline_rows(db_path, rows.events);
  traceloom::compat::replace_event_source_rows(db_path, rows.event_sources);
  traceloom::compat::replace_anchor_rows(db_path, rows.anchors);

  traceloom::compat::AuxAttributionSqlRows aux_rows;
  aux_rows.aux_slots = rows.aux_slots;
  aux_rows.aux_links = rows.aux_links;
  traceloom::compat::replace_aux_attribution_rows(db_path, aux_rows);
}

void write_node_coverage_fixture_via_asset_writers(
    const std::string& db_path,
    const traceloom::compat::NodeCoverageSqlRows& rows) {
  traceloom::compat::LoopTreeSqlRows loop_rows;
  loop_rows.nodes = rows.nodes;
  loop_rows.edges = rows.edges;
  loop_rows.loop_nodes = rows.loop_nodes;
  traceloom::compat::replace_loop_tree_rows(db_path, loop_rows);

  traceloom::compat::NodeAnchorCoverageSqlRows coverage_rows;
  coverage_rows.node_anchors = rows.node_anchors;
  coverage_rows.anchor_primary_nodes = rows.anchor_primary_nodes;
  traceloom::compat::replace_node_anchor_coverage_rows(db_path,
                                                       coverage_rows);
}

void write_graph_replay_fixture_via_asset_writers(
    const std::string& db_path,
    const traceloom::compat::GraphReplaySqlRows& rows) {
  traceloom::compat::replace_timeline_rows(db_path, rows.events);
  traceloom::compat::replace_event_source_rows(db_path, rows.event_sources);
  traceloom::compat::replace_anchor_rows(db_path, rows.anchors);

  traceloom::compat::GraphReplayEvidenceSqlRows evidence_rows;
  evidence_rows.graph_replays = rows.graph_replays;
  evidence_rows.graph_envelopes = rows.graph_envelopes;
  evidence_rows.reconstruction_regions = rows.reconstruction_regions;
  traceloom::compat::replace_graph_replay_evidence_rows(db_path,
                                                        evidence_rows);
}

void write_semantic_tree_fixture_via_asset_writers(
    const std::string& db_path,
    const traceloom::compat::SemanticTreeSqlRows& rows) {
  traceloom::compat::replace_semantic_tree_catalog_rows(db_path, rows.trees);

  traceloom::compat::SemanticGraphSqlRows graph_rows;
  graph_rows.nodes = rows.nodes;
  graph_rows.edges = rows.edges;
  traceloom::compat::replace_semantic_graph_rows(db_path, graph_rows);
}

void seed_anchor_cost_fixture(const std::string& db_path) {
  std::vector<traceloom::compat::AnchorCostBreakdownSqlRow> rows(2);
  rows[0].anchor_idx = 2;
  rows[0].symbol = "ACLL";
  rows[0].anchor_kind = "graph_l";
  rows[0].total_us = 123.456;
  rows[0].self_us = 1.0;
  rows[0].aux_us = 2.0;
  rows[0].graph_child_us = 120.0;
  rows[0].residual_us = 0.456;
  rows[0].raw_child_task_count = 20;
  rows[0].top_ops = "MatMul:16";
  rows[0].diagnostic_flags = "partial_overlap";
  rows[1].anchor_idx = 3;
  rows[1].symbol = "Kernel";
  rows[1].anchor_kind = "exec";
  rows[1].total_us = 7.0;
  rows[1].self_us = 7.0;
  traceloom::compat::replace_anchor_cost_breakdown_rows(db_path, rows);
}

void seed_anchor_aux_fixture(const std::string& db_path) {
  traceloom::compat::AnchorAuxSqlRows rows;

  traceloom::compat::EventSqlRow anchor_event;
  anchor_event.event_id = "event-anchor-1";
  anchor_event.step_idx = 10;
  anchor_event.source_table = "TASK";
  anchor_event.source_key = "task-10";
  anchor_event.stream_id = 3;
  anchor_event.start_ns = 1000;
  anchor_event.end_ns = 6000;
  anchor_event.dur_us = 5.0;
  anchor_event.category = "exec";
  anchor_event.role = "compute";
  anchor_event.semantic_role = "anchor";
  anchor_event.symbol = "MatMul";
  anchor_event.label = "MatMul";
  rows.events.push_back(anchor_event);

  traceloom::compat::EventSourceSqlRow anchor_source;
  anchor_source.event_id = anchor_event.event_id;
  anchor_source.source_ordinal = 0;
  anchor_source.source_table = anchor_event.source_table;
  anchor_source.source_key = anchor_event.source_key;
  anchor_source.source_role = "primary";
  rows.event_sources.push_back(anchor_source);

  traceloom::compat::EventSqlRow aux_event_a;
  aux_event_a.event_id = "event-aux-1";
  aux_event_a.step_idx = 8;
  aux_event_a.source_table = "TASK";
  aux_event_a.source_key = "task-8";
  aux_event_a.stream_id = 3;
  aux_event_a.start_ns = 800;
  aux_event_a.end_ns = 2000;
  aux_event_a.dur_us = 1.2;
  aux_event_a.category = "wait";
  aux_event_a.role = "prelude";
  aux_event_a.semantic_role = "aux";
  aux_event_a.symbol = "Memcpy";
  rows.events.push_back(aux_event_a);

  traceloom::compat::EventSourceSqlRow aux_source;
  aux_source.event_id = aux_event_a.event_id;
  aux_source.source_ordinal = 0;
  aux_source.source_table = aux_event_a.source_table;
  aux_source.source_key = aux_event_a.source_key;
  aux_source.source_role = "aux";
  rows.event_sources.push_back(aux_source);

  traceloom::compat::EventSqlRow aux_event_b;
  aux_event_b.event_id = "event-aux-2";
  aux_event_b.step_idx = 9;
  aux_event_b.source_table = "TASK";
  aux_event_b.source_key = "task-9";
  aux_event_b.stream_id = 3;
  aux_event_b.start_ns = 2100;
  aux_event_b.end_ns = 6400;
  aux_event_b.dur_us = 4.3;
  aux_event_b.category = "wait";
  aux_event_b.role = "prelude";
  aux_event_b.semantic_role = "aux";
  aux_event_b.symbol = "AclrtSynchronize";
  rows.events.push_back(aux_event_b);

  traceloom::compat::AnchorSqlRow anchor;
  anchor.anchor_id = "anchor-1";
  anchor.anchor_idx = 1;
  anchor.event_id = anchor_event.event_id;
  anchor.step_idx = anchor_event.step_idx;
  anchor.symbol = "MatMul";
  anchor.role = "compute";
  anchor.label = "MatMul";
  anchor.family = "compute";
  anchor.start_ns = anchor_event.start_ns;
  anchor.end_ns = anchor_event.end_ns;
  anchor.dur_us = anchor_event.dur_us;
  rows.anchors.push_back(anchor);

  traceloom::compat::AnchorAuxSlotSqlRow aux_slot;
  aux_slot.anchor_id = anchor.anchor_id;
  aux_slot.anchor_idx = anchor.anchor_idx;
  aux_slot.anchor_step_idx = anchor.step_idx;
  aux_slot.aux_start_step_idx = aux_event_a.step_idx;
  aux_slot.aux_end_step_idx = aux_event_b.step_idx;
  aux_slot.aux_event_count = 2;
  aux_slot.aux_dur_us = aux_event_a.dur_us + aux_event_b.dur_us;
  rows.aux_slots.push_back(aux_slot);

  traceloom::compat::AuxLinkSqlRow aux_link_a;
  aux_link_a.anchor_id = anchor.anchor_id;
  aux_link_a.aux_event_id = aux_event_a.event_id;
  aux_link_a.aux_order = 0;
  aux_link_a.aux_step_idx = aux_event_a.step_idx;
  aux_link_a.link_type = "prelude";
  aux_link_a.reason = "fixture";
  aux_link_a.aux_kind = "runtime";
  aux_link_a.aux_dur_us = aux_event_a.dur_us;
  rows.aux_links.push_back(aux_link_a);

  traceloom::compat::AuxLinkSqlRow aux_link_b;
  aux_link_b.anchor_id = anchor.anchor_id;
  aux_link_b.aux_event_id = aux_event_b.event_id;
  aux_link_b.aux_order = 1;
  aux_link_b.aux_step_idx = aux_event_b.step_idx;
  aux_link_b.link_type = "prelude";
  aux_link_b.reason = "fixture";
  aux_link_b.aux_kind = "runtime";
  aux_link_b.aux_dur_us = aux_event_b.dur_us;
  rows.aux_links.push_back(aux_link_b);

  write_anchor_aux_fixture_via_asset_writers(db_path, rows);
}

void seed_node_event_fixture(const std::string& db_path) {
  traceloom::compat::NodeCoverageSqlRows rows;

  traceloom::compat::VizNodeSqlRow parent;
  parent.node_id = "node-1";
  parent.local_node_id = "N001";
  parent.view_name = "semantic_tree";
  parent.path = "/N001";
  parent.node_type = "Repeat";
  parent.kind = "repeat";
  parent.symbol = "repeat";
  parent.label = "repeat x2";
  parent.category = "loop";
  parent.repeat_label = "x2";
  parent.repeat_count = 2;
  parent.occurrence_count = 1;
  parent.anchor_count = 1;
  parent.anchors_per_occurrence = 1.0;
  parent.first_anchor_idx = 1;
  parent.last_anchor_idx = 1;
  parent.compute_us = 10.0;
  parent.total_us = 12.0;
  parent.avg_compute_us = 5.0;
  parent.avg_total_us = 6.0;
  parent.aux_events = 2.0;
  parent.aux_us = 5.5;
  rows.nodes.push_back(parent);

  traceloom::compat::VizNodeSqlRow node;
  node.node_id = "node-27";
  node.local_node_id = "N027";
  node.view_name = "semantic_tree";
  node.path = "/N001/N027";
  node.node_type = "Atom";
  node.kind = "leaf";
  node.symbol = "MatMul";
  node.label = "MatMul";
  node.category = "compute";
  node.depth = 1;
  node.level = 1;
  node.anchor_count = 1;
  node.anchors_per_occurrence = 1.0;
  node.first_anchor_idx = 1;
  node.last_anchor_idx = 1;
  node.compute_us = 5.0;
  node.total_us = 10.5;
  node.avg_compute_us = 5.0;
  node.avg_total_us = 10.5;
  node.self_us = 5.0;
  node.aux_events = 2.0;
  node.aux_us = 5.5;
  rows.nodes.push_back(node);

  traceloom::compat::VizEdgeSqlRow edge;
  edge.parent_node_id = parent.node_id;
  edge.child_node_id = node.node_id;
  edge.view_name = node.view_name;
  edge.edge_order = 0;
  edge.edge_kind = "child";
  rows.edges.push_back(edge);

  traceloom::compat::VizNodeAnchorSqlRow parent_coverage;
  parent_coverage.node_id = parent.node_id;
  parent_coverage.anchor_id = "anchor-1";
  parent_coverage.view_name = parent.view_name;
  parent_coverage.occurrence_idx = 0;
  parent_coverage.anchor_order = 0;
  parent_coverage.coverage_kind = "self";
  parent_coverage.repeat_context = "N001#0";
  parent_coverage.compute_us = 10.0;
  parent_coverage.idle_us = 2.0;
  parent_coverage.total_us = 12.0;
  parent_coverage.aux_events = 2.0;
  parent_coverage.aux_us = 5.5;
  rows.node_anchors.push_back(parent_coverage);

  traceloom::compat::VizNodeAnchorSqlRow node_coverage;
  node_coverage.node_id = node.node_id;
  node_coverage.anchor_id = "anchor-1";
  node_coverage.view_name = node.view_name;
  node_coverage.occurrence_idx = 0;
  node_coverage.anchor_order = 0;
  node_coverage.coverage_kind = "self";
  node_coverage.repeat_context = "N001#0";
  node_coverage.compute_us = 5.0;
  node_coverage.idle_us = 5.5;
  node_coverage.total_us = 10.5;
  node_coverage.self_us = 5.0;
  node_coverage.aux_events = 2.0;
  node_coverage.aux_us = 5.5;
  rows.node_anchors.push_back(node_coverage);

  traceloom::compat::LoopNodeSqlRow loop_node;
  loop_node.node_id = parent.node_id;
  loop_node.view_name = parent.view_name;
  loop_node.loop_rank = 0;
  loop_node.repeat_label = parent.repeat_label;
  loop_node.repeat_count = parent.repeat_count;
  loop_node.occurrence_count = parent.occurrence_count;
  loop_node.anchor_count = parent.anchor_count;
  loop_node.total_us = parent.total_us;
  loop_node.avg_total_us = parent.avg_total_us;
  loop_node.compute_us = parent.compute_us;
  rows.loop_nodes.push_back(loop_node);

  traceloom::compat::AnchorPrimaryNodeSqlRow anchor_primary;
  anchor_primary.anchor_id = "anchor-1";
  anchor_primary.node_id = node.node_id;
  anchor_primary.view_name = node.view_name;
  anchor_primary.reason = "self_atom";
  rows.anchor_primary_nodes.push_back(anchor_primary);

  write_node_coverage_fixture_via_asset_writers(db_path, rows);
}

void seed_exact_graph_query_fixture(const std::string& db_path) {
  traceloom::compat::ExactGraphSqlRows rows;

  traceloom::compat::GraphLaunchSqlRow launch;
  launch.launch_id = "graph-launch-query-1";
  launch.graph_provider = "cuda";
  launch.graph_event_id = "event-graph-query-1";
  launch.anchor_id = "anchor-1";
  launch.replay_unit_id = 1;
  launch.graph_template_id = 2;
  launch.graph_launch_occurrence_id = 3;
  launch.replay_body_template_id = 4;
  launch.body_id = 5;
  launch.correlation_id = "101";
  launch.match_policy = "cuda_runtime_correlation";
  launch.association_policy = "cuda_graph_node_set";
  launch.start_ns = 1000;
  launch.end_ns = 6000;
  launch.dur_us = 5.0;
  rows.launches.push_back(launch);

  traceloom::compat::GraphBodyMemberSqlRow member;
  member.member_id = "graph-body-member-query-1";
  member.launch_id = launch.launch_id;
  member.graph_provider = launch.graph_provider;
  member.graph_event_id = launch.graph_event_id;
  member.replay_unit_id = launch.replay_unit_id;
  member.graph_template_id = launch.graph_template_id;
  member.graph_launch_occurrence_id = launch.graph_launch_occurrence_id;
  member.body_id = launch.body_id;
  member.replay_body_template_id = launch.replay_body_template_id;
  member.kind = "compute";
  member.event_id = "event-anchor-1";
  member.task_id = 10;
  member.source_table = "TASK";
  member.source_row_id = 10;
  member.raw_task_id = 10;
  member.start_ns = launch.start_ns;
  member.end_ns = launch.end_ns;
  member.dur_us = launch.dur_us;
  member.correlation_id = launch.correlation_id;
  member.graph_node_id = 8589934592LL;
  member.original_graph_node_id = 4294967296LL;
  member.match_policy = launch.match_policy;
  member.association_policy = launch.association_policy;
  rows.members.push_back(member);

  traceloom::compat::replace_exact_graph_rows(db_path, rows);
}

void seed_replay_cost_query_fixture(const std::string& db_path) {
  traceloom::compat::materialize_report_compatibility_views(db_path);
  execute_sql(db_path, R"SQL(
INSERT INTO traceloom_replay_cost_unit VALUES
 ('replay-cost-unit-1',0,0,1,2,1,1,'supported','');
INSERT INTO traceloom_replay_cost_launch VALUES
 ('graph-launch-query-1','replay-cost-unit-1',0,0,0,3,4,'cuda_graph',0,4,5,
  'supported','',1,5000,5000,5000,5000,0,0,1,2);
INSERT INTO traceloom_replay_cost_stream VALUES
 ('graph-launch-query-1',0,0,7,0,1,1,5000,5000,5000,0,0);
INSERT INTO traceloom_replay_cost_member VALUES
 ('graph-body-member-query-1','graph-launch-query-1','replay-cost-unit-1',
  0,0,4,'cuda_graph',0,4,5,7,0,0,'compute','event-anchor-1','MatMul',
  10,1000,6000,5000,0,5000,1000000,1,5000);
INSERT INTO traceloom_replay_cost_aggregate VALUES
 ('replay-cost-aggregate-0',0,0,2,'cuda_graph','role_collapsed',4,7,0,
  'MatMul','compute',1,1,1,1,1,1,5000,5000,5000,1000000,1,5000);
INSERT INTO traceloom_replay_cost_aggregate_member VALUES
 ('replay-cost-aggregate-0','graph-body-member-query-1',0,0,0);
)SQL");
  traceloom::compat::materialize_report_compatibility_views(db_path);
}

void seed_graph_replay_fixture(const std::string& db_path) {
  traceloom::compat::GraphReplaySqlRows rows;

  traceloom::compat::EventSqlRow graph_event;
  graph_event.event_id = "event-graph-1";
  graph_event.step_idx = 20;
  graph_event.source_table = "SYNTHETIC";
  graph_event.source_key = "aclgraph-20";
  graph_event.stream_id = 7;
  graph_event.start_ns = 10000;
  graph_event.end_ns = 18000;
  graph_event.dur_us = 8.0;
  graph_event.category = "graph";
  graph_event.role = "compute";
  graph_event.semantic_role = "anchor";
  graph_event.symbol = "ACLGraphReplay";
  graph_event.label = "ACLGraphReplay";
  graph_event.task_type = "ACL_GRAPH";
  rows.events.push_back(graph_event);

  traceloom::compat::EventSourceSqlRow graph_source;
  graph_source.event_id = graph_event.event_id;
  graph_source.source_ordinal = 0;
  graph_source.source_table = graph_event.source_table;
  graph_source.source_key = graph_event.source_key;
  graph_source.source_role = "synthetic_graph";
  rows.event_sources.push_back(graph_source);

  traceloom::compat::EventSqlRow child_event_a;
  child_event_a.event_id = "event-graph-child-1";
  child_event_a.step_idx = 21;
  child_event_a.source_table = "TASK";
  child_event_a.source_key = "task-21";
  child_event_a.stream_id = 7;
  child_event_a.start_ns = 11000;
  child_event_a.end_ns = 13000;
  child_event_a.dur_us = 2.0;
  child_event_a.category = "exec";
  child_event_a.role = "compute";
  child_event_a.semantic_role = "graph_child";
  child_event_a.symbol = "MatMul";
  rows.events.push_back(child_event_a);

  traceloom::compat::EventSqlRow child_event_b;
  child_event_b.event_id = "event-graph-child-2";
  child_event_b.step_idx = 22;
  child_event_b.source_table = "TASK";
  child_event_b.source_key = "task-22";
  child_event_b.stream_id = 7;
  child_event_b.start_ns = 14000;
  child_event_b.end_ns = 17000;
  child_event_b.dur_us = 3.0;
  child_event_b.category = "exec";
  child_event_b.role = "compute";
  child_event_b.semantic_role = "graph_child";
  child_event_b.symbol = "Add";
  rows.events.push_back(child_event_b);

  traceloom::compat::AnchorSqlRow graph_anchor;
  graph_anchor.anchor_id = "anchor-graph-1";
  graph_anchor.anchor_idx = 20;
  graph_anchor.event_id = graph_event.event_id;
  graph_anchor.step_idx = graph_event.step_idx;
  graph_anchor.symbol = graph_event.symbol;
  graph_anchor.role = graph_event.role;
  graph_anchor.label = graph_event.label;
  graph_anchor.family = "aclgraph";
  graph_anchor.start_ns = graph_event.start_ns;
  graph_anchor.end_ns = graph_event.end_ns;
  graph_anchor.dur_us = graph_event.dur_us;
  rows.anchors.push_back(graph_anchor);

  traceloom::compat::GraphReplaySqlRow replay;
  replay.graph_event_id = graph_event.event_id;
  replay.graph_provider = "aclgraph";
  replay.graph_kind = "aclgraph_replay";
  replay.graph_event_idx = 1;
  replay.event_id = graph_event.event_id;
  replay.step_idx = graph_event.step_idx;
  replay.stream_id = graph_event.stream_id;
  replay.graph_id = "graph-1";
  replay.graph_exec_id = "exec-1";
  replay.start_ns = graph_event.start_ns;
  replay.end_ns = graph_event.end_ns;
  replay.dur_us = graph_event.dur_us;
  replay.enclosed_event_count = 2;
  replay.enclosed_event_us = 5.0;
  replay.enclosed_kernel_count = 2;
  replay.enclosed_kernel_us = 5.0;
  rows.graph_replays.push_back(replay);

  traceloom::compat::GraphEnvelopeSqlRow envelope_a;
  envelope_a.envelope_id = "graph-envelope-1";
  envelope_a.graph_provider = replay.graph_provider;
  envelope_a.graph_kind = replay.graph_kind;
  envelope_a.envelope_idx = 1;
  envelope_a.graph_event_id = replay.graph_event_id;
  envelope_a.child_event_id = child_event_a.event_id;
  envelope_a.graph_step_idx = replay.step_idx;
  envelope_a.child_step_idx = child_event_a.step_idx;
  envelope_a.relation = "contains";
  envelope_a.stream_relation = "same_stream";
  envelope_a.graph_id = replay.graph_id;
  envelope_a.graph_exec_id = replay.graph_exec_id;
  envelope_a.graph_start_ns = replay.start_ns;
  envelope_a.graph_end_ns = replay.end_ns;
  envelope_a.child_start_ns = child_event_a.start_ns;
  envelope_a.child_end_ns = child_event_a.end_ns;
  envelope_a.start_offset_us = 1.0;
  envelope_a.end_offset_us = 5.0;
  envelope_a.child_dur_us = child_event_a.dur_us;
  rows.graph_envelopes.push_back(envelope_a);

  traceloom::compat::GraphEnvelopeSqlRow envelope_b;
  envelope_b.envelope_id = "graph-envelope-2";
  envelope_b.graph_provider = replay.graph_provider;
  envelope_b.graph_kind = replay.graph_kind;
  envelope_b.envelope_idx = 2;
  envelope_b.graph_event_id = replay.graph_event_id;
  envelope_b.child_event_id = child_event_b.event_id;
  envelope_b.graph_step_idx = replay.step_idx;
  envelope_b.child_step_idx = child_event_b.step_idx;
  envelope_b.relation = "contains";
  envelope_b.stream_relation = "same_stream";
  envelope_b.graph_id = replay.graph_id;
  envelope_b.graph_exec_id = replay.graph_exec_id;
  envelope_b.graph_start_ns = replay.start_ns;
  envelope_b.graph_end_ns = replay.end_ns;
  envelope_b.child_start_ns = child_event_b.start_ns;
  envelope_b.child_end_ns = child_event_b.end_ns;
  envelope_b.start_offset_us = 4.0;
  envelope_b.end_offset_us = 1.0;
  envelope_b.child_dur_us = child_event_b.dur_us;
  rows.graph_envelopes.push_back(envelope_b);

  write_graph_replay_fixture_via_asset_writers(db_path, rows);
}

void seed_reconstruction_capability_fixture(const std::string& db_path) {
  using namespace traceloom::compat;
  replace_metadata_rows(
      db_path,
      {{"source_kind", "ascend_sqlite_hot_path"},
       {"source_path", "capability-golden.db"}});

  GraphReplayEvidenceSqlRows rows;
  const auto add_region = [&](std::uint32_t order,
                              const std::string& status,
                              const std::string& order_policy) {
    GraphReconstructionRegionSqlRow row;
    row.region_id = "region-" + std::to_string(order);
    row.candidate_id = "candidate-1";
    row.region_order = order;
    row.status = status;
    row.boundary_policy = "exact_periodic_suffix";
    row.order_policy = order_policy;
    row.identity_policy = "graph_connection";
    row.shape_policy = "head_repeated_layer_tail";
    row.first_launch_occurrence_id = order * 10;
    row.last_launch_occurrence_id = order * 10 + 1;
    row.observed_launch_count = 2;
    row.expected_launch_count = 2;
    row.start_ns = static_cast<std::int64_t>(order) * 100;
    row.end_ns = row.start_ns + 100;
    row.dur_us = 0.1;
    rows.reconstruction_regions.push_back(std::move(row));
  };
  add_region(0, "recognized_complete_pattern", "host_submission_order");
  add_region(1, "recognized_complete_pattern", "host_submission_order");
  add_region(2, "unrecognized_missing_body_capability",
             "host_submission_order");
  add_region(3, "unrecognized_missing_completion_evidence",
             "device_execution_order");
  add_region(4, "unrecognized_incomplete_tail", "device_execution_order");

  GraphReplaySqlRow exact;
  exact.graph_event_id = "event-exact-replay";
  exact.graph_provider = "aclgraph";
  exact.graph_kind = "aclgraph_graph_replay";
  exact.graph_event_idx = 0;
  exact.event_id = exact.graph_event_id;
  exact.raw_json =
      "{\"reconstruction\":\"exact_replay_composition\"}";
  rows.graph_replays.push_back(std::move(exact));
  replace_graph_replay_evidence_rows(db_path, rows);
}

void seed_semantic_tree_fixture(const std::string& db_path) {
  traceloom::compat::SemanticTreeSqlRows rows;

  traceloom::compat::SemanticTreeHeaderSqlRow tree;
  tree.tree_id = "semantic-tree-1";
  tree.view_name = "anchor_tree";
  tree.tree_kind = "semantic";
  tree.root_node_id = "semantic-node-1";
  tree.schema_version = "compat-v1";
  tree.semantic_projection = "fixture";
  tree.macro_discovery = "fixture";
  tree.readable_macro_mode = "fixture";
  tree.auxiliary_attribution = "fixture";
  rows.trees.push_back(tree);

  traceloom::compat::SemanticNodeSqlRow root;
  root.node_id = "semantic-node-1";
  root.tree_id = tree.tree_id;
  root.view_name = "anchor_tree";
  root.tree_kind = "semantic";
  root.local_node_id = "N001";
  root.preorder_idx = 0;
  root.sibling_order = 0;
  root.path = "";
  root.node_type = "Repeat";
  root.semantic_kind = "repeat";
  root.label = "repeat x2";
  root.repeat_count = 2;
  root.occurrence_count = 2;
  root.anchor_count = 1;
  root.total_us = 12.0;
  rows.nodes.push_back(root);

  traceloom::compat::SemanticNodeSqlRow child;
  child.node_id = "semantic-node-27";
  child.tree_id = root.tree_id;
  child.view_name = root.view_name;
  child.tree_kind = root.tree_kind;
  child.local_node_id = "N027";
  child.parent_node_id = root.node_id;
  child.parent_local_node_id = root.local_node_id;
  child.preorder_idx = 1;
  child.sibling_order = 0;
  child.path = "/N001/N027";
  child.depth = 1;
  child.display_depth = 1;
  child.loop_depth = 1;
  child.node_type = "Atom";
  child.semantic_kind = "compute";
  child.symbol = "MatMul";
  child.label = "MatMul";
  child.category = "compute";
  child.occurrence_count = 1;
  child.anchor_count = 1;
  child.total_us = 10.5;
  child.hidden_aux_event_count = 2.0;
  child.hidden_aux_us = 5.5;
  rows.nodes.push_back(child);

  traceloom::compat::SemanticEdgeSqlRow edge;
  edge.parent_node_id = root.node_id;
  edge.child_node_id = child.node_id;
  edge.tree_id = tree.tree_id;
  edge.view_name = root.view_name;
  edge.tree_kind = root.tree_kind;
  edge.edge_order = 0;
  edge.edge_kind = "child";
  rows.edges.push_back(edge);

  write_semantic_tree_fixture_via_asset_writers(db_path, rows);
}

void seed_runtime_device_fixture(const std::string& db_path) {
  using namespace traceloom::compat;
  RuntimeDeviceSqlRows rows;
  const auto add_call = [&](const std::string& id, std::int64_t start_ns,
                            std::int64_t end_ns, const std::string& api_name,
                            const std::string& correlation) {
    RuntimeCallSqlRow row;
    row.runtime_call_id = id;
    row.provider = "cuda";
    row.clock_domain = "profiler_host";
    row.source_table = "CUPTI_ACTIVITY_KIND_RUNTIME";
    row.source_key = id;
    row.start_ns = start_ns;
    row.end_ns = end_ns;
    row.dur_us = static_cast<double>(end_ns - start_ns) / 1000.0;
    row.api_name = api_name;
    row.api_type = "cuda_runtime";
    row.process_id = "7";
    row.thread_id = "9";
    row.correlation_id = correlation;
    row.match_policy = "cuda_correlation_id";
    rows.runtime_calls.push_back(std::move(row));
  };
  add_call("runtime-call-left", 100, 110, "cudaLaunchKernel", "41");
  add_call("runtime-call-middle", 150, 160, "cudaEventQuery", "99");
  add_call("runtime-call-right", 200, 210, "cudaLaunchKernel", "42");

  DeviceWorkSqlRow work;
  work.device_work_id = "device-work-event-1";
  work.provider = "cuda";
  work.work_kind = "event";
  work.event_id = "event-1";
  work.task_id = "task-1";
  work.source_table = "CUPTI_ACTIVITY_KIND_KERNEL";
  work.source_key = "1";
  work.start_ns = 1000;
  work.end_ns = 1100;
  work.dur_us = 0.1;
  work.symbol = "kernel";
  rows.device_works.push_back(work);

  RuntimeDeviceRelationSqlRow relation;
  relation.relation_id = "runtime-device-relation-1";
  relation.runtime_call_id = "runtime-call-left";
  relation.device_work_id = work.device_work_id;
  relation.relation_kind = "provider_correlation";
  relation.match_policy = "cuda_correlation_id";
  relation.evidence_level = "direct_provider_identifier";
  relation.support_state = "supported_exact";
  relation.cardinality = "one_to_one";
  relation.runtime_candidate_count = 1;
  relation.device_candidate_count = 1;
  relation.correlation_id = "41";
  rows.relations.push_back(relation);

  AnchorHostIntervalSqlRow interval;
  interval.interval_id = "anchor-host-interval-anchor-1-anchor-2";
  interval.left_anchor_id = "anchor-1";
  interval.right_anchor_id = "anchor-2";
  interval.left_runtime_call_id = "runtime-call-left";
  interval.right_runtime_call_id = "runtime-call-right";
  interval.left_endpoint_count = 1;
  interval.right_endpoint_count = 1;
  interval.provider = "cuda";
  interval.clock_domain = "profiler_host";
  interval.host_start_ns = "110";
  interval.host_end_ns = "200";
  interval.scope_policy = "same_thread";
  interval.process_id = "7";
  interval.thread_id = "9";
  interval.support_state = "supported_ordered";
  rows.host_intervals.push_back(interval);
  rows.host_activities.push_back(
      AnchorHostActivitySqlRow{interval.interval_id, "runtime-call-middle", 0});

  replace_runtime_device_rows(db_path, rows);
  std::vector<AnchorSqlRow> anchors;
  AnchorSqlRow left_anchor;
  left_anchor.anchor_id = "anchor-1";
  left_anchor.anchor_idx = 1;
  left_anchor.symbol = "kernel";
  left_anchor.role = "compute";
  left_anchor.label = "kernel";
  left_anchor.family = "compute";
  left_anchor.start_ns = 1000;
  left_anchor.end_ns = 1100;
  left_anchor.dur_us = 0.1;
  anchors.push_back(left_anchor);
  AnchorSqlRow right_anchor = left_anchor;
  right_anchor.anchor_id = "anchor-2";
  right_anchor.anchor_idx = 2;
  right_anchor.start_ns = 2000;
  right_anchor.end_ns = 2100;
  anchors.push_back(right_anchor);
  replace_anchor_rows(db_path, anchors);

  NodeCoverageSqlRows node_rows;
  VizNodeSqlRow node;
  node.node_id = "node-1";
  node.local_node_id = "N001";
  node.view_name = "anchor_tree";
  node.path = "/N001";
  node.node_type = "Atom";
  node.kind = "leaf";
  node.symbol = "kernel";
  node.label = "kernel";
  node.category = "compute";
  node.occurrence_count = 2;
  node.total_us = 1.0;
  node_rows.nodes.push_back(node);
  VizNodeAnchorSqlRow node_anchor;
  node_anchor.node_id = node.node_id;
  node_anchor.anchor_id = left_anchor.anchor_id;
  node_anchor.view_name = node.view_name;
  node_anchor.occurrence_idx = 1;
  node_anchor.anchor_order = 1;
  node_anchor.coverage_kind = "self";
  node_rows.node_anchors.push_back(node_anchor);
  replace_node_coverage_rows(db_path, node_rows);
  materialize_report_compatibility_views(db_path);
}

std::vector<QueryCase> active_query_cases() {
  return {
      QueryCase{
          "anchor-host-activity.sql",
          {
              "left_anchor_id",
              "right_anchor_id",
              "support_state",
              "scope_policy",
              "host_start_ns",
              "host_end_ns",
              "observed_runtime_call_id",
              "api_name",
              "api_type",
              "observed_start_ns",
              "observed_end_ns",
              "call_duration_us",
              "interval_overlap_us",
              "interval_relation",
              "observed_source_table",
              "observed_source_key",
          },
          1,
      },
      QueryCase{
          "node-host-activity.sql",
          {
              "node_id",
              "local_node_id",
              "occurrence_idx",
              "anchor_order",
              "anchor_idx",
              "right_anchor_idx",
              "right_anchor_symbol",
              "right_anchor_role",
              "host_interval_us",
              "placement_semantics",
              "left_anchor_id",
              "right_anchor_id",
              "api_name",
              "observed_call_count",
              "scheduled_overlap_us",
              "scope_policy",
              "support_state",
          },
          1,
      },
      QueryCase{
          "anchor-aux.sql",
          {
              "anchor_idx",
              "symbol",
              "role",
              "aux_events",
              "aux_us",
              "first_aux_step_idx",
              "last_aux_step_idx",
          },
          1,
      },
      QueryCase{
          "cuda-graph-envelope.sql",
          {
              "graph_provider",
              "graph_kind",
              "graph_event_idx",
              "anchor_idx",
              "graph_exec_id",
              "graph_id",
              "stream_id",
              "graph_us",
              "enclosed_event_count",
              "enclosed_event_us",
              "enclosed_kernel_count",
              "enclosed_kernel_us",
              "first_child_step_idx",
              "last_child_step_idx",
          },
          1,
      },
      QueryCase{
          "event-graph-node-occurrences.sql",
          {
              "event_id",
              "member_symbol",
              "graph_node_id",
              "original_graph_node_id",
              "node_id",
              "occurrence_idx",
              "anchor_id",
              "node_member_order",
              "node_slot_order",
              "correlation_id",
              "source_table",
              "source_row_id",
          },
          1,
      },
      QueryCase{
          "node-events.sql",
          {
              "node",
              "occurrence_idx",
              "anchor_order",
              "anchor_idx",
              "anchor_symbol",
              "anchor_label",
              "source_table",
              "source_key",
              "stream_id",
              "start_ns",
              "end_ns",
              "dur_us",
              "role",
              "semantic_role",
          },
          1,
      },
      QueryCase{
          "node-graph-body-members.sql",
          {
              "node_id",
              "occurrence_idx",
              "anchor_id",
              "correlation_id",
              "lane_ordinal",
              "task_ordinal",
              "kind",
              "event_id",
              "member_symbol",
              "graph_node_id",
              "original_graph_node_id",
              "source_table",
              "source_row_id",
              "dur_us",
          },
          1,
      },
      QueryCase{
          "node-replay-cost-members.sql",
          {
              "node_id", "occurrence_idx", "anchor_id", "launch_id",
              "member_id", "slot_role", "slot_order", "stream_id",
              "lane_ordinal", "task_ordinal", "identity", "kind",
              "duration_ns", "scheduled_work_share_ppm", "event_id",
              "source_table", "source_row_id",
          },
          1,
      },
      QueryCase{
          "node-occurrences.sql",
          {
              "node",
              "occurrence_idx",
              "repeat_context",
              "anchor_start_idx",
              "anchor_end_idx",
              "anchor_count",
              "start_ns",
              "end_ns",
              "total_us",
              "compute_us",
              "comm_us",
              "idle_us",
              "aux_us",
          },
          1,
      },
      QueryCase{
          "repeat-overview.sql",
          {
              "local_node_id",
              "level",
              "kind",
              "node_type",
              "repeat_count",
              "occurrence_count",
              "anchor_count",
              "total_us",
              "avg_total_us",
              "anchor_us",
              "aux_us",
          },
          1,
      },
      QueryCase{
          "reconstruction-capability-matrix.sql",
          {
              "source_kind",
              "source_path",
              "capability_state",
              "body_capability",
              "completion_capability",
              "ordering_mode",
              "region_count",
              "recognized_region_count",
              "unrecognized_region_count",
              "missing_body_capability_count",
              "missing_body_evidence_count",
              "missing_completion_evidence_count",
              "incomplete_tail_count",
              "body_mismatch_count",
              "leading_context_count",
              "exact_replay_unit_count",
              "legacy_replay_unit_count",
          },
          1,
      },
      QueryCase{
          "runtime-device-relations.sql",
          {
              "relation_id",
              "provider",
              "api_name",
              "runtime_start_ns",
              "runtime_dur_us",
              "work_kind",
              "device_symbol",
              "device_start_ns",
              "device_dur_us",
              "match_policy",
              "evidence_level",
              "support_state",
              "cardinality",
          },
          1,
      },
      QueryCase{
          "replay-cost-hotspots.sql",
          {
              "aggregate_id", "graph_template_id", "slot_role",
              "replay_body_template_id", "stream_id", "task_ordinal",
              "identity", "kind", "member_occurrence_count",
              "replay_unit_count", "duration_p25_ns", "duration_median_ns",
              "duration_p75_ns", "scheduled_work_share_ppm",
          },
          1,
      },
      QueryCase{
          "replay-cost-explain.sql",
          {
              "aggregate_id", "aggregation_scope", "aggregate_identity",
              "duration_p25_ns", "duration_median_ns", "duration_p75_ns",
              "contributor_order", "cost_unit_id", "launch_id", "member_id",
              "slot_order", "stream_id", "task_ordinal", "duration_ns",
              "event_id", "source_table", "source_row_id",
          },
          1,
      },
      QueryCase{
          "repeat-children.sql",
          {
              "local_node_id",
              "level",
              "kind",
              "node_type",
              "repeat_count",
              "occurrence_count",
              "anchor_count",
              "total_us",
              "avg_total_us",
          },
          1,
      },
      QueryCase{
          "tree-map.sql",
          {
              "node",
              "label",
              "depth",
              "occ",
              "avg_total_us",
              "avg_aux_us",
              "total_us",
          },
          1,
      },
      QueryCase{
          "node-cost-breakdown.sql",
          {
              "node",
              "depth",
              "loop_depth",
              "kind",
              "label",
              "repeat_count",
              "occurrence_count",
              "total_us",
              "avg_total_us",
              "avg_compute_us",
              "avg_comm_us",
              "avg_idle_us",
              "avg_active_us",
              "avg_aux_us",
              "compute_pct",
              "comm_pct",
              "idle_pct",
              "active_pct",
              "aux_pct",
          },
          1,
      },
      QueryCase{
          "semantic-tree-readable.sql",
          {
              "line",
          },
          1,
      },
      QueryCase{
          "anchor-cost-breakdown.sql",
          traceloom::compat::column_names(
              traceloom::compat::anchor_cost_breakdown_table_schema()),
          1,
      },
  };
}

std::vector<std::string> checked_in_report_sql_filenames() {
  std::vector<std::string> filenames;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(report_sql_dir())) {
    if (entry.is_regular_file() && entry.path().extension() == ".sql") {
      filenames.push_back(entry.path().filename().string());
    }
  }
  std::sort(filenames.begin(), filenames.end());
  return filenames;
}

std::vector<std::string> active_query_case_filenames(
    const std::vector<QueryCase>& query_cases) {
  std::vector<std::string> filenames;
  filenames.reserve(query_cases.size());
  for (const QueryCase& query_case : query_cases) {
    filenames.push_back(query_case.filename);
  }
  std::sort(filenames.begin(), filenames.end());
  return filenames;
}

}  // namespace

int main() {
  using traceloom::testing::require;

  const std::vector<QueryCase> query_cases = active_query_cases();
  require(active_query_case_filenames(query_cases) ==
          checked_in_report_sql_filenames());

  const std::string empty_db_path = temp_db_path();
  traceloom::compat::materialize_report_compatibility_views(empty_db_path);
  for (const QueryCase& query_case : query_cases) {
    const QueryResult result = run_query(empty_db_path, query_case);
    require(result.columns == query_case.expected_columns);
    require(result.row_count == 0);
  }
  std::remove(empty_db_path.c_str());

  for (const QueryCase& query_case : query_cases) {
    const std::string db_path = temp_db_path();
    if (query_case.filename == "anchor-cost-breakdown.sql") {
      seed_anchor_cost_fixture(db_path);
    } else if (query_case.filename == "anchor-host-activity.sql" ||
               query_case.filename == "node-host-activity.sql" ||
               query_case.filename == "runtime-device-relations.sql") {
      seed_runtime_device_fixture(db_path);
    } else if (query_case.filename == "anchor-aux.sql") {
      seed_anchor_aux_fixture(db_path);
      require_anchor_aux_invariants(db_path);
    } else if (query_case.filename == "cuda-graph-envelope.sql") {
      seed_graph_replay_fixture(db_path);
      require_graph_replay_invariants(db_path);
    } else if (query_case.filename ==
               "reconstruction-capability-matrix.sql") {
      seed_reconstruction_capability_fixture(db_path);
    } else if (query_case.filename == "node-events.sql" ||
               query_case.filename == "node-occurrences.sql" ||
               query_case.filename == "node-graph-body-members.sql" ||
               query_case.filename == "node-replay-cost-members.sql" ||
               query_case.filename == "event-graph-node-occurrences.sql" ||
               query_case.filename == "repeat-overview.sql" ||
               query_case.filename == "repeat-children.sql" ||
               query_case.filename == "tree-map.sql" ||
               query_case.filename == "node-cost-breakdown.sql") {
      seed_anchor_aux_fixture(db_path);
      seed_node_event_fixture(db_path);
      if (query_case.filename == "node-graph-body-members.sql" ||
          query_case.filename == "node-replay-cost-members.sql" ||
          query_case.filename == "event-graph-node-occurrences.sql") {
        seed_exact_graph_query_fixture(db_path);
      }
      if (query_case.filename == "node-replay-cost-members.sql") {
        seed_replay_cost_query_fixture(db_path);
      }
      require_anchor_aux_invariants(db_path);
      require_node_coverage_invariants(db_path);
    } else if (query_case.filename == "semantic-tree-readable.sql") {
      seed_semantic_tree_fixture(db_path);
      require_semantic_tree_invariants(db_path);
    } else if (query_case.filename == "replay-cost-hotspots.sql" ||
               query_case.filename == "replay-cost-explain.sql") {
      seed_anchor_aux_fixture(db_path);
      seed_node_event_fixture(db_path);
      seed_exact_graph_query_fixture(db_path);
      seed_replay_cost_query_fixture(db_path);
    }

    const QueryResult result = run_query(db_path, query_case);
    const std::string column_message =
        query_case.filename + ": unexpected report columns";
    const std::string row_message =
        query_case.filename + ": expected seeded report rows";
    require(result.columns == query_case.expected_columns,
            column_message.c_str());
    require(result.row_count >= query_case.expected_min_rows,
            row_message.c_str());
    if (query_case.filename == "anchor-cost-breakdown.sql") {
      require(result.row_count == 2);
      require(result.first_row[0] == "2");
      require(result.first_row[1] == "ACLL");
      require(result.first_row[2] == "graph_l");
      require(result.first_row[10] == "partial_overlap");
    } else if (query_case.filename == "anchor-aux.sql") {
      require(result.row_count == 1);
      require(result.first_row[0] == "1");
      require(result.first_row[1] == "MatMul");
      require(result.first_row[2] == "compute");
      require(result.first_row[3] == "2");
      require(result.first_row[5] == "8");
      require(result.first_row[6] == "9");
    } else if (query_case.filename == "cuda-graph-envelope.sql") {
      require(result.row_count == 1);
      require(result.first_row[0] == "aclgraph");
      require(result.first_row[1] == "aclgraph_replay");
      require(result.first_row[2] == "1");
      require(result.first_row[3] == "20");
      require(result.first_row[4] == "exec-1");
      require(result.first_row[5] == "graph-1");
      require(result.first_row[7] == "8.0" || result.first_row[7] == "8");
      require(result.first_row[8] == "2");
      require(result.first_row[12] == "21");
      require(result.first_row[13] == "22");
    } else if (query_case.filename ==
               "reconstruction-capability-matrix.sql") {
      require(result.row_count == 1);
      require(result.first_row[0] == "ascend_sqlite_hot_path");
      require(result.first_row[1] == "capability-golden.db");
      require(result.first_row[2] == "capability_incomplete");
      require(result.first_row[3] == "unavailable");
      require(result.first_row[4] == "incomplete");
      require(result.first_row[5] == "mixed");
      require(result.first_row[6] == "5");
      require(result.first_row[7] == "2");
      require(result.first_row[8] == "3");
      require(result.first_row[9] == "1");
      require(result.first_row[10] == "0");
      require(result.first_row[11] == "1");
      require(result.first_row[12] == "1");
      require(result.first_row[15] == "1");
      require(result.first_row[16] == "0");
    } else if (query_case.filename == "node-events.sql") {
      require(result.row_count == 1);
      require(result.first_row[0] == "N027");
      require(result.first_row[3] == "1");
      require(result.first_row[4] == "MatMul");
      require(result.first_row[6] == "TASK");
      require(result.first_row[7] == "task-10");
      require(result.first_row[12] == "compute");
      require(result.first_row[13] == "anchor");
    } else if (query_case.filename == "node-graph-body-members.sql") {
      require(result.row_count == 1);
      require(result.first_row[0] == "node-1");
      require(result.first_row[1] == "0");
      require(result.first_row[2] == "anchor-1");
      require(result.first_row[3] == "101");
      require(result.first_row[7] == "event-anchor-1");
      require(result.first_row[8] == "MatMul");
      require(result.first_row[9] == "8589934592");
      require(result.first_row[10] == "4294967296");
    } else if (query_case.filename == "node-replay-cost-members.sql") {
      require(result.row_count == 1);
      require(result.first_row[4] == "graph-body-member-query-1");
      require(result.first_row[10] == "MatMul");
      require(result.first_row[12] == "5000");
      require(result.first_row[15] == "TASK");
    } else if (query_case.filename == "replay-cost-hotspots.sql") {
      require(result.row_count == 1);
      require(result.first_row[0] == "replay-cost-aggregate-0");
      require(result.first_row[6] == "MatMul");
      require(result.first_row[11] == "5000");
    } else if (query_case.filename == "replay-cost-explain.sql") {
      require(result.row_count == 1);
      require(result.first_row[1] == "role_collapsed");
      require(result.first_row[9] == "graph-body-member-query-1");
      require(result.first_row[15] == "TASK");
    } else if (query_case.filename ==
               "event-graph-node-occurrences.sql") {
      require(result.row_count == 2);
      require(result.first_row[0] == "event-anchor-1");
      require(result.first_row[1] == "MatMul");
      require(result.first_row[2] == "8589934592");
      require(result.first_row[3] == "4294967296");
      require(result.first_row[4] == "node-1");
      require(result.first_row[6] == "anchor-1");
      require(result.first_row[9] == "101");
    } else if (query_case.filename == "node-occurrences.sql") {
      require(result.row_count == 1);
      require(result.first_row[0] == "N027");
      require(result.first_row[3] == "1");
      require(result.first_row[4] == "1");
      require(result.first_row[5] == "1");
      require(result.first_row[8] == "10.5");
      require(result.first_row[9] == "5.0" || result.first_row[9] == "5");
      require(result.first_row[12] == "5.5");
    } else if (query_case.filename == "repeat-overview.sql") {
      require(result.row_count == 1);
      require(result.first_row[0] == "N001");
      require(result.first_row[2] == "repeat");
      require(result.first_row[4] == "2");
      require(result.first_row[7] == "12.0" || result.first_row[7] == "12");
      require(result.first_row[8] == "6.0" || result.first_row[8] == "6");
      require(result.first_row[9] == "5.0" || result.first_row[9] == "5");
      require(result.first_row[10] == "5.5");
    } else if (query_case.filename == "repeat-children.sql") {
      require(result.row_count == 1);
      require(result.first_row[0] == "N027");
      require(result.first_row[1] == "1");
      require(result.first_row[2] == "leaf");
      require(result.first_row[3] == "Atom");
      require(result.first_row[7] == "10.5");
    } else if (query_case.filename == "tree-map.sql") {
      require(result.row_count == 2);
      require(result.first_row[0] == "N001");
      require(result.first_row[1] == "repeat x2");
      require(result.first_row[2] == "0");
      require(result.first_row[3] == "1");
      require(result.first_row[4] == "6.0" || result.first_row[4] == "6");
      require(result.first_row[5] == "2.75");
    } else if (query_case.filename == "node-cost-breakdown.sql") {
      require(result.row_count == 1);
      require(result.first_row[0] == "N027");
      require(result.first_row[1] == "1");
      require(result.first_row[2] == "1");
      require(result.first_row[3] == "leaf");
      require(result.first_row[7] == "10.5");
      require(result.first_row[13] == "5.5");
      require(result.first_row[14] == "47.62");
      require(result.first_row[18] == "52.38");
    } else if (query_case.filename == "semantic-tree-readable.sql") {
      require(result.row_count == 2);
      require(result.first_row[0] ==
              "- [root] N001 Repeat x2 | repeat x2 | anchors=1 "
              "total_us=12.000");
    }
    std::remove(db_path.c_str());
  }

  return 0;
}
