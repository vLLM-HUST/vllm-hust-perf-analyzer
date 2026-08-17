#include "native_sidecar_materializer_test_support.h"

#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <cmath>
#include <string>
#include <vector>

namespace traceloom::testing::sidecar_materializer {
namespace {

struct HostWindowCall {
  std::string runtime_call_id;
  double overlap_us = 0.0;
  std::string interval_relation;
  int observed_order = -1;
};

std::vector<HostWindowCall> query_host_window_calls(
    const std::string& path,
    const std::string& interval_id) {
  sqlite3* db = nullptr;
  traceloom::testing::require(
      sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) ==
      SQLITE_OK);

  sqlite3_stmt* catalog = nullptr;
  traceloom::testing::require(
      sqlite3_prepare_v2(
          db,
          "SELECT example_sql FROM traceloom_projection_recipe WHERE "
          "projection_name = 'host_window_calls'",
          -1, &catalog, nullptr) == SQLITE_OK);
  traceloom::testing::require(sqlite3_step(catalog) == SQLITE_ROW);
  const unsigned char* raw_sql = sqlite3_column_text(catalog, 0);
  traceloom::testing::require(raw_sql != nullptr);
  const std::string sql(reinterpret_cast<const char*>(raw_sql));
  traceloom::testing::require(sqlite3_step(catalog) == SQLITE_DONE);
  sqlite3_finalize(catalog);

  sqlite3_stmt* plan = nullptr;
  const std::string plan_sql = "EXPLAIN QUERY PLAN " + sql;
  traceloom::testing::require(
      sqlite3_prepare_v2(db, plan_sql.c_str(), -1, &plan, nullptr) ==
          SQLITE_OK,
      sqlite3_errmsg(db));
  const int plan_interval_parameter =
      sqlite3_bind_parameter_index(plan, ":interval_id");
  traceloom::testing::require(plan_interval_parameter > 0);
  traceloom::testing::require(
      sqlite3_bind_text(plan, plan_interval_parameter, interval_id.c_str(),
                        -1, SQLITE_TRANSIENT) == SQLITE_OK);
  bool uses_runtime_time_index = false;
  while (sqlite3_step(plan) == SQLITE_ROW) {
    const unsigned char* detail = sqlite3_column_text(plan, 3);
    uses_runtime_time_index =
        uses_runtime_time_index ||
        (detail != nullptr &&
         std::string(reinterpret_cast<const char*>(detail))
                 .find("idx_traceloom_runtime_call_time") !=
             std::string::npos);
  }
  sqlite3_finalize(plan);
  traceloom::testing::require(uses_runtime_time_index);

  sqlite3_stmt* query = nullptr;
  traceloom::testing::require(
      sqlite3_prepare_v2(db, sql.c_str(), -1, &query, nullptr) == SQLITE_OK,
      sqlite3_errmsg(db));
  const int interval_parameter =
      sqlite3_bind_parameter_index(query, ":interval_id");
  traceloom::testing::require(interval_parameter > 0);
  traceloom::testing::require(
      sqlite3_bind_text(query, interval_parameter, interval_id.c_str(), -1,
                        SQLITE_TRANSIENT) == SQLITE_OK);

  std::vector<HostWindowCall> calls;
  while (sqlite3_step(query) == SQLITE_ROW) {
    const unsigned char* call_id = sqlite3_column_text(query, 6);
    const unsigned char* relation = sqlite3_column_text(query, 13);
    if (call_id == nullptr) {
      calls.push_back({});
      continue;
    }
    traceloom::testing::require(relation != nullptr);
    calls.push_back({reinterpret_cast<const char*>(call_id),
                     sqlite3_column_double(query, 12),
                     reinterpret_cast<const char*>(relation),
                     sqlite3_column_int(query, 14)});
  }
  sqlite3_finalize(query);
  sqlite3_close(db);
  return calls;
}

struct BubbleFamilyStat {
  std::string api_family;
  int presence_count = 0;
  double average_calls = 0.0;
  double average_overlap_us = 0.0;
};

std::vector<BubbleFamilyStat> query_bubble_host_context(
    const std::string& path,
    const std::string& structural_position_id) {
  sqlite3* db = nullptr;
  traceloom::testing::require(
      sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) ==
      SQLITE_OK);
  sqlite3_stmt* catalog = nullptr;
  traceloom::testing::require(
      sqlite3_prepare_v2(
          db,
          "SELECT example_sql FROM traceloom_projection_recipe WHERE "
          "projection_name = 'bubble_host_context'",
          -1, &catalog, nullptr) == SQLITE_OK);
  traceloom::testing::require(sqlite3_step(catalog) == SQLITE_ROW);
  const unsigned char* raw_sql = sqlite3_column_text(catalog, 0);
  traceloom::testing::require(raw_sql != nullptr);
  const std::string sql(reinterpret_cast<const char*>(raw_sql));
  sqlite3_finalize(catalog);

