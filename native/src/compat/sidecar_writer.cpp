#include "traceloom/compat/sidecar_writer.h"

#include <stdexcept>
#include <string>

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
#include <sqlite3.h>
#endif

namespace traceloom::compat {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
namespace {

class SqliteDb {
 public:
  explicit SqliteDb(const std::string& path) {
    sqlite3* raw = nullptr;
    const int rc =
        sqlite3_open_v2(path.c_str(), &raw,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    db_ = raw;
    if (rc != SQLITE_OK) {
      const std::string message =
          db_ == nullptr ? "unknown sqlite open error" : sqlite3_errmsg(db_);
      if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
      }
      throw std::runtime_error("failed to open compatibility sidecar: " +
                               message);
    }
  }

  ~SqliteDb() {
    if (db_ != nullptr) {
      sqlite3_close(db_);
    }
  }

  SqliteDb(const SqliteDb&) = delete;
  SqliteDb& operator=(const SqliteDb&) = delete;

  void exec(const std::string& sql) {
    char* error = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
      const std::string message =
          error == nullptr ? sqlite3_errmsg(db_) : error;
      sqlite3_free(error);
      throw std::runtime_error("failed to materialize compatibility sidecar: " +
                               message);
    }
  }

  sqlite3* get() const noexcept { return db_; }

 private:
  sqlite3* db_ = nullptr;
};

class SqliteStmt {
 public:
  SqliteStmt(sqlite3* db, const std::string& sql) : db_(db) {
    sqlite3_stmt* raw = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &raw, nullptr);
    stmt_ = raw;
    if (rc != SQLITE_OK) {
      throw std::runtime_error("failed to prepare compatibility sidecar SQL: " +
                               std::string(sqlite3_errmsg(db_)));
    }
  }

  ~SqliteStmt() {
    if (stmt_ != nullptr) {
      sqlite3_finalize(stmt_);
    }
  }

  SqliteStmt(const SqliteStmt&) = delete;
  SqliteStmt& operator=(const SqliteStmt&) = delete;

  sqlite3_stmt* get() const noexcept { return stmt_; }
  sqlite3* db() const noexcept { return db_; }

 private:
  sqlite3* db_ = nullptr;
  sqlite3_stmt* stmt_ = nullptr;
};

bool table_has_column(sqlite3* db,
                      const std::string& table,
                      const std::string& column) {
  SqliteStmt stmt(db, "PRAGMA table_info(" + table + ")");
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
    const unsigned char* raw_name = sqlite3_column_text(stmt.get(), 1);
    if (raw_name != nullptr &&
        column == reinterpret_cast<const char*>(raw_name)) {
      return true;
    }
  }
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to inspect compatibility table: " +
                             std::string(sqlite3_errmsg(db)));
  }
  return false;
}

void ensure_viz_node_anchor_cost_columns(SqliteDb& db) {
  static const char* const columns[] = {
      "compute_us", "comm_us",   "idle_us",   "total_us",
      "self_us",    "aux_events", "aux_us",
  };
  for (const char* column : columns) {
    if (table_has_column(db.get(), "traceloom_viz_node_anchor", column)) {
      continue;
    }
    db.exec(
        std::string("ALTER TABLE traceloom_viz_node_anchor ADD COLUMN ") +
        column + " REAL NOT NULL DEFAULT 0.0");
  }
}

