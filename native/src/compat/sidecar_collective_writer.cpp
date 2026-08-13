#include "traceloom/compat/sidecar_writer.h"

#include <stdexcept>
#include <string>

#include "sidecar_row_bindings.h"
#include "sidecar_views.h"
#include "sqlite_support.h"

namespace traceloom::compat {

void replace_collective_global_link_rows(
    const std::string& sqlite_path,
    const std::vector<CollectiveGlobalLinkSqlRow>& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(sqlite_path,
                                   {collective_global_link_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_collective_global_link");
    SqliteStmt stmt(
        db.get(),
        "INSERT INTO traceloom_collective_global_link ("
        "candidate_collective_key, db_name, db_idx, device_id, member_id, "
        "pair_id, local_node_id, occurrence_idx, idx_in_occurrence, op_type, "
        "anchor_id, event_id, source_table, source_key, connection_id, op_id, "
        "start_ns, end_ns, dur_us, validation_status, confidence"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?)");
    for (const CollectiveGlobalLinkSqlRow& row : rows) {
      insert_collective_global_link_row(stmt, row);
    }
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

void replace_global_collective_summary_rows(
    const std::string& sqlite_path,
    const std::vector<GlobalCollectiveSummarySqlRow>& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_global_collective_compatibility_schema(sqlite_path);

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_global_collective_summary");

    SqliteStmt summary_stmt(
        db.get(),
        "INSERT INTO traceloom_global_collective_summary ("
        "candidate_collective_key, pair_id, occurrence_idx, op_type, "
        "idx_in_occurrence, member_count, expected_world_size, start_skew_us, "
        "duration_skew_us, connection_ids, op_ids, members, missing_members, "
        "validation_status, confidence"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const GlobalCollectiveSummarySqlRow& row : rows) {
      insert_global_collective_summary_row(summary_stmt, row);
    }

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

void replace_global_collective_member_rows(
    const std::string& sqlite_path,
    const std::vector<GlobalCollectiveMemberSqlRow>& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_global_collective_compatibility_schema(sqlite_path);

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_global_collective_member");

    SqliteStmt member_stmt(
        db.get(),
        "INSERT INTO traceloom_global_collective_member ("
        "candidate_collective_key, db_name, db_idx, device_id, member_id, "
        "pair_id, local_node_id, occurrence_idx, idx_in_occurrence, op_type, "
        "anchor_id, event_id, source_table, source_key, connection_id, op_id, "
        "start_ns, end_ns, dur_us, validation_status, confidence"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?)");
    for (const GlobalCollectiveMemberSqlRow& row : rows) {
      insert_global_collective_member_row(member_stmt, row);
    }

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

void replace_global_collective_rows(const std::string& sqlite_path,
                                    const GlobalCollectiveSqlRows& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_global_collective_compatibility_schema(sqlite_path);

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_global_collective_member");
    db.exec("DELETE FROM traceloom_global_collective_summary");

    SqliteStmt summary_stmt(
        db.get(),
        "INSERT INTO traceloom_global_collective_summary ("
        "candidate_collective_key, pair_id, occurrence_idx, op_type, "
        "idx_in_occurrence, member_count, expected_world_size, start_skew_us, "
        "duration_skew_us, connection_ids, op_ids, members, missing_members, "
        "validation_status, confidence"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const GlobalCollectiveSummarySqlRow& row : rows.summaries) {
      insert_global_collective_summary_row(summary_stmt, row);
    }

    SqliteStmt member_stmt(
        db.get(),
        "INSERT INTO traceloom_global_collective_member ("
        "candidate_collective_key, db_name, db_idx, device_id, member_id, "
        "pair_id, local_node_id, occurrence_idx, idx_in_occurrence, op_type, "
        "anchor_id, event_id, source_table, source_key, connection_id, op_id, "
        "start_ns, end_ns, dur_us, validation_status, confidence"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?)");
    for (const GlobalCollectiveMemberSqlRow& row : rows.members) {
      insert_global_collective_member_row(member_stmt, row);
    }

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
