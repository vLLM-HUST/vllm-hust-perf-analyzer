#include "augmented_catalog_materializer.h"

#include "augmented_projection_catalog.h"
#include "augmented_replay_body_projection_catalog.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "sidecar_sqlite_utils.h"

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
namespace traceloom::compat::detail {
namespace {

struct OperatorAuditRow {
  std::string operator_name;
  std::string task_type;
  std::uint64_t occurrence_count = 0;
  std::uint64_t total_duration_ns = 0;
  std::uint64_t graph_body_member_count = 0;
  std::uint64_t anchor_event_count = 0;
};

std::string symbol_text(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

SymbolId task_operator_symbol(const TaskRow& task) {
  if (task.op_type_symbol_id.valid()) {
    return task.op_type_symbol_id;
  }
  if (task.op_name_symbol_id.valid()) {
    return task.op_name_symbol_id;
  }
  return task.comm_name_symbol_id;
}

std::vector<OperatorAuditRow> build_operator_audit_rows(const NativeIr& ir) {
  std::unordered_set<TaskId::value_type> graph_body_tasks;
  for (const GraphLaunchBodyMemberRow& member :
       ir.graph_launch_body_members.rows()) {
    graph_body_tasks.insert(member.task_id.value());
  }
  std::unordered_set<TraceEventId::value_type> anchor_events;
  for (const AnchorRow& anchor : ir.anchors.rows()) {
    if (anchor.trace_event_id.valid()) {
      anchor_events.insert(anchor.trace_event_id.value());
    }
  }

  using Key = std::pair<std::string, std::string>;
  std::map<Key, OperatorAuditRow> aggregates;
  for (const TaskRow& task : ir.tasks.rows()) {
    const SymbolId operator_symbol = task_operator_symbol(task);
    if (!operator_symbol.valid() || !task.trace_event_id.valid() ||
        task.trace_event_id.value() >= ir.trace_events.size()) {
      continue;
    }
    const std::string operator_name = symbol_text(ir, operator_symbol);
    const std::string task_type = symbol_text(ir, task.task_type_symbol_id);
    OperatorAuditRow& row = aggregates[{operator_name, task_type}];
    row.operator_name = operator_name;
    row.task_type = task_type;
    ++row.occurrence_count;
    const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
    row.total_duration_ns += static_cast<std::uint64_t>(
        std::max<std::int64_t>(0, event.end_ns - event.start_ns));
    if (graph_body_tasks.find(task.id.value()) != graph_body_tasks.end()) {
      ++row.graph_body_member_count;
    }
    if (anchor_events.find(task.trace_event_id.value()) !=
        anchor_events.end()) {
      ++row.anchor_event_count;
    }
  }

  std::vector<OperatorAuditRow> rows;
  rows.reserve(aggregates.size());
  for (auto& item : aggregates) {
    rows.push_back(std::move(item.second));
  }
  std::stable_sort(rows.begin(), rows.end(),
                   [](const OperatorAuditRow& lhs,
                      const OperatorAuditRow& rhs) {
                     if (lhs.graph_body_member_count !=
                         rhs.graph_body_member_count) {
                       return lhs.graph_body_member_count >
                              rhs.graph_body_member_count;
                     }
                     if (lhs.total_duration_ns != rhs.total_duration_ns) {
                       return lhs.total_duration_ns > rhs.total_duration_ns;
                     }
                     if (lhs.occurrence_count != rhs.occurrence_count) {
                       return lhs.occurrence_count > rhs.occurrence_count;
                     }
                     return std::tie(lhs.operator_name, lhs.task_type) <
                            std::tie(rhs.operator_name, rhs.task_type);
                   });
  return rows;
}

std::string source_locator_union_sql(const RawTableCopy& table) {
  const std::string source_path = quote_literal(table.source_path);
  const std::string source_table = quote_literal(table.source_table);
  return
      "SELECT source_key FROM traceloom_event_source WHERE source_table = " +
      source_table + " AND json_extract(raw_json, '$.source_path') = " +
      source_path +
      " UNION ALL SELECT source_key FROM traceloom_runtime_call WHERE "
      "source_table = " +
      source_table + " AND json_extract(raw_json, '$.source_path') = " +
      source_path +
      " UNION ALL SELECT source_key FROM traceloom_device_work WHERE "
      "source_table = " +
      source_table + " AND json_extract(raw_json, '$.source_path') = " +
      source_path;
}

void validate_embedded_source_locators(
    sqlite3* db, const RawPackagingResult& packaging) {
  std::map<std::pair<std::string, std::string>, std::uint64_t>
      locator_counts;
  sqlite3_stmt* raw_stmt = nullptr;
  const char* inventory_sql =
      "SELECT source_path, source_table, COUNT(*) FROM ("
      "SELECT json_extract(raw_json, '$.source_path') AS source_path, "
      "source_table FROM traceloom_event_source UNION ALL "
      "SELECT json_extract(raw_json, '$.source_path'), source_table FROM "
      "traceloom_runtime_call UNION ALL "
      "SELECT json_extract(raw_json, '$.source_path'), source_table FROM "
      "traceloom_device_work) WHERE source_path IS NOT NULL GROUP BY "
      "source_path, source_table";
  if (sqlite3_prepare_v2(db, inventory_sql, -1, &raw_stmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error("failed to inventory source locator domains: " +
                             std::string(sqlite3_errmsg(db)));
  }
  while (true) {
    const int rc = sqlite3_step(raw_stmt);
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      const std::string message = sqlite3_errmsg(db);
      sqlite3_finalize(raw_stmt);
      throw std::runtime_error("failed to inventory source locator domains: " +
                               message);
    }
    const unsigned char* path_text = sqlite3_column_text(raw_stmt, 0);
    const unsigned char* table_text = sqlite3_column_text(raw_stmt, 1);
    if (path_text == nullptr || table_text == nullptr) {
      sqlite3_finalize(raw_stmt);
      throw std::runtime_error(
          "source locator domain has a NULL path or table");
    }
    locator_counts.emplace(
        std::make_pair(reinterpret_cast<const char*>(path_text),
                       reinterpret_cast<const char*>(table_text)),
        static_cast<std::uint64_t>(sqlite3_column_int64(raw_stmt, 2)));
  }
  sqlite3_finalize(raw_stmt);

  for (const RawTableCopy& table : packaging.tables) {
    const auto count_found =
        locator_counts.find({table.source_path, table.source_table});
    if (count_found == locator_counts.end()) {
      continue;
    }
    const std::uint64_t locator_count = count_found->second;
    const std::string locators = source_locator_union_sql(table);
    if (table.source_rowid_column.empty()) {
      throw std::runtime_error(
          "source-linked analysis rows reference embedded table without a "
          "stable rowid: " +
          table.source_path + ":" + table.source_table);
    }

    const std::string embedded = quote_identifier(table.embedded_table_name);
    const std::string rowid = quote_identifier(table.source_rowid_column);
    const std::uint64_t unresolved_count = sqlite_scalar_u64(
        db,
        "WITH locator(source_key) AS (" + locators + ") "
        "SELECT COUNT(*) FROM locator l LEFT JOIN " +
            embedded + " raw ON raw." + rowid +
            " = CAST(l.source_key AS INTEGER) WHERE "
            "l.source_key <> printf('%lld', CAST(l.source_key AS INTEGER)) "
            "OR raw." +
            rowid + " IS NULL",
        "failed to validate source locator rows");
    if (unresolved_count != 0) {
      throw std::runtime_error(
          "source-linked analysis rows do not resolve to an embedded raw "
          "row: " +
          table.source_path + ":" + table.source_table + " (" +
          std::to_string(unresolved_count) + " unresolved of " +
          std::to_string(locator_count) + ")");
    }
  }
}


}  // namespace

void materialize_augmented_catalog(const std::string& path,
                                   const RawPackagingResult& packaging,
                                   const NativeIr& ir) {
  sqlite3* db = open_sqlite_readwrite(path);
  try {
    sqlite_exec(db, "BEGIN IMMEDIATE", "failed to begin catalog transaction");
    sqlite_exec(
        db,
        "CREATE TABLE traceloom_raw_source_database("
        "source_id TEXT NOT NULL PRIMARY KEY, source_ordinal INTEGER NOT NULL, "
        "source_path TEXT NOT NULL, embedded_mode TEXT NOT NULL, "
        "size_bytes INTEGER NOT NULL, sha256 TEXT NOT NULL)",
        "failed to create raw source catalog");
    sqlite_exec(
        db,
        "CREATE TABLE traceloom_raw_table("
        "source_id TEXT NOT NULL, source_path TEXT NOT NULL, "
        "source_table TEXT NOT NULL, embedded_table_name TEXT NOT NULL, "
        "source_rowid_column TEXT, row_count INTEGER NOT NULL, "
        "PRIMARY KEY(source_id, source_table))",
        "failed to create raw table catalog");
    for (const RawSourceDatabase& source : packaging.sources) {
      sqlite_exec(
          db,
          "INSERT INTO traceloom_raw_source_database VALUES(" +
              quote_literal(source.source_id) + "," +
              std::to_string(source.source_ordinal) + "," +
              quote_literal(source.source_path) + "," +
              quote_literal(source.embedded_mode) + "," +
              std::to_string(source.size_bytes) + "," +
              quote_literal(source.sha256) + ")",
          "failed to insert raw source catalog row");
    }
    for (const RawTableCopy& table : packaging.tables) {
      sqlite_exec(
          db,
          "INSERT INTO traceloom_raw_table VALUES(" +
              quote_literal(table.source_id) + "," +
              quote_literal(table.source_path) + "," +
              quote_literal(table.source_table) + "," +
              quote_literal(table.embedded_table_name) + "," +
              (table.source_rowid_column.empty()
                   ? std::string("NULL")
                   : quote_literal(table.source_rowid_column)) +
              "," + std::to_string(table.row_count) + ")",
          "failed to insert raw table catalog row");
    }
    sqlite_exec(
        db,
        "CREATE INDEX traceloom_raw_table_source_locator_idx ON "
        "traceloom_raw_table(source_path, source_table)",
        "failed to index raw table catalog");
    // `embedded_raw` is a row-level promise, not merely evidence that the
    // named provider table was copied. Validate every literal event/runtime/
    // device-work key before publishing locator views so a stale source key
    // cannot masquerade as auditable provenance.
    validate_embedded_source_locators(db, packaging);
    sqlite_exec(
        db,
        "CREATE VIEW traceloom_v_event_source_locator AS SELECT "
        "es.event_id, es.source_ordinal, es.db_idx, es.device_id, "
        "json_extract(es.raw_json, '$.source_ref_id') AS source_ref_id, "
        "json_extract(es.raw_json, '$.source_path') AS source_path, "
        "es.source_table, es.source_key, es.source_role, "
        "rt.source_id, rt.embedded_table_name, rt.source_rowid_column, "
        "CASE WHEN rt.embedded_table_name IS NOT NULL THEN 'embedded_raw' "
        "WHEN es.source_table IN ('CUDA_GRAPH_REPLAY_UNIT', "
        "'ACLGRAPH_REPLAY_UNIT', 'SYNTHETIC') THEN 'analysis_synthetic' "
        "ELSE 'unresolved' END AS resolution_status "
        "FROM traceloom_event_source es LEFT JOIN traceloom_raw_table rt ON "
        "rt.source_path = json_extract(es.raw_json, '$.source_path') AND "
        "rt.source_table = es.source_table",
        "failed to create event source locator view");
    sqlite_exec(
        db,
        "CREATE VIEW traceloom_v_runtime_call_source_locator AS SELECT "
        "c.*, rt.source_id, rt.embedded_table_name, rt.source_rowid_column, "
        "CASE WHEN rt.embedded_table_name IS NOT NULL THEN 'embedded_raw' "
        "ELSE 'unresolved' END AS resolution_status "
        "FROM traceloom_runtime_call c LEFT JOIN traceloom_raw_table rt ON "
        "rt.source_path = json_extract(c.raw_json, '$.source_path') AND "
        "rt.source_table = c.source_table",
        "failed to create runtime call source locator view");
    sqlite_exec(
        db,
        "CREATE VIEW traceloom_v_device_work_source_locator AS SELECT "
        "w.*, rt.source_id, rt.embedded_table_name, rt.source_rowid_column, "
        "CASE WHEN rt.embedded_table_name IS NOT NULL THEN 'embedded_raw' "
        "ELSE 'unresolved' END AS resolution_status "
        "FROM traceloom_device_work w LEFT JOIN traceloom_raw_table rt ON "
        "rt.source_path = json_extract(w.raw_json, '$.source_path') AND "
        "rt.source_table = w.source_table",
        "failed to create device work source locator view");
    sqlite_exec(
        db,
        "CREATE VIEW traceloom_v_exact_replay_partition_status AS "
        "WITH tree_anchor AS ("
        "SELECT DISTINCT t.tree_id,t.db_idx,t.device_id,na.anchor_id,"
        "a.anchor_idx FROM traceloom_semantic_tree t JOIN "
        "traceloom_tree_node_anchor na ON na.node_id=t.root_node_id AND "
        "na.db_idx=t.db_idx AND na.device_id=t.device_id AND "
        "na.coverage_kind='body' JOIN traceloom_anchor a ON "
        "a.anchor_id=na.anchor_id AND a.db_idx=na.db_idx AND "
        "a.device_id=na.device_id WHERE t.tree_kind='semantic'),"
        "interval_base AS ("
        "SELECT t.tree_id,t.db_idx,t.device_id,p.protected_interval_id,"
        "p.replay_unit_id,p.support_state,fa.anchor_idx AS first_anchor_idx,"
        "la.anchor_idx AS last_anchor_idx FROM traceloom_semantic_tree t "
        "LEFT JOIN traceloom_protected_interval p ON p.db_idx=t.db_idx AND "
        "p.device_id=t.device_id AND p.kind='graph_replay_unit' "
        "LEFT JOIN tree_anchor fa ON fa.tree_id=t.tree_id AND "
        "fa.db_idx=t.db_idx AND fa.device_id=t.device_id AND "
        "fa.anchor_id=p.first_anchor_id LEFT JOIN tree_anchor la ON "
        "la.tree_id=t.tree_id AND la.db_idx=t.db_idx AND "
        "la.device_id=t.device_id AND la.anchor_id=p.last_anchor_id "
        "WHERE t.tree_kind='semantic'), ordered AS ("
        "SELECT *,LEAD(first_anchor_idx) OVER (PARTITION BY tree_id,db_idx,"
        "device_id ORDER BY first_anchor_idx,protected_interval_id) AS "
        "next_first_anchor_idx "
        "FROM interval_base), summary AS ("
        "SELECT tree_id,db_idx,device_id,COUNT(protected_interval_id) AS "
        "replay_interval_count,SUM(CASE WHEN protected_interval_id IS NOT "
        "NULL AND support_state='supported' THEN 1 ELSE 0 END) AS "
        "exact_replay_count,SUM(CASE WHEN protected_interval_id IS NOT NULL "
        "AND support_state<>'supported' THEN 1 ELSE 0 END) AS "
        "unsupported_interval_count,SUM(CASE WHEN protected_interval_id IS "
        "NOT NULL AND (first_anchor_idx IS NULL OR last_anchor_idx IS NULL "
        "OR first_anchor_idx>last_anchor_idx) THEN 1 ELSE 0 END) AS "
        "invalid_bound_count,SUM(CASE WHEN next_first_anchor_idx IS NOT NULL "
        "AND last_anchor_idx>=next_first_anchor_idx THEN 1 ELSE 0 END) AS "
        "overlap_count FROM ordered GROUP BY tree_id,db_idx,device_id) "
        "SELECT tree_id,db_idx,device_id,replay_interval_count,"
        "exact_replay_count,"
        "unsupported_interval_count,invalid_bound_count,overlap_count,"
        "CASE WHEN replay_interval_count=0 THEN 'unsupported' "
        "WHEN unsupported_interval_count>0 THEN 'unsupported' "
        "WHEN invalid_bound_count>0 THEN 'unsupported' "
        "WHEN overlap_count>0 THEN 'unsupported' ELSE 'supported' END AS "
        "support_state,CASE WHEN replay_interval_count=0 THEN "
        "'no_exact_replays' "
        "WHEN unsupported_interval_count>0 THEN 'non_exact_protected_interval' "
        "WHEN invalid_bound_count>0 THEN 'invalid_anchor_bounds' "
        "WHEN overlap_count>0 THEN 'overlapping_replay_intervals' ELSE "
        "'ordered_disjoint_exact_replays' END AS reason_code FROM summary",
        "failed to create exact replay partition status view");
    sqlite_exec(
        db,
        "CREATE VIEW traceloom_v_exact_replay_partition AS "
        "WITH replay AS ("
        "SELECT s.tree_id,s.db_idx,s.device_id,"
        "ROW_NUMBER() OVER (PARTITION BY s.tree_id,s.db_idx,s.device_id "
        "ORDER BY fa.anchor_idx,p.protected_interval_id)-1 AS replay_index,"
        "COUNT(*) OVER (PARTITION BY s.tree_id,s.db_idx,s.device_id) AS "
        "replay_count,"
        "p.protected_interval_id,p.replay_unit_id,"
        "fa.anchor_idx AS first_anchor_idx,la.anchor_idx AS last_anchor_idx,"
        "LEAD(p.protected_interval_id) OVER (PARTITION BY s.tree_id,s.db_idx,"
        "s.device_id ORDER BY fa.anchor_idx,p.protected_interval_id) AS "
        "next_protected_interval_id,LEAD(p.replay_unit_id) OVER (PARTITION BY "
        "s.tree_id,s.db_idx,s.device_id ORDER BY fa.anchor_idx,"
        "p.protected_interval_id) AS next_replay_unit_id,"
        "LEAD(fa.anchor_idx) OVER (PARTITION BY s.tree_id,s.db_idx,s.device_id "
        "ORDER BY fa.anchor_idx,p.protected_interval_id) AS "
        "next_first_anchor_idx "
        "FROM traceloom_v_exact_replay_partition_status s "
        "JOIN traceloom_protected_interval p ON p.db_idx=s.db_idx AND "
        "p.device_id=s.device_id AND p.kind='graph_replay_unit' AND "
        "p.support_state='supported' "
        "JOIN traceloom_anchor fa ON fa.anchor_id=p.first_anchor_id AND "
        "fa.db_idx=p.db_idx AND fa.device_id=p.device_id "
        "JOIN traceloom_anchor la ON la.anchor_id=p.last_anchor_id AND "
        "la.db_idx=p.db_idx AND la.device_id=p.device_id "
        "WHERE s.support_state='supported'), root_anchor AS ("
        "SELECT t.tree_id,t.db_idx,t.device_id,a.anchor_idx,na.compute_us,"
        "na.comm_us,na.idle_us,na.total_us,na.aux_us "
        "FROM traceloom_semantic_tree t JOIN traceloom_tree_node_anchor na "
        "ON na.node_id=t.root_node_id AND na.db_idx=t.db_idx AND "
        "na.device_id=t.device_id AND na.coverage_kind='body' "
        "JOIN traceloom_anchor a ON a.anchor_id=na.anchor_id AND "
        "a.db_idx=na.db_idx AND a.device_id=na.device_id WHERE "
        "t.tree_kind='semantic'), segments AS ("
        "SELECT r.tree_id,r.db_idx,r.device_id,0 AS segment_order,"
        "'open_boundary' AS coordinate_kind,0 AS coordinate_index,"
        "'X1' AS segment_label,NULL AS left_protected_interval_id,"
        "r.protected_interval_id AS right_protected_interval_id,"
        "NULL AS left_replay_unit_id,r.replay_unit_id AS right_replay_unit_id,"
        "MIN(a.anchor_idx) AS first_anchor_idx,MAX(a.anchor_idx) AS "
        "last_anchor_idx,COUNT(a.anchor_idx) AS anchor_count,"
        "COALESCE(SUM(a.compute_us),0.0) AS compute_us,"
        "COALESCE(SUM(a.comm_us),0.0) AS comm_us,"
        "COALESCE(SUM(a.idle_us),0.0) AS idle_us,"
        "COALESCE(SUM(a.total_us),0.0) AS total_us,"
        "COALESCE(SUM(a.aux_us),0.0) AS aux_us FROM replay r "
        "LEFT JOIN root_anchor a ON a.tree_id=r.tree_id AND "
        "a.db_idx=r.db_idx AND a.device_id=r.device_id AND "
        "a.anchor_idx<r.first_anchor_idx WHERE r.replay_index=0 "
        "GROUP BY r.tree_id,r.db_idx,r.device_id,r.protected_interval_id,"
        "r.replay_unit_id UNION ALL "
        "SELECT r.tree_id,r.db_idx,r.device_id,2*r.replay_index+1,'replay',"
        "r.replay_index,'R'||(r.replay_index+1),r.protected_interval_id,"
        "r.protected_interval_id,r.replay_unit_id,r.replay_unit_id,"
        "MIN(a.anchor_idx),MAX(a.anchor_idx),COUNT(a.anchor_idx),"
        "COALESCE(SUM(a.compute_us),0.0),COALESCE(SUM(a.comm_us),0.0),"
        "COALESCE(SUM(a.idle_us),0.0),COALESCE(SUM(a.total_us),0.0),"
        "COALESCE(SUM(a.aux_us),0.0) FROM replay r LEFT JOIN root_anchor a "
        "ON a.tree_id=r.tree_id AND a.db_idx=r.db_idx AND "
        "a.device_id=r.device_id AND a.anchor_idx BETWEEN "
        "r.first_anchor_idx AND r.last_anchor_idx GROUP BY r.tree_id,"
        "r.db_idx,r.device_id,r.replay_index,r.protected_interval_id,"
        "r.replay_unit_id UNION ALL "
        "SELECT r.tree_id,r.db_idx,r.device_id,2*r.replay_index+2,"
        "'between_replays',r.replay_index,'U'||(r.replay_index+1),"
        "r.protected_interval_id,r.next_protected_interval_id,"
        "r.replay_unit_id,r.next_replay_unit_id,MIN(a.anchor_idx),"
        "MAX(a.anchor_idx),COUNT(a.anchor_idx),"
        "COALESCE(SUM(a.compute_us),0.0),COALESCE(SUM(a.comm_us),0.0),"
        "COALESCE(SUM(a.idle_us),0.0),COALESCE(SUM(a.total_us),0.0),"
        "COALESCE(SUM(a.aux_us),0.0) FROM replay r LEFT JOIN root_anchor a "
        "ON a.tree_id=r.tree_id AND a.db_idx=r.db_idx AND "
        "a.device_id=r.device_id AND a.anchor_idx>r.last_anchor_idx AND "
        "a.anchor_idx<r.next_first_anchor_idx WHERE "
        "r.next_first_anchor_idx IS NOT NULL GROUP BY r.tree_id,r.db_idx,"
        "r.device_id,r.replay_index,r.protected_interval_id,"
        "r.next_protected_interval_id,r.replay_unit_id,r.next_replay_unit_id "
        "UNION ALL SELECT r.tree_id,r.db_idx,r.device_id,2*r.replay_count,"
        "'open_boundary',1,'X2',r.protected_interval_id,NULL,"
        "r.replay_unit_id,NULL,MIN(a.anchor_idx),MAX(a.anchor_idx),"
        "COUNT(a.anchor_idx),COALESCE(SUM(a.compute_us),0.0),"
        "COALESCE(SUM(a.comm_us),0.0),COALESCE(SUM(a.idle_us),0.0),"
        "COALESCE(SUM(a.total_us),0.0),COALESCE(SUM(a.aux_us),0.0) "
        "FROM replay r LEFT JOIN root_anchor a ON a.tree_id=r.tree_id AND "
        "a.db_idx=r.db_idx AND a.device_id=r.device_id AND "
        "a.anchor_idx>r.last_anchor_idx WHERE "
        "r.replay_index=r.replay_count-1 GROUP BY r.tree_id,r.db_idx,"
        "r.device_id,r.replay_count,r.protected_interval_id,r.replay_unit_id) "
        "SELECT printf('db%06d:d%06d:%s:replay-partition:%06d',db_idx,"
        "device_id,tree_id,segment_order) AS partition_id,tree_id,db_idx,"
        "device_id,segment_order,"
        "coordinate_kind,coordinate_index,segment_label,'supported' AS "
        "support_state,left_protected_interval_id,right_protected_interval_id,"
        "left_replay_unit_id,right_replay_unit_id,first_anchor_idx,"
        "last_anchor_idx,anchor_count,compute_us,comm_us,idle_us,total_us,"
        "aux_us FROM segments",
        "failed to create exact replay partition view");
    sqlite_exec(
        db,
        "CREATE TABLE traceloom_analysis_surface("
        "surface_name TEXT NOT NULL PRIMARY KEY, relation_name TEXT NOT NULL, "
        "row_grain TEXT NOT NULL, purpose TEXT NOT NULL, "
        "example_sql TEXT NOT NULL)",
        "failed to create analysis surface catalog");
    sqlite_exec(
        db,
        "CREATE TABLE traceloom_projection_recipe("
        "projection_name TEXT NOT NULL PRIMARY KEY, "
        "display_order INTEGER NOT NULL, scope_kind TEXT NOT NULL, "
        "population_mode TEXT NOT NULL, resolution TEXT NOT NULL, "
        "observation_domain TEXT NOT NULL, measure_lens TEXT NOT NULL, "
        "selector_parameters TEXT NOT NULL, purpose TEXT NOT NULL, "
        "example_sql TEXT NOT NULL)",
        "failed to create projection recipe catalog");
    sqlite_exec(
        db,
        "CREATE TABLE traceloom_projection_parameter("
        "projection_name TEXT NOT NULL, parameter_order INTEGER NOT NULL, "
        "parameter_name TEXT NOT NULL, sqlite_type TEXT NOT NULL, "
        "is_nullable INTEGER NOT NULL CHECK(is_nullable IN (0, 1)), "
        "coordinate_kind TEXT NOT NULL, "
        "selection_relation TEXT NOT NULL, selection_column TEXT NOT NULL, "
        "purpose TEXT NOT NULL, PRIMARY KEY(projection_name, parameter_name), "
        "FOREIGN KEY(projection_name) REFERENCES "
        "traceloom_projection_recipe(projection_name))",
        "failed to create projection parameter catalog");
    sqlite_exec(
        db,
        "CREATE TABLE traceloom_projection_coordinate("
        "projection_name TEXT NOT NULL, coordinate_order INTEGER NOT NULL, "
        "result_column TEXT NOT NULL, coordinate_kind TEXT NOT NULL, "
        "purpose TEXT NOT NULL, PRIMARY KEY(projection_name, result_column), "
        "FOREIGN KEY(projection_name) REFERENCES "
        "traceloom_projection_recipe(projection_name))",
        "failed to create projection coordinate catalog");
    sqlite_exec(
        db,
        "CREATE VIEW traceloom_v_projection_continuation AS "
        "WITH readiness AS ("
        "SELECT source.projection_name AS source_projection, "
        "target.projection_name AS target_projection, "
        "(SELECT COUNT(*) FROM traceloom_projection_parameter required "
        "WHERE required.projection_name = target.projection_name AND "
        "required.is_nullable = 0) AS required_coordinate_count, "
        "(SELECT COUNT(*) FROM traceloom_projection_parameter required "
        "WHERE required.projection_name = target.projection_name AND "
        "required.is_nullable = 0 AND EXISTS (SELECT 1 FROM "
        "traceloom_projection_coordinate available WHERE "
        "available.projection_name = source.projection_name AND "
        "available.coordinate_kind = required.coordinate_kind)) AS "
        "matched_required_coordinate_count "
        "FROM traceloom_projection_recipe source CROSS JOIN "
        "traceloom_projection_recipe target WHERE source.projection_name != "
        "target.projection_name) "
        "SELECT ready.source_projection, ready.target_projection, "
        "output.result_column AS source_column, "
        "parameter.parameter_name AS target_parameter, "
        "output.coordinate_kind, parameter.is_nullable AS "
        "target_parameter_nullable, ready.required_coordinate_count "
        "FROM readiness ready JOIN traceloom_projection_coordinate output "
        "ON output.projection_name = ready.source_projection JOIN "
        "traceloom_projection_parameter parameter ON "
        "parameter.projection_name = ready.target_projection AND "
        "parameter.coordinate_kind = output.coordinate_kind "
        "WHERE ready.required_coordinate_count = "
        "ready.matched_required_coordinate_count",
        "failed to create projection continuation view");
    sqlite_exec(
        db,
        "CREATE TABLE traceloom_operator_audit("
        "operator_name TEXT NOT NULL, task_type TEXT NOT NULL, "
        "occurrence_count INTEGER NOT NULL, total_duration_ns INTEGER NOT NULL, "
        "graph_body_member_count INTEGER NOT NULL, "
        "anchor_event_count INTEGER NOT NULL, "
        "PRIMARY KEY(operator_name, task_type))",
        "failed to create operator audit table");
    const std::vector<OperatorAuditRow> operator_rows =
        build_operator_audit_rows(ir);
    std::uint64_t operator_occurrences = 0;
    for (const OperatorAuditRow& row : operator_rows) {
      operator_occurrences += row.occurrence_count;
      sqlite_exec(
          db,
          "INSERT INTO traceloom_operator_audit VALUES(" +
              quote_literal(row.operator_name) + "," +
              quote_literal(row.task_type) + "," +
              std::to_string(row.occurrence_count) + "," +
              std::to_string(row.total_duration_ns) + "," +
              std::to_string(row.graph_body_member_count) + "," +
              std::to_string(row.anchor_event_count) + ")",
          "failed to insert operator audit row");
    }
    sqlite_exec(
        db,
        "INSERT INTO traceloom_metadata(key, value) VALUES"
        "('operator_audit_status', 'observed_inventory'),"
        "('operator_identity_count', " +
            quote_literal(std::to_string(operator_rows.size())) +
            "),('operator_occurrence_count', " +
            quote_literal(std::to_string(operator_occurrences)) + ")",
        "failed to add operator audit metadata");
    materialize_projection_catalog(db, packaging);
    materialize_replay_body_projection_catalog(db);
    sqlite_exec(db, "COMMIT", "failed to commit catalog transaction");
    sqlite3_close(db);
  } catch (...) {
    try {
      sqlite_exec(db, "ROLLBACK", "failed to roll back catalog transaction");
    } catch (...) {
    }
    sqlite3_close(db);
    throw;
  }
}

}  // namespace traceloom::compat::detail
#endif
