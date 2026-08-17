#include "sidecar_row_bindings.h"

#include <stdexcept>
#include <string>

namespace traceloom::compat {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)

void insert_metadata_row(SqliteStmt& stmt, const MetadataSqlRow& row) {
  bind_borrowed_text(stmt, 1, row.key);
  bind_borrowed_text(stmt, 2, row.value);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility metadata row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_anchor_cost_breakdown_row(SqliteStmt& stmt,
                                      const AnchorCostBreakdownSqlRow& row) {
  bind_int64(stmt, 1, row.anchor_idx);
  bind_borrowed_text(stmt, 2, row.symbol);
  bind_borrowed_text(stmt, 3, row.anchor_kind);
  bind_double(stmt, 4, row.total_us);
  bind_double(stmt, 5, row.self_us);
  bind_double(stmt, 6, row.aux_us);
  bind_double(stmt, 7, row.graph_child_us);
  bind_double(stmt, 8, row.residual_us);
  bind_int64(stmt, 9, row.raw_child_task_count);
  bind_borrowed_text(stmt, 10, row.top_ops);
  bind_borrowed_text(stmt, 11, row.diagnostic_flags);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to insert compatibility sidecar row: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_event_row(SqliteStmt& stmt, const EventSqlRow& row) {
  bind_borrowed_text(stmt, 1, row.event_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_int64(stmt, 3, row.device_id);
  bind_int64(stmt, 4, row.step_idx);
  bind_borrowed_text(stmt, 5, row.source_table);
  bind_borrowed_text(stmt, 6, row.source_key);
  bind_int64(stmt, 7, row.stream_id);
  bind_int64(stmt, 8, row.start_ns);
  bind_int64(stmt, 9, row.end_ns);
  bind_double(stmt, 10, row.dur_us);
  bind_borrowed_text(stmt, 11, row.category);
  bind_borrowed_text(stmt, 12, row.role);
  bind_borrowed_text(stmt, 13, row.semantic_role);
  bind_borrowed_text(stmt, 14, row.semantic_role_reason);
  bind_borrowed_text(stmt, 15, row.symbol);
  bind_borrowed_text(stmt, 16, row.label);
  bind_borrowed_text(stmt, 17, row.raw_label);
  bind_borrowed_text(stmt, 18, row.op_type);
  bind_borrowed_text(stmt, 19, row.compute_task_type);
  bind_borrowed_text(stmt, 20, row.family);
  bind_borrowed_text(stmt, 21, row.task_type);
  bind_borrowed_text(stmt, 22, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to insert compatibility event row: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_event_source_row(SqliteStmt& stmt, const EventSourceSqlRow& row) {
  bind_borrowed_text(stmt, 1, row.event_id);
  bind_int64(stmt, 2, row.source_ordinal);
  bind_int64(stmt, 3, row.db_idx);
  bind_int64(stmt, 4, row.device_id);
  bind_borrowed_text(stmt, 5, row.source_table);
  bind_borrowed_text(stmt, 6, row.source_key);
  bind_borrowed_text(stmt, 7, row.source_role);
  bind_borrowed_text(stmt, 8, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility event source row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_runtime_call_row(SqliteStmt& stmt, const RuntimeCallSqlRow& row) {
  bind_borrowed_text(stmt, 1, row.runtime_call_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_borrowed_text(stmt, 3, row.provider);
  bind_borrowed_text(stmt, 4, row.clock_domain);
  bind_borrowed_text(stmt, 5, row.source_table);
  bind_borrowed_text(stmt, 6, row.source_key);
  bind_int64(stmt, 7, row.start_ns);
  bind_int64(stmt, 8, row.end_ns);
  bind_double(stmt, 9, row.dur_us);
  bind_nullable_borrowed_text(stmt, 10, row.api_name);
  bind_nullable_borrowed_text(stmt, 11, row.api_type);
  bind_nullable_borrowed_text(stmt, 12, row.process_id);
  bind_nullable_borrowed_text(stmt, 13, row.thread_id);
  bind_nullable_borrowed_text(stmt, 14, row.global_tid);
  bind_nullable_borrowed_text(stmt, 15, row.context_id);
  bind_nullable_borrowed_text(stmt, 16, row.device_id);
  bind_nullable_borrowed_text(stmt, 17, row.correlation_id);
  bind_borrowed_text(stmt, 18, row.match_policy);
  bind_nullable_borrowed_text(stmt, 19, row.raw_json);
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to insert runtime call row: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_device_work_row(SqliteStmt& stmt, const DeviceWorkSqlRow& row) {
  bind_borrowed_text(stmt, 1, row.device_work_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_borrowed_text(stmt, 3, row.provider);
  bind_int64(stmt, 4, row.device_id);
  bind_borrowed_text(stmt, 5, row.work_kind);
  bind_nullable_borrowed_text(stmt, 6, row.event_id);
  bind_nullable_borrowed_text(stmt, 7, row.task_id);
  if (row.graph_launch_occurrence_id < 0) {
    bind_null(stmt, 8);
  } else {
    bind_int64(stmt, 8, row.graph_launch_occurrence_id);
  }
  bind_borrowed_text(stmt, 9, row.source_table);
  bind_borrowed_text(stmt, 10, row.source_key);
  bind_int64(stmt, 11, row.start_ns);
  bind_int64(stmt, 12, row.end_ns);
  bind_double(stmt, 13, row.dur_us);
  bind_nullable_borrowed_text(stmt, 14, row.symbol);
  bind_nullable_borrowed_text(stmt, 15, row.raw_json);
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to insert device work row: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_runtime_device_relation_row(
    SqliteStmt& stmt, const RuntimeDeviceRelationSqlRow& row) {
  bind_borrowed_text(stmt, 1, row.relation_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_nullable_borrowed_text(stmt, 3, row.runtime_call_id);
  bind_nullable_borrowed_text(stmt, 4, row.device_work_id);
  bind_borrowed_text(stmt, 5, row.relation_kind);
  bind_borrowed_text(stmt, 6, row.match_policy);
  bind_borrowed_text(stmt, 7, row.evidence_level);
  bind_borrowed_text(stmt, 8, row.support_state);
  bind_borrowed_text(stmt, 9, row.cardinality);
  bind_int64(stmt, 10, row.runtime_candidate_count);
  bind_int64(stmt, 11, row.device_candidate_count);
  bind_nullable_borrowed_text(stmt, 12, row.correlation_id);
  bind_nullable_borrowed_text(stmt, 13, row.raw_json);
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to insert runtime/device relation row: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_anchor_runtime_relation_row(
    SqliteStmt& stmt, const AnchorRuntimeRelationSqlRow& row) {
  bind_borrowed_text(stmt, 1, row.anchor_id);
  bind_borrowed_text(stmt, 2, row.relation_id);
  bind_nullable_borrowed_text(stmt, 3, row.runtime_call_id);
  bind_borrowed_text(stmt, 4, row.device_work_id);
  bind_borrowed_text(stmt, 5, row.endpoint_kind);
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to insert anchor/runtime relation row: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_anchor_host_interval_row(
    SqliteStmt& stmt, const AnchorHostIntervalSqlRow& row) {
  bind_borrowed_text(stmt, 1, row.interval_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_int64(stmt, 3, row.device_id);
  bind_borrowed_text(stmt, 4, row.left_anchor_id);
  bind_borrowed_text(stmt, 5, row.right_anchor_id);
  bind_nullable_borrowed_text(stmt, 6, row.left_runtime_call_id);
  bind_nullable_borrowed_text(stmt, 7, row.right_runtime_call_id);
  bind_int64(stmt, 8, row.left_endpoint_count);
  bind_int64(stmt, 9, row.right_endpoint_count);
  bind_nullable_borrowed_text(stmt, 10, row.provider);
  bind_nullable_borrowed_text(stmt, 11, row.clock_domain);
  bind_nullable_int64_text(stmt, 12, row.host_start_ns);
  bind_nullable_int64_text(stmt, 13, row.host_end_ns);
  bind_borrowed_text(stmt, 14, row.scope_policy);
  bind_nullable_borrowed_text(stmt, 15, row.process_id);
  bind_nullable_borrowed_text(stmt, 16, row.thread_id);
  bind_borrowed_text(stmt, 17, row.support_state);
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to insert anchor host interval row: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_anchor_row(SqliteStmt& stmt, const AnchorSqlRow& row) {
  bind_borrowed_text(stmt, 1, row.anchor_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_int64(stmt, 3, row.device_id);
  bind_int64(stmt, 4, row.anchor_idx);
  bind_borrowed_text(stmt, 5, row.event_id);
  bind_int64(stmt, 6, row.step_idx);
  bind_borrowed_text(stmt, 7, row.symbol);
  bind_borrowed_text(stmt, 8, row.role);
  bind_borrowed_text(stmt, 9, row.label);
  bind_borrowed_text(stmt, 10, row.family);
  bind_int64(stmt, 11, row.start_ns);
  bind_int64(stmt, 12, row.end_ns);
  bind_double(stmt, 13, row.dur_us);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to insert compatibility anchor row: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_aux_link_row(SqliteStmt& stmt, const AuxLinkSqlRow& row) {
  bind_borrowed_text(stmt, 1, row.anchor_id);
  bind_borrowed_text(stmt, 2, row.aux_event_id);
  bind_int64(stmt, 3, row.db_idx);
  bind_int64(stmt, 4, row.device_id);
  bind_int64(stmt, 5, row.aux_order);
  bind_int64(stmt, 6, row.aux_step_idx);
  bind_borrowed_text(stmt, 7, row.link_type);
  bind_borrowed_text(stmt, 8, row.reason);
  bind_borrowed_text(stmt, 9, row.aux_kind);
  bind_double(stmt, 10, row.aux_dur_us);
  bind_borrowed_text(stmt, 11, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to insert compatibility aux link row: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_anchor_aux_slot_row(SqliteStmt& stmt,
                                const AnchorAuxSlotSqlRow& row) {
  bind_borrowed_text(stmt, 1, row.anchor_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_int64(stmt, 3, row.device_id);
  bind_int64(stmt, 4, row.anchor_idx);
  bind_int64(stmt, 5, row.anchor_step_idx);
  bind_int64(stmt, 6, row.aux_start_step_idx);
  bind_int64(stmt, 7, row.aux_end_step_idx);
  bind_int64(stmt, 8, row.aux_event_count);
  bind_double(stmt, 9, row.aux_dur_us);
  bind_borrowed_text(stmt, 10, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility anchor aux slot row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

#endif

}  // namespace traceloom::compat
