#include "traceloom/compat/structural_position_rows.h"

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
       position_member_table_schema(), semantic_node_table_schema(),
       anchor_table_schema(), event_table_schema()});
  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_position_member");
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
