#include "traceloom/compat/structural_position_rows.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#include "sqlite_support.h"
#include "traceloom/analysis/structural_position_model.h"
#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/schema.h"
#include "traceloom/compat/sidecar_writer.h"

namespace traceloom::compat {
namespace {

std::string position_id(const StructuralNodeDef& def,
                        std::uint32_t device_id,
                        bool scope_by_device) {
  return scope_by_device
             ? "node-d" + std::to_string(device_id) + "-" + def.local_node_id
             : "node-" + def.local_node_id;
}

std::string occurrence_id(const std::string& position,
                          std::uint32_t occurrence_idx) {
  return position + "-occurrence-" + std::to_string(occurrence_idx);
}

std::string edge_role_id(std::uint32_t db_idx,
                         std::uint32_t device_id,
                         const std::string& tree_id,
                         const std::string& parent_tree_path,
                         std::uint32_t role_ordinal) {
  return "edge-role:d" + std::to_string(db_idx) + ":dev" +
         std::to_string(device_id) + ":" + tree_id + ":" +
         parent_tree_path + ":e" + std::to_string(role_ordinal);
}

std::string edge_id(std::uint32_t db_idx,
                    std::uint32_t device_id,
                    const std::string& tree_id,
                    const std::string& parent_occurrence_id,
                    std::uint32_t edge_order) {
  return "edge:d" + std::to_string(db_idx) + ":dev" +
         std::to_string(device_id) + ":" + tree_id + ":" +
         parent_occurrence_id + ":" + std::to_string(edge_order);
}

struct MemberAddress {
  std::uint32_t slot_ordinal = 0;
  std::uint32_t member_order = 0;
};

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)

void finish_row(SqliteStmt& stmt, const char* context) {
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string(context) + ": " +
                             sqlite3_errmsg(stmt.db()));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void create_indexes_and_views(SqliteDb& db) {
  db.exec(R"SQL(
CREATE UNIQUE INDEX IF NOT EXISTS idx_traceloom_position_refinement_coordinate
  ON traceloom_position_refinement(
    db_idx, tree_id, parent_position_id, slot_ordinal);
CREATE UNIQUE INDEX IF NOT EXISTS idx_traceloom_position_occurrence_identity
  ON traceloom_position_occurrence(db_idx, tree_id, occurrence_id);
CREATE INDEX IF NOT EXISTS idx_traceloom_position_occurrence_population
  ON traceloom_position_occurrence(
    db_idx, tree_id, position_id, occurrence_idx);
CREATE UNIQUE INDEX IF NOT EXISTS idx_traceloom_position_member_coordinate
  ON traceloom_position_member(
    db_idx, tree_id, parent_occurrence_id, slot_ordinal, member_order);
CREATE INDEX IF NOT EXISTS idx_traceloom_position_member_child
  ON traceloom_position_member(db_idx, tree_id, child_occurrence_id);
CREATE UNIQUE INDEX IF NOT EXISTS idx_traceloom_execution_tree_edge_identity
  ON traceloom_execution_tree_edge(db_idx, tree_id, edge_id);
CREATE UNIQUE INDEX IF NOT EXISTS idx_traceloom_execution_tree_edge_order
  ON traceloom_execution_tree_edge(
    db_idx, tree_id, parent_occurrence_id, edge_order);
CREATE INDEX IF NOT EXISTS idx_traceloom_execution_tree_edge_role
  ON traceloom_execution_tree_edge(
    db_idx, tree_id, edge_role_id, parent_occurrence_idx, edge_order);
CREATE INDEX IF NOT EXISTS idx_traceloom_execution_tree_edge_role_lookup
  ON traceloom_execution_tree_edge(
    edge_role_id, parent_occurrence_id, parent_occurrence_idx, edge_order);
CREATE INDEX IF NOT EXISTS idx_traceloom_execution_tree_edge_parent_lookup
  ON traceloom_execution_tree_edge(parent_occurrence_id, edge_order);
CREATE INDEX IF NOT EXISTS idx_traceloom_execution_tree_edge_parent_position
  ON traceloom_execution_tree_edge(
    db_idx, tree_id, parent_position_id, parent_tree_path, edge_order);
CREATE INDEX IF NOT EXISTS idx_traceloom_execution_tree_edge_child
  ON traceloom_execution_tree_edge(
    db_idx, tree_id, child_occurrence_id);
CREATE UNIQUE INDEX IF NOT EXISTS idx_traceloom_execution_tree_edge_role_id
  ON traceloom_execution_tree_edge_role(db_idx, tree_id, edge_role_id);
CREATE INDEX IF NOT EXISTS idx_traceloom_execution_tree_edge_role_lookup_id
  ON traceloom_execution_tree_edge_role(edge_role_id);
CREATE INDEX IF NOT EXISTS idx_traceloom_execution_tree_edge_role_parent
  ON traceloom_execution_tree_edge_role(
    db_idx, tree_id, parent_position_id, parent_tree_path, first_edge_order);
CREATE INDEX IF NOT EXISTS idx_traceloom_semantic_node_coordinate
  ON traceloom_semantic_node(
    node_id, db_idx, device_id, tree_id, view_name);

DROP VIEW IF EXISTS traceloom_v_position;
CREATE VIEW traceloom_v_position AS
SELECT
  node_id AS position_id, tree_id, db_idx, device_id, view_name,
  local_node_id, parent_node_id AS display_parent_position_id,
  preorder_idx, sibling_order, path AS display_path, depth, display_depth,
  loop_depth, node_type, semantic_kind, symbol, label, category, repeat_count,
  occurrence_count, anchor_count, start_ns, end_ns, compute_us, comm_us,
  idle_us, total_us, self_us, aux_us
FROM traceloom_semantic_node;

DROP VIEW IF EXISTS traceloom_v_position_occurrence;
CREATE VIEW traceloom_v_position_occurrence AS
SELECT o.*, p.node_type, p.semantic_kind, p.symbol, p.label, p.category
FROM traceloom_position_occurrence o
JOIN traceloom_v_position p
  ON p.position_id = o.position_id
 AND p.db_idx = o.db_idx
 AND p.tree_id = o.tree_id;

DROP VIEW IF EXISTS traceloom_v_position_member;
CREATE VIEW traceloom_v_position_member AS
SELECT
  m.*, a.event_id, e.symbol AS terminal_symbol, e.role AS terminal_role,
  e.semantic_role AS terminal_semantic_role, e.stream_id, e.start_ns,
  e.end_ns, e.dur_us
FROM traceloom_position_member m
LEFT JOIN traceloom_anchor a
  ON a.anchor_id = m.terminal_anchor_id
 AND a.db_idx = m.db_idx
 AND a.device_id = m.device_id
LEFT JOIN traceloom_event e
  ON e.event_id = a.event_id
 AND e.db_idx = a.db_idx
 AND e.device_id = a.device_id;

DROP VIEW IF EXISTS traceloom_v_tree_edge_cost;
DROP VIEW IF EXISTS traceloom_v_tree_edge_role;
DROP VIEW IF EXISTS traceloom_v_tree_edge;
CREATE VIEW traceloom_v_tree_edge AS
SELECT
  edge.edge_id, edge.edge_role_id, edge.edge_order,
  edge.edge_ordinal_in_role,
  edge.db_idx, edge.device_id, edge.tree_id, edge.view_name,
  tree.semantic_projection AS cost_view_name,
  edge.parent_position_id, edge.parent_tree_path,
  edge.parent_occurrence_id, edge.parent_occurrence_idx,
  edge.parent_repeat_iteration, edge.parent_occurrence_path,
  parent.label AS parent_label,
  edge.child_position_id, edge.child_occurrence_id,
  edge.child_occurrence_idx, edge.child_repeat_iteration,
  edge.child_token_start_ordinal, edge.child_token_end_exclusive,
  edge.child_occurrence_path,
  child.node_type AS edge_node_type,
  child.semantic_kind AS edge_semantic_kind,
  child.symbol AS edge_symbol, child.label AS edge_label,
  child.category AS edge_category
FROM traceloom_execution_tree_edge edge
JOIN traceloom_semantic_tree tree
  ON tree.tree_id = edge.tree_id
 AND tree.db_idx = edge.db_idx
 AND tree.device_id = edge.device_id
 AND tree.view_name = edge.view_name
JOIN traceloom_semantic_node parent
  ON parent.node_id = edge.parent_position_id
 AND parent.db_idx = edge.db_idx
 AND parent.device_id = edge.device_id
 AND parent.tree_id = edge.tree_id
 AND parent.view_name = edge.view_name
JOIN traceloom_semantic_node child
  ON child.node_id = edge.child_position_id
 AND child.db_idx = edge.db_idx
 AND child.device_id = edge.device_id
 AND child.tree_id = edge.tree_id
 AND child.view_name = edge.view_name;

CREATE VIEW traceloom_v_tree_edge_cost AS
SELECT edge.*,
  CASE WHEN EXISTS (
    SELECT 1 FROM traceloom_viz_node_anchor member
    WHERE member.node_id = edge.child_position_id
      AND member.db_idx = edge.db_idx
      AND member.device_id = edge.device_id
      AND member.view_name = edge.cost_view_name
      AND member.occurrence_idx = edge.child_occurrence_idx)
    THEN 'supported' ELSE 'missing' END AS child_cost_support,
  (SELECT round(sum(member.compute_us), 3)
   FROM traceloom_viz_node_anchor member
   WHERE member.node_id = edge.child_position_id
     AND member.db_idx = edge.db_idx
     AND member.device_id = edge.device_id
     AND member.view_name = edge.cost_view_name
     AND member.occurrence_idx = edge.child_occurrence_idx) AS child_compute_us,
  (SELECT round(sum(member.comm_us), 3)
   FROM traceloom_viz_node_anchor member
   WHERE member.node_id = edge.child_position_id
     AND member.db_idx = edge.db_idx
     AND member.device_id = edge.device_id
     AND member.view_name = edge.cost_view_name
     AND member.occurrence_idx = edge.child_occurrence_idx) AS child_comm_us,
  (SELECT round(sum(member.idle_us), 3)
   FROM traceloom_viz_node_anchor member
   WHERE member.node_id = edge.child_position_id
     AND member.db_idx = edge.db_idx
     AND member.device_id = edge.device_id
     AND member.view_name = edge.cost_view_name
     AND member.occurrence_idx = edge.child_occurrence_idx) AS child_uncovered_us,
  (SELECT round(sum(member.total_us), 3)
   FROM traceloom_viz_node_anchor member
   WHERE member.node_id = edge.child_position_id
     AND member.db_idx = edge.db_idx
     AND member.device_id = edge.device_id
     AND member.view_name = edge.cost_view_name
     AND member.occurrence_idx = edge.child_occurrence_idx) AS child_total_us,
  (SELECT round(sum(member.self_us), 3)
   FROM traceloom_viz_node_anchor member
   WHERE member.node_id = edge.child_position_id
     AND member.db_idx = edge.db_idx
     AND member.device_id = edge.device_id
     AND member.view_name = edge.cost_view_name
     AND member.occurrence_idx = edge.child_occurrence_idx) AS child_self_us,
  (SELECT round(sum(member.aux_us), 3)
   FROM traceloom_viz_node_anchor member
   WHERE member.node_id = edge.child_position_id
     AND member.db_idx = edge.db_idx
     AND member.device_id = edge.device_id
     AND member.view_name = edge.cost_view_name
     AND member.occurrence_idx = edge.child_occurrence_idx) AS child_aux_us
FROM traceloom_v_tree_edge edge;

CREATE VIEW traceloom_v_tree_edge_role AS
SELECT role.edge_role_id, role.db_idx, role.device_id, role.tree_id,
       role.view_name, role.parent_position_id, role.parent_tree_path,
       parent.label AS parent_label, role.child_position_id,
       child.node_type AS edge_node_type,
       child.semantic_kind AS edge_semantic_kind,
       child.symbol AS edge_symbol, child.label AS edge_label,
       child.category AS edge_category,
       role.parent_occurrence_count, role.concrete_edge_count,
       role.edges_per_parent_min, role.edges_per_parent_max,
       role.population_support, role.first_edge_order
FROM traceloom_execution_tree_edge_role role
JOIN traceloom_semantic_node parent
  ON parent.node_id = role.parent_position_id
 AND parent.db_idx = role.db_idx
 AND parent.device_id = role.device_id
 AND parent.tree_id = role.tree_id
 AND parent.view_name = role.view_name
JOIN traceloom_semantic_node child
  ON child.node_id = role.child_position_id
 AND child.db_idx = role.db_idx
 AND child.device_id = role.device_id
 AND child.tree_id = role.tree_id
 AND child.view_name = role.view_name;
)SQL");
}

#endif

}  // namespace

