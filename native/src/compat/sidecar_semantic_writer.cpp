#include "traceloom/compat/sidecar_writer.h"

#include <stdexcept>
#include <string>

#include "sidecar_row_bindings.h"
#include "sidecar_views.h"
#include "sqlite_support.h"

namespace traceloom::compat {

void replace_semantic_tree_catalog_rows(
    const std::string& sqlite_path,
    const std::vector<SemanticTreeHeaderSqlRow>& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(sqlite_path,
                                   {semantic_tree_table_schema(),
                                    semantic_node_table_schema(),
                                    semantic_edge_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_semantic_tree");

    SqliteStmt tree_stmt(
        db.get(),
        "INSERT INTO traceloom_semantic_tree ("
        "tree_id, db_idx, device_id, view_name, tree_kind, stem, "
        "root_node_id, schema_version, semantic_projection, macro_discovery, "
        "readable_macro_mode, auxiliary_attribution, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const SemanticTreeHeaderSqlRow& row : rows) {
      insert_semantic_tree_header_row(tree_stmt, row);
    }

    materialize_semantic_tree_views(db);
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
  (void)rows;
  throw std::runtime_error(
      "compatibility sidecar writer requires SQLite support");
#endif
}

void replace_semantic_graph_rows(const std::string& sqlite_path,
                                 const SemanticGraphSqlRows& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(sqlite_path,
                                   {semantic_tree_table_schema(),
                                    semantic_node_table_schema(),
                                    semantic_edge_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_semantic_edge");
    db.exec("DELETE FROM traceloom_semantic_node");

    SqliteStmt node_stmt(
        db.get(),
        "INSERT INTO traceloom_semantic_node ("
        "node_id, tree_id, db_idx, device_id, view_name, tree_kind, "
        "local_node_id, parent_node_id, parent_local_node_id, preorder_idx, "
        "sibling_order, path, depth, display_depth, loop_depth, node_type, "
        "semantic_kind, symbol, label, category, repeat_count, "
        "occurrence_count, anchor_count, first_anchor_idx, last_anchor_idx, "
        "start_ns, end_ns, compute_us, comm_us, idle_us, total_us, "
        "avg_compute_us, avg_comm_us, avg_idle_us, avg_total_us, self_us, "
        "aux_event_count, aux_us, hidden_aux_event_count, hidden_aux_us, "
        "raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const SemanticNodeSqlRow& row : rows.nodes) {
      insert_semantic_node_row(node_stmt, row);
    }

    SqliteStmt edge_stmt(
        db.get(),
        "INSERT INTO traceloom_semantic_edge ("
        "parent_node_id, child_node_id, tree_id, db_idx, device_id, "
        "view_name, tree_kind, edge_order, edge_kind, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const SemanticEdgeSqlRow& row : rows.edges) {
      insert_semantic_edge_row(edge_stmt, row);
    }

    materialize_semantic_tree_views(db);
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
  (void)rows;
  throw std::runtime_error(
      "compatibility sidecar writer requires SQLite support");
#endif
}

void replace_semantic_tree_rows(const std::string& sqlite_path,
                                const SemanticTreeSqlRows& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(sqlite_path,
                                   {semantic_tree_table_schema(),
                                    semantic_node_table_schema(),
                                    semantic_edge_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_semantic_edge");
    db.exec("DELETE FROM traceloom_semantic_node");
    db.exec("DELETE FROM traceloom_semantic_tree");

    SqliteStmt tree_stmt(
        db.get(),
        "INSERT INTO traceloom_semantic_tree ("
        "tree_id, db_idx, device_id, view_name, tree_kind, stem, "
        "root_node_id, schema_version, semantic_projection, macro_discovery, "
        "readable_macro_mode, auxiliary_attribution, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const SemanticTreeHeaderSqlRow& row : rows.trees) {
      insert_semantic_tree_header_row(tree_stmt, row);
    }

    SqliteStmt node_stmt(
        db.get(),
        "INSERT INTO traceloom_semantic_node ("
        "node_id, tree_id, db_idx, device_id, view_name, tree_kind, "
        "local_node_id, parent_node_id, parent_local_node_id, preorder_idx, "
        "sibling_order, path, depth, display_depth, loop_depth, node_type, "
        "semantic_kind, symbol, label, category, repeat_count, "
        "occurrence_count, anchor_count, first_anchor_idx, last_anchor_idx, "
        "start_ns, end_ns, compute_us, comm_us, idle_us, total_us, "
        "avg_compute_us, avg_comm_us, avg_idle_us, avg_total_us, self_us, "
        "aux_event_count, aux_us, hidden_aux_event_count, hidden_aux_us, "
        "raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const SemanticNodeSqlRow& row : rows.nodes) {
      insert_semantic_node_row(node_stmt, row);
    }

    SqliteStmt edge_stmt(
        db.get(),
        "INSERT INTO traceloom_semantic_edge ("
        "parent_node_id, child_node_id, tree_id, db_idx, device_id, "
        "view_name, tree_kind, edge_order, edge_kind, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const SemanticEdgeSqlRow& row : rows.edges) {
      insert_semantic_edge_row(edge_stmt, row);
    }

    materialize_semantic_tree_views(db);
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
  (void)rows;
  throw std::runtime_error(
      "compatibility sidecar writer requires SQLite support");
#endif
}


}  // namespace traceloom::compat
