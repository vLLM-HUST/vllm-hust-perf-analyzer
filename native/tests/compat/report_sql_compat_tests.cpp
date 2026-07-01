#include "traceloom/compat/schema.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

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

  traceloom::compat::VizNodeSqlRow node;
  node.node_id = "node-27";
  node.local_node_id = "N027";
  node.view_name = "semantic_tree";
  node.path = "/N027";
  node.node_type = "Atom";
  node.kind = "leaf";
  node.symbol = "MatMul";
  node.label = "MatMul";
  node.category = "compute";
  node.anchor_count = 1;
  node.anchors_per_occurrence = 1.0;
  node.first_anchor_idx = 1;
  node.last_anchor_idx = 1;
  node.compute_us = 5.0;
  node.total_us = 5.0;
  node.avg_compute_us = 5.0;
  node.avg_total_us = 5.0;
  node.self_us = 5.0;
  rows.nodes.push_back(node);

  traceloom::compat::VizNodeAnchorSqlRow coverage;
  coverage.node_id = node.node_id;
  coverage.anchor_id = "anchor-1";
  coverage.view_name = node.view_name;
  coverage.occurrence_idx = 0;
  coverage.anchor_order = 0;
  coverage.coverage_kind = "self";
  rows.node_anchors.push_back(coverage);

  traceloom::compat::replace_node_coverage_rows(db_path, rows);
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
          "anchor-cost-breakdown.sql",
          traceloom::compat::column_names(
              traceloom::compat::anchor_cost_breakdown_table_schema()),
          1,
      },
  };
}

}  // namespace

int main() {
  using traceloom::testing::require;

  const std::string db_path = temp_db_path();
  seed_anchor_aux_fixture(db_path);
  seed_node_event_fixture(db_path);
  seed_anchor_cost_fixture(db_path);

  for (const QueryCase& query_case : active_query_cases()) {
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
    }
  }

  std::remove(db_path.c_str());
  return 0;
}