StructuralPositionSqlRows build_structural_position_sql_rows(
    const StructuralOccurrenceGraph& graph,
    const std::vector<StructuralProjectionToken>& tokens,
    std::uint32_t db_idx,
    std::string tree_id,
    std::string view_name,
    bool scope_position_ids_by_device) {
  const StructuralPositionModel model = build_structural_position_model(
      graph, static_cast<std::uint32_t>(tokens.size()));
  const std::uint32_t device_id = tokens.empty() ? 0 : tokens.front().device_id;
  StructuralPositionSqlRows out;

  for (const StructuralPositionRefinement& row : model.refinements) {
    out.refinements.push_back(PositionRefinementSqlRow{
        position_id(structural_node_def(graph, row.parent_position_id),
                    device_id, scope_position_ids_by_device),
        row.slot_ordinal,
        position_id(structural_node_def(graph, row.child_position_id),
                    device_id, scope_position_ids_by_device),
        db_idx, device_id, tree_id, view_name});
  }

  std::map<StructuralNodeOccurrenceId::value_type, MemberAddress>
      address_by_child;
  for (const StructuralPositionMember& row : model.members) {
    if (row.kind == StructuralPositionMemberKind::kChildOccurrence) {
      address_by_child.emplace(
          row.child_occurrence_id.value(),
          MemberAddress{row.slot_ordinal, row.member_order});
    }
  }

  std::map<StructuralNodeOccurrenceId::value_type, std::string>
      occurrence_ids;
  std::map<StructuralNodeOccurrenceId::value_type, std::string>
      rooted_position_paths;
  std::map<StructuralNodeOccurrenceId::value_type, std::string>
      occurrence_paths;
  for (const StructuralNodeOccurrence& occurrence : graph.occurrences) {
    const StructuralNodeDef& def =
        structural_node_def(graph, occurrence.node_def_id);
    const std::string position =
        position_id(def, device_id, scope_position_ids_by_device);
    const std::uint32_t occurrence_idx = occurrence.occurrence_index_for_def + 1;
    const std::string occurrence_name = occurrence_id(position, occurrence_idx);
    std::string parent_occurrence;
    std::string rooted_path = position;
    std::string measured_path = occurrence_name;
    if (occurrence.parent_occurrence_id.valid()) {
      const auto parent_name =
          occurrence_ids.find(occurrence.parent_occurrence_id.value());
      const auto parent_root =
          rooted_position_paths.find(occurrence.parent_occurrence_id.value());
      const auto parent_path =
          occurrence_paths.find(occurrence.parent_occurrence_id.value());
      const auto address = address_by_child.find(occurrence.id.value());
      if (parent_name == occurrence_ids.end() ||
          parent_root == rooted_position_paths.end() ||
          parent_path == occurrence_paths.end() ||
          address == address_by_child.end()) {
        throw std::invalid_argument(
            "structural Position occurrence path is not parent-first");
      }
      parent_occurrence = parent_name->second;
      rooted_path = parent_root->second + "/s" +
                    std::to_string(address->second.slot_ordinal) + "/" +
                    position;
      measured_path = parent_path->second + "/s" +
                      std::to_string(address->second.slot_ordinal) + "/r" +
                      std::to_string(address->second.member_order) + "/" +
                      occurrence_name;
    }
    occurrence_ids.emplace(occurrence.id.value(), occurrence_name);
    rooted_position_paths.emplace(occurrence.id.value(), rooted_path);
    occurrence_paths.emplace(occurrence.id.value(), measured_path);
    out.occurrences.push_back(PositionOccurrenceSqlRow{
        occurrence_name,
        position,
        parent_occurrence,
        db_idx,
        device_id,
        tree_id,
        view_name,
        occurrence_idx,
        occurrence.repeat_iteration,
        occurrence.token_start_ordinal,
        occurrence.token_end_ordinal,
        rooted_path,
        measured_path});
  }

  std::vector<const StructuralNodeOccurrence*> concrete_children;
  concrete_children.reserve(graph.occurrences.size());
  for (const StructuralNodeOccurrence& occurrence : graph.occurrences) {
    if (occurrence.parent_occurrence_id.valid()) {
      concrete_children.push_back(&occurrence);
    }
  }
  std::sort(concrete_children.begin(), concrete_children.end(),
            [](const StructuralNodeOccurrence* lhs,
               const StructuralNodeOccurrence* rhs) {
              if (lhs->parent_occurrence_id != rhs->parent_occurrence_id) {
                return lhs->parent_occurrence_id < rhs->parent_occurrence_id;
              }
              if (lhs->edge_order != rhs->edge_order) {
                return lhs->edge_order < rhs->edge_order;
              }
              return lhs->id < rhs->id;
            });
  std::map<std::pair<StructuralNodeOccurrenceId::value_type, std::uint32_t>,
           std::uint32_t>
      next_ordinal_by_role;
  for (const StructuralNodeOccurrence* child_ptr : concrete_children) {
    const StructuralNodeOccurrence& child = *child_ptr;
    const StructuralNodeOccurrence& parent = structural_node_occurrence(
        graph, child.parent_occurrence_id);
    const auto address = address_by_child.find(child.id.value());
    if (address == address_by_child.end()) {
      throw std::invalid_argument(
          "execution tree edge has no structural role address");
    }
    const auto role_key =
        std::make_pair(parent.id.value(), address->second.slot_ordinal);
    const std::uint32_t ordinal_in_role = ++next_ordinal_by_role[role_key];
    if (ordinal_in_role != address->second.member_order) {
      throw std::invalid_argument(
          "execution tree edge role rank disagrees with measured order");
    }
    const std::string& parent_occurrence =
        occurrence_ids.at(parent.id.value());
    const std::string& parent_tree_path =
        rooted_position_paths.at(parent.id.value());
    const StructuralNodeDef& parent_def =
        structural_node_def(graph, parent.node_def_id);
    const StructuralNodeDef& child_def =
        structural_node_def(graph, child.node_def_id);
    out.edges.push_back(ExecutionTreeEdgeSqlRow{
        edge_id(db_idx, device_id, tree_id, parent_occurrence,
                child.edge_order),
        edge_role_id(db_idx, device_id, tree_id, parent_tree_path,
                     address->second.slot_ordinal),
        child.edge_order,
        ordinal_in_role,
        position_id(parent_def, device_id, scope_position_ids_by_device),
        parent_tree_path,
        parent_occurrence,
        parent.occurrence_index_for_def + 1,
        parent.repeat_iteration,
        occurrence_paths.at(parent.id.value()),
        position_id(child_def, device_id, scope_position_ids_by_device),
        occurrence_ids.at(child.id.value()),
        child.occurrence_index_for_def + 1,
        child.repeat_iteration,
        child.token_start_ordinal,
        child.token_end_ordinal,
        occurrence_paths.at(child.id.value()),
        db_idx,
        device_id,
        tree_id,
        view_name});
  }

  for (const StructuralPositionMember& row : model.members) {
    const StructuralNodeOccurrence& parent =
        structural_node_occurrence(graph, row.parent_occurrence_id);
    const std::string parent_position = position_id(
        structural_node_def(graph, parent.node_def_id), device_id,
        scope_position_ids_by_device);
    const std::string parent_occurrence =
        occurrence_ids.at(row.parent_occurrence_id.value());
    PositionMemberSqlRow sql_row;
    sql_row.parent_position_id = parent_position;
    sql_row.parent_occurrence_id = parent_occurrence;
    sql_row.slot_ordinal = row.slot_ordinal;
    sql_row.member_order = row.member_order;
    sql_row.member_kind = structural_position_member_kind_name(row.kind);
    sql_row.db_idx = db_idx;
    sql_row.device_id = device_id;
    sql_row.tree_id = tree_id;
    sql_row.view_name = view_name;
    if (row.kind == StructuralPositionMemberKind::kChildOccurrence) {
      sql_row.child_position_id = position_id(
          structural_node_def(graph, row.child_position_id), device_id,
          scope_position_ids_by_device);
      sql_row.child_occurrence_id =
          occurrence_ids.at(row.child_occurrence_id.value());
      sql_row.member_path =
          occurrence_paths.at(row.child_occurrence_id.value());
    } else {
      if (row.terminal_token_ordinal >= tokens.size()) {
        throw std::invalid_argument(
            "terminal Position member references an invalid token");
      }
      sql_row.terminal_token_ordinal = row.terminal_token_ordinal;
      const StructuralProjectionToken& token = tokens[row.terminal_token_ordinal];
      if (token.anchor_id.valid()) {
        sql_row.terminal_anchor_id = anchor_compat_id(token.anchor_id);
      }
      sql_row.member_path =
          occurrence_paths.at(row.parent_occurrence_id.value()) + "/terminal/r" +
                            std::to_string(row.member_order) + "/token-" +
                            std::to_string(row.terminal_token_ordinal);
    }
    out.members.push_back(std::move(sql_row));
  }
  return out;
}

