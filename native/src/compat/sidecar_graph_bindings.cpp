#include "sidecar_row_bindings.h"

#include <stdexcept>
#include <string>

namespace traceloom::compat {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)

void insert_graph_replay_row(SqliteStmt& stmt, const GraphReplaySqlRow& row) {
  bind_text(stmt, 1, row.graph_event_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_int64(stmt, 3, row.device_id);
  bind_text(stmt, 4, row.graph_provider);
  bind_text(stmt, 5, row.graph_kind);
  bind_int64(stmt, 6, row.graph_event_idx);
  bind_text(stmt, 7, row.event_id);
  bind_int64(stmt, 8, row.step_idx);
  bind_int64(stmt, 9, row.stream_id);
  bind_text(stmt, 10, row.correlation_id);
  bind_text(stmt, 11, row.graph_id);
  bind_text(stmt, 12, row.graph_exec_id);
  bind_text(stmt, 13, row.context_id);
  bind_int64(stmt, 14, row.start_ns);
  bind_int64(stmt, 15, row.end_ns);
  bind_double(stmt, 16, row.dur_us);
  bind_int64(stmt, 17, row.enclosed_event_count);
  bind_double(stmt, 18, row.enclosed_event_us);
  bind_int64(stmt, 19, row.enclosed_kernel_count);
  bind_double(stmt, 20, row.enclosed_kernel_us);
  bind_text(stmt, 21, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility graph replay row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_graph_envelope_row(SqliteStmt& stmt,
                               const GraphEnvelopeSqlRow& row) {
  bind_text(stmt, 1, row.envelope_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_int64(stmt, 3, row.device_id);
  bind_text(stmt, 4, row.graph_provider);
  bind_text(stmt, 5, row.graph_kind);
  bind_int64(stmt, 6, row.envelope_idx);
  bind_text(stmt, 7, row.graph_event_id);
  bind_text(stmt, 8, row.child_event_id);
  bind_int64(stmt, 9, row.graph_step_idx);
  bind_int64(stmt, 10, row.child_step_idx);
  bind_text(stmt, 11, row.relation);
  bind_text(stmt, 12, row.stream_relation);
  bind_text(stmt, 13, row.graph_id);
  bind_text(stmt, 14, row.graph_exec_id);
  bind_text(stmt, 15, row.graph_correlation_id);
  bind_int64(stmt, 16, row.graph_start_ns);
  bind_int64(stmt, 17, row.graph_end_ns);
  bind_int64(stmt, 18, row.child_start_ns);
  bind_int64(stmt, 19, row.child_end_ns);
  bind_double(stmt, 20, row.start_offset_us);
  bind_double(stmt, 21, row.end_offset_us);
  bind_double(stmt, 22, row.child_dur_us);
  bind_text(stmt, 23, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility graph envelope row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_graph_reconstruction_region_row(
    SqliteStmt& stmt,
    const GraphReconstructionRegionSqlRow& row) {
  bind_text(stmt, 1, row.region_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_int64(stmt, 3, row.device_id);
  bind_text(stmt, 4, row.graph_provider);
  bind_text(stmt, 5, row.candidate_id);
  bind_int64(stmt, 6, row.region_order);
  bind_text(stmt, 7, row.status);
  bind_text(stmt, 8, row.boundary_policy);
  bind_text(stmt, 9, row.order_policy);
  bind_text(stmt, 10, row.identity_policy);
  bind_text(stmt, 11, row.shape_policy);
  bind_int64(stmt, 12, row.first_launch_occurrence_id);
  bind_int64(stmt, 13, row.last_launch_occurrence_id);
  bind_int64(stmt, 14, row.observed_launch_count);
  bind_int64(stmt, 15, row.expected_launch_count);
  bind_int64(stmt, 16, row.start_ns);
  bind_int64(stmt, 17, row.end_ns);
  bind_double(stmt, 18, row.dur_us);
  bind_text(stmt, 19, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert ACLGraph reconstruction region row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_graph_launch_row(SqliteStmt& stmt,
                              const GraphLaunchSqlRow& row) {
  bind_text(stmt, 1, row.launch_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_int64(stmt, 3, row.device_id);
  bind_text(stmt, 4, row.graph_provider);
  bind_text(stmt, 5, row.graph_event_id);
  if (row.anchor_id.empty()) {
    bind_null(stmt, 6);
  } else {
    bind_text(stmt, 6, row.anchor_id);
  }
  bind_int64(stmt, 7, row.replay_unit_id);
  bind_int64(stmt, 8, row.graph_template_id);
  bind_int64(stmt, 9, row.graph_launch_occurrence_id);
  bind_int64(stmt, 10, row.replay_body_template_id);
  bind_int64(stmt, 11, row.body_id);
  bind_int64(stmt, 12, row.member_order);
  if (row.slot_order < 0) {
    bind_null(stmt, 13);
  } else {
    bind_int64(stmt, 13, row.slot_order);
  }
  if (row.correlation_id.empty()) {
    bind_null(stmt, 14);
  } else {
    bind_text(stmt, 14, row.correlation_id);
  }
  bind_text(stmt, 15, row.match_policy);
  bind_text(stmt, 16, row.association_policy);
  bind_int64(stmt, 17, row.start_ns);
  bind_int64(stmt, 18, row.end_ns);
  bind_double(stmt, 19, row.dur_us);
  bind_text(stmt, 20, row.evidence_level);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility exact graph launch row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_graph_body_member_row(SqliteStmt& stmt,
                                  const GraphBodyMemberSqlRow& row) {
  bind_text(stmt, 1, row.member_id);
  bind_text(stmt, 2, row.launch_id);
  bind_int64(stmt, 3, row.db_idx);
  bind_int64(stmt, 4, row.device_id);
  bind_text(stmt, 5, row.graph_provider);
  bind_text(stmt, 6, row.graph_event_id);
  bind_int64(stmt, 7, row.replay_unit_id);
  bind_int64(stmt, 8, row.graph_template_id);
  bind_int64(stmt, 9, row.graph_launch_occurrence_id);
  bind_int64(stmt, 10, row.body_id);
  bind_int64(stmt, 11, row.replay_body_template_id);
  bind_int64(stmt, 12, row.member_order);
  if (row.slot_order < 0) {
    bind_null(stmt, 13);
  } else {
    bind_int64(stmt, 13, row.slot_order);
  }
  bind_int64(stmt, 14, row.lane_ordinal);
  bind_int64(stmt, 15, row.task_ordinal);
  bind_text(stmt, 16, row.kind);
  bind_text(stmt, 17, row.event_id);
  bind_int64(stmt, 18, row.task_id);
  bind_text(stmt, 19, row.source_table);
  bind_int64(stmt, 20, row.source_row_id);
  bind_int64(stmt, 21, row.raw_task_id);
  bind_int64(stmt, 22, row.start_ns);
  bind_int64(stmt, 23, row.end_ns);
  bind_double(stmt, 24, row.dur_us);
  if (row.correlation_id.empty()) {
    bind_null(stmt, 25);
  } else {
    bind_text(stmt, 25, row.correlation_id);
  }
  if (row.graph_node_id < 0) {
    bind_null(stmt, 26);
  } else {
    bind_int64(stmt, 26, row.graph_node_id);
  }
  if (row.original_graph_node_id < 0) {
    bind_null(stmt, 27);
  } else {
    bind_int64(stmt, 27, row.original_graph_node_id);
  }
  bind_text(stmt, 28, row.match_policy);
  bind_text(stmt, 29, row.association_policy);
  bind_text(stmt, 30, row.evidence_level);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility exact graph body member row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

#endif

}  // namespace traceloom::compat
