#include "traceloom/compat/event_reconciliation_rows.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/schema.h"
#include "traceloom/compat/timeline_rows.h"
#include "sqlite_support.h"

namespace traceloom::compat {
namespace {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)

std::string decision_id(EventReconciliationDecisionId id) {
  return "reconciliation-decision-" + std::to_string(id.value());
}

void finish_row(SqliteStmt& stmt, const char* failure) {
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string(failure) + ": " +
                             sqlite3_errmsg(stmt.db()));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

void bind_optional_id(SqliteStmt& stmt, int column, std::int64_t value) {
  if (value < 0) {
    bind_null(stmt, column);
  } else {
    bind_int64(stmt, column, value);
  }
}

#endif

}  // namespace

void replace_event_reconciliation_rows(const std::string& sqlite_path,
                                       const NativeIr& ir,
                                       std::uint32_t db_idx) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  {
    SqliteDb db(sqlite_path);
    const bool legacy_rule =
        table_has_column(db.get(),
                         "traceloom_event_reconciliation_rule", "rule_id") &&
        !table_has_column(db.get(),
                          "traceloom_event_reconciliation_rule",
                          "task_op_type");
    const bool legacy_member =
        table_has_column(db.get(),
                         "traceloom_event_reconciliation_member",
                         "event_id") &&
        !table_has_column(db.get(),
                          "traceloom_event_reconciliation_member",
                          "source_domain");
    if (legacy_rule || legacy_member) {
      db.exec("BEGIN IMMEDIATE");
      try {
        db.exec("DROP VIEW IF EXISTS traceloom_v_event_reconciliation");
        db.exec(
            "DROP INDEX IF EXISTS "
            "traceloom_reconciliation_member_event_idx");
        db.exec(
            "DROP INDEX IF EXISTS "
            "traceloom_reconciliation_decision_status_idx");
        db.exec(
            "DROP TABLE IF EXISTS "
            "traceloom_event_reconciliation_member");
        db.exec(
            "DROP TABLE IF EXISTS "
            "traceloom_event_reconciliation_decision");
        db.exec(
            "DROP TABLE IF EXISTS traceloom_event_reconciliation_rule");
        db.exec(
            "DROP TABLE IF EXISTS traceloom_event_reconciliation_policy");
        db.exec("COMMIT");
      } catch (...) {
        try {
          db.exec("ROLLBACK");
        } catch (...) {
        }
        throw;
      }
    }
  }
  materialize_compatibility_schema(
      sqlite_path,
      {event_reconciliation_policy_table_schema(),
       event_reconciliation_rule_table_schema(),
       event_reconciliation_decision_table_schema(),
       event_reconciliation_member_table_schema()});

  std::unordered_map<TraceEventId::value_type, std::string>
      anchor_by_event;
  for (const AnchorRow& anchor : ir.anchors.rows()) {
    if (anchor.trace_event_id.valid()) {
      anchor_by_event.emplace(anchor.trace_event_id.value(),
                              anchor_compat_id(anchor.id));
    }
  }
  std::unordered_map<EventReconciliationDecisionId::value_type,
                     std::uint64_t>
      member_counts;
  std::unordered_map<EventReconciliationDecisionId::value_type,
                     std::uint64_t>
      next_member_order;
  for (const EventReconciliationMemberRow& member :
       ir.event_reconciliation.members) {
    ++member_counts[member.decision_id.value()];
  }

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DROP VIEW IF EXISTS traceloom_v_event_reconciliation");
    db.exec("DROP INDEX IF EXISTS traceloom_reconciliation_member_event_idx");
    db.exec("DROP INDEX IF EXISTS traceloom_reconciliation_decision_status_idx");
    db.exec("DELETE FROM traceloom_event_reconciliation_member");
    db.exec("DELETE FROM traceloom_event_reconciliation_decision");
    db.exec("DELETE FROM traceloom_event_reconciliation_rule");
    db.exec("DELETE FROM traceloom_event_reconciliation_policy");

    const EventReconciliationPolicySnapshot& policy =
        ir.event_reconciliation.policy;
    if (ir.event_reconciliation.initialized()) {
      SqliteStmt policy_stmt(
          db.get(),
          "INSERT INTO traceloom_event_reconciliation_policy ("
          "policy_id, policy_version, manifest_schema, source_manifest, "
          "manifest_sha256, unmatched_behavior, description"
          ") VALUES (?, ?, ?, ?, ?, ?, ?)");
      bind_text(policy_stmt, 1, policy.policy_id);
      bind_text(policy_stmt, 2, policy.policy_version);
      bind_text(policy_stmt, 3, policy.manifest_schema);
      bind_text(policy_stmt, 4, policy.source_manifest);
      bind_text(policy_stmt, 5, policy.manifest_sha256);
      bind_text(policy_stmt, 6, policy.unmatched_behavior);
      bind_text(policy_stmt, 7,
                "Sparse identity reconciliation for duplicate profiler "
                "observations; unmatched and uncertain events stay "
                "independent");
      finish_row(policy_stmt,
                 "failed to insert event-reconciliation policy row");

      SqliteStmt rule_stmt(
          db.get(),
          "INSERT INTO traceloom_event_reconciliation_rule ("
          "policy_id, policy_version, rule_id, priority, provider_scope, "
          "source_domain, task_type, generic_context_id, "
          "concrete_context_id, min_contained_fraction, task_op_type, "
          "communication_op_name_prefix, identity_policy, rule_origin, "
          "rule_origin_sha256, source_line, note"
          ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
      for (const EventReconciliationRuleSnapshot& rule : policy.rules) {
        bind_text(rule_stmt, 1, policy.policy_id);
        bind_text(rule_stmt, 2, policy.policy_version);
        bind_text(rule_stmt, 3, rule.rule_id);
        bind_int64(rule_stmt, 4, rule.priority);
        bind_text(rule_stmt, 5, rule.provider_scope);
        bind_text(rule_stmt, 6, rule.source_domain);
        bind_text(rule_stmt, 7, rule.task_type);
        bind_int64(rule_stmt, 8, rule.generic_context_id);
        bind_int64(rule_stmt, 9, rule.concrete_context_id);
        bind_double(rule_stmt, 10, rule.min_contained_fraction);
        bind_text(rule_stmt, 11, rule.task_op_type);
        bind_text(rule_stmt, 12, rule.communication_op_name_prefix);
        bind_text(rule_stmt, 13, rule.identity_policy);
        bind_text(rule_stmt, 14, rule.rule_origin);
        bind_text(rule_stmt, 15, rule.rule_origin_sha256);
        bind_int64(rule_stmt, 16,
                   static_cast<std::int64_t>(rule.source_line));
        bind_text(rule_stmt, 17, rule.note);
        finish_row(rule_stmt,
                   "failed to insert event-reconciliation rule row");
      }
    }

    SqliteStmt decision_stmt(
        db.get(),
        "INSERT INTO traceloom_event_reconciliation_decision ("
        "decision_id, db_idx, policy_id, policy_version, rule_id, status, "
        "reason_code, canonical_event_id, envelope_event_id, "
        "canonical_anchor_id, canonical_start_ns, canonical_end_ns, "
        "contained_fraction, member_count"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const EventReconciliationDecisionRow& decision :
         ir.event_reconciliation.decisions) {
      bind_text(decision_stmt, 1, decision_id(decision.id));
      bind_int64(decision_stmt, 2, db_idx);
      bind_text(decision_stmt, 3, policy.policy_id);
      bind_text(decision_stmt, 4, policy.policy_version);
      bind_text(decision_stmt, 5, decision.rule_id);
      bind_text(decision_stmt, 6,
                event_reconciliation_status_name(decision.status));
      bind_text(decision_stmt, 7, decision.reason_code);
      if (decision.canonical_event_id.valid()) {
        bind_text(decision_stmt, 8,
                  trace_event_compat_id(decision.canonical_event_id));
      } else {
        bind_null(decision_stmt, 8);
      }
      if (decision.envelope_event_id.valid()) {
        bind_text(decision_stmt, 9,
                  trace_event_compat_id(decision.envelope_event_id));
      } else {
        bind_null(decision_stmt, 9);
      }
      const auto anchor = decision.canonical_event_id.valid()
                              ? anchor_by_event.find(
                                    decision.canonical_event_id.value())
                              : anchor_by_event.end();
      if (anchor == anchor_by_event.end()) {
        bind_null(decision_stmt, 10);
      } else {
        bind_text(decision_stmt, 10, anchor->second);
      }
      if (decision.status == EventReconciliationStatus::kReconciled) {
        bind_int64(decision_stmt, 11, decision.canonical_start_ns);
        bind_int64(decision_stmt, 12, decision.canonical_end_ns);
        bind_double(decision_stmt, 13, decision.contained_fraction);
      } else {
        bind_null(decision_stmt, 11);
        bind_null(decision_stmt, 12);
        bind_null(decision_stmt, 13);
      }
      bind_int64(decision_stmt, 14, member_counts[decision.id.value()]);
      finish_row(decision_stmt,
                 "failed to insert event-reconciliation decision row");
    }

    SqliteStmt member_stmt(
        db.get(),
        "INSERT INTO traceloom_event_reconciliation_member ("
        "decision_id, member_order, db_idx, event_id, source_domain, task_id, "
        "communication_op_id, source_path, source_table, source_key, "
        "device_id, stream_id, raw_task_id, raw_global_task_id, "
        "raw_connection_id, raw_context_id, member_role, contributes_timing, "
        "contributes_symbol, contributes_cost, "
        "retained_as_normalized_evidence"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?)");
    for (const EventReconciliationMemberRow& member :
         ir.event_reconciliation.members) {
      if (!member.event_id.valid() ||
          member.event_id.value() >= ir.trace_events.size()) {
        throw std::logic_error(
            "event-reconciliation member references an invalid event");
      }
      const TaskRow* task = nullptr;
      const CommunicationOpRow* communication = nullptr;
      if (member.task_id.valid()) {
        if (member.task_id.value() >= ir.tasks.size()) {
          throw std::logic_error(
              "event-reconciliation member references an invalid task");
        }
        task = &ir.tasks.row(member.task_id);
      }
      if (member.communication_op_id.valid()) {
        if (member.communication_op_id.value() >=
            ir.communication_ops.size()) {
          throw std::logic_error(
              "event-reconciliation member references an invalid "
              "communication op");
        }
        communication =
            &ir.communication_ops.row(member.communication_op_id);
      }
      if ((task == nullptr) == (communication == nullptr)) {
        throw std::logic_error(
            "event-reconciliation member must reference exactly one typed "
            "observation");
      }
      const TraceEventRow& event = ir.trace_events.row(member.event_id);
      const SourceRefRow& source = ir.source_refs.row(event.source_ref_id);
      bind_text(member_stmt, 1, decision_id(member.decision_id));
      bind_int64(member_stmt, 2,
                 next_member_order[member.decision_id.value()]++);
      bind_int64(member_stmt, 3, db_idx);
      bind_text(member_stmt, 4, trace_event_compat_id(member.event_id));
      bind_text(member_stmt, 5,
                task == nullptr ? "communication_op" : "task");
      if (task == nullptr) {
        bind_null(member_stmt, 6);
        bind_int64(member_stmt, 7, communication->id.value());
      } else {
        bind_int64(member_stmt, 6, task->id.value());
        bind_null(member_stmt, 7);
      }
      bind_text(member_stmt, 8, source.source_path);
      bind_text(member_stmt, 9, source.table_name);
      bind_text(member_stmt, 10, std::to_string(event.source_row_id));
      bind_int64(member_stmt, 11, event.device_id);
      bind_int64(member_stmt, 12, event.stream_id);
      if (task == nullptr) {
        bind_null(member_stmt, 13);
      } else {
        bind_int64(member_stmt, 13,
                   static_cast<std::int64_t>(task->raw_task_id));
      }
      bind_optional_id(member_stmt, 14,
                       task == nullptr ? -1 : task->raw_global_task_id);
      bind_optional_id(
          member_stmt, 15,
          task == nullptr ? communication->raw_connection_id
                          : task->raw_connection_id);
      bind_optional_id(member_stmt, 16,
                       task == nullptr ? -1 : task->raw_context_id);
      bind_text(member_stmt, 17,
                event_reconciliation_member_role_name(member.role));
      bind_int64(member_stmt, 18, member.contributes_timing ? 1 : 0);
      bind_int64(member_stmt, 19, member.contributes_symbol ? 1 : 0);
      bind_int64(member_stmt, 20, member.contributes_cost ? 1 : 0);
      bind_int64(member_stmt, 21,
                 member.retained_as_normalized_evidence ? 1 : 0);
      finish_row(member_stmt,
                 "failed to insert event-reconciliation member row");
    }

    db.exec(
        "CREATE INDEX traceloom_reconciliation_member_event_idx ON "
        "traceloom_event_reconciliation_member(db_idx, event_id)");
    db.exec(
        "CREATE INDEX traceloom_reconciliation_decision_status_idx ON "
        "traceloom_event_reconciliation_decision(db_idx, status, rule_id)");
    db.exec(
        "CREATE VIEW traceloom_v_event_reconciliation AS SELECT "
        "m.*, d.policy_id, d.policy_version, d.rule_id, d.status, "
        "d.reason_code, d.canonical_event_id, d.envelope_event_id, "
        "d.canonical_anchor_id, d.canonical_start_ns, d.canonical_end_ns, "
        "d.contained_fraction, e.symbol AS observed_symbol, "
        "e.start_ns AS observed_start_ns, e.end_ns AS observed_end_ns, "
        "e.dur_us AS observed_dur_us, a.symbol AS canonical_symbol, "
        "a.start_ns AS canonical_anchor_start_ns, "
        "a.end_ns AS canonical_anchor_end_ns, "
        "a.dur_us AS canonical_anchor_dur_us "
        "FROM traceloom_event_reconciliation_member m "
        "JOIN traceloom_event_reconciliation_decision d ON "
        "d.decision_id = m.decision_id AND d.db_idx = m.db_idx "
        "LEFT JOIN traceloom_event e ON e.event_id = m.event_id AND "
        "e.db_idx = m.db_idx LEFT JOIN traceloom_anchor a ON "
        "a.anchor_id = d.canonical_anchor_id AND a.db_idx = d.db_idx");
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
  (void)ir;
  (void)db_idx;
  throw std::runtime_error(
      "event-reconciliation writer requires SQLite support");
#endif
}

}  // namespace traceloom::compat