void replace_structural_position_rows(
    const std::string& sqlite_path, const StructuralPositionSqlRows& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(
      sqlite_path,
      {position_refinement_table_schema(), position_occurrence_table_schema(),
       position_member_table_schema(), execution_tree_edge_table_schema(),
       execution_tree_edge_role_table_schema(),
       semantic_tree_table_schema(), semantic_node_table_schema(),
       anchor_table_schema(), event_table_schema()});
  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_position_member");
    db.exec("DELETE FROM traceloom_execution_tree_edge");
    db.exec("DELETE FROM traceloom_execution_tree_edge_role");
    db.exec("DELETE FROM traceloom_position_occurrence");
    db.exec("DELETE FROM traceloom_position_refinement");

    SqliteStmt refinement_stmt(
        db.get(), "INSERT INTO traceloom_position_refinement VALUES "
                  "(?,?,?,?,?,?,?)");
    for (const PositionRefinementSqlRow& row : rows.refinements) {
      int column = 1;
      bind_text(refinement_stmt, column++, row.parent_position_id);
      bind_int64(refinement_stmt, column++, row.slot_ordinal);
      bind_text(refinement_stmt, column++, row.child_position_id);
      bind_int64(refinement_stmt, column++, row.db_idx);
      bind_int64(refinement_stmt, column++, row.device_id);
      bind_text(refinement_stmt, column++, row.tree_id);
      bind_text(refinement_stmt, column++, row.view_name);
      finish_row(refinement_stmt, "failed to insert Position refinement");
    }

    SqliteStmt occurrence_stmt(
        db.get(), "INSERT INTO traceloom_position_occurrence VALUES "
                  "(?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const PositionOccurrenceSqlRow& row : rows.occurrences) {
      int column = 1;
      bind_text(occurrence_stmt, column++, row.occurrence_id);
      bind_text(occurrence_stmt, column++, row.position_id);
      if (row.parent_occurrence_id.empty()) {
        bind_null(occurrence_stmt, column++);
      } else {
        bind_text(occurrence_stmt, column++, row.parent_occurrence_id);
      }
      bind_int64(occurrence_stmt, column++, row.db_idx);
      bind_int64(occurrence_stmt, column++, row.device_id);
      bind_text(occurrence_stmt, column++, row.tree_id);
      bind_text(occurrence_stmt, column++, row.view_name);
      bind_int64(occurrence_stmt, column++, row.occurrence_idx);
      bind_int64(occurrence_stmt, column++, row.repeat_iteration);
      bind_int64(occurrence_stmt, column++, row.token_start_ordinal);
      bind_int64(occurrence_stmt, column++, row.token_end_exclusive);
      bind_text(occurrence_stmt, column++, row.rooted_position_path);
      bind_text(occurrence_stmt, column++, row.occurrence_path);
      finish_row(occurrence_stmt, "failed to insert Position occurrence");
    }

    SqliteStmt member_stmt(
        db.get(), "INSERT INTO traceloom_position_member VALUES "
                  "(?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const PositionMemberSqlRow& row : rows.members) {
      int column = 1;
      bind_text(member_stmt, column++, row.parent_position_id);
      bind_text(member_stmt, column++, row.parent_occurrence_id);
      bind_int64(member_stmt, column++, row.slot_ordinal);
      bind_int64(member_stmt, column++, row.member_order);
      bind_text(member_stmt, column++, row.member_kind);
      row.child_position_id.empty()
          ? bind_null(member_stmt, column++)
          : bind_text(member_stmt, column++, row.child_position_id);
      row.child_occurrence_id.empty()
          ? bind_null(member_stmt, column++)
          : bind_text(member_stmt, column++, row.child_occurrence_id);
      row.terminal_token_ordinal < 0
          ? bind_null(member_stmt, column++)
          : bind_int64(member_stmt, column++, row.terminal_token_ordinal);
      row.terminal_anchor_id.empty()
          ? bind_null(member_stmt, column++)
          : bind_text(member_stmt, column++, row.terminal_anchor_id);
      bind_int64(member_stmt, column++, row.db_idx);
      bind_int64(member_stmt, column++, row.device_id);
      bind_text(member_stmt, column++, row.tree_id);
      bind_text(member_stmt, column++, row.view_name);
      bind_text(member_stmt, column++, row.member_path);
      finish_row(member_stmt, "failed to insert Position member");
    }

    SqliteStmt edge_stmt(
        db.get(), "INSERT INTO traceloom_execution_tree_edge VALUES "
                  "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const ExecutionTreeEdgeSqlRow& row : rows.edges) {
      int column = 1;
      bind_text(edge_stmt, column++, row.edge_id);
      bind_text(edge_stmt, column++, row.edge_role_id);
      bind_int64(edge_stmt, column++, row.edge_order);
      bind_int64(edge_stmt, column++, row.edge_ordinal_in_role);
      bind_text(edge_stmt, column++, row.parent_position_id);
      bind_text(edge_stmt, column++, row.parent_tree_path);
      bind_text(edge_stmt, column++, row.parent_occurrence_id);
      bind_int64(edge_stmt, column++, row.parent_occurrence_idx);
      bind_int64(edge_stmt, column++, row.parent_repeat_iteration);
      bind_text(edge_stmt, column++, row.parent_occurrence_path);
      bind_text(edge_stmt, column++, row.child_position_id);
      bind_text(edge_stmt, column++, row.child_occurrence_id);
      bind_int64(edge_stmt, column++, row.child_occurrence_idx);
      bind_int64(edge_stmt, column++, row.child_repeat_iteration);
      bind_int64(edge_stmt, column++, row.child_token_start_ordinal);
      bind_int64(edge_stmt, column++, row.child_token_end_exclusive);
      bind_text(edge_stmt, column++, row.child_occurrence_path);
      bind_int64(edge_stmt, column++, row.db_idx);
      bind_int64(edge_stmt, column++, row.device_id);
      bind_text(edge_stmt, column++, row.tree_id);
      bind_text(edge_stmt, column++, row.view_name);
      finish_row(edge_stmt, "failed to insert execution tree edge");
    }
    db.exec(R"SQL(
INSERT INTO traceloom_execution_tree_edge_role
WITH per_parent AS (
  SELECT edge_role_id, parent_position_id, parent_tree_path,
         child_position_id, parent_occurrence_id,
         count(*) AS edge_count, min(edge_order) AS first_edge_order,
         db_idx, device_id, tree_id, view_name
  FROM traceloom_execution_tree_edge
  GROUP BY edge_role_id, parent_position_id, parent_tree_path,
           child_position_id, parent_occurrence_id,
           db_idx, device_id, tree_id, view_name
)
SELECT edge_role_id, parent_position_id, parent_tree_path, child_position_id,
       count(*) AS parent_occurrence_count,
       sum(edge_count) AS concrete_edge_count,
       min(edge_count) AS edges_per_parent_min,
       max(edge_count) AS edges_per_parent_max,
       CASE WHEN min(edge_count) = max(edge_count)
            THEN 'uniform' ELSE 'nonuniform' END AS population_support,
       min(first_edge_order), db_idx, device_id, tree_id, view_name
FROM per_parent
GROUP BY edge_role_id, parent_position_id, parent_tree_path,
         child_position_id, db_idx, device_id, tree_id, view_name;
)SQL");
    create_indexes_and_views(db);
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
