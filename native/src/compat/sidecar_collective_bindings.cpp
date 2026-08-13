#include "sidecar_row_bindings.h"

#include <stdexcept>
#include <string>

namespace traceloom::compat {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)

void insert_collective_global_link_row(
    SqliteStmt& stmt,
    const CollectiveGlobalLinkSqlRow& row) {
  bind_text(stmt, 1, row.candidate_collective_key);
  bind_text(stmt, 2, row.db_name);
  bind_int64(stmt, 3, row.db_idx);
  bind_int64(stmt, 4, row.device_id);
  bind_text(stmt, 5, row.member_id);
  bind_text(stmt, 6, row.pair_id);
  bind_text(stmt, 7, row.local_node_id);
  bind_int64(stmt, 8, row.occurrence_idx);
  bind_int64(stmt, 9, row.idx_in_occurrence);
  bind_text(stmt, 10, row.op_type);
  bind_text(stmt, 11, row.anchor_id);
  bind_text(stmt, 12, row.event_id);
  bind_text(stmt, 13, row.source_table);
  bind_text(stmt, 14, row.source_key);
  bind_text(stmt, 15, row.connection_id);
  bind_text(stmt, 16, row.op_id);
  bind_int64(stmt, 17, row.start_ns);
  bind_int64(stmt, 18, row.end_ns);
  bind_double(stmt, 19, row.dur_us);
  bind_text(stmt, 20, row.validation_status);
  bind_double(stmt, 21, row.confidence);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility collective link row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_global_collective_summary_row(
    SqliteStmt& stmt,
    const GlobalCollectiveSummarySqlRow& row) {
  bind_text(stmt, 1, row.candidate_collective_key);
  bind_text(stmt, 2, row.pair_id);
  bind_int64(stmt, 3, row.occurrence_idx);
  bind_text(stmt, 4, row.op_type);
  bind_int64(stmt, 5, row.idx_in_occurrence);
  bind_int64(stmt, 6, row.member_count);
  bind_int64(stmt, 7, row.expected_world_size);
  bind_double(stmt, 8, row.start_skew_us);
  bind_double(stmt, 9, row.duration_skew_us);
  bind_text(stmt, 10, row.connection_ids);
  bind_text(stmt, 11, row.op_ids);
  bind_text(stmt, 12, row.members);
  bind_text(stmt, 13, row.missing_members);
  bind_text(stmt, 14, row.validation_status);
  bind_double(stmt, 15, row.confidence);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility global collective summary row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_global_collective_member_row(
    SqliteStmt& stmt,
    const GlobalCollectiveMemberSqlRow& row) {
  bind_text(stmt, 1, row.candidate_collective_key);
  bind_text(stmt, 2, row.db_name);
  bind_int64(stmt, 3, row.db_idx);
  bind_int64(stmt, 4, row.device_id);
  bind_text(stmt, 5, row.member_id);
  bind_text(stmt, 6, row.pair_id);
  bind_text(stmt, 7, row.local_node_id);
  bind_int64(stmt, 8, row.occurrence_idx);
  bind_int64(stmt, 9, row.idx_in_occurrence);
  bind_text(stmt, 10, row.op_type);
  bind_text(stmt, 11, row.anchor_id);
  bind_text(stmt, 12, row.event_id);
  bind_text(stmt, 13, row.source_table);
  bind_text(stmt, 14, row.source_key);
  bind_text(stmt, 15, row.connection_id);
  bind_text(stmt, 16, row.op_id);
  bind_int64(stmt, 17, row.start_ns);
  bind_int64(stmt, 18, row.end_ns);
  bind_double(stmt, 19, row.dur_us);
  bind_text(stmt, 20, row.validation_status);
  bind_double(stmt, 21, row.confidence);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility global collective member row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

#endif

}  // namespace traceloom::compat
