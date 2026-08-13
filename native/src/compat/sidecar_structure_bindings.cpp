#include "sidecar_row_bindings.h"

#include <stdexcept>
#include <string>

namespace traceloom::compat {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)

void insert_viz_node_row(SqliteStmt& stmt, const VizNodeSqlRow& row) {
  bind_text(stmt, 1, row.node_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_int64(stmt, 3, row.device_id);
  bind_text(stmt, 4, row.view_name);
  bind_text(stmt, 5, row.local_node_id);
  bind_text(stmt, 6, row.path);
  bind_text(stmt, 7, row.node_type);
  bind_text(stmt, 8, row.kind);
  bind_text(stmt, 9, row.symbol);
  bind_text(stmt, 10, row.label);
  bind_text(stmt, 11, row.category);
  bind_int64(stmt, 12, row.depth);
  bind_int64(stmt, 13, row.level);
  bind_text(stmt, 14, row.repeat_label);
  if (row.repeat_count == 0) {
    bind_null(stmt, 15);
  } else {
    bind_int64(stmt, 15, row.repeat_count);
  }
  bind_int64(stmt, 16, row.occurrence_count);
  bind_int64(stmt, 17, row.anchor_count);
  bind_double(stmt, 18, row.anchors_per_occurrence);
  bind_int64(stmt, 19, row.first_anchor_idx);
  bind_int64(stmt, 20, row.last_anchor_idx);
  bind_double(stmt, 21, row.compute_us);
  bind_double(stmt, 22, row.comm_us);
  bind_double(stmt, 23, row.idle_us);
  bind_double(stmt, 24, row.total_us);
  bind_double(stmt, 25, row.avg_compute_us);
  bind_double(stmt, 26, row.avg_comm_us);
  bind_double(stmt, 27, row.avg_idle_us);
  bind_double(stmt, 28, row.avg_total_us);
  bind_double(stmt, 29, row.self_us);
  bind_double(stmt, 30, row.aux_events);
  bind_double(stmt, 31, row.aux_us);
  bind_text(stmt, 32, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to insert compatibility viz node row: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_viz_edge_row(SqliteStmt& stmt, const VizEdgeSqlRow& row) {
  bind_text(stmt, 1, row.parent_node_id);
  bind_text(stmt, 2, row.child_node_id);
  bind_int64(stmt, 3, row.db_idx);
  bind_int64(stmt, 4, row.device_id);
  bind_text(stmt, 5, row.view_name);
  bind_int64(stmt, 6, row.edge_order);
  bind_text(stmt, 7, row.edge_kind);
  bind_text(stmt, 8, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to insert compatibility viz edge row: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_viz_node_anchor_row(SqliteStmt& stmt,
                                const VizNodeAnchorSqlRow& row) {
  bind_text(stmt, 1, row.node_id);
  bind_text(stmt, 2, row.anchor_id);
  bind_int64(stmt, 3, row.db_idx);
  bind_int64(stmt, 4, row.device_id);
  bind_text(stmt, 5, row.view_name);
  bind_int64(stmt, 6, row.occurrence_idx);
  bind_int64(stmt, 7, row.anchor_order);
  bind_text(stmt, 8, row.coverage_kind);
  bind_text(stmt, 9, row.repeat_context);
  bind_double(stmt, 10, row.compute_us);
  bind_double(stmt, 11, row.comm_us);
  bind_double(stmt, 12, row.idle_us);
  bind_double(stmt, 13, row.total_us);
  bind_double(stmt, 14, row.self_us);
  bind_double(stmt, 15, row.aux_events);
  bind_double(stmt, 16, row.aux_us);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility viz node anchor row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_anchor_primary_node_row(SqliteStmt& stmt,
                                    const AnchorPrimaryNodeSqlRow& row) {
  bind_text(stmt, 1, row.anchor_id);
  bind_text(stmt, 2, row.node_id);
  bind_int64(stmt, 3, row.db_idx);
  bind_int64(stmt, 4, row.device_id);
  bind_text(stmt, 5, row.view_name);
  bind_text(stmt, 6, row.reason);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility anchor primary node row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_loop_node_row(SqliteStmt& stmt, const LoopNodeSqlRow& row) {
  bind_text(stmt, 1, row.node_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_int64(stmt, 3, row.device_id);
  bind_text(stmt, 4, row.view_name);
  bind_int64(stmt, 5, row.loop_rank);
  bind_text(stmt, 6, row.repeat_label);
  if (row.repeat_count == 0) {
    bind_null(stmt, 7);
  } else {
    bind_int64(stmt, 7, row.repeat_count);
  }
  bind_int64(stmt, 8, row.occurrence_count);
  bind_int64(stmt, 9, row.anchor_count);
  bind_double(stmt, 10, row.total_us);
  bind_double(stmt, 11, row.avg_total_us);
  bind_double(stmt, 12, row.compute_us);
  bind_double(stmt, 13, row.comm_us);
  bind_double(stmt, 14, row.idle_us);
  bind_double(stmt, 15, row.loop_total_pct);
  bind_text(stmt, 16, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to insert compatibility loop node row: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

#endif

}  // namespace traceloom::compat
