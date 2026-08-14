#include "augmented_catalog_materializer.h"

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
    const std::vector<std::vector<std::string>> surfaces = {
        {"composable_projection", "traceloom_projection_recipe",
         "one reusable analytical projection recipe",
         "select a scope and compose population, resolution, observation "
         "domain, and measure lens",
         "SELECT projection_name, scope_kind, population_mode, resolution, "
         "observation_domain, measure_lens, selector_parameters, purpose "
         "FROM traceloom_projection_recipe ORDER BY display_order;"},
        {"projection_parameter", "traceloom_projection_parameter",
         "one named parameter accepted by one projection recipe",
         "discover typed selectors and the public relation that supplies "
         "candidate coordinates",
         "SELECT * FROM traceloom_projection_parameter ORDER BY "
         "projection_name, parameter_order;"},
        {"projection_coordinate", "traceloom_projection_coordinate",
         "one reusable coordinate returned by one projection recipe",
         "discover which result columns remain valid inputs to later "
         "projections",
         "SELECT * FROM traceloom_projection_coordinate ORDER BY "
         "projection_name, coordinate_order;"},
        {"projection_continuation", "traceloom_v_projection_continuation",
         "one ready coordinate transfer between projection recipes",
         "discover compatible next queries without parsing example SQL or "
         "reconstructing scope identity",
         "SELECT * FROM traceloom_v_projection_continuation ORDER BY "
         "source_projection, target_projection, source_column;"},
        {"tree_map", "traceloom_v_tree_node", "one structural node",
         "read the hierarchical cost map from coarse loops to leaves",
         "SELECT * FROM traceloom_v_tree_node ORDER BY db_idx, device_id, "
         "view_name, display_order;"},
        {"tree_occurrence", "traceloom_tree_node_occurrence",
         "one structural node occurrence",
         "compare repeated instances without losing hierarchy",
         "SELECT * FROM traceloom_tree_node_occurrence ORDER BY db_idx, "
         "device_id, view_name, node_id, occurrence_idx;"},
        {"node_cost", "traceloom_v_node_cost", "one structural node cost",
         "rank and compare overlap-safe cost lenses by structural node",
         "SELECT * FROM traceloom_v_node_cost ORDER BY total_us DESC, "
         "db_idx, device_id, view_name, node_id;"},
        {"normalized_event", "traceloom_event", "one normalized event",
         "inspect fine-grained timing and operator evidence",
         "SELECT * FROM traceloom_event ORDER BY db_idx, device_id, step_idx;"},
        {"event_reconciliation_policy",
         "traceloom_event_reconciliation_policy",
         "one effective event-reconciliation policy",
         "audit the manifest identity, digest, and unmatched behavior used "
         "for this analysis",
         "SELECT * FROM traceloom_event_reconciliation_policy ORDER BY "
         "policy_id;"},
        {"event_reconciliation_rule",
         "traceloom_event_reconciliation_rule",
         "one effective event-reconciliation rule",
         "inspect the exact provider predicate, containment threshold, and "
         "rule origin admitted by the effective policy",
         "SELECT * FROM traceloom_event_reconciliation_rule ORDER BY "
         "priority DESC, rule_id;"},
        {"event_reconciliation_decision",
         "traceloom_event_reconciliation_decision",
         "one candidate event-reconciliation group",
         "find reconciled, independent, ambiguous, and conflicting candidate "
         "groups before drilling into member contributions",
         "SELECT status, reason_code, COUNT(*) AS decision_count FROM "
         "traceloom_event_reconciliation_decision GROUP BY status, "
         "reason_code ORDER BY status, reason_code;"},
        {"event_reconciliation_member",
         "traceloom_event_reconciliation_member",
         "one normalized event in one reconciliation decision",
         "audit which original observation contributes timing, structural "
         "symbol, or cost and retain its raw-source locator",
         "SELECT * FROM traceloom_event_reconciliation_member ORDER BY "
         "db_idx, decision_id, member_order LIMIT 200;"},
        {"event_reconciliation", "traceloom_v_event_reconciliation",
         "one candidate observation in one sparse reconciliation decision",
         "audit when multiple profiler rows contribute timing, symbol, and "
         "cost to one canonical anchor without deleting raw evidence",
         "SELECT * FROM traceloom_v_event_reconciliation ORDER BY db_idx, "
         "decision_id, member_order;"},
        {"evidence_role_policy", "traceloom_evidence_role_policy",
         "one effective projection policy",
         "audit the flat input table identity and explicit config precedence",
         "SELECT * FROM traceloom_evidence_role_policy ORDER BY policy_id;"},
        {"evidence_role_rule", "traceloom_evidence_role_rule",
         "one effective policy or system rule",
         "explain stable rule identifiers, predicates, capabilities, and "
         "retention treatments",
         "SELECT * FROM traceloom_evidence_role_rule ORDER BY policy_id, "
         "priority DESC, declaration_order, rule_id;"},
        {"evidence_role_decision", "traceloom_v_evidence_role_decision",
         "one normalized event projection decision",
         "walk from an event or raw-source locator to its typed role outcome",
         "SELECT * FROM traceloom_v_evidence_role_decision WHERE event_id = "
         "'event-0';"},
        {"evidence_role_placement", "traceloom_v_evidence_role_placement",
         "one role-decision structural placement",
         "walk in either direction between a role decision and retained "
         "anchor, auxiliary, graph, replay, or boundary membership",
         "SELECT * FROM traceloom_v_evidence_role_placement WHERE "
         "placement_id = 'anchor-0' OR owner_id = 'anchor-0' ORDER BY "
         "decision_id, placement_order;"},
        {"evidence_role_structure", "traceloom_v_evidence_role_structure",
         "one role-decision placement in recovered structure",
         "locate anchor, omitted, and protected evidence in tree occurrences",
         "SELECT * FROM traceloom_v_evidence_role_structure WHERE event_id = "
         "'event-0' ORDER BY node_id, occurrence_idx, placement_kind;"},
        {"evidence_role_cost_coverage",
         "traceloom_v_evidence_role_cost_coverage",
         "one provider, policy, role, and support-state cost aggregate",
         "compare retained cost inside and outside identity matching",
         "SELECT * FROM traceloom_v_evidence_role_cost_coverage ORDER BY "
         "db_idx, input_provider_scope, final_role, support_state;"},
        {"evidence_role_issue", "traceloom_evidence_role_issue",
         "one typed projection audit issue",
         "find conflicts, missing placement, and unsupported outcomes",
         "SELECT * FROM traceloom_evidence_role_issue ORDER BY code, "
         "decision_id, issue_id;"},
        {"protected_interval", "traceloom_protected_interval",
         "one typed generic-discovery boundary",
         "audit exact and typed-open protected composites without inferring "
         "membership from timestamps",
         "SELECT * FROM traceloom_protected_interval ORDER BY db_idx, "
         "device_id, start_ns, protected_interval_id;"},
        {"exact_replay_partition_status",
         "traceloom_v_exact_replay_partition_status",
         "one device-tree replay-partition support result",
         "check whether exact replay intervals form one ordered disjoint "
         "partition domain before reading its cost segments",
         "SELECT * FROM traceloom_v_exact_replay_partition_status ORDER BY "
         "db_idx, device_id, tree_id;"},
        {"exact_replay_partition", "traceloom_v_exact_replay_partition",
         "one open, replay, or between-replays device segment",
         "compare a complete right-anchored cost partition induced by exact "
         "replay boundaries without reconstructing intervals in client SQL",
         "SELECT tree_id,db_idx,device_id,segment_order,coordinate_kind,"
         "coordinate_index,segment_label,anchor_count,compute_us,comm_us,"
         "idle_us,total_us,aux_us FROM "
         "traceloom_v_exact_replay_partition ORDER BY db_idx,device_id,"
         "tree_id,segment_order;"},
        {"symbol_normalization_rule",
         "traceloom_symbol_normalization_rule",
         "one versioned structural-symbol rule",
         "audit explicit provider aliases and typed fallback behavior",
         "SELECT * FROM traceloom_symbol_normalization_rule ORDER BY "
         "policy_id, policy_version, precedence, rule_id;"},
        {"anchor_symbol_lineage", "traceloom_v_anchor_symbol_lineage",
         "one structural anchor symbol decision",
         "explain an anchor's observed provider symbol, structural symbol, "
         "rule, and source locator",
         "SELECT * FROM traceloom_v_anchor_symbol_lineage ORDER BY db_idx, "
         "device_id, anchor_idx;"},
        {"symbol_normalization_placement",
         "traceloom_v_symbol_normalization_placement",
         "one symbol decision in one recovered structural placement",
         "walk between backend identity and family occurrence position",
         "SELECT * FROM traceloom_v_symbol_normalization_placement WHERE "
         "coverage_kind = 'self' ORDER BY db_idx, device_id, node_id, "
         "occurrence_idx, anchor_order;"},
        {"symbol_variant_cost", "traceloom_v_symbol_variant_cost",
         "one structural position and observed backend symbol",
         "compare counts and cost distributions across backend variants",
         "SELECT * FROM traceloom_v_symbol_variant_cost ORDER BY total_us "
         "DESC, db_idx, device_id, node_id, anchor_order, "
         "observed_symbol;"},
        {"runtime_device_relation", "traceloom_v_runtime_device",
         "one runtime-call/device-work relation outcome",
         "inspect direct provider correlation, explicit cardinality, and open "
         "or ambiguous outcomes",
         "SELECT * FROM traceloom_v_runtime_device ORDER BY db_idx, provider, "
         "runtime_start_ns, device_start_ns, relation_id LIMIT 200;"},
        {"synchronization_action", "traceloom_v_sync_runtime_call",
         "one profiler-visible synchronization action/runtime relation",
         "inspect typed synchronization observations and their exact, "
         "deterministic, ambiguous, or rejected runtime endpoints",
         "SELECT * FROM traceloom_v_sync_runtime_call ORDER BY db_idx, "
         "device_start_ns, sync_action_id, runtime_start_ns LIMIT 200;"},
        {"anchor_runtime_call", "traceloom_v_anchor_runtime_call",
         "one structural anchor/runtime-call relation",
         "walk backward from anchor or graph launch to observed host runtime",
         "SELECT * FROM traceloom_v_anchor_runtime_call ORDER BY db_idx, "
         "device_id, anchor_idx, runtime_start_ns, relation_id LIMIT 200;"},
        {"node_runtime_call", "traceloom_v_node_runtime_call",
         "one tree-node occurrence/anchor/runtime-call placement",
         "query host/device relations inside recovered structure in either "
         "direction",
         "SELECT * FROM traceloom_v_node_runtime_call WHERE coverage_kind = "
         "'self' ORDER BY db_idx, device_id, node_id, occurrence_idx, "
         "anchor_order, runtime_start_ns LIMIT 200;"},
        {"aux_runtime_call", "traceloom_v_aux_runtime_call",
         "one auxiliary-device-event/runtime-call relation",
         "walk backward from device auxiliary work to observed host runtime",
         "SELECT * FROM traceloom_v_aux_runtime_call ORDER BY db_idx, "
         "device_id, anchor_id, aux_order, runtime_start_ns LIMIT 200;"},
        {"anchor_host_interval", "traceloom_v_anchor_host_interval",
         "one adjacent-anchor pair with host runtime endpoints",
         "inspect whether adjacent device anchors delimit a queryable host "
         "runtime interval",
         "SELECT * FROM traceloom_v_anchor_host_interval ORDER BY db_idx, "
         "device_id, left_anchor_id LIMIT 200;"},
        {"node_host_interval", "traceloom_v_node_host_interval",
         "one node-occurrence/anchor position and typed host interval",
         "retain every structural coordinate while inspecting supported or "
         "unsupported adjacent-anchor host endpoints",
         "SELECT * FROM traceloom_v_node_host_interval WHERE coverage_kind = "
         "'self' ORDER BY db_idx, device_id, node_id, occurrence_idx, "
         "anchor_order LIMIT 200;"},
        {"anchor_host_activity", "traceloom_v_anchor_host_activity",
         "one observed runtime call overlapping an anchor-delimited host "
         "interval",
         "inspect profiler-visible host runtime behavior between device "
         "structure endpoints without assigning an idle cause",
         "SELECT * FROM traceloom_v_anchor_host_activity WHERE left_anchor_id "
         "= (SELECT left_anchor_id FROM traceloom_anchor_host_interval WHERE "
         "support_state = 'supported_ordered' ORDER BY db_idx, device_id, "
         "host_start_ns LIMIT 1) ORDER BY observed_start_ns, "
         "observed_runtime_call_id LIMIT 200;"},
        {"node_host_activity", "traceloom_v_node_host_activity",
         "one node-occurrence/anchor-delimited observed runtime call",
         "compare profiler-visible host runtime distributions after the same "
         "recovered structural position across occurrences",
         "SELECT * FROM traceloom_v_node_host_activity WHERE coverage_kind = "
         "'self' AND node_id = (SELECT node_id FROM "
         "traceloom_v_tree_node WHERE node_type = 'Atom' AND "
         "occurrence_count > 1 "
         "ORDER BY total_us DESC LIMIT 1) ORDER BY occurrence_idx, "
         "anchor_order, observed_order LIMIT 200;"},
        {"structure_bubble", "traceloom_v_structure_bubble_occurrence",
         "one uncovered device interval before one structural occurrence",
         "rank overlap-safe device bubbles and inspect their supported host "
         "observation scope without assigning a cause",
         "SELECT * FROM traceloom_v_structure_bubble_occurrence ORDER BY "
         "bubble_us DESC, bubble_id LIMIT 200;"},
        {"structure_bubble_position",
         "traceloom_v_structure_bubble_position",
         "one recurrent structural bubble position",
         "rank overlap-safe bubble populations while retaining typed host "
         "support counts for every position",
         "SELECT * FROM traceloom_v_structure_bubble_position ORDER BY "
         "total_bubble_us DESC, structural_position_id;"},
        {"structure_bubble_api_distribution",
         "traceloom_v_structure_bubble_api_stats",
         "one structural position and public runtime API family",
         "compare bubble costs with upstream API-family occurrence, count, "
         "duration, and observation-coverage distributions",
         "SELECT * FROM traceloom_v_structure_bubble_api_stats ORDER BY "
         "total_bubble_us DESC, structural_position_id, api_family;"},
        {"structure_bubble_host_context",
         "traceloom_v_structure_bubble_host_context",
         "one recurrent bubble position and optional host API family",
         "change observation domain without dropping unsupported-only or "
         "supported-but-empty structural positions",
         "SELECT * FROM traceloom_v_structure_bubble_host_context ORDER BY "
         "total_bubble_us DESC, structural_position_id, api_family;"},
        {"structure_bubble_runtime_call",
         "traceloom_v_structure_bubble_runtime_call",
         "one profiler-visible runtime call in one bubble observation scope",
         "drill from a selected bubble distribution to exact runtime calls "
         "and source locators without causal attribution",
         "SELECT * FROM traceloom_v_structure_bubble_runtime_call WHERE "
         "bubble_id = (SELECT bubble_id FROM "
         "traceloom_v_structure_bubble_occurrence ORDER BY bubble_us DESC "
         "LIMIT 1) ORDER BY observed_order LIMIT 200;"},
        {"event_source", "traceloom_v_event_source_locator",
         "one event-to-raw-source link",
         "resolve normalized evidence to the embedded profiler table",
         "SELECT * FROM traceloom_v_event_source_locator ORDER BY db_idx, "
         "device_id, event_id, source_ordinal;"},
        {"runtime_call_source", "traceloom_v_runtime_call_source_locator",
         "one runtime call-to-raw-source locator",
         "resolve host runtime observations to embedded profiler rows",
         "SELECT * FROM traceloom_v_runtime_call_source_locator ORDER BY "
         "db_idx, provider, start_ns, runtime_call_id;"},
        {"device_work_source", "traceloom_v_device_work_source_locator",
         "one device-work-to-raw-source locator",
         "resolve correlated device work to embedded profiler rows",
         "SELECT * FROM traceloom_v_device_work_source_locator ORDER BY "
         "db_idx, device_id, start_ns, device_work_id;"},
        {"exact_graph_member", "traceloom_v_node_graph_body_member",
         "one exact graph body member in one tree occurrence",
         "drill through replay structure to exact member cost and provenance",
         "SELECT * FROM traceloom_v_node_graph_body_member ORDER BY db_idx, "
         "device_id, node_id, occurrence_idx, node_member_order, "
         "lane_ordinal, task_ordinal;"},
        {"replay_hotspot", "traceloom_replay_cost_aggregate",
         "one role-collapsed replay member distribution",
         "rank stable replay-internal cost distributions",
         "SELECT * FROM traceloom_replay_cost_aggregate ORDER BY "
         "duration_median_ns DESC, aggregate_id;"},
        {"analysis_issue", "traceloom_replay_cost_issue",
         "one typed replay analysis issue",
         "audit unsupported or unrecognized replay analysis",
         "SELECT * FROM traceloom_replay_cost_issue ORDER BY db_idx, "
         "device_id, replay_unit_id, issue_id;"},
        {"reconstruction_status",
         "traceloom_aclgraph_reconstruction_region",
         "one graph reconstruction region",
         "audit recognized and unrecognized graph reconstruction evidence",
         "SELECT * FROM traceloom_aclgraph_reconstruction_region ORDER BY "
         "db_idx, device_id, region_order;"},
        {"operator_audit", "traceloom_operator_audit",
         "one observed operator identity",
         "rank concrete device operators without an allowlist",
         "SELECT * FROM traceloom_operator_audit ORDER BY "
         "graph_body_member_count DESC, total_duration_ns DESC, "
         "operator_name;"},
        {"raw_table", "traceloom_raw_table", "one embedded profiler table",
         "discover collision-free raw evidence storage",
         "SELECT * FROM traceloom_raw_table ORDER BY source_id, source_table;"},
    };
    for (const auto& surface : surfaces) {
      sqlite_exec(db,
                  "INSERT INTO traceloom_analysis_surface VALUES(" +
                      quote_literal(surface[0]) + "," +
                      quote_literal(surface[1]) + "," +
                      quote_literal(surface[2]) + "," +
                      quote_literal(surface[3]) + "," +
                      quote_literal(surface[4]) + ")",
                  "failed to insert analysis surface row");
    }
    const std::vector<std::vector<std::string>> projection_recipes = {
        {"scope_catalog", "5", "structural_node", "candidate_scopes",
         "folded", "device", "node_cost", "(none)",
         "rank and select reusable structural scopes before composing more "
         "specific projections",
         "SELECT node_id, parent_node_id, local_node_id, db_idx, device_id, "
         "view_name, display_order, path, symbol, label, node_type, "
         "repeat_count, occurrence_count, anchor_count, first_anchor_idx, "
         "last_anchor_idx, total_us, avg_total_us FROM "
         "traceloom_v_tree_node ORDER BY total_us DESC, db_idx, device_id, "
         "view_name, display_order;"},
        {"scope_occurrences", "10", "structural_node",
         "one_or_all_occurrences", "folded", "device",
         "occurrence_cost",
         ":node_id, :occurrence_idx (NULL selects all)",
         "inspect one realized occurrence or the full cost population of the "
         "same structural scope",
         "SELECT node_id, local_node_id, occurrence_idx, repeat_context, "
         "start_ns, end_ns, anchor_count, compute_us, comm_us, idle_us, "
         "total_us, self_us, aux_us FROM traceloom_tree_node_occurrence "
         "WHERE node_id = :node_id AND (:occurrence_idx IS NULL OR "
         "occurrence_idx = :occurrence_idx) ORDER BY occurrence_idx;"},
        {"scope_hierarchy", "20", "structural_node", "definition",
         "immediate_children", "device", "node_cost",
         ":node_id",
         "keep a selected scope folded while reading its ordered child "
         "patterns and costs",
         "SELECT parent_node_id, child_node_id, edge_order, local_node_id, "
         "label, node_type, repeat_count, occurrence_count, total_us, "
         "avg_total_us FROM traceloom_v_node_children WHERE parent_node_id = "
         ":node_id ORDER BY edge_order;"},
        {"scope_members", "30", "structural_node",
         "one_or_all_occurrences", "anchors_and_events", "device",
         "member_cost",
         ":node_id, :occurrence_idx (NULL selects all)",
         "expand a selected scope to ordered anchors and normalized event "
         "evidence",
         "SELECT na.node_id, na.occurrence_idx, na.anchor_order, "
         "na.coverage_kind, a.anchor_id, a.anchor_idx, a.symbol, e.event_id, "
         "e.stream_id, e.start_ns, e.end_ns, e.dur_us, e.role, "
         "e.semantic_role FROM traceloom_tree_node_anchor na JOIN "
         "traceloom_anchor a ON a.anchor_id = na.anchor_id LEFT JOIN "
         "traceloom_event e ON e.event_id = a.event_id WHERE na.node_id = "
         ":node_id AND (:occurrence_idx IS NULL OR na.occurrence_idx = "
         ":occurrence_idx) ORDER BY na.occurrence_idx, na.anchor_order;"},
        {"scope_exact_replay_members", "40", "structural_node",
         "one_or_all_occurrences", "exact_replay_members", "device",
         "scheduled_member_cost_and_provenance",
         ":node_id, :occurrence_idx (NULL selects all)",
         "expand supported graph/replay anchors to exact ordered body members "
         "without inferring membership from time overlap",
         "SELECT member.node_id, member.occurrence_idx, "
         "cost.cost_unit_id, member.replay_unit_id, "
         "member.node_launch_id AS launch_id, member.node_slot_order AS "
         "slot_order, member.node_member_order, member.lane_ordinal, "
         "member.task_ordinal, member.event_id, member.member_symbol, "
         "member.start_ns, member.end_ns, member.dur_us, "
         "member.source_table, member.source_row_id, member.evidence_level "
         "FROM traceloom_v_node_graph_body_member member LEFT JOIN "
         "traceloom_replay_cost_unit cost ON cost.db_idx = member.db_idx AND "
         "cost.device_id = member.device_id AND cost.replay_unit_id = "
         "member.replay_unit_id WHERE member.node_id = :node_id AND "
         "(:occurrence_idx IS NULL OR member.occurrence_idx = "
         ":occurrence_idx) ORDER BY member.occurrence_idx, "
         "member.node_member_order, member.lane_ordinal, "
         "member.task_ordinal;"},
        {"replay_cost_units", "41", "exact_replay_unit",
         "candidate_occurrences", "unit_cost_summary", "device",
         "support_and_launch_inventory", "(none)",
         "discover exact replay-unit cost support before selecting one "
         "occurrence for launch-level inspection",
         "SELECT cost_unit_id, db_idx, device_id, replay_unit_id, "
         "graph_template_id, launch_member_count, resolved_launch_count, "
         "support_status, reason_codes FROM traceloom_replay_cost_unit "
         "ORDER BY db_idx, device_id, replay_unit_id;"},
        {"replay_cost_launches", "42", "exact_replay_unit",
         "one_or_all_slots", "ordered_launch_slots", "device",
         "launch_cost_lenses",
         ":cost_unit_id, :slot_order (NULL selects all)",
         "inspect every ordered launch slot or one selected slot occurrence "
         "inside an exact replay unit",
         "SELECT launch_id, cost_unit_id, db_idx, device_id, replay_unit_id, "
         "member_order, graph_launch_occurrence_id, composition_slot_id, "
         "slot_role, slot_order, replay_body_template_id, body_id, "
         "support_status, reason_code, member_count, task_sum_ns, "
         "busy_union_ns, envelope_ns, compute_ns, communication_ns, "
         "data_move_ns FROM traceloom_replay_cost_launch WHERE "
         "cost_unit_id = :cost_unit_id AND (:slot_order IS NULL OR "
         "slot_order = :slot_order) ORDER BY member_order, launch_id;"},
        {"replay_cost_members", "43", "graph_launch",
         "all_members", "ordered_body_members", "device",
         "scheduled_member_cost_and_provenance", ":launch_id",
         "expand one supported replay launch to exact ordered member costs "
         "and normalized event coordinates",
         "SELECT member_id, launch_id, cost_unit_id, db_idx, device_id, "
         "composition_slot_id, slot_role, slot_order, "
         "replay_body_template_id, body_id, stream_id, lane_ordinal, "
         "task_ordinal, kind, event_id, identity, raw_task_id, start_ns, "
         "end_ns, duration_ns, relative_start_ns, relative_end_ns, "
         "scheduled_work_share_ppm, scheduled_work_share_supported, "
         "scheduled_work_denominator_body_task_sum_ns FROM "
         "traceloom_replay_cost_member WHERE launch_id = :launch_id "
         "ORDER BY lane_ordinal, task_ordinal, member_id;"},
        {"replay_structural_placements", "44", "exact_replay_unit",
         "one_unit", "structural_occurrences", "device",
         "structural_membership", ":replay_unit_id",
         "locate every recovered graph-unit tree occurrence that realizes "
         "one exact replay unit without assuming one placement",
         "SELECT DISTINCT member.replay_unit_id, member.db_idx, "
         "member.device_id, member.node_id, member.occurrence_idx, "
         "node.display_order, node.path, node.label, node.category FROM "
         "traceloom_v_node_graph_body_member member JOIN "
         "traceloom_v_tree_node node ON node.node_id = member.node_id WHERE "
         "member.replay_unit_id = :replay_unit_id AND node.category = "
         "'graph_unit' ORDER BY "
         "node.display_order, member.occurrence_idx, member.node_id;"},
        {"exact_replay_partition", "45", "device_sequence",
         "complete_partition", "open_replay_between_segments", "device",
         "right_anchored_disjoint_cost", "(none)",
         "read the complete cost partition induced by ordered exact replay "
         "boundaries without rebuilding intervals in client SQL",
         "SELECT tree_id,db_idx,device_id,segment_order,coordinate_kind,"
         "coordinate_index,segment_label,left_protected_interval_id,"
         "right_protected_interval_id,anchor_count,compute_us,comm_us,"
         "idle_us,total_us,aux_us FROM "
         "traceloom_v_exact_replay_partition ORDER BY db_idx,device_id,"
         "tree_id,segment_order;"},
        {"position_population", "50", "structural_node",
         "all_occurrences", "aligned_positions", "device",
         "position_cost_distribution", ":node_id",
         "compare corresponding ordered positions across every occurrence of "
         "the selected scope",
         "SELECT na.node_id, na.anchor_order, na.coverage_kind, a.symbol, "
         "count(DISTINCT na.occurrence_idx) AS occurrence_count, "
         "avg(na.total_us) AS avg_total_us, min(na.total_us) AS min_total_us, "
         "max(na.total_us) AS max_total_us, avg(na.compute_us) AS "
         "avg_compute_us, avg(na.comm_us) AS avg_comm_us, avg(na.idle_us) AS "
         "avg_idle_us, avg(na.aux_us) AS avg_aux_us FROM "
         "traceloom_tree_node_anchor na JOIN traceloom_anchor a ON "
         "a.anchor_id = na.anchor_id WHERE na.node_id = :node_id GROUP BY "
         "na.node_id, na.anchor_order, na.coverage_kind, a.symbol ORDER BY "
         "na.anchor_order, a.symbol;"},
        {"position_occurrences", "55", "structural_position",
         "one_or_all_occurrences", "aligned_position_members", "device",
         "position_occurrence_cost",
         ":node_id, :anchor_order, :occurrence_idx (NULL selects all)",
         "inspect the occurrence population behind one aligned structural "
         "position, then select an outlier without losing its coordinates",
         "SELECT na.node_id, na.occurrence_idx, na.anchor_order, "
         "na.coverage_kind, na.anchor_id, a.symbol, e.event_id, e.stream_id, "
         "e.start_ns, e.end_ns, e.dur_us, na.compute_us, na.comm_us, "
         "na.idle_us, na.total_us, na.aux_us FROM "
         "traceloom_tree_node_anchor na JOIN traceloom_anchor a ON "
         "a.anchor_id = na.anchor_id LEFT JOIN traceloom_event e ON "
         "e.event_id = a.event_id WHERE na.node_id = :node_id AND "
         "na.anchor_order = :anchor_order AND (:occurrence_idx IS NULL OR "
         "na.occurrence_idx = :occurrence_idx) ORDER BY na.occurrence_idx;"},
        {"scope_host_windows", "58", "structural_node",
         "one_or_all_occurrences", "anchor_pair_windows", "device_and_host",
         "typed_host_interval_support",
         ":node_id, :occurrence_idx (NULL selects all)",
         "project every adjacent-anchor position in a selected device scope "
         "to a supported or explicitly unsupported host interval",
         "SELECT node_id, occurrence_idx, anchor_order, coverage_kind, "
         "interval_id, left_anchor_id, right_anchor_id, right_anchor_symbol, "
         "support_state, provider, clock_domain, host_start_ns, host_end_ns, "
         "host_interval_us, left_endpoint_count, right_endpoint_count FROM "
         "traceloom_v_node_host_interval WHERE node_id = :node_id AND "
         "(:occurrence_idx IS NULL OR occurrence_idx = :occurrence_idx) "
         "ORDER BY occurrence_idx, anchor_order;"},
        {"scope_host_context", "60", "structural_node",
         "one_or_all_occurrences", "anchor_pair_windows", "host",
         "runtime_api_distribution",
         ":node_id, :occurrence_idx (NULL selects all)",
         "project the same device scope into typed host windows and compare "
         "profiler-visible runtime API distributions without hiding "
         "unsupported or empty windows",
         "SELECT i.node_id, i.occurrence_idx, i.anchor_order, "
         "i.right_anchor_symbol, i.coverage_kind, i.interval_id, "
         "i.support_state, i.host_interval_us, c.api_name, "
         "count(a.runtime_call_id) AS observed_calls, "
         "COALESCE(ROUND(sum((MIN(c.end_ns, i.host_end_ns) - "
         "MAX(c.start_ns, i.host_start_ns)) / 1000.0), 3), 0.0) AS "
         "scheduled_overlap_us FROM traceloom_v_node_host_interval i LEFT "
         "JOIN traceloom_anchor_host_activity a ON a.interval_id = "
         "i.interval_id LEFT JOIN traceloom_runtime_call c ON "
         "c.runtime_call_id = a.runtime_call_id WHERE i.node_id = :node_id AND "
         "(:occurrence_idx IS NULL OR i.occurrence_idx = :occurrence_idx) "
         "GROUP BY i.node_id, i.occurrence_idx, i.anchor_order, "
         "i.right_anchor_symbol, i.coverage_kind, i.interval_id, "
         "i.support_state, i.host_interval_us, c.api_name ORDER BY "
         "i.occurrence_idx, i.anchor_order, scheduled_overlap_us DESC;"},
        {"bubble_hotspots", "65", "structural_position",
         "all_occurrences", "position_summary", "device_and_host",
         "bubble_cost_and_host_support", "(none)",
         "rank recurrent uncovered-device positions while retaining typed "
         "host-observation coverage",
         "SELECT structural_position_id, right_local_node_id, "
         "right_node_path, right_node_symbol, bubble_occurrence_count, "
         "supported_host_occurrence_count, missing_endpoint_occurrence_count, "
         "nonmonotonic_occurrence_count, "
         "other_unsupported_occurrence_count, host_observation_coverage, "
         "total_bubble_us, avg_bubble_us, min_bubble_us, max_bubble_us FROM "
         "traceloom_v_structure_bubble_position ORDER BY total_bubble_us "
         "DESC, bubble_occurrence_count DESC;"},
        {"bubble_occurrences", "68", "structural_position",
         "one_or_all_occurrences", "bubble_occurrences", "device_and_host",
         "bubble_cost_and_host_support",
         ":structural_position_id, :bubble_id (NULL selects all)",
         "inspect one recurrent bubble population or select one occurrence "
         "for host-window and source drill-down",
         "SELECT structural_position_id, bubble_id, right_node_id, "
         "right_occurrence_idx, left_anchor_id, right_anchor_id, bubble_us, "
         "transition_total_us, host_interval_id, host_observation_status, "
         "host_interval_us, provider, host_start_ns, host_end_ns FROM "
         "traceloom_v_structure_bubble_occurrence WHERE "
         "structural_position_id = :structural_position_id AND (:bubble_id "
         "IS NULL OR bubble_id = :bubble_id) ORDER BY bubble_us DESC, "
         "right_occurrence_idx;"},
        {"bubble_host_context", "70", "structural_position",
         "all_occurrences", "bubble_population", "device_and_host",
         "bubble_and_runtime_api_distribution",
         ":structural_position_id",
         "compare recurrent uncovered-device cost with supported upstream "
         "host API-family observations without assigning a cause or hiding "
         "unsupported-only positions",
         "SELECT structural_position_id, bubble_occurrence_count, "
         "supported_host_occurrence_count, missing_endpoint_occurrence_count, "
         "nonmonotonic_occurrence_count, host_observation_coverage, "
         "total_bubble_us, avg_bubble_us, api_family, presence_count, "
         "avg_calls_per_observable_bubble, "
         "avg_scheduled_overlap_us_per_bubble FROM "
         "traceloom_v_structure_bubble_host_context WHERE "
         "structural_position_id = :structural_position_id ORDER BY "
         "total_bubble_us DESC, api_family;"},
        {"host_window_calls", "75", "host_interval",
         "one_interval", "literal_runtime_calls", "host",
         "runtime_call_observations", ":interval_id",
         "expand one typed host interval to literal observed runtime calls; "
         "an unsupported or empty interval remains a row",
         "SELECT i.interval_id, i.support_state, i.provider, i.clock_domain, "
         "i.host_start_ns, i.host_end_ns, c.runtime_call_id, c.api_name, "
         "c.api_type, c.start_ns AS observed_start_ns, c.end_ns AS "
         "observed_end_ns, c.dur_us AS observed_dur_us, "
         "ROUND((MIN(c.end_ns, i.host_end_ns) - MAX(c.start_ns, "
         "i.host_start_ns)) / 1000.0, 3) AS observed_overlap_us, "
         "CASE WHEN c.runtime_call_id IS NULL THEN NULL WHEN c.start_ns >= "
         "i.host_start_ns AND c.end_ns <= i.host_end_ns THEN 'contained' "
         "ELSE 'boundary_overlap' END AS "
         "interval_relation, a.observed_order FROM "
         "traceloom_v_anchor_host_interval i LEFT JOIN "
         "traceloom_anchor_host_activity a ON a.interval_id = i.interval_id "
         "LEFT JOIN traceloom_runtime_call c ON c.runtime_call_id = "
         "a.runtime_call_id WHERE i.interval_id = :interval_id ORDER BY "
         "a.observed_order;"},
        {"runtime_call_audit", "78", "runtime_call", "one_call",
         "source_rows", "profiler_evidence", "raw_observation",
         ":runtime_call_id",
         "audit a projected host runtime call through its embedded profiler "
         "source locator",
         "SELECT * FROM traceloom_v_runtime_call_source_locator WHERE "
         "runtime_call_id = :runtime_call_id;"},
        {"device_window_events", "80", "bounded_device_window",
         "one_window", "events", "device", "event_duration",
         ":db_idx, :device_id, :start_ns, :end_ns",
         "inspect normalized events overlapping a user-selected device "
         "window without promoting that window to a recovered pattern",
         "SELECT event_id, db_idx, device_id, step_idx, symbol, role, "
         "semantic_role, stream_id, start_ns, end_ns, dur_us, source_table, "
         "source_key FROM "
         "traceloom_event WHERE db_idx = :db_idx AND device_id = :device_id "
         "AND start_ns < :end_ns AND end_ns > :start_ns ORDER BY start_ns, "
         "end_ns, stream_id, event_id;"},
        {"event_reconciliation_audit", "85", "normalized_event",
         "one_event", "reconciliation_members", "device",
         "identity_contribution", ":event_id",
         "inspect whether an event stayed independent or contributed timing, "
         "symbol, or cost to one canonical structural anchor",
         "SELECT * FROM traceloom_v_event_reconciliation WHERE event_id = "
         ":event_id OR canonical_event_id = :event_id OR envelope_event_id = "
         ":event_id ORDER BY decision_id, member_order;"},
        {"event_audit", "90", "normalized_event", "one_event",
         "source_rows", "profiler_evidence", "raw_observation",
         ":event_id",
         "audit any projected event through its embedded profiler source "
         "locator",
         "SELECT * FROM traceloom_v_event_source_locator WHERE event_id = "
         ":event_id ORDER BY source_ordinal;"},
    };
    for (const auto& recipe : projection_recipes) {
      sqlite_exec(db,
                  "INSERT INTO traceloom_projection_recipe VALUES(" +
                      quote_literal(recipe[0]) + "," + recipe[1] + "," +
                      quote_literal(recipe[2]) + "," +
                      quote_literal(recipe[3]) + "," +
                      quote_literal(recipe[4]) + "," +
                      quote_literal(recipe[5]) + "," +
                      quote_literal(recipe[6]) + "," +
                      quote_literal(recipe[7]) + "," +
                      quote_literal(recipe[8]) + "," +
                      quote_literal(recipe[9]) + ")",
                  "failed to insert projection recipe row");
    }
    const std::vector<std::vector<std::string>> projection_parameters = {
        {"scope_occurrences", "10", "node_id", "TEXT", "0",
         "structural_node_id", "traceloom_v_tree_node", "node_id",
         "selected structural scope"},
        {"scope_occurrences", "20", "occurrence_idx", "INTEGER", "1",
         "structural_occurrence_index", "traceloom_tree_node_occurrence",
         "occurrence_idx",
         "NULL selects all occurrences; a value selects one execution"},
        {"scope_hierarchy", "10", "node_id", "TEXT", "0",
         "structural_node_id", "traceloom_v_tree_node", "node_id",
         "selected structural scope"},
        {"scope_members", "10", "node_id", "TEXT", "0",
         "structural_node_id", "traceloom_v_tree_node", "node_id",
         "selected structural scope"},
        {"scope_members", "20", "occurrence_idx", "INTEGER", "1",
         "structural_occurrence_index", "traceloom_tree_node_occurrence",
         "occurrence_idx",
         "NULL selects all occurrences; a value selects one execution"},
        {"scope_exact_replay_members", "10", "node_id", "TEXT", "0",
         "structural_node_id", "traceloom_v_tree_node", "node_id",
         "selected structural scope containing supported replay evidence"},
        {"scope_exact_replay_members", "20", "occurrence_idx", "INTEGER",
         "1", "structural_occurrence_index",
         "traceloom_tree_node_occurrence", "occurrence_idx",
         "NULL selects all occurrences; a value selects one execution"},
        {"replay_cost_launches", "10", "cost_unit_id", "TEXT", "0",
         "replay_cost_unit_id", "traceloom_replay_cost_unit",
         "cost_unit_id", "selected exact replay-unit cost occurrence"},
        {"replay_cost_launches", "20", "slot_order", "INTEGER", "1",
         "replay_slot_order", "traceloom_replay_cost_launch", "slot_order",
         "NULL selects all launch slots; a value selects one slot"},
        {"replay_cost_members", "10", "launch_id", "TEXT", "0",
         "replay_cost_launch_id", "traceloom_replay_cost_launch",
         "launch_id", "selected supported replay launch"},
        {"replay_structural_placements", "10", "replay_unit_id", "INTEGER",
         "0", "replay_unit_id", "traceloom_replay_cost_unit",
         "replay_unit_id", "selected exact replay occurrence"},
        {"position_population", "10", "node_id", "TEXT", "0",
         "structural_node_id", "traceloom_v_tree_node", "node_id",
         "selected structural scope whose ordered positions are aligned"},
        {"position_occurrences", "10", "node_id", "TEXT", "0",
         "structural_node_id", "traceloom_v_tree_node", "node_id",
         "selected structural scope"},
        {"position_occurrences", "20", "anchor_order", "INTEGER", "0",
         "structural_anchor_order", "traceloom_tree_node_anchor",
         "anchor_order", "selected aligned position inside the scope"},
        {"position_occurrences", "30", "occurrence_idx", "INTEGER", "1",
         "structural_occurrence_index", "traceloom_tree_node_anchor",
         "occurrence_idx",
         "NULL selects the position population; a value selects one member"},
        {"scope_host_windows", "10", "node_id", "TEXT", "0",
         "structural_node_id", "traceloom_v_tree_node", "node_id",
         "selected structural scope"},
        {"scope_host_windows", "20", "occurrence_idx", "INTEGER", "1",
         "structural_occurrence_index", "traceloom_tree_node_occurrence",
         "occurrence_idx",
         "NULL selects all occurrences; a value selects one execution"},
        {"scope_host_context", "10", "node_id", "TEXT", "0",
         "structural_node_id", "traceloom_v_tree_node", "node_id",
         "selected structural scope"},
        {"scope_host_context", "20", "occurrence_idx", "INTEGER", "1",
         "structural_occurrence_index", "traceloom_tree_node_occurrence",
         "occurrence_idx",
         "NULL selects all occurrences; a value selects one execution"},
        {"bubble_occurrences", "10", "structural_position_id", "TEXT",
         "0", "structural_position_id",
         "traceloom_v_structure_bubble_position", "structural_position_id",
         "selected recurrent bubble position"},
        {"bubble_occurrences", "20", "bubble_id", "TEXT", "1",
         "bubble_id", "traceloom_v_structure_bubble_occurrence",
         "bubble_id",
         "NULL selects all bubbles; a value selects one occurrence"},
        {"bubble_host_context", "10", "structural_position_id", "TEXT",
         "0", "structural_position_id",
         "traceloom_v_structure_bubble_position",
         "structural_position_id", "selected recurrent bubble position"},
        {"host_window_calls", "10", "interval_id", "TEXT", "0",
         "host_interval_id", "traceloom_v_anchor_host_interval",
         "interval_id", "selected typed host interval"},
        {"runtime_call_audit", "10", "runtime_call_id", "TEXT", "0",
         "runtime_call_id", "traceloom_runtime_call", "runtime_call_id",
         "selected observed host runtime call"},
        {"device_window_events", "10", "db_idx", "INTEGER", "0",
         "database_index", "traceloom_event", "db_idx",
         "source database coordinate"},
        {"device_window_events", "20", "device_id", "INTEGER", "0",
         "device_id", "traceloom_event", "device_id", "device coordinate"},
        {"device_window_events", "30", "start_ns", "INTEGER", "0",
         "time_start_ns", "traceloom_event", "start_ns",
         "inclusive window start"},
        {"device_window_events", "40", "end_ns", "INTEGER", "0",
         "time_end_ns", "traceloom_event", "end_ns",
         "exclusive window end"},
        {"event_reconciliation_audit", "10", "event_id", "TEXT", "0",
         "normalized_event_id", "traceloom_event", "event_id",
         "selected normalized event"},
        {"event_audit", "10", "event_id", "TEXT", "0",
         "normalized_event_id", "traceloom_event", "event_id",
         "selected normalized event"},
    };
    for (const auto& parameter : projection_parameters) {
      sqlite_exec(
          db,
          "INSERT INTO traceloom_projection_parameter VALUES(" +
              quote_literal(parameter[0]) + "," + parameter[1] + "," +
              quote_literal(parameter[2]) + "," +
              quote_literal(parameter[3]) + "," + parameter[4] + "," +
              quote_literal(parameter[5]) + "," +
              quote_literal(parameter[6]) + "," +
              quote_literal(parameter[7]) + "," +
              quote_literal(parameter[8]) + ")",
          "failed to insert projection parameter row");
    }
    const std::vector<std::vector<std::string>> projection_coordinates = {
        {"scope_catalog", "10", "node_id", "structural_node_id",
         "selected structural scope"},
        {"scope_catalog", "20", "db_idx", "database_index",
         "source database coordinate"},
        {"scope_catalog", "30", "device_id", "device_id",
         "device coordinate"},
        {"scope_catalog", "40", "view_name", "structural_view_name",
         "structural projection identity"},
        {"scope_catalog", "50", "parent_node_id", "structural_node_id",
         "immediate parent reusable as another structural scope"},
        {"scope_occurrences", "10", "node_id", "structural_node_id",
         "selected structural scope"},
        {"scope_occurrences", "20", "occurrence_idx",
         "structural_occurrence_index", "selected realized occurrence"},
        {"scope_hierarchy", "10", "parent_node_id", "structural_node_id",
         "current folded scope"},
        {"scope_hierarchy", "20", "child_node_id", "structural_node_id",
         "ordered child usable as a new scope"},
        {"scope_members", "10", "node_id", "structural_node_id",
         "selected structural scope"},
        {"scope_members", "20", "occurrence_idx",
         "structural_occurrence_index", "selected realized occurrence"},
        {"scope_members", "30", "anchor_order", "structural_anchor_order",
         "ordered position inside the selected scope"},
        {"scope_members", "40", "anchor_id", "anchor_id",
         "selected structural anchor"},
        {"scope_members", "50", "event_id", "normalized_event_id",
         "normalized event available for source audit"},
        {"scope_exact_replay_members", "10", "node_id",
         "structural_node_id", "selected structural scope"},
        {"scope_exact_replay_members", "20", "occurrence_idx",
         "structural_occurrence_index", "selected realized occurrence"},
        {"scope_exact_replay_members", "30", "cost_unit_id",
         "replay_cost_unit_id", "selected exact replay cost unit"},
        {"scope_exact_replay_members", "40", "replay_unit_id",
         "replay_unit_id", "selected exact replay occurrence"},
        {"scope_exact_replay_members", "50", "launch_id",
         "replay_cost_launch_id", "selected graph launch cost occurrence"},
        {"scope_exact_replay_members", "60", "slot_order",
         "replay_slot_order", "ordered launch slot inside the replay unit"},
        {"scope_exact_replay_members", "70", "event_id",
         "normalized_event_id", "exact replay member available for audit"},
        {"replay_cost_units", "10", "cost_unit_id",
         "replay_cost_unit_id", "selected exact replay cost unit"},
        {"replay_cost_units", "20", "replay_unit_id", "replay_unit_id",
         "selected exact replay occurrence"},
        {"replay_cost_units", "30", "graph_template_id",
         "graph_template_id", "recovered replay template"},
        {"replay_cost_units", "40", "device_id", "device_id",
         "device coordinate"},
        {"replay_cost_launches", "10", "launch_id",
         "replay_cost_launch_id", "selected replay launch cost occurrence"},
        {"replay_cost_launches", "20", "cost_unit_id",
         "replay_cost_unit_id", "owning exact replay cost unit"},
        {"replay_cost_launches", "30", "replay_unit_id",
         "replay_unit_id", "owning exact replay occurrence"},
        {"replay_cost_launches", "40", "graph_launch_occurrence_id",
         "graph_launch_occurrence_id", "exact graph launch occurrence"},
        {"replay_cost_launches", "50", "composition_slot_id",
         "replay_composition_slot_id", "recovered composition slot"},
        {"replay_cost_launches", "60", "slot_order",
         "replay_slot_order", "ordered launch slot inside the replay unit"},
        {"replay_cost_members", "10", "member_id",
         "replay_cost_member_id", "selected exact replay member cost row"},
        {"replay_cost_members", "20", "launch_id",
         "replay_cost_launch_id", "owning replay launch cost occurrence"},
        {"replay_cost_members", "30", "cost_unit_id",
         "replay_cost_unit_id", "owning exact replay cost unit"},
        {"replay_cost_members", "40", "event_id",
         "normalized_event_id", "normalized event available for audit"},
        {"replay_structural_placements", "10", "replay_unit_id",
         "replay_unit_id", "selected exact replay occurrence"},
        {"replay_structural_placements", "20", "node_id",
         "structural_node_id", "recovered structural scope containing it"},
        {"replay_structural_placements", "30", "occurrence_idx",
         "structural_occurrence_index", "realized structural occurrence"},
        {"exact_replay_partition", "10", "tree_id", "structural_tree_id",
         "device-tree whose exact replay boundaries induce the partition"},
        {"exact_replay_partition", "20", "db_idx", "database_index",
         "source database coordinate"},
        {"exact_replay_partition", "30", "device_id", "device_id",
         "device coordinate"},
        {"exact_replay_partition", "40", "coordinate_kind",
         "replay_partition_kind", "open, replay, or between-replays class"},
        {"exact_replay_partition", "50", "coordinate_index",
         "replay_partition_index", "stable index within the segment class"},
        {"position_population", "10", "node_id", "structural_node_id",
         "selected structural scope"},
        {"position_population", "20", "anchor_order",
         "structural_anchor_order", "aligned position inside the scope"},
        {"position_occurrences", "10", "node_id", "structural_node_id",
         "selected structural scope"},
        {"position_occurrences", "20", "occurrence_idx",
         "structural_occurrence_index", "selected position occurrence"},
        {"position_occurrences", "30", "anchor_order",
         "structural_anchor_order", "aligned position inside the scope"},
        {"position_occurrences", "40", "anchor_id", "anchor_id",
         "selected structural anchor"},
        {"position_occurrences", "50", "event_id", "normalized_event_id",
         "normalized event available for source audit"},
        {"scope_host_windows", "10", "node_id", "structural_node_id",
         "selected structural scope"},
        {"scope_host_windows", "20", "occurrence_idx",
         "structural_occurrence_index", "selected realized occurrence"},
        {"scope_host_windows", "30", "anchor_order",
         "structural_anchor_order", "device position delimiting the window"},
        {"scope_host_windows", "40", "interval_id", "host_interval_id",
         "typed host interval available for call drill-down"},
        {"scope_host_windows", "50", "left_anchor_id", "anchor_id",
         "left device endpoint"},
        {"scope_host_windows", "60", "right_anchor_id", "anchor_id",
         "right device endpoint"},
        {"scope_host_context", "10", "node_id", "structural_node_id",
         "selected structural scope"},
        {"scope_host_context", "20", "occurrence_idx",
         "structural_occurrence_index", "selected realized occurrence"},
        {"scope_host_context", "30", "anchor_order",
         "structural_anchor_order", "device position delimiting the window"},
        {"scope_host_context", "40", "interval_id", "host_interval_id",
         "typed host interval available for literal-call drill-down"},
        {"bubble_hotspots", "10", "structural_position_id",
         "structural_position_id", "recurrent bubble position"},
        {"bubble_occurrences", "10", "structural_position_id",
         "structural_position_id", "selected recurrent bubble position"},
        {"bubble_occurrences", "20", "bubble_id", "bubble_id",
         "selected bubble occurrence"},
        {"bubble_occurrences", "30", "right_node_id",
         "structural_node_id", "right-hand structural node"},
        {"bubble_occurrences", "40", "left_anchor_id", "anchor_id",
         "left device endpoint"},
        {"bubble_occurrences", "50", "right_anchor_id", "anchor_id",
         "right device endpoint"},
        {"bubble_occurrences", "60", "host_interval_id",
         "host_interval_id", "typed upstream host interval"},
        {"bubble_host_context", "10", "structural_position_id",
         "structural_position_id", "selected recurrent bubble position"},
        {"host_window_calls", "10", "interval_id", "host_interval_id",
         "selected typed host interval"},
        {"host_window_calls", "20", "runtime_call_id", "runtime_call_id",
         "observed runtime call available for source audit"},
        {"runtime_call_audit", "10", "runtime_call_id", "runtime_call_id",
         "selected observed host runtime call"},
        {"device_window_events", "10", "event_id", "normalized_event_id",
         "normalized event available for source audit"},
        {"device_window_events", "20", "db_idx", "database_index",
         "source database coordinate"},
        {"device_window_events", "30", "device_id", "device_id",
         "device coordinate"},
        {"device_window_events", "40", "start_ns", "time_start_ns",
         "event start usable as a bounded window start"},
        {"device_window_events", "50", "end_ns", "time_end_ns",
         "event end usable as a bounded window end"},
        {"event_reconciliation_audit", "10", "event_id",
         "normalized_event_id", "observed reconciliation member"},
        {"event_reconciliation_audit", "20", "canonical_event_id",
         "normalized_event_id", "canonical event when reconciliation is supported"},
        {"event_reconciliation_audit", "30", "envelope_event_id",
         "normalized_event_id", "timing-envelope event when present"},
        {"event_audit", "10", "event_id", "normalized_event_id",
         "selected normalized event"},
    };
    for (const auto& coordinate : projection_coordinates) {
      sqlite_exec(
          db,
          "INSERT INTO traceloom_projection_coordinate VALUES(" +
              quote_literal(coordinate[0]) + "," + coordinate[1] + "," +
              quote_literal(coordinate[2]) + "," +
              quote_literal(coordinate[3]) + "," +
              quote_literal(coordinate[4]) + ")",
          "failed to insert projection coordinate row");
    }
    sqlite_exec(db,
                "INSERT INTO traceloom_metadata(key, value) VALUES"
                "('analytical_projection_contract', "
                "'scope_population_resolution_domain_lens_coordinates_v2'),"
                "('raw_source_database_count', " +
                    quote_literal(std::to_string(packaging.sources.size())) +
                    "),('raw_table_count', " +
                    quote_literal(std::to_string(packaging.tables.size())) +
                    ")",
                "failed to add augmented catalog metadata");
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
