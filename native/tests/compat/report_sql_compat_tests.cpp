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

  traceloom::compat::replace_anchor_aux_rows(db_path, rows);
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
  parent.occurrence_count = 2;
  parent.anchor_count = 1;
  parent.anchors_per_occurrence = 0.5;
  parent.first_anchor_idx = 1;
  parent.last_anchor_idx = 1;
  parent.compute_us = 10.0;
  parent.total_us = 12.0;
  parent.avg_compute_us = 5.0;
  parent.avg_total_us = 6.0;
  parent.self_us = 10.0;
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
  rows.node_anchors.push_back(parent_coverage);

  traceloom::compat::VizNodeAnchorSqlRow node_coverage;
  node_coverage.node_id = node.node_id;
  node_coverage.anchor_id = "anchor-1";
  node_coverage.view_name = node.view_name;
  node_coverage.occurrence_idx = 0;
  node_coverage.anchor_order = 0;
  node_coverage.coverage_kind = "self";
  node_coverage.repeat_context = "N001#0";
  rows.node_anchors.push_back(node_coverage);

  traceloom::compat::replace_node_coverage_rows(db_path, rows);
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

  traceloom::compat::replace_graph_replay_rows(db_path, rows);
}

void seed_semantic_tree_fixture(const std::string& db_path) {
  traceloom::compat::SemanticTreeSqlRows rows;

  traceloom::compat::SemanticNodeSqlRow root;
  root.node_id = "semantic-node-1";
  root.tree_id = "semantic-tree-1";
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

  traceloom::compat::replace_semantic_tree_rows(db_path, rows);
}

std::vector<QueryCase> active_query_cases() {
  return {
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
              "avg_self_us",
              "avg_aux_us",
              "compute_pct",
              "comm_pct",
              "idle_pct",
              "self_pct",
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

  for (const QueryCase& query_case : query_cases) {
    const std::string db_path = temp_db_path();
    if (query_case.filename == "anchor-cost-breakdown.sql") {
      seed_anchor_cost_fixture(db_path);
    } else if (query_case.filename == "anchor-aux.sql") {
      seed_anchor_aux_fixture(db_path);
    } else if (query_case.filename == "cuda-graph-envelope.sql") {
      seed_graph_replay_fixture(db_path);
    } else if (query_case.filename == "node-events.sql" ||
               query_case.filename == "node-occurrences.sql" ||
               query_case.filename == "repeat-overview.sql" ||
               query_case.filename == "repeat-children.sql" ||
               query_case.filename == "tree-map.sql" ||
               query_case.filename == "node-cost-breakdown.sql") {
      seed_anchor_aux_fixture(db_path);
      seed_node_event_fixture(db_path);
    } else if (query_case.filename == "semantic-tree-readable.sql") {
      seed_semantic_tree_fixture(db_path);
    }

    const QueryResult result = run_query(db_path, query_case);
    require(result.columns == query_case.expected_columns);
    require(result.row_count >= query_case.expected_min_rows);
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
    } else if (query_case.filename == "node-events.sql") {
      require(result.row_count == 1);
      require(result.first_row[0] == "N027");
      require(result.first_row[3] == "1");
      require(result.first_row[4] == "MatMul");
      require(result.first_row[6] == "TASK");
      require(result.first_row[7] == "task-10");
      require(result.first_row[12] == "compute");
      require(result.first_row[13] == "anchor");
    } else if (query_case.filename == "node-occurrences.sql") {
      require(result.row_count == 1);
      require(result.first_row[0] == "N027");
      require(result.first_row[3] == "1");
      require(result.first_row[4] == "1");
      require(result.first_row[5] == "1");
      require(result.first_row[8] == "5.0" || result.first_row[8] == "5");
      require(result.first_row[9] == "5.0" || result.first_row[9] == "5");
      require(result.first_row[12] == "5.5");
    } else if (query_case.filename == "repeat-overview.sql") {
      require(result.row_count == 1);
      require(result.first_row[0] == "N001");
      require(result.first_row[2] == "repeat");
      require(result.first_row[4] == "2");
      require(result.first_row[7] == "12.0" || result.first_row[7] == "12");
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
      require(result.first_row[3] == "2");
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