void bind_text(SqliteStmt& stmt, int column, const std::string& value) {
  const int rc = sqlite3_bind_text(stmt.get(), column, value.c_str(), -1,
                                   SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    throw std::runtime_error("failed to bind compatibility sidecar text: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
}

void bind_int64(SqliteStmt& stmt, int column, sqlite3_int64 value) {
  const int rc = sqlite3_bind_int64(stmt.get(), column, value);
  if (rc != SQLITE_OK) {
    throw std::runtime_error("failed to bind compatibility sidecar integer: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
}

void bind_null(SqliteStmt& stmt, int column) {
  const int rc = sqlite3_bind_null(stmt.get(), column);
  if (rc != SQLITE_OK) {
    throw std::runtime_error("failed to bind compatibility sidecar null: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
}

void bind_double(SqliteStmt& stmt, int column, double value) {
  const int rc = sqlite3_bind_double(stmt.get(), column, value);
  if (rc != SQLITE_OK) {
    throw std::runtime_error("failed to bind compatibility sidecar real: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
}

void insert_metadata_row(SqliteStmt& stmt, const MetadataSqlRow& row) {
  bind_text(stmt, 1, row.key);
  bind_text(stmt, 2, row.value);

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
  bind_text(stmt, 2, row.symbol);
  bind_text(stmt, 3, row.anchor_kind);
  bind_double(stmt, 4, row.total_us);
  bind_double(stmt, 5, row.self_us);
  bind_double(stmt, 6, row.aux_us);
  bind_double(stmt, 7, row.graph_child_us);
  bind_double(stmt, 8, row.residual_us);
  bind_int64(stmt, 9, row.raw_child_task_count);
  bind_text(stmt, 10, row.top_ops);
  bind_text(stmt, 11, row.diagnostic_flags);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to insert compatibility sidecar row: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_event_row(SqliteStmt& stmt, const EventSqlRow& row) {
  bind_text(stmt, 1, row.event_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_int64(stmt, 3, row.device_id);
  bind_int64(stmt, 4, row.step_idx);
  bind_text(stmt, 5, row.source_table);
  bind_text(stmt, 6, row.source_key);
  bind_int64(stmt, 7, row.stream_id);
  bind_int64(stmt, 8, row.start_ns);
  bind_int64(stmt, 9, row.end_ns);
  bind_double(stmt, 10, row.dur_us);
  bind_text(stmt, 11, row.category);
  bind_text(stmt, 12, row.role);
  bind_text(stmt, 13, row.semantic_role);
  bind_text(stmt, 14, row.semantic_role_reason);
  bind_text(stmt, 15, row.symbol);
  bind_text(stmt, 16, row.label);
  bind_text(stmt, 17, row.raw_label);
  bind_text(stmt, 18, row.op_type);
  bind_text(stmt, 19, row.compute_task_type);
  bind_text(stmt, 20, row.family);
  bind_text(stmt, 21, row.task_type);
  bind_text(stmt, 22, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to insert compatibility event row: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_event_source_row(SqliteStmt& stmt, const EventSourceSqlRow& row) {
  bind_text(stmt, 1, row.event_id);
  bind_int64(stmt, 2, row.source_ordinal);
  bind_int64(stmt, 3, row.db_idx);
  bind_int64(stmt, 4, row.device_id);
  bind_text(stmt, 5, row.source_table);
  bind_text(stmt, 6, row.source_key);
  bind_text(stmt, 7, row.source_role);
  bind_text(stmt, 8, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility event source row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_anchor_row(SqliteStmt& stmt, const AnchorSqlRow& row) {
  bind_text(stmt, 1, row.anchor_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_int64(stmt, 3, row.device_id);
  bind_int64(stmt, 4, row.anchor_idx);
  bind_text(stmt, 5, row.event_id);
  bind_int64(stmt, 6, row.step_idx);
  bind_text(stmt, 7, row.symbol);
  bind_text(stmt, 8, row.role);
  bind_text(stmt, 9, row.label);
  bind_text(stmt, 10, row.family);
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
  bind_text(stmt, 1, row.anchor_id);
  bind_text(stmt, 2, row.aux_event_id);
  bind_int64(stmt, 3, row.db_idx);
  bind_int64(stmt, 4, row.device_id);
  bind_int64(stmt, 5, row.aux_order);
  bind_int64(stmt, 6, row.aux_step_idx);
  bind_text(stmt, 7, row.link_type);
  bind_text(stmt, 8, row.reason);
  bind_text(stmt, 9, row.aux_kind);
  bind_double(stmt, 10, row.aux_dur_us);
  bind_text(stmt, 11, row.raw_json);

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
  bind_text(stmt, 1, row.anchor_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_int64(stmt, 3, row.device_id);
  bind_int64(stmt, 4, row.anchor_idx);
  bind_int64(stmt, 5, row.anchor_step_idx);
  bind_int64(stmt, 6, row.aux_start_step_idx);
  bind_int64(stmt, 7, row.aux_end_step_idx);
  bind_int64(stmt, 8, row.aux_event_count);
  bind_double(stmt, 9, row.aux_dur_us);
  bind_text(stmt, 10, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility anchor aux slot row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_viz_node_row(SqliteStmt& stmt, const VizNodeSqlRow& row) {
  bind_text(stmt, 1, row.node_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_int64(stmt, 3, row.device_id);
  bind_text(stmt, 4, row.view_name);
  bind_text(stmt, 5, row.local_node_id);
  bind_text(stmt, 6, row.path);
  bind_text(stmt, 7, row.node_type);
  bind_text(stmt, 8, row.kind);
  bind_text(stmt, 9, row.symbol);
  bind_text(stmt, 10, row.label);
  bind_text(stmt, 11, row.category);
  bind_int64(stmt, 12, row.depth);
  bind_int64(stmt, 13, row.level);
  bind_text(stmt, 14, row.repeat_label);
  if (row.repeat_count == 0) {
    bind_null(stmt, 15);
  } else {
    bind_int64(stmt, 15, row.repeat_count);
  }
  bind_int64(stmt, 16, row.occurrence_count);
  bind_int64(stmt, 17, row.anchor_count);
  bind_double(stmt, 18, row.anchors_per_occurrence);
  bind_int64(stmt, 19, row.first_anchor_idx);
  bind_int64(stmt, 20, row.last_anchor_idx);
  bind_double(stmt, 21, row.compute_us);
  bind_double(stmt, 22, row.comm_us);
  bind_double(stmt, 23, row.idle_us);
  bind_double(stmt, 24, row.total_us);
  bind_double(stmt, 25, row.avg_compute_us);
  bind_double(stmt, 26, row.avg_comm_us);
  bind_double(stmt, 27, row.avg_idle_us);
  bind_double(stmt, 28, row.avg_total_us);
  bind_double(stmt, 29, row.self_us);
  bind_double(stmt, 30, row.aux_events);
  bind_double(stmt, 31, row.aux_us);
  bind_text(stmt, 32, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to insert compatibility viz node row: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_viz_edge_row(SqliteStmt& stmt, const VizEdgeSqlRow& row) {
  bind_text(stmt, 1, row.parent_node_id);
  bind_text(stmt, 2, row.child_node_id);
  bind_int64(stmt, 3, row.db_idx);
  bind_int64(stmt, 4, row.device_id);
  bind_text(stmt, 5, row.view_name);
  bind_int64(stmt, 6, row.edge_order);
  bind_text(stmt, 7, row.edge_kind);
  bind_text(stmt, 8, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to insert compatibility viz edge row: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_viz_node_anchor_row(SqliteStmt& stmt,
                                const VizNodeAnchorSqlRow& row) {
  bind_text(stmt, 1, row.node_id);
  bind_text(stmt, 2, row.anchor_id);
  bind_int64(stmt, 3, row.db_idx);
  bind_int64(stmt, 4, row.device_id);
  bind_text(stmt, 5, row.view_name);
  bind_int64(stmt, 6, row.occurrence_idx);
  bind_int64(stmt, 7, row.anchor_order);
  bind_text(stmt, 8, row.coverage_kind);
  bind_text(stmt, 9, row.repeat_context);
  bind_double(stmt, 10, row.compute_us);
  bind_double(stmt, 11, row.comm_us);
  bind_double(stmt, 12, row.idle_us);
  bind_double(stmt, 13, row.total_us);
  bind_double(stmt, 14, row.self_us);
  bind_double(stmt, 15, row.aux_events);
  bind_double(stmt, 16, row.aux_us);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility viz node anchor row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_anchor_primary_node_row(SqliteStmt& stmt,
                                    const AnchorPrimaryNodeSqlRow& row) {
  bind_text(stmt, 1, row.anchor_id);
  bind_text(stmt, 2, row.node_id);
  bind_int64(stmt, 3, row.db_idx);
  bind_int64(stmt, 4, row.device_id);
  bind_text(stmt, 5, row.view_name);
  bind_text(stmt, 6, row.reason);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(
        "failed to insert compatibility anchor primary node row: " +
        std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_loop_node_row(SqliteStmt& stmt, const LoopNodeSqlRow& row) {
  bind_text(stmt, 1, row.node_id);
  bind_int64(stmt, 2, row.db_idx);
  bind_int64(stmt, 3, row.device_id);
  bind_text(stmt, 4, row.view_name);
  bind_int64(stmt, 5, row.loop_rank);
  bind_text(stmt, 6, row.repeat_label);
  if (row.repeat_count == 0) {
    bind_null(stmt, 7);
  } else {
    bind_int64(stmt, 7, row.repeat_count);
  }
  bind_int64(stmt, 8, row.occurrence_count);
  bind_int64(stmt, 9, row.anchor_count);
  bind_double(stmt, 10, row.total_us);
  bind_double(stmt, 11, row.avg_total_us);
  bind_double(stmt, 12, row.compute_us);
  bind_double(stmt, 13, row.comm_us);
  bind_double(stmt, 14, row.idle_us);
  bind_double(stmt, 15, row.loop_total_pct);
  bind_text(stmt, 16, row.raw_json);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to insert compatibility loop node row: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

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

void finish_idle_insert(SqliteStmt& stmt, const char* row_kind) {
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("failed to insert compatibility ") +
                             row_kind + " row: " +
                             sqlite3_errmsg(stmt.db()));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void insert_run_metadata_row(SqliteStmt& stmt, const RunMetadataSqlRow& row) {
  bind_text(stmt, 1, row.run_id);
  bind_text(stmt, 2, row.analysis_status);
  if (row.has_span) {
    bind_int64(stmt, 3, row.span_start_ns);
    bind_int64(stmt, 4, row.span_end_ns);
  } else {
    bind_null(stmt, 3);
    bind_null(stmt, 4);
  }
  bind_text(stmt, 5, row.contract_version);
  bind_text(stmt, 6, row.semantic_rules_version);
  bind_text(stmt, 7, row.semantic_rules_sha256);
  bind_text(stmt, 8, row.attribution_rule_version);
  bind_text(stmt, 9, row.host_api_rules_version);
  bind_text(stmt, 10, row.host_api_rules_sha256);
  bind_text(stmt, 11, row.collection_status);
  bind_int64(stmt, 12, row.db_idx);
  bind_text(stmt, 13, row.source_kind);
  bind_text(stmt, 14, row.source_path);
  bind_text(stmt, 15, row.metadata_json);
  finish_idle_insert(stmt, "run metadata");
}

void insert_device_interval_row(SqliteStmt& stmt,
                                const DeviceIntervalSqlRow& row) {
  bind_text(stmt, 1, row.interval_id);
  bind_text(stmt, 2, row.run_id);
  bind_int64(stmt, 3, row.db_idx);
  bind_int64(stmt, 4, row.device_id);
  bind_int64(stmt, 5, row.interval_order);
  bind_int64(stmt, 6, row.start_ns);
  bind_int64(stmt, 7, row.end_ns);
  bind_int64(stmt, 8, static_cast<sqlite3_int64>(row.duration_ns));
  bind_double(stmt, 9, row.duration_us);
  bind_text(stmt, 10, row.interval_kind);
  bind_int64(stmt, 11, static_cast<sqlite3_int64>(row.source_count));
  bind_text(stmt, 12, row.clock_domain);
  bind_text(stmt, 13, row.contract_version);
  bind_text(stmt, 14, row.semantic_rules_version);
  bind_text(stmt, 15, row.attribution_rule_version);
  finish_idle_insert(stmt, "device interval");
}

void insert_stream_state_row(SqliteStmt& stmt, const StreamStateSqlRow& row) {
  bind_text(stmt, 1, row.state_id);
  bind_text(stmt, 2, row.run_id);
  bind_int64(stmt, 3, row.db_idx);
  bind_int64(stmt, 4, row.device_id);
  bind_int64(stmt, 5, static_cast<sqlite3_int64>(row.stream_id));
  bind_int64(stmt, 6, row.state_order);
  bind_int64(stmt, 7, row.start_ns);
  bind_int64(stmt, 8, row.end_ns);
  bind_int64(stmt, 9, static_cast<sqlite3_int64>(row.duration_ns));
  bind_double(stmt, 10, row.duration_us);
  bind_text(stmt, 11, row.state);
  bind_int64(stmt, 12, static_cast<sqlite3_int64>(row.source_count));
  bind_text(stmt, 13, row.stream_universe_kind);
  bind_int64(stmt, 14,
             static_cast<sqlite3_int64>(row.stream_universe_size));
  bind_int64(stmt, 15,
             static_cast<sqlite3_int64>(row.observed_stream_count));
  bind_int64(stmt, 16, row.observed_universe_scan_complete ? 1 : 0);
  bind_text(stmt, 17, row.collection_status);
  bind_text(stmt, 18, row.clock_domain);
  bind_text(stmt, 19, row.contract_version);
  bind_text(stmt, 20, row.semantic_rules_version);
  bind_text(stmt, 21, row.attribution_rule_version);
  finish_idle_insert(stmt, "stream state");
}

void insert_idle_explanation_row(SqliteStmt& stmt,
                                 const IdleExplanationSqlRow& row) {
  bind_text(stmt, 1, row.idle_explanation_id);
  bind_text(stmt, 2, row.run_id);
  bind_text(stmt, 3, row.gap_interval_id);
  bind_int64(stmt, 4, row.db_idx);
  bind_int64(stmt, 5, row.device_id);
  bind_int64(stmt, 6, row.explanation_order);
  bind_int64(stmt, 7, row.start_ns);
  bind_int64(stmt, 8, row.end_ns);
  bind_int64(stmt, 9, static_cast<sqlite3_int64>(row.duration_ns));
  bind_double(stmt, 10, row.duration_us);
  bind_text(stmt, 11, row.category);
  bind_text(stmt, 12, row.evidence_level);
  bind_text(stmt, 13, row.evidence_relation);
  bind_text(stmt, 14, row.alignment_status);
  bind_text(stmt, 15, row.collection_status);
  bind_text(stmt, 16, row.reason);
  bind_int64(stmt, 17, static_cast<sqlite3_int64>(row.source_count));
  bind_text(stmt, 18, row.clock_domain);
  bind_text(stmt, 19, row.contract_version);
  bind_text(stmt, 20, row.semantic_rules_version);
  bind_text(stmt, 21, row.attribution_rule_version);
  finish_idle_insert(stmt, "idle explanation");
}

void insert_evidence_link_row(SqliteStmt& stmt,
                              const EvidenceLinkSqlRow& row) {
  bind_text(stmt, 1, row.owner_kind);
  bind_text(stmt, 2, row.owner_id);
  bind_int64(stmt, 3, row.evidence_ordinal);
  bind_text(stmt, 4, row.source_kind);
  bind_text(stmt, 5, row.source_table);
  bind_text(stmt, 6, row.source_key);
  bind_text(stmt, 7, row.relation);
  bind_text(stmt, 8, row.evidence_level);
  if (row.has_overlap) {
    bind_int64(stmt, 9, row.overlap_start_ns);
    bind_int64(stmt, 10, row.overlap_end_ns);
  } else {
    bind_null(stmt, 9);
    bind_null(stmt, 10);
  }
  if (row.has_stream_id) {
    bind_int64(stmt, 11, static_cast<sqlite3_int64>(row.stream_id));
  } else {
    bind_null(stmt, 11);
  }
  row.state.empty() ? bind_null(stmt, 12) : bind_text(stmt, 12, row.state);
  bind_text(stmt, 13, row.trace_event_id);
  row.matched_rule_id.empty() ? bind_null(stmt, 14)
                              : bind_text(stmt, 14, row.matched_rule_id);
  finish_idle_insert(stmt, "evidence link");
}

void insert_anchor_idle_row(SqliteStmt& stmt,
                            const AnchorIdleExplanationSqlRow& row) {
  bind_text(stmt, 1, row.anchor_id);
  bind_text(stmt, 2, row.run_id);
  bind_int64(stmt, 3, row.db_idx);
  bind_int64(stmt, 4, row.device_id);
  bind_int64(stmt, 5, row.anchor_idx);
  bind_text(stmt, 6, row.category);
  bind_text(stmt, 7, row.evidence_level);
  bind_int64(stmt, 8, static_cast<sqlite3_int64>(row.slice_count));
  bind_int64(stmt, 9, static_cast<sqlite3_int64>(row.duration_ns));
  bind_double(stmt, 10, row.duration_us);
  finish_idle_insert(stmt, "anchor idle explanation");
}

void insert_node_idle_row(SqliteStmt& stmt,
                          const NodeIdleExplanationSqlRow& row) {
  bind_text(stmt, 1, row.node_id);
  bind_text(stmt, 2, row.run_id);
  bind_int64(stmt, 3, row.db_idx);
  bind_int64(stmt, 4, row.device_id);
  bind_text(stmt, 5, row.view_name);
  bind_text(stmt, 6, row.category);
  bind_text(stmt, 7, row.evidence_level);
  bind_int64(stmt, 8, static_cast<sqlite3_int64>(row.slice_count));
  bind_int64(stmt, 9, static_cast<sqlite3_int64>(row.duration_ns));
  bind_double(stmt, 10, row.duration_us);
  finish_idle_insert(stmt, "node idle explanation");
}

void materialize_cuda_graph_views(SqliteDb& db) {
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_cuda_graph_replay AS "
      "SELECT "
      "g.*, "
      "e.symbol, "
      "e.label, "
      "e.task_type, "
      "e.semantic_role, "
      "e.semantic_role_reason, "
      "a.anchor_idx "
      "FROM traceloom_cuda_graph_replay g "
      "JOIN traceloom_event e ON e.event_id = g.event_id "
      "LEFT JOIN traceloom_anchor a ON a.event_id = e.event_id");
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_cuda_graph_envelope AS "
      "SELECT "
      "ge.*, "
      "graph_anchor.anchor_idx AS graph_anchor_idx, "
      "graph.label AS graph_label, "
      "graph.stream_id AS graph_stream_id, "
      "child.label AS child_label, "
      "child.task_type AS child_task_type, "
      "child.source_table AS child_source_table, "
      "child.stream_id AS child_stream_id, "
      "child.symbol AS child_symbol, "
      "child.semantic_role AS child_semantic_role "
      "FROM traceloom_cuda_graph_envelope ge "
      "JOIN traceloom_event graph ON graph.event_id = ge.graph_event_id "
      "LEFT JOIN traceloom_anchor graph_anchor ON "
      "graph_anchor.event_id = graph.event_id "
      "JOIN traceloom_event child ON child.event_id = ge.child_event_id");
}

void materialize_exact_graph_views(SqliteDb& db) {
  // Canonical tree-occurrence view over the exact graph relations. Rows are
  // the exact ordered members of a graph launch whose promoted tree anchor
  // exists in traceloom_viz_node_anchor; exact launches without a tree anchor
  // stay in the base tables and do not masquerade as node-view rows. The
  // launch<->anchor, launch<->member, and member<->event joins are composite
  // (id + db_idx + device_id), never bare IDs. Filter by (node_id,
  // occurrence_idx) or node_event_id to walk a tree node occurrence to its
  // exact members/events; filter by event_id to walk a member event back to
  // the tree occurrences that contain it.
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_node_graph_body_member AS "
      "SELECT "
      "na.node_id AS node_id, "
      "na.view_name AS view_name, "
      "na.occurrence_idx AS occurrence_idx, "
      "(SELECT COUNT(*) FROM traceloom_viz_node_anchor na2 "
      "WHERE na2.node_id = na.node_id AND na2.db_idx = na.db_idx "
      "AND na2.device_id = na.device_id AND na2.view_name = na.view_name "
      "AND na2.occurrence_idx = na.occurrence_idx "
      "AND na2.anchor_order < na.anchor_order) AS idx_in_occurrence, "
      "na.anchor_order AS anchor_order, "
      "na.coverage_kind AS coverage_kind, "
      "na.repeat_context AS repeat_context, "
      "na.compute_us AS anchor_compute_us, "
      "na.comm_us AS anchor_comm_us, "
      "na.idle_us AS anchor_idle_us, "
      "na.total_us AS anchor_total_us, "
      "na.self_us AS anchor_self_us, "
      "na.aux_events AS anchor_aux_events, "
      "na.aux_us AS anchor_aux_us, "
      "l.launch_id AS node_launch_id, "
      "l.graph_event_id AS node_event_id, "
      "l.anchor_id AS node_anchor_id, "
      "l.replay_unit_id AS node_replay_unit_id, "
      "l.graph_template_id AS node_graph_template_id, "
      "l.graph_launch_occurrence_id AS node_graph_launch_occurrence_id, "
      "l.member_order AS node_member_order, "
      "l.slot_order AS node_slot_order, "
      "l.correlation_id AS launch_correlation_id, "
      "l.match_policy AS launch_match_policy, "
      "l.association_policy AS launch_association_policy, "
      "l.start_ns AS launch_start_ns, "
      "l.end_ns AS launch_end_ns, "
      "l.dur_us AS launch_dur_us, "
      "m.member_id, "
      "m.db_idx, "
      "m.device_id, "
      "m.graph_provider, "
      "m.replay_unit_id, "
      "m.graph_template_id, "
      "m.graph_launch_occurrence_id, "
      "m.body_id, "
      "m.replay_body_template_id, "
      "m.member_order, "
      "m.slot_order, "
      "m.lane_ordinal, "
      "m.task_ordinal, "
      "m.kind, "
      "m.event_id, "
      "m.task_id, "
      "m.source_table, "
      "m.source_row_id, "
      "m.raw_task_id, "
      "m.start_ns, "
      "m.end_ns, "
      "m.dur_us, "
      "m.correlation_id, "
      "m.graph_node_id, "
      "m.original_graph_node_id, "
      "m.match_policy, "
      "m.association_policy, "
      "m.evidence_level, "
      "e.symbol AS member_symbol, "
      "e.label AS member_label, "
      "e.task_type AS member_task_type, "
      "e.semantic_role AS member_semantic_role "
      "FROM traceloom_graph_launch l "
      "JOIN traceloom_viz_node_anchor na "
      "ON na.anchor_id = l.anchor_id AND na.db_idx = l.db_idx "
      "AND na.device_id = l.device_id "
      "JOIN traceloom_graph_body_member m "
      "ON m.launch_id = l.launch_id AND m.db_idx = l.db_idx "
      "AND m.device_id = l.device_id "
      "JOIN traceloom_event e "
      "ON e.event_id = m.event_id AND e.db_idx = m.db_idx "
      "AND e.device_id = m.device_id");
}

void materialize_replay_cost_views(SqliteDb& db) {
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_node_replay_cost_member AS "
      "SELECT g.node_id, g.occurrence_idx, g.view_name, g.coverage_kind, "
      "g.node_anchor_id, g.node_member_order, g.node_slot_order, c.*, "
      "g.source_table, g.source_row_id, g.graph_node_id, "
      "g.original_graph_node_id, g.evidence_level "
      "FROM traceloom_v_node_graph_body_member g "
      "JOIN traceloom_replay_cost_member c "
      "ON c.member_id = g.member_id AND c.db_idx = g.db_idx "
      "AND c.device_id = g.device_id "
      "WHERE g.coverage_kind = 'self'");
}

void materialize_tree_node_anchor_view(SqliteDb& db) {
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_tree_node_anchor AS "
      "SELECT "
      "na.node_id, "
      "n.local_node_id, "
      "na.anchor_id, "
      "na.db_idx, "
      "na.device_id, "
      "na.view_name, "
      "na.occurrence_idx, "
      "na.anchor_order, "
      "na.coverage_kind, "
      "na.repeat_context, "
      "na.compute_us, "
      "na.comm_us, "
      "na.idle_us, "
      "na.total_us, "
      "na.self_us, "
      "na.aux_events, "
      "na.aux_us "
      "FROM traceloom_viz_node_anchor na "
      "JOIN traceloom_viz_node n ON n.node_id = na.node_id "
      "AND n.db_idx = na.db_idx "
      "AND n.device_id = na.device_id "
      "AND n.view_name = na.view_name");
}

void materialize_tree_node_occurrence_view(SqliteDb& db) {
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_tree_node_occurrence AS "
      "WITH anchor_span AS ("
      "SELECT "
      "na.node_id, "
      "na.db_idx, "
      "na.device_id, "
      "na.view_name, "
      "na.occurrence_idx, "
      "MIN(a.anchor_idx) AS anchor_start_idx, "
      "MAX(a.anchor_idx) AS anchor_end_idx, "
      "COUNT(*) AS anchor_count, "
      "MIN(a.start_ns) AS start_ns, "
      "MAX(a.end_ns) AS end_ns, "
      "SUM(na.compute_us) AS compute_us, "
      "SUM(na.comm_us) AS comm_us, "
      "SUM(na.idle_us) AS idle_us, "
      "SUM(na.total_us) AS total_us, "
      "SUM(na.self_us) AS self_us, "
      "SUM(na.aux_events) AS aux_events, "
      "SUM(na.aux_us) AS aux_us, "
      "MIN(na.repeat_context) AS repeat_context "
      "FROM traceloom_viz_node_anchor na "
      "JOIN traceloom_anchor a ON a.anchor_id = na.anchor_id "
      "AND a.db_idx = na.db_idx "
      "AND a.device_id = na.device_id "
      "GROUP BY na.node_id, na.db_idx, na.device_id, na.view_name, "
      "na.occurrence_idx"
      ") "
      "SELECT "
      "a.node_id, "
      "n.local_node_id, "
      "a.db_idx, "
      "a.device_id, "
      "a.view_name, "
      "a.occurrence_idx, "
      "a.repeat_context, "
      "a.anchor_start_idx, "
      "a.anchor_end_idx, "
      "a.anchor_count, "
      "a.start_ns, "
      "a.end_ns, "
      "ROUND(COALESCE(a.compute_us, 0.0), 3) AS compute_us, "
      "ROUND(COALESCE(a.comm_us, 0.0), 3) AS comm_us, "
      "ROUND(COALESCE(a.idle_us, 0.0), 3) AS idle_us, "
      "ROUND(COALESCE(a.total_us, 0.0), 3) AS total_us, "
      "ROUND(COALESCE(a.self_us, 0.0), 3) AS self_us, "
      "COALESCE(a.aux_events, 0) AS aux_events, "
      "ROUND(COALESCE(a.aux_us, 0.0), 3) AS aux_us "
      "FROM anchor_span a "
      "JOIN traceloom_viz_node n ON n.node_id = a.node_id "
      "AND n.db_idx = a.db_idx "
      "AND n.device_id = a.device_id "
      "AND n.view_name = a.view_name");
}

void materialize_node_cost_views(SqliteDb& db) {
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_node_anchor_cost AS "
      "SELECT "
      "na.node_id, "
      "na.anchor_id, "
      "na.occurrence_idx, "
      "na.anchor_order, "
      "e.dur_us AS anchor_dur_us, "
      "e.role AS anchor_role, "
      "e.symbol AS anchor_symbol, "
      "e.label AS anchor_label "
      "FROM traceloom_viz_node_anchor na "
      "JOIN traceloom_anchor a ON a.anchor_id = na.anchor_id "
      "JOIN traceloom_event e ON e.event_id = a.event_id");
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_node_aux_cost AS "
      "SELECT "
      "na.node_id, "
      "al.anchor_id, "
      "al.aux_event_id, "
      "al.aux_order, "
      "e.dur_us AS aux_dur_us, "
      "e.role AS aux_role, "
      "e.symbol AS aux_symbol, "
      "e.label AS aux_label "
      "FROM traceloom_viz_node_anchor na "
      "JOIN traceloom_aux_link al ON al.anchor_id = na.anchor_id "
      "JOIN traceloom_event e ON e.event_id = al.aux_event_id");
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_node_cost AS "
      "SELECT "
      "n.*, "
      "COALESCE(anchor_cost.anchor_dur_us, 0.0) AS sql_anchor_us, "
      "COALESCE(aux_cost.aux_dur_us, 0.0) AS sql_aux_us "
      "FROM traceloom_viz_node n "
      "LEFT JOIN ("
      "SELECT node_id, SUM(anchor_dur_us) AS anchor_dur_us "
      "FROM traceloom_v_node_anchor_cost "
      "GROUP BY node_id"
      ") anchor_cost ON anchor_cost.node_id = n.node_id "
      "LEFT JOIN ("
      "SELECT node_id, SUM(aux_dur_us) AS aux_dur_us "
      "FROM traceloom_v_node_aux_cost "
      "GROUP BY node_id"
      ") aux_cost ON aux_cost.node_id = n.node_id");
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_node_children AS "
      "SELECT "
      "e.parent_node_id, "
      "e.child_node_id, "
      "e.edge_order, "
      "child.* "
      "FROM traceloom_viz_edge e "
      "JOIN traceloom_viz_node child ON child.node_id = e.child_node_id");
}

void materialize_tree_node_view(SqliteDb& db) {
  db.exec(
      "CREATE VIEW IF NOT EXISTS traceloom_v_tree_node AS "
      "WITH RECURSIVE tree AS ("
      "SELECT "
      "n.node_id, "
      "CAST(NULL AS TEXT) AS parent_node_id, "
      "n.db_idx, "
      "n.device_id, "
      "n.view_name, "
      "n.local_node_id, "
      "CAST(SUBSTR(n.local_node_id, 2) AS INTEGER) AS display_order, "
      "n.path, "
      "n.depth AS tree_depth, "
      "n.level AS depth, "
      "CASE WHEN n.kind = 'repeat' THEN 1 ELSE 0 END AS loop_depth, "
      "n.node_type, "
      "n.kind, "
      "n.symbol, "
      "n.label, "
      "n.category, "
      "n.repeat_label, "
      "n.repeat_count, "
      "n.occurrence_count, "
      "n.anchor_count, "
      "n.anchors_per_occurrence, "
      "n.anchors_per_occurrence AS avg_anchor, "
      "n.first_anchor_idx, "
      "n.last_anchor_idx, "
      "n.compute_us, "
      "n.comm_us, "
      "n.idle_us, "
      "n.total_us, "
      "n.avg_compute_us, "
      "n.avg_comm_us, "
      "n.avg_idle_us, "
      "n.avg_total_us, "
      "COALESCE(n.compute_us, 0.0) + COALESCE(n.comm_us, 0.0) "
      "AS active_us, "
      "ROUND((COALESCE(n.compute_us, 0.0) + COALESCE(n.comm_us, 0.0)) / "
      "CASE WHEN COALESCE(n.occurrence_count, 0) = 0 THEN 1 ELSE "
      "n.occurrence_count * CASE WHEN n.kind = 'repeat' AND "
      "COALESCE(n.repeat_count, 0) > 0 THEN n.repeat_count ELSE 1 END "
      "END, 3) AS avg_active_us, "
      "n.self_us, "
      "n.aux_events, "
      "n.aux_us, "
      "ROUND(COALESCE(n.aux_us, 0.0) / CASE WHEN "
      "COALESCE(n.occurrence_count, 0) = 0 THEN 1 ELSE "
      "n.occurrence_count * CASE WHEN n.kind = 'repeat' AND "
      "COALESCE(n.repeat_count, 0) > 0 THEN n.repeat_count ELSE 1 END "
      "END, 3) AS avg_aux_us, "
      "ROUND(CASE WHEN COALESCE(n.total_us, 0.0) = 0.0 THEN 0.0 ELSE "
      "COALESCE(n.comm_us, 0.0) / n.total_us END, 6) AS comm_pct, "
      "ROUND(CASE WHEN COALESCE(n.total_us, 0.0) = 0.0 THEN 0.0 ELSE "
      "COALESCE(n.idle_us, 0.0) / n.total_us END, 6) AS idle_pct "
      "FROM traceloom_viz_node n "
      "WHERE NOT EXISTS ("
      "SELECT 1 FROM traceloom_viz_edge e WHERE e.child_node_id = n.node_id"
      ") "
      "UNION ALL "
      "SELECT "
      "child.node_id, "
      "e.parent_node_id, "
      "child.db_idx, "
      "child.device_id, "
      "child.view_name, "
      "child.local_node_id, "
      "CAST(SUBSTR(child.local_node_id, 2) AS INTEGER) AS display_order, "
      "child.path, "
      "child.depth AS tree_depth, "
      "child.level AS depth, "
      "tree.loop_depth + CASE WHEN child.kind = 'repeat' THEN 1 ELSE 0 END "
      "AS loop_depth, "
      "child.node_type, "
      "child.kind, "
      "child.symbol, "
      "child.label, "
      "child.category, "
      "child.repeat_label, "
      "child.repeat_count, "
      "child.occurrence_count, "
      "child.anchor_count, "
      "child.anchors_per_occurrence, "
      "child.anchors_per_occurrence AS avg_anchor, "
      "child.first_anchor_idx, "
      "child.last_anchor_idx, "
      "child.compute_us, "
      "child.comm_us, "
      "child.idle_us, "
      "child.total_us, "
      "child.avg_compute_us, "
      "child.avg_comm_us, "
      "child.avg_idle_us, "
      "child.avg_total_us, "
      "COALESCE(child.compute_us, 0.0) + COALESCE(child.comm_us, 0.0) "
      "AS active_us, "
      "ROUND((COALESCE(child.compute_us, 0.0) + "
      "COALESCE(child.comm_us, 0.0)) / CASE WHEN "
      "COALESCE(child.occurrence_count, 0) = 0 THEN 1 ELSE "
      "child.occurrence_count * CASE WHEN child.kind = 'repeat' AND "
      "COALESCE(child.repeat_count, 0) > 0 THEN child.repeat_count ELSE 1 "
      "END END, 3) AS avg_active_us, "
      "child.self_us, "
      "child.aux_events, "
      "child.aux_us, "
      "ROUND(COALESCE(child.aux_us, 0.0) / CASE WHEN "
      "COALESCE(child.occurrence_count, 0) = 0 THEN 1 ELSE "
      "child.occurrence_count * CASE WHEN child.kind = 'repeat' AND "
      "COALESCE(child.repeat_count, 0) > 0 THEN child.repeat_count ELSE 1 "
      "END END, 3) AS avg_aux_us, "
      "ROUND(CASE WHEN COALESCE(child.total_us, 0.0) = 0.0 THEN 0.0 "
      "ELSE COALESCE(child.comm_us, 0.0) / child.total_us END, 6) "
      "AS comm_pct, "
      "ROUND(CASE WHEN COALESCE(child.total_us, 0.0) = 0.0 THEN 0.0 "
      "ELSE COALESCE(child.idle_us, 0.0) / child.total_us END, 6) "
      "AS idle_pct "
      "FROM tree "
      "JOIN traceloom_viz_edge e ON e.parent_node_id = tree.node_id "
      "JOIN traceloom_viz_node child ON child.node_id = e.child_node_id"
      ") "
      "SELECT * FROM tree");
}

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

void materialize_report_compatibility_indexes(SqliteDb& db) {
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_event_device_step "
      "ON traceloom_event(db_idx, device_id, step_idx)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_event_id "
      "ON traceloom_event(event_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_event_identity "
      "ON traceloom_event(event_id, db_idx, device_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_event_source_lookup "
      "ON traceloom_event_source(source_table, source_key)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_anchor_device_idx "
      "ON traceloom_anchor(db_idx, device_id, anchor_idx)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_anchor_key "
      "ON traceloom_anchor(anchor_id, db_idx, device_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_aux_anchor "
      "ON traceloom_aux_link(anchor_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_cuda_graph_replay_exec "
      "ON traceloom_cuda_graph_replay(db_idx, device_id, graph_exec_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_cuda_graph_envelope_graph "
      "ON traceloom_cuda_graph_envelope(graph_event_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_cuda_graph_envelope_child "
      "ON traceloom_cuda_graph_envelope(child_event_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_aclgraph_region_status "
      "ON traceloom_aclgraph_reconstruction_region("
      "db_idx, device_id, status)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_graph_launch_node "
      "ON traceloom_graph_launch(db_idx, device_id, graph_event_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_graph_launch_anchor "
      "ON traceloom_graph_launch(anchor_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_graph_launch_identity "
      "ON traceloom_graph_launch(launch_id, db_idx, device_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_graph_launch_anchor_identity "
      "ON traceloom_graph_launch(anchor_id, db_idx, device_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_graph_body_member_launch "
      "ON traceloom_graph_body_member(launch_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS "
      "idx_traceloom_graph_body_member_launch_identity "
      "ON traceloom_graph_body_member(launch_id, db_idx, device_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_graph_body_member_event "
      "ON traceloom_graph_body_member(event_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS "
      "idx_traceloom_graph_body_member_event_identity "
      "ON traceloom_graph_body_member(event_id, db_idx, device_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_graph_body_member_node "
      "ON traceloom_graph_body_member(graph_node_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_replay_cost_member_event "
      "ON traceloom_replay_cost_member(event_id, db_idx, device_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_replay_cost_member_launch "
      "ON traceloom_replay_cost_member(launch_id, db_idx, device_id, "
      "lane_ordinal, task_ordinal)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_replay_cost_aggregate_hotspot "
      "ON traceloom_replay_cost_aggregate(db_idx, device_id, "
      "duration_median_ns DESC)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_replay_cost_contributor "
      "ON traceloom_replay_cost_aggregate_member(aggregate_id, "
      "contributor_order)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_node_anchor_node "
      "ON traceloom_viz_node_anchor(node_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_viz_node_id "
      "ON traceloom_viz_node(node_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_node_anchor_occurrence "
      "ON traceloom_viz_node_anchor(node_id, db_idx, device_id, view_name, "
      "occurrence_idx)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_node_anchor_anchor "
      "ON traceloom_viz_node_anchor(anchor_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_semantic_node_tree_order "
      "ON traceloom_semantic_node(tree_id, preorder_idx)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_semantic_node_parent "
      "ON traceloom_semantic_node(parent_node_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_semantic_edge_tree "
      "ON traceloom_semantic_edge(tree_id, edge_order)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_collective_key "
      "ON traceloom_collective_global_link(candidate_collective_key)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_collective_pair "
      "ON traceloom_collective_global_link(pair_id, occurrence_idx, op_type, "
      "idx_in_occurrence)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_device_interval_time "
      "ON traceloom_device_interval(run_id, device_id, start_ns, end_ns)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_device_interval_id "
      "ON traceloom_device_interval(interval_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_stream_state_time "
      "ON traceloom_stream_state(run_id, device_id, stream_id, start_ns, "
      "end_ns)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_stream_state_id "
      "ON traceloom_stream_state(state_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_idle_explanation_category "
      "ON traceloom_idle_explanation(run_id, device_id, category, start_ns, "
      "end_ns)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_idle_explanation_id "
      "ON traceloom_idle_explanation(idle_explanation_id)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_idle_explanation_gap "
      "ON traceloom_idle_explanation(run_id, gap_interval_id, "
      "explanation_order)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_evidence_owner "
      "ON traceloom_evidence_link(owner_kind, owner_id, evidence_ordinal)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_anchor_idle "
      "ON traceloom_anchor_idle_explanation(anchor_id, category)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_traceloom_node_idle "
      "ON traceloom_node_idle_explanation(node_id, category)");
}

void materialize_global_collective_indexes(SqliteDb& db) {
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_global_collective_status "
      "ON traceloom_global_collective_summary(validation_status)");
  db.exec(
      "CREATE INDEX IF NOT EXISTS idx_global_collective_member_key "
      "ON traceloom_global_collective_member(candidate_collective_key)");
}

}  // namespace

#endif

void materialize_compatibility_schema(const std::string& sqlite_path) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(sqlite_path, compatibility_table_schemas());
#else
  (void)sqlite_path;
  throw std::runtime_error(
      "compatibility sidecar writer requires SQLite support");
#endif
}

void materialize_compatibility_schema(
    const std::string& sqlite_path,
    const std::vector<CompatTableSchema>& schemas) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    for (const CompatTableSchema& schema : schemas) {
      db.exec(sqlite_create_table_sql(schema));
      if (schema.name == "traceloom_viz_node_anchor") {
        ensure_viz_node_anchor_cost_columns(db);
      }
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
  (void)schemas;
  throw std::runtime_error(
      "compatibility sidecar writer requires SQLite support");
#endif
}

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
void drop_report_compatibility_views(SqliteDb& db) {
  db.exec("DROP VIEW IF EXISTS traceloom_v_semantic_tree_readable");
  db.exec("DROP VIEW IF EXISTS traceloom_v_semantic_tree_node");
  db.exec("DROP VIEW IF EXISTS traceloom_v_tree_node");
  db.exec("DROP VIEW IF EXISTS traceloom_v_node_children");
  db.exec("DROP VIEW IF EXISTS traceloom_v_node_cost");
  db.exec("DROP VIEW IF EXISTS traceloom_v_node_aux_cost");
  db.exec("DROP VIEW IF EXISTS traceloom_v_node_anchor_cost");
  db.exec("DROP VIEW IF EXISTS traceloom_tree_node_occurrence");
  db.exec("DROP VIEW IF EXISTS traceloom_tree_node_anchor");
  db.exec("DROP VIEW IF EXISTS traceloom_v_cuda_graph_envelope");
  db.exec("DROP VIEW IF EXISTS traceloom_v_cuda_graph_replay");
  db.exec("DROP VIEW IF EXISTS traceloom_v_node_replay_cost_member");
  db.exec("DROP VIEW IF EXISTS traceloom_v_node_graph_body_member");
}
#endif

void materialize_report_compatibility_views(const std::string& sqlite_path) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(sqlite_path);

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    drop_report_compatibility_views(db);
    materialize_report_compatibility_indexes(db);
    materialize_cuda_graph_views(db);
    materialize_exact_graph_views(db);
    materialize_replay_cost_views(db);
    materialize_tree_node_anchor_view(db);
    materialize_tree_node_occurrence_view(db);
    materialize_node_cost_views(db);
    materialize_tree_node_view(db);
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
  throw std::runtime_error(
      "compatibility sidecar writer requires SQLite support");
#endif
}

void materialize_global_collective_compatibility_schema(
    const std::string& sqlite_path) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(sqlite_path,
                                   global_collective_table_schemas());

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    materialize_global_collective_indexes(db);
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
  throw std::runtime_error(
      "compatibility sidecar writer requires SQLite support");
#endif
}

void replace_metadata_rows(const std::string& sqlite_path,
                           const std::vector<MetadataSqlRow>& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(sqlite_path, {metadata_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_metadata");
    SqliteStmt stmt(db.get(),
                    "INSERT INTO traceloom_metadata (key, value) "
                    "VALUES (?, ?)");
    for (const MetadataSqlRow& row : rows) {
      insert_metadata_row(stmt, row);
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

void replace_event_rows(const std::string& sqlite_path,
                        const EventSqlRows& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(sqlite_path,
                                   {event_table_schema(),
                                    event_source_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_event_source");
    db.exec("DELETE FROM traceloom_event");

    SqliteStmt event_stmt(
        db.get(),
        "INSERT INTO traceloom_event ("
        "event_id, db_idx, device_id, step_idx, source_table, source_key, "
        "stream_id, start_ns, end_ns, dur_us, category, role, semantic_role, "
        "semantic_role_reason, symbol, label, raw_label, op_type, "
        "compute_task_type, family, task_type, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?)");
    for (const EventSqlRow& row : rows.events) {
      insert_event_row(event_stmt, row);
    }

    SqliteStmt event_source_stmt(
        db.get(),
        "INSERT INTO traceloom_event_source ("
        "event_id, source_ordinal, db_idx, device_id, source_table, "
        "source_key, source_role, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    for (const EventSourceSqlRow& row : rows.event_sources) {
      insert_event_source_row(event_source_stmt, row);
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

void replace_timeline_rows(const std::string& sqlite_path,
                           const std::vector<EventSqlRow>& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(sqlite_path, {event_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_event");

    SqliteStmt event_stmt(
        db.get(),
        "INSERT INTO traceloom_event ("
        "event_id, db_idx, device_id, step_idx, source_table, source_key, "
        "stream_id, start_ns, end_ns, dur_us, category, role, semantic_role, "
        "semantic_role_reason, symbol, label, raw_label, op_type, "
        "compute_task_type, family, task_type, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?)");
    for (const EventSqlRow& row : rows) {
      insert_event_row(event_stmt, row);
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

void replace_event_source_rows(
    const std::string& sqlite_path,
    const std::vector<EventSourceSqlRow>& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(sqlite_path,
                                   {event_source_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_event_source");

    SqliteStmt event_source_stmt(
        db.get(),
        "INSERT INTO traceloom_event_source ("
        "event_id, source_ordinal, db_idx, device_id, source_table, "
        "source_key, source_role, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    for (const EventSourceSqlRow& row : rows) {
      insert_event_source_row(event_source_stmt, row);
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

void replace_anchor_rows(const std::string& sqlite_path,
                         const std::vector<AnchorSqlRow>& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(sqlite_path, {anchor_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_anchor");

    SqliteStmt anchor_stmt(
        db.get(),
        "INSERT INTO traceloom_anchor ("
        "anchor_id, db_idx, device_id, anchor_idx, event_id, step_idx, "
        "symbol, role, label, family, start_ns, end_ns, dur_us"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const AnchorSqlRow& row : rows) {
      insert_anchor_row(anchor_stmt, row);
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

void replace_anchor_cost_breakdown_rows(
    const std::string& sqlite_path,
    const std::vector<AnchorCostBreakdownSqlRow>& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(
      sqlite_path, {anchor_cost_breakdown_sql_row_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_anchor_cost_breakdown");
    SqliteStmt stmt(
        db.get(),
        "INSERT INTO traceloom_anchor_cost_breakdown ("
        "anchor_idx, symbol, anchor_kind, total_us, self_us, aux_us, "
        "graph_child_us, residual_us, raw_child_task_count, top_ops, "
        "diagnostic_flags"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const AnchorCostBreakdownSqlRow& row : rows) {
      insert_anchor_cost_breakdown_row(stmt, row);
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

void replace_aux_attribution_rows(const std::string& sqlite_path,
                                  const AuxAttributionSqlRows& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(
      sqlite_path, {anchor_aux_slot_table_schema(), aux_link_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_aux_link");
    db.exec("DELETE FROM traceloom_anchor_aux_slot");

    SqliteStmt aux_slot_stmt(
        db.get(),
        "INSERT INTO traceloom_anchor_aux_slot ("
        "anchor_id, db_idx, device_id, anchor_idx, anchor_step_idx, "
        "aux_start_step_idx, aux_end_step_idx, aux_event_count, aux_dur_us, "
        "raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const AnchorAuxSlotSqlRow& row : rows.aux_slots) {
      insert_anchor_aux_slot_row(aux_slot_stmt, row);
    }

    SqliteStmt aux_link_stmt(
        db.get(),
        "INSERT INTO traceloom_aux_link ("
        "anchor_id, aux_event_id, db_idx, device_id, aux_order, aux_step_idx, "
        "link_type, reason, aux_kind, aux_dur_us, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const AuxLinkSqlRow& row : rows.aux_links) {
      insert_aux_link_row(aux_link_stmt, row);
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

void replace_anchor_aux_rows(const std::string& sqlite_path,
                             const AnchorAuxSqlRows& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(
      sqlite_path,
      {event_table_schema(), event_source_table_schema(), anchor_table_schema(),
       anchor_aux_slot_table_schema(), aux_link_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_aux_link");
    db.exec("DELETE FROM traceloom_anchor_aux_slot");
    db.exec("DELETE FROM traceloom_anchor");
    db.exec("DELETE FROM traceloom_event_source");
    db.exec("DELETE FROM traceloom_event");

    SqliteStmt event_stmt(
        db.get(),
        "INSERT INTO traceloom_event ("
        "event_id, db_idx, device_id, step_idx, source_table, source_key, "
        "stream_id, start_ns, end_ns, dur_us, category, role, semantic_role, "
        "semantic_role_reason, symbol, label, raw_label, op_type, "
        "compute_task_type, family, task_type, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?)");
    for (const EventSqlRow& row : rows.events) {
      insert_event_row(event_stmt, row);
    }

    SqliteStmt event_source_stmt(
        db.get(),
        "INSERT INTO traceloom_event_source ("
        "event_id, source_ordinal, db_idx, device_id, source_table, "
        "source_key, source_role, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    for (const EventSourceSqlRow& row : rows.event_sources) {
      insert_event_source_row(event_source_stmt, row);
    }

    SqliteStmt anchor_stmt(
        db.get(),
        "INSERT INTO traceloom_anchor ("
        "anchor_id, db_idx, device_id, anchor_idx, event_id, step_idx, "
        "symbol, role, label, family, start_ns, end_ns, dur_us"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const AnchorSqlRow& row : rows.anchors) {
      insert_anchor_row(anchor_stmt, row);
    }

    SqliteStmt aux_slot_stmt(
        db.get(),
        "INSERT INTO traceloom_anchor_aux_slot ("
        "anchor_id, db_idx, device_id, anchor_idx, anchor_step_idx, "
        "aux_start_step_idx, aux_end_step_idx, aux_event_count, aux_dur_us, "
        "raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const AnchorAuxSlotSqlRow& row : rows.aux_slots) {
      insert_anchor_aux_slot_row(aux_slot_stmt, row);
    }

    SqliteStmt aux_link_stmt(
        db.get(),
        "INSERT INTO traceloom_aux_link ("
        "anchor_id, aux_event_id, db_idx, device_id, aux_order, aux_step_idx, "
        "link_type, reason, aux_kind, aux_dur_us, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const AuxLinkSqlRow& row : rows.aux_links) {
      insert_aux_link_row(aux_link_stmt, row);
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

void replace_graph_replay_evidence_rows(
    const std::string& sqlite_path,
    const GraphReplayEvidenceSqlRows& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(
      sqlite_path,
      {cuda_graph_replay_table_schema(), cuda_graph_envelope_table_schema(),
       aclgraph_reconstruction_region_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_cuda_graph_envelope");
    db.exec("DELETE FROM traceloom_cuda_graph_replay");
    db.exec("DELETE FROM traceloom_aclgraph_reconstruction_region");

    SqliteStmt graph_stmt(
        db.get(),
        "INSERT INTO traceloom_cuda_graph_replay ("
        "graph_event_id, db_idx, device_id, graph_provider, graph_kind, "
        "graph_event_idx, event_id, step_idx, stream_id, correlation_id, "
        "graph_id, graph_exec_id, context_id, start_ns, end_ns, dur_us, "
        "enclosed_event_count, enclosed_event_us, enclosed_kernel_count, "
        "enclosed_kernel_us, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?)");
    for (const GraphReplaySqlRow& row : rows.graph_replays) {
      insert_graph_replay_row(graph_stmt, row);
    }

    SqliteStmt envelope_stmt(
        db.get(),
        "INSERT INTO traceloom_cuda_graph_envelope ("
        "envelope_id, db_idx, device_id, graph_provider, graph_kind, "
        "envelope_idx, graph_event_id, child_event_id, graph_step_idx, "
        "child_step_idx, relation, stream_relation, graph_id, graph_exec_id, "
        "graph_correlation_id, graph_start_ns, graph_end_ns, child_start_ns, "
        "child_end_ns, start_offset_us, end_offset_us, child_dur_us, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?)");
    for (const GraphEnvelopeSqlRow& row : rows.graph_envelopes) {
      insert_graph_envelope_row(envelope_stmt, row);
    }

    SqliteStmt region_stmt(
        db.get(),
        "INSERT INTO traceloom_aclgraph_reconstruction_region ("
        "region_id, db_idx, device_id, graph_provider, candidate_id, "
        "region_order, status, boundary_policy, order_policy, "
        "identity_policy, shape_policy, first_launch_occurrence_id, "
        "last_launch_occurrence_id, observed_launch_count, "
        "expected_launch_count, start_ns, end_ns, dur_us, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?)");
    for (const GraphReconstructionRegionSqlRow& row :
         rows.reconstruction_regions) {
      insert_graph_reconstruction_region_row(region_stmt, row);
    }

    materialize_cuda_graph_views(db);
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

void replace_exact_graph_rows(const std::string& sqlite_path,
                                  const ExactGraphSqlRows& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(
      sqlite_path,
      {graph_launch_table_schema(), graph_body_member_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_graph_body_member");
    db.exec("DELETE FROM traceloom_graph_launch");

    SqliteStmt launch_stmt(
        db.get(),
        "INSERT INTO traceloom_graph_launch ("
        "launch_id, db_idx, device_id, graph_provider, graph_event_id, "
        "anchor_id, replay_unit_id, graph_template_id, "
        "graph_launch_occurrence_id, replay_body_template_id, body_id, "
        "member_order, slot_order, correlation_id, match_policy, "
        "association_policy, start_ns, end_ns, dur_us, evidence_level"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?)");
    for (const GraphLaunchSqlRow& row : rows.launches) {
      insert_graph_launch_row(launch_stmt, row);
    }

    SqliteStmt member_stmt(
        db.get(),
        "INSERT INTO traceloom_graph_body_member ("
        "member_id, launch_id, db_idx, device_id, graph_provider, "
        "graph_event_id, replay_unit_id, graph_template_id, "
        "graph_launch_occurrence_id, body_id, replay_body_template_id, "
        "member_order, slot_order, lane_ordinal, task_ordinal, kind, "
        "event_id, task_id, source_table, source_row_id, raw_task_id, "
        "start_ns, end_ns, dur_us, correlation_id, graph_node_id, "
        "original_graph_node_id, match_policy, association_policy, "
        "evidence_level"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const GraphBodyMemberSqlRow& row : rows.members) {
      insert_graph_body_member_row(member_stmt, row);
    }

    materialize_exact_graph_views(db);
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

void replace_graph_replay_rows(const std::string& sqlite_path,

                               const GraphReplaySqlRows& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(
      sqlite_path,
      {event_table_schema(), event_source_table_schema(), anchor_table_schema(),
       cuda_graph_replay_table_schema(), cuda_graph_envelope_table_schema(),
       aclgraph_reconstruction_region_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_cuda_graph_envelope");
    db.exec("DELETE FROM traceloom_cuda_graph_replay");
    db.exec("DELETE FROM traceloom_aclgraph_reconstruction_region");
    db.exec("DELETE FROM traceloom_anchor");
    db.exec("DELETE FROM traceloom_event_source");
    db.exec("DELETE FROM traceloom_event");

    SqliteStmt event_stmt(
        db.get(),
        "INSERT INTO traceloom_event ("
        "event_id, db_idx, device_id, step_idx, source_table, source_key, "
        "stream_id, start_ns, end_ns, dur_us, category, role, semantic_role, "
        "semantic_role_reason, symbol, label, raw_label, op_type, "
        "compute_task_type, family, task_type, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?)");
    for (const EventSqlRow& row : rows.events) {
      insert_event_row(event_stmt, row);
    }

    SqliteStmt event_source_stmt(
        db.get(),
        "INSERT INTO traceloom_event_source ("
        "event_id, source_ordinal, db_idx, device_id, source_table, "
        "source_key, source_role, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    for (const EventSourceSqlRow& row : rows.event_sources) {
      insert_event_source_row(event_source_stmt, row);
    }

    SqliteStmt anchor_stmt(
        db.get(),
        "INSERT INTO traceloom_anchor ("
        "anchor_id, db_idx, device_id, anchor_idx, event_id, step_idx, "
        "symbol, role, label, family, start_ns, end_ns, dur_us"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const AnchorSqlRow& row : rows.anchors) {
      insert_anchor_row(anchor_stmt, row);
    }

    SqliteStmt graph_stmt(
        db.get(),
        "INSERT INTO traceloom_cuda_graph_replay ("
        "graph_event_id, db_idx, device_id, graph_provider, graph_kind, "
        "graph_event_idx, event_id, step_idx, stream_id, correlation_id, "
        "graph_id, graph_exec_id, context_id, start_ns, end_ns, dur_us, "
        "enclosed_event_count, enclosed_event_us, enclosed_kernel_count, "
        "enclosed_kernel_us, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?)");
    for (const GraphReplaySqlRow& row : rows.graph_replays) {
      insert_graph_replay_row(graph_stmt, row);
    }

    SqliteStmt envelope_stmt(
        db.get(),
        "INSERT INTO traceloom_cuda_graph_envelope ("
        "envelope_id, db_idx, device_id, graph_provider, graph_kind, "
        "envelope_idx, graph_event_id, child_event_id, graph_step_idx, "
        "child_step_idx, relation, stream_relation, graph_id, graph_exec_id, "
        "graph_correlation_id, graph_start_ns, graph_end_ns, child_start_ns, "
        "child_end_ns, start_offset_us, end_offset_us, child_dur_us, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?)");
    for (const GraphEnvelopeSqlRow& row : rows.graph_envelopes) {
      insert_graph_envelope_row(envelope_stmt, row);
    }

    SqliteStmt region_stmt(
        db.get(),
        "INSERT INTO traceloom_aclgraph_reconstruction_region ("
        "region_id, db_idx, device_id, graph_provider, candidate_id, "
        "region_order, status, boundary_policy, order_policy, "
        "identity_policy, shape_policy, first_launch_occurrence_id, "
        "last_launch_occurrence_id, observed_launch_count, "
        "expected_launch_count, start_ns, end_ns, dur_us, raw_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?)");
    for (const GraphReconstructionRegionSqlRow& row :
         rows.reconstruction_regions) {
      insert_graph_reconstruction_region_row(region_stmt, row);
    }

    materialize_cuda_graph_views(db);
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

void replace_idle_evidence_rows(const std::string& sqlite_path,
                                const IdleEvidenceSqlRows& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(
      sqlite_path,
      {run_metadata_table_schema(), device_interval_table_schema(),
       stream_state_table_schema(), idle_explanation_table_schema(),
       evidence_link_table_schema(), anchor_idle_explanation_table_schema(),
       node_idle_explanation_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_node_idle_explanation");
    db.exec("DELETE FROM traceloom_anchor_idle_explanation");
    db.exec("DELETE FROM traceloom_evidence_link");
    db.exec("DELETE FROM traceloom_idle_explanation");
    db.exec("DELETE FROM traceloom_stream_state");
    db.exec("DELETE FROM traceloom_device_interval");
    db.exec("DELETE FROM traceloom_run_metadata");

    SqliteStmt metadata_stmt(
        db.get(),
        "INSERT INTO traceloom_run_metadata (run_id, analysis_status, "
        "span_start_ns, span_end_ns, contract_version, "
        "semantic_rules_version, semantic_rules_sha256, "
        "attribution_rule_version, host_api_rules_version, "
        "host_api_rules_sha256, collection_status, db_idx, source_kind, "
        "source_path, metadata_json) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?)");
    for (const RunMetadataSqlRow& row : rows.run_metadata) {
      insert_run_metadata_row(metadata_stmt, row);
    }

    SqliteStmt interval_stmt(
        db.get(),
        "INSERT INTO traceloom_device_interval (interval_id, run_id, db_idx, "
        "device_id, interval_order, start_ns, end_ns, duration_ns, "
        "duration_us, interval_kind, source_count, clock_domain, "
        "contract_version, semantic_rules_version, attribution_rule_version) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const DeviceIntervalSqlRow& row : rows.device_intervals) {
      insert_device_interval_row(interval_stmt, row);
    }

    SqliteStmt state_stmt(
        db.get(),
        "INSERT INTO traceloom_stream_state (state_id, run_id, db_idx, "
        "device_id, stream_id, state_order, start_ns, end_ns, duration_ns, "
        "duration_us, state, source_count, stream_universe_kind, "
        "stream_universe_size, observed_stream_count, "
        "observed_universe_scan_complete, collection_status, clock_domain, "
        "contract_version, semantic_rules_version, attribution_rule_version) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?)");
    for (const StreamStateSqlRow& row : rows.stream_states) {
      insert_stream_state_row(state_stmt, row);
    }

    SqliteStmt explanation_stmt(
        db.get(),
        "INSERT INTO traceloom_idle_explanation (idle_explanation_id, run_id, "
        "gap_interval_id, db_idx, device_id, explanation_order, start_ns, "
        "end_ns, duration_ns, duration_us, category, evidence_level, "
        "evidence_relation, alignment_status, collection_status, reason, "
        "source_count, clock_domain, contract_version, "
        "semantic_rules_version, attribution_rule_version) VALUES (?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const IdleExplanationSqlRow& row : rows.idle_explanations) {
      insert_idle_explanation_row(explanation_stmt, row);
    }

    SqliteStmt evidence_stmt(
        db.get(),
        "INSERT INTO traceloom_evidence_link (owner_kind, owner_id, "
        "evidence_ordinal, source_kind, source_table, source_key, relation, "
        "evidence_level, overlap_start_ns, overlap_end_ns, stream_id, state, "
        "trace_event_id, matched_rule_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?)");
    for (const EvidenceLinkSqlRow& row : rows.evidence_links) {
      insert_evidence_link_row(evidence_stmt, row);
    }

    SqliteStmt anchor_stmt(
        db.get(),
        "INSERT INTO traceloom_anchor_idle_explanation (anchor_id, run_id, "
        "db_idx, device_id, anchor_idx, category, evidence_level, slice_count, "
        "duration_ns, duration_us) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const AnchorIdleExplanationSqlRow& row : rows.anchor_attribution) {
      insert_anchor_idle_row(anchor_stmt, row);
    }

    SqliteStmt node_stmt(
        db.get(),
        "INSERT INTO traceloom_node_idle_explanation (node_id, run_id, db_idx, "
        "device_id, view_name, category, evidence_level, slice_count, "
        "duration_ns, duration_us) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const NodeIdleExplanationSqlRow& row : rows.node_attribution) {
      insert_node_idle_row(node_stmt, row);
    }
    db.exec(
        "CREATE INDEX IF NOT EXISTS idx_traceloom_device_interval_time "
        "ON traceloom_device_interval(run_id, device_id, start_ns, end_ns)");
    db.exec(
        "CREATE INDEX IF NOT EXISTS idx_traceloom_device_interval_id "
        "ON traceloom_device_interval(interval_id)");
    db.exec(
        "CREATE INDEX IF NOT EXISTS idx_traceloom_stream_state_time "
        "ON traceloom_stream_state(run_id, device_id, stream_id, start_ns, "
        "end_ns)");
    db.exec(
        "CREATE INDEX IF NOT EXISTS idx_traceloom_stream_state_id "
        "ON traceloom_stream_state(state_id)");
    db.exec(
        "CREATE INDEX IF NOT EXISTS idx_traceloom_idle_explanation_category "
        "ON traceloom_idle_explanation(run_id, device_id, category, start_ns, "
        "end_ns)");
    db.exec(
        "CREATE INDEX IF NOT EXISTS idx_traceloom_idle_explanation_id "
        "ON traceloom_idle_explanation(idle_explanation_id)");
    db.exec(
        "CREATE INDEX IF NOT EXISTS idx_traceloom_idle_explanation_gap "
        "ON traceloom_idle_explanation(run_id, gap_interval_id, "
        "explanation_order)");
    db.exec(
        "CREATE INDEX IF NOT EXISTS idx_traceloom_evidence_owner "
        "ON traceloom_evidence_link(owner_kind, owner_id, evidence_ordinal)");
    db.exec(
        "CREATE INDEX IF NOT EXISTS idx_traceloom_anchor_idle "
        "ON traceloom_anchor_idle_explanation(anchor_id, category)");
    db.exec(
        "CREATE INDEX IF NOT EXISTS idx_traceloom_node_idle "
        "ON traceloom_node_idle_explanation(node_id, category)");
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
