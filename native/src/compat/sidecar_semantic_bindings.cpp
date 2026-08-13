#include "sidecar_row_bindings.h"

#include <stdexcept>
#include <string>

namespace traceloom::compat {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)

void insert_semantic_tree_header_row(SqliteStmt& stmt,
                                     const SemanticTreeHeaderSqlRow& row) {
  bind_text(stmt, 1, row.tree_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_int64(stmt, 3, row.device_id);
  bind_text(stmt, 4, row.view_name);
  bind_text(stmt, 5, row.tree_kind);
  bind_text(stmt, 6, row.stem);
  bind_text(stmt, 7, row.root_node_id);
  bind_text(stmt, 8, row.schema_version);
  bind_text(stmt, 9, row.semantic_projection);
  bind_text(stmt, 10, row.macro_discovery);
  bind_text(stmt, 11, row.readable_macro_mode);
  bind_text(stmt, 12, row.auxiliary_attribution);
  bind_text(stmt, 13, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility semantic tree row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_semantic_node_row(SqliteStmt& stmt, const SemanticNodeSqlRow& row) {
  bind_text(stmt, 1, row.node_id);
  bind_text(stmt, 2, row.tree_id);
  bind_int64(stmt, 3, row.db_idx);
  bind_int64(stmt, 4, row.device_id);
  bind_text(stmt, 5, row.view_name);
  bind_text(stmt, 6, row.tree_kind);
  bind_text(stmt, 7, row.local_node_id);
  bind_text(stmt, 8, row.parent_node_id);
  bind_text(stmt, 9, row.parent_local_node_id);
  bind_int64(stmt, 10, row.preorder_idx);
  bind_int64(stmt, 11, row.sibling_order);
  bind_text(stmt, 12, row.path);
  bind_int64(stmt, 13, row.depth);
  bind_int64(stmt, 14, row.display_depth);
  bind_int64(stmt, 15, row.loop_depth);
  bind_text(stmt, 16, row.node_type);
  bind_text(stmt, 17, row.semantic_kind);
  bind_text(stmt, 18, row.symbol);
  bind_text(stmt, 19, row.label);
  bind_text(stmt, 20, row.category);
  if (row.repeat_count == 0) {
    bind_null(stmt, 21);
  } else {
    bind_int64(stmt, 21, row.repeat_count);
  }
  bind_int64(stmt, 22, row.occurrence_count);
  bind_int64(stmt, 23, row.anchor_count);
  bind_int64(stmt, 24, row.first_anchor_idx);
  bind_int64(stmt, 25, row.last_anchor_idx);
  bind_int64(stmt, 26, row.start_ns);
  bind_int64(stmt, 27, row.end_ns);
  bind_double(stmt, 28, row.compute_us);
  bind_double(stmt, 29, row.comm_us);
  bind_double(stmt, 30, row.idle_us);
  bind_double(stmt, 31, row.total_us);
  bind_double(stmt, 32, row.avg_compute_us);
  bind_double(stmt, 33, row.avg_comm_us);
  bind_double(stmt, 34, row.avg_idle_us);
  bind_double(stmt, 35, row.avg_total_us);
  bind_double(stmt, 36, row.self_us);
  bind_double(stmt, 37, row.aux_event_count);
  bind_double(stmt, 38, row.aux_us);
  bind_double(stmt, 39, row.hidden_aux_event_count);
  bind_double(stmt, 40, row.hidden_aux_us);
  bind_text(stmt, 41, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility semantic node row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_semantic_edge_row(SqliteStmt& stmt, const SemanticEdgeSqlRow& row) {
  bind_text(stmt, 1, row.parent_node_id);
  bind_text(stmt, 2, row.child_node_id);
  bind_text(stmt, 3, row.tree_id);
  bind_int64(stmt, 4, row.db_idx);
  bind_int64(stmt, 5, row.device_id);
  bind_text(stmt, 6, row.view_name);
  bind_text(stmt, 7, row.tree_kind);
  bind_int64(stmt, 8, row.edge_order);
  bind_text(stmt, 9, row.edge_kind);
  bind_text(stmt, 10, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility semantic edge row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

#endif

}  // namespace traceloom::compat
