#include "traceloom/compat/sidecar_writer.h"

#include <stdexcept>
#include <string>

#include "sidecar_row_bindings.h"
#include "sidecar_views.h"
#include "sqlite_support.h"

namespace traceloom::compat {

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

void replace_runtime_device_rows(const std::string& sqlite_path,
                                 const RuntimeDeviceSqlRows& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(
      sqlite_path,
      {runtime_call_table_schema(), device_work_table_schema(),
       runtime_device_relation_table_schema(),
       anchor_runtime_relation_table_schema(),
       anchor_host_interval_table_schema(),
       anchor_host_activity_table_schema(),
       anchor_host_api_summary_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_anchor_host_activity");
    db.exec("DELETE FROM traceloom_anchor_host_api_summary");
    db.exec("DELETE FROM traceloom_anchor_host_interval");
    db.exec("DELETE FROM traceloom_anchor_runtime_relation");
    db.exec("DELETE FROM traceloom_runtime_device_relation");
    db.exec("DELETE FROM traceloom_device_work");
    db.exec("DELETE FROM traceloom_runtime_call");

    SqliteStmt runtime_stmt(
        db.get(),
        "INSERT INTO traceloom_runtime_call ("
        "runtime_call_id, db_idx, provider, clock_domain, source_table, "
        "source_key, start_ns, end_ns, dur_us, api_name, api_type, process_id, "
        "thread_id, global_tid, context_id, device_id, correlation_id, "
        "match_policy, raw_json) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?)");
    for (const RuntimeCallSqlRow& row : rows.runtime_calls) {
      insert_runtime_call_row(runtime_stmt, row);
    }

    SqliteStmt work_stmt(
        db.get(),
        "INSERT INTO traceloom_device_work ("
        "device_work_id, db_idx, provider, device_id, work_kind, event_id, "
        "task_id, "
        "graph_launch_occurrence_id, source_table, source_key, start_ns, "
        "end_ns, dur_us, symbol, raw_json) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?)");
    for (const DeviceWorkSqlRow& row : rows.device_works) {
      insert_device_work_row(work_stmt, row);
    }

    SqliteStmt relation_stmt(
        db.get(),
        "INSERT INTO traceloom_runtime_device_relation ("
        "relation_id, db_idx, runtime_call_id, device_work_id, relation_kind, "
        "match_policy, evidence_level, support_state, cardinality, "
        "runtime_candidate_count, device_candidate_count, correlation_id, "
        "raw_json) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const RuntimeDeviceRelationSqlRow& row : rows.relations) {
      insert_runtime_device_relation_row(relation_stmt, row);
    }

    SqliteStmt anchor_relation_stmt(
        db.get(),
        "INSERT INTO traceloom_anchor_runtime_relation ("
        "anchor_id, relation_id, runtime_call_id, device_work_id, "
        "endpoint_kind) VALUES (?, ?, ?, ?, ?)");
    for (const AnchorRuntimeRelationSqlRow& row : rows.anchor_relations) {
      insert_anchor_runtime_relation_row(anchor_relation_stmt, row);
    }

    SqliteStmt interval_stmt(
        db.get(),
        "INSERT INTO traceloom_anchor_host_interval ("
        "interval_id, db_idx, device_id, left_anchor_id, right_anchor_id, "
        "left_runtime_call_id, right_runtime_call_id, left_endpoint_count, "
        "right_endpoint_count, provider, clock_domain, host_start_ns, "
        "host_end_ns, scope_policy, process_id, thread_id, support_state) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const AnchorHostIntervalSqlRow& row : rows.host_intervals) {
      insert_anchor_host_interval_row(interval_stmt, row);
    }

    SqliteStmt activity_stmt(
        db.get(),
        "INSERT INTO traceloom_anchor_host_activity ("
        "interval_id, runtime_call_id, observed_order) VALUES (?, ?, ?)");
    for (const AnchorHostActivitySqlRow& row : rows.host_activities) {
      insert_anchor_host_activity_row(activity_stmt, row);
    }

    SqliteStmt summary_stmt(
        db.get(),
        "INSERT INTO traceloom_anchor_host_api_summary ("
        "interval_id, api_family, call_count, distinct_api_name_count, "
        "scheduled_call_us, scheduled_overlap_us) VALUES (?, ?, ?, ?, ?, ?)");
    for (const AnchorHostApiSummarySqlRow& row : rows.host_api_summaries) {
      insert_anchor_host_api_summary_row(summary_stmt, row);
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


}  // namespace traceloom::compat