  sqlite3_stmt* plan = nullptr;
  const std::string plan_sql = "EXPLAIN QUERY PLAN " + sql;
  traceloom::testing::require(
      sqlite3_prepare_v2(db, plan_sql.c_str(), -1, &plan, nullptr) ==
          SQLITE_OK,
      sqlite3_errmsg(db));
  const int plan_parameter =
      sqlite3_bind_parameter_index(plan, ":structural_position_id");
  traceloom::testing::require(plan_parameter > 0);
  traceloom::testing::require(
      sqlite3_bind_text(plan, plan_parameter, structural_position_id.c_str(),
                        -1, SQLITE_TRANSIENT) == SQLITE_OK);
  bool uses_position_index = false;
  bool uses_runtime_time_index = false;
  while (sqlite3_step(plan) == SQLITE_ROW) {
    const unsigned char* detail = sqlite3_column_text(plan, 3);
    const std::string text =
        detail == nullptr ? "" : reinterpret_cast<const char*>(detail);
    uses_position_index =
        uses_position_index ||
        text.find("idx_traceloom_structure_bubble_position_id") !=
            std::string::npos;
    uses_runtime_time_index =
        uses_runtime_time_index ||
        text.find("idx_traceloom_runtime_call_time") != std::string::npos;
  }
  sqlite3_finalize(plan);
  traceloom::testing::require(uses_position_index);
  traceloom::testing::require(uses_runtime_time_index);

  sqlite3_stmt* query = nullptr;
  traceloom::testing::require(
      sqlite3_prepare_v2(db, sql.c_str(), -1, &query, nullptr) == SQLITE_OK,
      sqlite3_errmsg(db));
  const int parameter =
      sqlite3_bind_parameter_index(query, ":structural_position_id");
  traceloom::testing::require(parameter > 0);
  traceloom::testing::require(
      sqlite3_bind_text(query, parameter, structural_position_id.c_str(), -1,
                        SQLITE_TRANSIENT) == SQLITE_OK);
  std::vector<BubbleFamilyStat> stats;
  while (sqlite3_step(query) == SQLITE_ROW) {
    const unsigned char* family = sqlite3_column_text(query, 8);
    traceloom::testing::require(family != nullptr);
    stats.push_back({reinterpret_cast<const char*>(family),
                     sqlite3_column_int(query, 9),
                     sqlite3_column_double(query, 10),
                     sqlite3_column_double(query, 11)});
  }
  sqlite3_finalize(query);
  sqlite3_close(db);
  return stats;
}

}  // namespace

