#include "sidecar_views.h"

#include <stdexcept>
#include <string>

#include "traceloom/compat/schema.h"
#include "traceloom/compat/sidecar_writer.h"
#include "sqlite_support.h"

namespace traceloom::compat {

void materialize_report_compatibility_views(const std::string& sqlite_path) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(sqlite_path);

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    drop_report_compatibility_views(db);
    materialize_report_compatibility_indexes(db);
    materialize_runtime_device_views(db);
    materialize_structure_bubble_views(db);
    materialize_cuda_graph_views(db);
    materialize_exact_graph_views(db);
    materialize_replay_cost_views(db);
    materialize_tree_node_anchor_view(db);
    materialize_tree_node_occurrence_view(db);
    materialize_node_cost_views(db);
    materialize_tree_node_view(db);
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
