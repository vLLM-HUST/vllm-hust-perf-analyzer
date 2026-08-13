#include "traceloom/compat/sidecar_writer.h"

#include <stdexcept>
#include <string>

#include "sidecar_row_bindings.h"
#include "sidecar_views.h"
#include "sqlite_support.h"

namespace traceloom::compat {

void replace_loop_tree_rows(const std::string& sqlite_path,
                            const LoopTreeSqlRows& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(
      sqlite_path,
      {event_table_schema(), anchor_table_schema(),
       anchor_aux_slot_table_schema(), aux_link_table_schema(),
       viz_node_table_schema(), viz_edge_table_schema(),
       viz_node_anchor_table_schema(), loop_node_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_loop_node");
    db.exec("DELETE FROM traceloom_viz_edge");
    db.exec("DELETE FROM traceloom_viz_node");

    SqliteStmt node_stmt(
        db.get(),
        "INSERT INTO traceloom_viz_node ("
        "node_id, db_idx, device_id, view_name, local_node_id, path, "
        "node_type, kind, symbol, label, category, depth, level, "
        "repeat_label, repeat_count, occurrence_count, anchor_count, "
        "anchors_per_occurrence, first_anchor_idx, last_anchor_idx, "
        "compute_us, comm_us, idle_us, total_us, avg_compute_us, "
        "avg_comm_us, avg_idle_us, avg_total_us, self_us, aux_events, aux_us, "
        "raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const VizNodeSqlRow& row : rows.nodes) {
      insert_viz_node_row(node_stmt, row);
    }

    SqliteStmt edge_stmt(
        db.get(),
        "INSERT INTO traceloom_viz_edge ("
        "parent_node_id, child_node_id, db_idx, device_id, view_name, "
        "edge_order, edge_kind, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    for (const VizEdgeSqlRow& row : rows.edges) {
      insert_viz_edge_row(edge_stmt, row);
    }

    SqliteStmt loop_node_stmt(
        db.get(),
        "INSERT INTO traceloom_loop_node ("
        "node_id, db_idx, device_id, view_name, loop_rank, repeat_label, "
        "repeat_count, occurrence_count, anchor_count, total_us, "
        "avg_total_us, compute_us, comm_us, idle_us, loop_total_pct, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const LoopNodeSqlRow& row : rows.loop_nodes) {
      insert_loop_node_row(loop_node_stmt, row);
    }

    materialize_node_cost_views(db);
    materialize_tree_node_view(db);
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

void replace_node_anchor_coverage_rows(
    const std::string& sqlite_path,
    const NodeAnchorCoverageSqlRows& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(
      sqlite_path,
      {event_table_schema(), anchor_table_schema(),
       anchor_aux_slot_table_schema(), aux_link_table_schema(),
       viz_node_table_schema(), viz_node_anchor_table_schema(),
       anchor_primary_node_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_anchor_primary_node");
    db.exec("DELETE FROM traceloom_viz_node_anchor");

    SqliteStmt node_anchor_stmt(
        db.get(),
        "INSERT INTO traceloom_viz_node_anchor ("
        "node_id, anchor_id, db_idx, device_id, view_name, occurrence_idx, "
        "anchor_order, coverage_kind, repeat_context, compute_us, comm_us, "
        "idle_us, total_us, self_us, aux_events, aux_us"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const VizNodeAnchorSqlRow& row : rows.node_anchors) {
      insert_viz_node_anchor_row(node_anchor_stmt, row);
    }

    SqliteStmt anchor_primary_stmt(
        db.get(),
        "INSERT INTO traceloom_anchor_primary_node ("
        "anchor_id, node_id, db_idx, device_id, view_name, reason"
        ") VALUES (?, ?, ?, ?, ?, ?)");
    for (const AnchorPrimaryNodeSqlRow& row : rows.anchor_primary_nodes) {
      insert_anchor_primary_node_row(anchor_primary_stmt, row);
    }

    db.exec("DROP VIEW IF EXISTS traceloom_tree_node_occurrence");
    db.exec("DROP VIEW IF EXISTS traceloom_tree_node_anchor");
    materialize_tree_node_anchor_view(db);
    materialize_tree_node_occurrence_view(db);
    materialize_node_cost_views(db);
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

void replace_node_coverage_rows(const std::string& sqlite_path,
                                const NodeCoverageSqlRows& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(
      sqlite_path,
      {event_table_schema(), anchor_table_schema(),
       anchor_aux_slot_table_schema(), aux_link_table_schema(),
       viz_node_table_schema(), viz_edge_table_schema(),
       viz_node_anchor_table_schema(), anchor_primary_node_table_schema(),
       loop_node_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_anchor_primary_node");
    db.exec("DELETE FROM traceloom_loop_node");
    db.exec("DELETE FROM traceloom_viz_node_anchor");
    db.exec("DELETE FROM traceloom_viz_edge");
    db.exec("DELETE FROM traceloom_viz_node");

    SqliteStmt node_stmt(
        db.get(),
        "INSERT INTO traceloom_viz_node ("
        "node_id, db_idx, device_id, view_name, local_node_id, path, "
        "node_type, kind, symbol, label, category, depth, level, "
        "repeat_label, repeat_count, occurrence_count, anchor_count, "
        "anchors_per_occurrence, first_anchor_idx, last_anchor_idx, "
        "compute_us, comm_us, idle_us, total_us, avg_compute_us, "
        "avg_comm_us, avg_idle_us, avg_total_us, self_us, aux_events, aux_us, "
        "raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const VizNodeSqlRow& row : rows.nodes) {
      insert_viz_node_row(node_stmt, row);
    }

    SqliteStmt edge_stmt(
        db.get(),
        "INSERT INTO traceloom_viz_edge ("
        "parent_node_id, child_node_id, db_idx, device_id, view_name, "
        "edge_order, edge_kind, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    for (const VizEdgeSqlRow& row : rows.edges) {
      insert_viz_edge_row(edge_stmt, row);
    }

    SqliteStmt node_anchor_stmt(
        db.get(),
        "INSERT INTO traceloom_viz_node_anchor ("
        "node_id, anchor_id, db_idx, device_id, view_name, occurrence_idx, "
        "anchor_order, coverage_kind, repeat_context, compute_us, comm_us, "
        "idle_us, total_us, self_us, aux_events, aux_us"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const VizNodeAnchorSqlRow& row : rows.node_anchors) {
      insert_viz_node_anchor_row(node_anchor_stmt, row);
    }

    SqliteStmt anchor_primary_stmt(
        db.get(),
        "INSERT INTO traceloom_anchor_primary_node ("
        "anchor_id, node_id, db_idx, device_id, view_name, reason"
        ") VALUES (?, ?, ?, ?, ?, ?)");
    for (const AnchorPrimaryNodeSqlRow& row : rows.anchor_primary_nodes) {
      insert_anchor_primary_node_row(anchor_primary_stmt, row);
    }

    SqliteStmt loop_node_stmt(
        db.get(),
        "INSERT INTO traceloom_loop_node ("
        "node_id, db_idx, device_id, view_name, loop_rank, repeat_label, "
        "repeat_count, occurrence_count, anchor_count, total_us, "
        "avg_total_us, compute_us, comm_us, idle_us, loop_total_pct, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const LoopNodeSqlRow& row : rows.loop_nodes) {
      insert_loop_node_row(loop_node_stmt, row);
    }

    db.exec("DROP VIEW IF EXISTS traceloom_tree_node_occurrence");
    db.exec("DROP VIEW IF EXISTS traceloom_tree_node_anchor");
    materialize_tree_node_anchor_view(db);
    materialize_tree_node_occurrence_view(db);
    materialize_node_cost_views(db);
    materialize_tree_node_view(db);
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
