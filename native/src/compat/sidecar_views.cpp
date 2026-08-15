#include "sidecar_views.h"

#include <stdexcept>
#include <string>

#include "traceloom/compat/schema.h"
#include "traceloom/compat/sidecar_writer.h"
#include "sqlite_support.h"

namespace traceloom::compat {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
namespace {

void materialize_event_reconciliation_view(SqliteDb& db) {
  db.exec("DROP VIEW IF EXISTS traceloom_v_event_reconciliation");
  db.exec(
      "CREATE VIEW traceloom_v_event_reconciliation AS SELECT "
      "m.*, d.policy_id, d.policy_version, d.rule_id, d.status, "
      "d.reason_code, d.canonical_event_id, d.envelope_event_id, "
      "d.canonical_anchor_id, d.canonical_start_ns, d.canonical_end_ns, "
      "d.contained_fraction, e.symbol AS observed_symbol, "
      "e.start_ns AS observed_start_ns, e.end_ns AS observed_end_ns, "
      "e.dur_us AS observed_dur_us, a.symbol AS canonical_symbol, "
      "a.start_ns AS canonical_anchor_start_ns, "
      "a.end_ns AS canonical_anchor_end_ns, "
      "a.dur_us AS canonical_anchor_dur_us "
      "FROM traceloom_event_reconciliation_member m "
      "JOIN traceloom_event_reconciliation_decision d ON "
      "d.decision_id = m.decision_id AND d.db_idx = m.db_idx "
      "LEFT JOIN traceloom_event e ON e.event_id = m.event_id AND "
      "e.db_idx = m.db_idx LEFT JOIN traceloom_anchor a ON "
      "a.anchor_id = d.canonical_anchor_id AND a.db_idx = d.db_idx");
}

}  // namespace
#endif

void materialize_structural_compatibility_views(const std::string& sqlite_path) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(sqlite_path);

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    drop_structural_compatibility_views(db);
    materialize_structural_compatibility_indexes(db);
    materialize_runtime_device_views(db);
    materialize_structure_bubble_views(db);
    materialize_event_reconciliation_view(db);
    materialize_cuda_graph_views(db);
    materialize_exact_graph_views(db);
    materialize_replay_cost_views(db);
    materialize_tree_node_anchor_view(db);
    materialize_tree_node_occurrence_view(db);
    materialize_node_cost_views(db);
    materialize_tree_node_view(db);
    materialize_symbol_normalization_views(db);
    materialize_semantic_tree_views(db);
    // The queryable DB is a read-mostly analytical artifact. Persist planner
    // statistics after all materialized relations and indexes exist so agent
    // drill-downs begin with selective interval/identity indexes instead of a
    // cold full scan.
    db.exec("ANALYZE traceloom_runtime_call");
    db.exec("ANALYZE traceloom_runtime_device_relation");
    db.exec("ANALYZE traceloom_anchor_runtime_relation");
    db.exec("ANALYZE traceloom_anchor_host_interval");
    db.exec("ANALYZE traceloom_anchor_host_activity");
    db.exec("ANALYZE traceloom_structure_bubble_occurrence");
    db.exec("ANALYZE traceloom_structure_bubble_api_occurrence");
    db.exec("ANALYZE traceloom_structure_bubble_api_stats");
    db.exec("COMMIT");
  } catch (...) {
    try {
      db.exec("ROLLBACK");
    } catch (...) {
    }
    throw;
  }
#else
  (void)sqlite_path;
  throw std::runtime_error(
      "compatibility sidecar writer requires SQLite support");
#endif
}

void materialize_global_collective_compatibility_schema(
    const std::string& sqlite_path) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(sqlite_path,
                                   global_collective_table_schemas());

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    materialize_global_collective_indexes(db);
    db.exec("COMMIT");
  } catch (...) {
    try {
      db.exec("ROLLBACK");
    } catch (...) {
    }
    throw;
  }
#else
  (void)sqlite_path;
  throw std::runtime_error(
      "compatibility sidecar writer requires SQLite support");
#endif
}

}  // namespace traceloom::compat