void run_host_window_query_tests(const std::string& augmented_path) {
  using traceloom::testing::require;

  run_sql(
      augmented_path,
      "INSERT INTO traceloom_anchor_host_interval("
      "interval_id, db_idx, device_id, left_anchor_id, right_anchor_id, "
      "left_runtime_call_id, right_runtime_call_id, left_endpoint_count, "
      "right_endpoint_count, provider, clock_domain, host_start_ns, "
      "host_end_ns, scope_policy, process_id, thread_id, support_state) "
      "VALUES('interval-queryable', 7, 0, 'left', 'right', NULL, NULL, 1, 1, "
      "'ascend', 'host', 100000, 200000, 'same_thread', 'p1', 't1', "
      "'supported_ordered'),"
      "('interval-unsupported', 7, 0, 'left-u', 'right-u', NULL, NULL, 0, 0, "
      "'ascend', 'host', 100000, 200000, 'same_thread', 'p1', 't1', "
      "'unsupported_missing_runtime_endpoint');"
      "INSERT INTO traceloom_runtime_call("
      "runtime_call_id, db_idx, provider, clock_domain, source_table, "
      "source_key, start_ns, end_ns, dur_us, api_name, api_type, process_id, "
      "thread_id, global_tid, context_id, device_id, correlation_id, "
      "match_policy, raw_json) VALUES"
      "('call-boundary', 7, 'ascend', 'host', 'CANN_API', '1', 90000, "
      "110000, 20.0, 'aclrtSynchronizeStream', 'sync', 'p1', 't1', NULL, "
      "NULL, NULL, NULL, 'fixture', NULL),"
      "('call-contained', 7, 'ascend', 'host', 'CANN_API', '2', 120000, "
      "150000, 30.0, 'aclrtLaunchKernel', 'launch', 'p1', 't1', NULL, NULL, "
      "NULL, NULL, 'fixture', NULL),"
      "('call-wrong-db', 8, 'ascend', 'host', 'CANN_API', '3', 125000, "
      "135000, 10.0, 'wrongDb', 'launch', 'p1', 't1', NULL, NULL, NULL, "
      "NULL, 'fixture', NULL),"
      "('call-wrong-thread', 7, 'ascend', 'host', 'CANN_API', '4', 130000, "
      "140000, 10.0, 'wrongThread', 'launch', 'p1', 't2', NULL, NULL, NULL, "
      "NULL, 'fixture', NULL),"
      "('call-before', 7, 'ascend', 'host', 'CANN_API', '5', 80000, "
      "100000, 20.0, 'before', 'launch', 'p1', 't1', NULL, NULL, NULL, "
      "NULL, 'fixture', NULL),"
      "('call-after', 7, 'ascend', 'host', 'CANN_API', '6', 200000, "
      "210000, 10.0, 'after', 'launch', 'p1', 't1', NULL, NULL, NULL, NULL, "
      "'fixture', NULL);"
      "INSERT INTO traceloom_structure_bubble_position("
      "db_idx, device_id, view_name, structural_position_id, right_node_id, "
      "right_local_node_id, right_node_path, right_node_kind, "
      "right_node_symbol, bubble_occurrence_count, "
      "supported_host_occurrence_count, missing_endpoint_occurrence_count, "
      "nonmonotonic_occurrence_count, other_unsupported_occurrence_count, "
      "total_bubble_us, avg_bubble_us, min_bubble_us, max_bubble_us, "
      "host_observation_coverage) VALUES(7, 0, 'tree', "
      "'position-queryable', 'node', 'local-node', 'root/node', 'leaf', "
      "'MatMul', 1, 1, 0, 0, 0, 50.0, 50.0, 50.0, 50.0, 1.0);"
      "INSERT INTO traceloom_structure_bubble_occurrence("
      "bubble_id, db_idx, device_id, view_name, structural_position_id, "
      "host_interval_id, provider, host_clock_domain, scope_policy, "
      "process_id, thread_id, host_observation_status, host_start_ns, "
      "host_end_ns, bubble_us) VALUES('bubble-queryable', 7, 0, 'tree', "
      "'position-queryable', 'interval-queryable', 'ascend', 'host', "
      "'same_thread', 'p1', 't1', 'supported_ordered', 100000, 200000, "
      "50.0);");

  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_anchor_host_activity WHERE "
              "interval_id = 'interval-queryable'") == 0);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_anchor_host_api_summary WHERE "
              "interval_id = 'interval-queryable'") == 0);
  require(run_scalar_int(
              augmented_path,
              "SELECT COUNT(*) FROM traceloom_v_anchor_host_activity WHERE "
              "interval_id = 'interval-queryable'") == 2);
  require(run_scalar_text(
              augmented_path,
              "SELECT value FROM traceloom_metadata WHERE key = "
              "'anchor_host_activity_materialization_state'") ==
          "query_time_only");
  require(run_scalar_text(
              augmented_path,
              "SELECT sql FROM sqlite_master WHERE type = 'index' AND "
              "name = 'idx_traceloom_runtime_call_time'")
              .find("(db_idx, provider, clock_domain, start_ns, end_ns)") !=
          std::string::npos);

  const std::vector<HostWindowCall> calls =
      query_host_window_calls(augmented_path, "interval-queryable");
  require(calls.size() == 2);
  require(calls[0].runtime_call_id == "call-boundary");
  require(calls[0].interval_relation == "boundary_overlap");
  require(calls[0].observed_order == 0);
  require(std::abs(calls[0].overlap_us - 10.0) < 1e-9);
  require(calls[1].runtime_call_id == "call-contained");
  require(calls[1].interval_relation == "contained");
  require(calls[1].observed_order == 1);
  require(std::abs(calls[1].overlap_us - 30.0) < 1e-9);

  const std::vector<HostWindowCall> unsupported =
      query_host_window_calls(augmented_path, "interval-unsupported");
  require(unsupported.size() == 1);
  require(unsupported[0].runtime_call_id.empty());

  const std::vector<BubbleFamilyStat> bubble_stats =
      query_bubble_host_context(augmented_path, "position-queryable");
  require(bubble_stats.size() == 2);
  require(bubble_stats[0].api_family == "launch");
  require(bubble_stats[0].presence_count == 1);
  require(std::abs(bubble_stats[0].average_calls - 1.0) < 1e-9);
  require(std::abs(bubble_stats[0].average_overlap_us - 30.0) < 1e-9);
  require(bubble_stats[1].api_family == "synchronize");
  require(bubble_stats[1].presence_count == 1);
  require(std::abs(bubble_stats[1].average_calls - 1.0) < 1e-9);
  require(std::abs(bubble_stats[1].average_overlap_us - 10.0) < 1e-9);
}

}  // namespace traceloom::testing::sidecar_materializer
