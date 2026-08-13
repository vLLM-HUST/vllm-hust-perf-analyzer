#include "traceloom/compat/sidecar_writer.h"

#include <stdexcept>
#include <string>

#include "sidecar_row_bindings.h"
#include "sidecar_views.h"
#include "sqlite_support.h"

namespace traceloom::compat {

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


}  // namespace traceloom::compat
