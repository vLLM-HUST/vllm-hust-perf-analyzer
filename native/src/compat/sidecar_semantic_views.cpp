#include "sidecar_views.h"

#include "sqlite_support.h"

namespace traceloom::compat {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)

void materialize_semantic_tree_views(SqliteDb& db) {
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_semantic_tree_node AS "
      "SELECT "
      "n.*, "
      "parent.local_node_id AS parent_local_id, "
      "parent.label AS parent_label, "
      "CASE WHEN COALESCE(n.total_us, 0.0) = 0.0 THEN 0.0 ELSE "
      "ROUND(COALESCE(n.comm_us, 0.0) / n.total_us, 6) END AS comm_pct, "
      "CASE WHEN COALESCE(n.total_us, 0.0) = 0.0 THEN 0.0 ELSE "
      "ROUND(COALESCE(n.idle_us, 0.0) / n.total_us, 6) END AS idle_pct "
      "FROM traceloom_semantic_node n "
      "LEFT JOIN traceloom_semantic_node parent ON "
      "parent.node_id = n.parent_node_id");
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_semantic_tree_readable AS "
      "SELECT "
      "n.tree_id, "
      "n.db_idx, "
      "n.device_id, "
      "n.view_name, "
      "n.tree_kind, "
      "n.preorder_idx, "
      "n.local_node_id, "
      "n.parent_local_node_id, "
      "n.path, "
      "n.display_depth, "
      "printf('%*s', COALESCE(n.display_depth, 0) * 2, '') || "
      "'- [' || COALESCE(NULLIF(n.path, ''), 'root') || '] ' || "
      "n.local_node_id || ' ' || "
      "CASE "
      "WHEN n.node_type = 'Repeat' THEN 'Repeat x' || "
      "COALESCE(n.repeat_count, 1) "
      "WHEN n.node_type = 'Seq' THEN 'Seq' "
      "ELSE COALESCE(NULLIF(n.node_type, ''), 'Node') "
      "END || "
      "' | ' || COALESCE(NULLIF(n.label, ''), NULLIF(n.symbol, ''), "
      "n.semantic_kind, '') || "
      "' | anchors=' || COALESCE(n.anchor_count, 0) || "
      "' total_us=' || printf('%.3f', COALESCE(n.total_us, 0.0)) || "
      "CASE WHEN COALESCE(n.hidden_aux_event_count, 0.0) > 0.0 THEN "
      "' hidden_aux=' || printf('%.0f', n.hidden_aux_event_count) || "
      "' hidden_aux_us=' || printf('%.3f', COALESCE(n.hidden_aux_us, 0.0)) "
      "ELSE '' END AS line "
      "FROM traceloom_semantic_node n");
}

#endif

}  // namespace traceloom::compat
