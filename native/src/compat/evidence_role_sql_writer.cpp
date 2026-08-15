#include "evidence_role_sql_internal.h"

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <sqlite3.h>

namespace traceloom::compat::detail {
namespace {

class Db {
 public:
  explicit Db(const std::string& path) {
    if (sqlite3_open_v2(path.c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        nullptr) != SQLITE_OK) {
      const std::string message = db_ ? sqlite3_errmsg(db_) : "open failed";
      if (db_ != nullptr) {
        sqlite3_close(db_);
      }
      db_ = nullptr;
      throw std::runtime_error(
          "failed to open evidence-role database: " + message);
    }
  }
  ~Db() {
    if (db_ != nullptr) {
      sqlite3_close(db_);
    }
  }
  Db(const Db&) = delete;
  Db& operator=(const Db&) = delete;
  sqlite3* get() const { return db_; }
  void exec(const std::string& sql) {
    char* error = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
      const std::string message = error ? error : sqlite3_errmsg(db_);
      sqlite3_free(error);
      throw std::runtime_error(
          "failed to materialize evidence-role SQL: " + message);
    }
  }

 private:
  sqlite3* db_ = nullptr;
};

class Stmt {
 public:
  Stmt(sqlite3* db, const char* sql) : db_(db) {
    if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
      throw std::runtime_error(
          "failed to prepare evidence-role SQL: " +
          std::string(sqlite3_errmsg(db)));
    }
  }
  ~Stmt() {
    if (stmt_ != nullptr) {
      sqlite3_finalize(stmt_);
    }
  }
  sqlite3_stmt* get() const { return stmt_; }
  void text(int index, const std::string& value) {
    if (sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT) !=
        SQLITE_OK) {
      fail("text bind");
    }
  }
  // Use only when `value` remains alive through run(). SQLite can then read
  // the caller-owned bytes directly instead of copying every field into a
  // transient binding buffer before the immediate insert step.
  void borrowed_text(int index, const std::string& value) {
    if (sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_STATIC) !=
        SQLITE_OK) {
      fail("borrowed text bind");
    }
  }
  void integer(int index, std::int64_t value) {
    if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK) {
      fail("integer bind");
    }
  }
  void boolean(int index, bool value) { integer(index, value ? 1 : 0); }
  void null(int index) {
    if (sqlite3_bind_null(stmt_, index) != SQLITE_OK) {
      fail("null bind");
    }
  }
  void run() {
    if (sqlite3_step(stmt_) != SQLITE_DONE) {
      fail("insert");
    }
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
  }

 private:
  [[noreturn]] void fail(const char* operation) const {
    throw std::runtime_error(std::string("evidence-role ") + operation +
                             " failed: " + sqlite3_errmsg(db_));
  }
  sqlite3* db_ = nullptr;
  sqlite3_stmt* stmt_ = nullptr;
};

void create_schema(Db& db) {
  db.exec(R"SQL(
CREATE TABLE IF NOT EXISTS traceloom_evidence_role_policy (
  policy_id TEXT NOT NULL PRIMARY KEY, policy_version TEXT NOT NULL,
  manifest_schema TEXT NOT NULL, manifest_sha256 TEXT NOT NULL,
  provider_scopes TEXT NOT NULL, fallback_identity_role TEXT NOT NULL,
  fallback_cost_treatment TEXT NOT NULL,
  fallback_context_treatment TEXT NOT NULL,
  fallback_provenance_treatment TEXT NOT NULL,
  missing_evidence_behavior TEXT NOT NULL, input_format TEXT NOT NULL,
  input_sources TEXT NOT NULL, effective_config_sha256 TEXT NOT NULL,
  config_overrides TEXT NOT NULL, config_override_contract TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS traceloom_evidence_role_rule (
  policy_id TEXT NOT NULL, rule_id TEXT NOT NULL, rule_class TEXT NOT NULL,
  priority INTEGER NOT NULL, declaration_order INTEGER NOT NULL,
  provider_scope TEXT NOT NULL, source_domain TEXT NOT NULL,
  match_field TEXT NOT NULL, match_kind TEXT NOT NULL, pattern TEXT NOT NULL,
  role TEXT NOT NULL, required_fields TEXT NOT NULL,
  structural_participation TEXT NOT NULL, cost_treatment TEXT NOT NULL,
  context_treatment TEXT NOT NULL, provenance_treatment TEXT NOT NULL,
  missing_evidence_behavior TEXT NOT NULL,
  concrete_identity_behavior TEXT NOT NULL, note TEXT NOT NULL,
  source_line INTEGER NOT NULL,
  PRIMARY KEY(policy_id, rule_id));
CREATE TABLE IF NOT EXISTS traceloom_evidence_role_decision (
  decision_id TEXT NOT NULL PRIMARY KEY, db_idx INTEGER NOT NULL,
  device_id INTEGER NOT NULL, task_id INTEGER, event_id TEXT NOT NULL,
  source_ref_id INTEGER NOT NULL, source_domain TEXT NOT NULL,
  input_provider_scope TEXT NOT NULL, policy_id TEXT NOT NULL,
  policy_version TEXT NOT NULL, manifest_sha256 TEXT NOT NULL,
  policy_role TEXT, final_role TEXT NOT NULL, rule_id TEXT NOT NULL,
  rule_class TEXT NOT NULL, matched_rule INTEGER NOT NULL,
  priority INTEGER NOT NULL, declaration_order INTEGER NOT NULL,
  policy_structural_participation TEXT NOT NULL,
  effective_structural_participation TEXT NOT NULL,
  support_state TEXT NOT NULL, reason_code TEXT NOT NULL,
  available_fields TEXT NOT NULL, required_fields TEXT NOT NULL,
  missing_required_fields TEXT NOT NULL,
  missing_capability_rule_ids TEXT NOT NULL, cost_treatment TEXT NOT NULL,
  context_treatment TEXT NOT NULL, provenance_treatment TEXT NOT NULL,
  source_table TEXT NOT NULL, source_key TEXT NOT NULL,
  start_ns INTEGER NOT NULL, end_ns INTEGER NOT NULL,
  duration_ns INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS traceloom_evidence_role_placement (
  decision_id TEXT NOT NULL, placement_order INTEGER NOT NULL,
  placement_kind TEXT NOT NULL, placement_id TEXT NOT NULL,
  owner_id TEXT, relation_name TEXT NOT NULL, support_state TEXT NOT NULL,
  reason_code TEXT NOT NULL,
  PRIMARY KEY(decision_id, placement_order));
CREATE TABLE IF NOT EXISTS traceloom_replay_unit_anchor (
  replay_unit_id TEXT NOT NULL, anchor_id TEXT NOT NULL,
  db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL,
  PRIMARY KEY(replay_unit_id, anchor_id, db_idx, device_id));
CREATE TABLE IF NOT EXISTS traceloom_protected_interval (
  protected_interval_id TEXT NOT NULL PRIMARY KEY,
  db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL, kind TEXT NOT NULL,
  boundary_policy TEXT NOT NULL, first_anchor_id TEXT NOT NULL,
  last_anchor_id TEXT NOT NULL, replay_unit_id TEXT,
  evidence_source_ref_id INTEGER NOT NULL, start_ns INTEGER NOT NULL,
  end_ns INTEGER NOT NULL, support_state TEXT NOT NULL,
  reason_code TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS traceloom_evidence_role_issue (
  issue_id TEXT NOT NULL PRIMARY KEY, decision_id TEXT NOT NULL,
  code TEXT NOT NULL, support_state TEXT NOT NULL,
  related_ids TEXT NOT NULL);
)SQL");
}

void insert_policy_and_rules(Db& db,
                             const SignalClassificationRuleset& ruleset) {
  const SignalClassificationPolicyMetadata& metadata = ruleset.metadata();
  Stmt policy(
      db.get(),
      "INSERT INTO traceloom_evidence_role_policy VALUES "
      "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
  int i = 1;
  policy.text(i++, metadata.policy_id);
  policy.text(i++, metadata.policy_version);
  policy.text(i++, metadata.manifest_schema);
  policy.text(i++, metadata.manifest_sha256);
  policy.text(i++, metadata.provider_scopes);
  policy.text(i++, signal_role_name(metadata.fallback_identity_role));
  policy.text(i++,
              signal_cost_treatment_name(metadata.fallback_cost_treatment));
  policy.text(
      i++, signal_context_treatment_name(metadata.fallback_context_treatment));
  policy.text(i++, signal_provenance_treatment_name(
                       metadata.fallback_provenance_treatment));
  policy.text(i++, signal_missing_evidence_behavior_name(
                       metadata.missing_evidence_behavior));
  policy.text(i++, metadata.manifest_format);
  policy.text(i++, metadata.manifest_source_path);
  policy.text(i++, metadata.effective_config_sha256);
  policy.text(i++, metadata.config_overrides);
  policy.text(i++,
              "explicit --classification-rules replaces environment/default; "
              "--extend-classification-rules is applied afterward; repeatable "
              "--classification-rule-override entries overwrite typed rule "
              "fields last");
  policy.run();

  Stmt rule(db.get(),
            "INSERT INTO traceloom_evidence_role_rule VALUES "
            "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
  const auto insert_rule = [&](const std::string& rule_id,
                               const std::string& rule_class,
                               std::int32_t priority,
                               std::uint64_t declaration_order,
                               const std::string& provider_scope,
                               const std::string& source_domain,
                               const std::string& match_field,
                               const std::string& match_kind,
                               const std::string& pattern,
                               const std::string& role_name,
                               const std::string& required_fields,
                               const std::string& participation,
                               const std::string& cost,
                               const std::string& context,
                               const std::string& provenance,
                               const std::string& missing,
                               const std::string& concrete,
                               const std::string& note,
                               std::uint64_t source_line) {
    int index = 1;
    rule.text(index++, metadata.policy_id);
    rule.text(index++, rule_id);
    rule.text(index++, rule_class);
    rule.integer(index++, priority);
    rule.integer(index++, static_cast<std::int64_t>(declaration_order));
    rule.text(index++, provider_scope);
    rule.text(index++, source_domain);
    rule.text(index++, match_field);
    rule.text(index++, match_kind);
    rule.text(index++, pattern);
    rule.text(index++, role_name);
    rule.text(index++, required_fields);
    rule.text(index++, participation);
    rule.text(index++, cost);
    rule.text(index++, context);
    rule.text(index++, provenance);
    rule.text(index++, missing);
    rule.text(index++, concrete);
    rule.text(index++, note);
    rule.integer(index++, static_cast<std::int64_t>(source_line));
    rule.run();
  };
  for (const SignalClassificationRule& item : ruleset.rules()) {
    insert_rule(
        item.rule_id, "positive_policy", item.priority,
        item.declaration_order, item.provider_scope, item.source_domain,
        signal_match_field_name(item.field),
        signal_match_kind_name(item.match), item.pattern,
        signal_role_name(item.role), item.required_fields,
        signal_structural_participation_name(item.structural_participation),
        signal_cost_treatment_name(item.cost_treatment),
        signal_context_treatment_name(item.context_treatment),
        signal_provenance_treatment_name(item.provenance_treatment),
        signal_missing_evidence_behavior_name(item.missing_evidence_behavior),
        signal_concrete_identity_behavior_name(item.concrete_identity_behavior),
        item.note, item.source_line);
  }
  insert_rule(
      "fallback.unknown_observation", "fallback", 0,
      ruleset.rules().size(), "any", "task", "none", "fallback", "",
      "unknown_anchor", "", "identity",
      signal_cost_treatment_name(metadata.fallback_cost_treatment),
      signal_context_treatment_name(metadata.fallback_context_treatment),
      signal_provenance_treatment_name(
          metadata.fallback_provenance_treatment),
      signal_missing_evidence_behavior_name(metadata.missing_evidence_behavior),
      "apply", "Unknown-first fallback when no supported rule admits an event",
      0);

  struct SystemRule {
    const char* id;
    const char* rule_class;
    const char* role;
    const char* required;
    const char* participation;
    const char* note;
  };
  const SystemRule system_rules[] = {
      {"system.protected_composite", "protected_membership",
       "protected_boundary", "replay_unit_membership", "atomic_boundary",
       "Provider composite membership protects generic discovery boundaries"},
      {"system.communication_anchor", "provider_relation", "anchor",
       "communication_membership", "identity",
       "Communication membership is represented by a communication anchor"},
      {"system.event_reconciliation", "provider_relation", "anchor",
       "event_reconciliation_membership", "identity",
       "A reconciled timing envelope is represented by its canonical anchor"},
      {"system.analysis_config_exclusion", "analysis_config", "auxiliary",
       "task_type", "excluded",
       "Explicit analysis configuration excludes this task type"},
      {"system.unfiltered_task_anchor", "analysis_config", "anchor",
       "task_event", "identity",
       "Auxiliary filtering is disabled for this analysis"},
      {"system.existing_event_anchor", "structural_membership", "anchor",
       "anchor", "identity", "A non-task normalized event owns an anchor"},
      {"system.unsupported_event_kind", "unsupported", "unsupported",
       "normalized_event", "not_applicable",
       "The normalized event is retained but is not a projection candidate"},
  };
  for (const SystemRule& item : system_rules) {
    insert_rule(item.id, item.rule_class, 0, 0, "any", "system",
                "membership", "typed", "", item.role, item.required,
                item.participation, "retained_for_attribution", "retained",
                "retained", "continue_or_fallback", "apply", item.note, 0);
  }
}

void insert_decisions(Db& db, const std::vector<EvidenceRoleDecisionRow>& rows) {
  Stmt decision(
      db.get(),
      "INSERT INTO traceloom_evidence_role_decision VALUES "
      "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
  Stmt placement(
      db.get(),
      "INSERT INTO traceloom_evidence_role_placement VALUES "
      "(?,?,?,?,?,?,?,?)");
  Stmt issue(db.get(),
             "INSERT INTO traceloom_evidence_role_issue VALUES "
             "(?,?,?,?,?)");
  std::uint64_t issue_index = 0;
  for (const EvidenceRoleDecisionRow& row : rows) {
    int i = 1;
    decision.borrowed_text(i++, row.decision_id);
    decision.integer(i++, row.db_idx);
    decision.integer(i++, row.device_id);
    if (row.task_id < 0) {
      decision.null(i++);
    } else {
      decision.integer(i++, row.task_id);
    }
    decision.borrowed_text(i++, row.event_id);
    decision.integer(i++, row.source_ref_id);
    decision.borrowed_text(i++, row.source_domain);
    decision.borrowed_text(i++, row.input_provider_scope);
    decision.borrowed_text(i++, row.policy_id);
    decision.borrowed_text(i++, row.policy_version);
    decision.borrowed_text(i++, row.manifest_sha256);
    if (row.policy_role.empty()) {
      decision.null(i++);
    } else {
      decision.borrowed_text(i++, row.policy_role);
    }
    decision.borrowed_text(i++, row.final_role);
    decision.borrowed_text(i++, row.rule_id);
    decision.borrowed_text(i++, row.rule_class);
    decision.boolean(i++, row.matched_rule);
    decision.integer(i++, row.priority);
    decision.integer(i++, static_cast<std::int64_t>(row.declaration_order));
    decision.borrowed_text(i++, row.policy_structural_participation);
    decision.borrowed_text(i++, row.effective_structural_participation);
    decision.borrowed_text(i++, row.support_state);
    decision.borrowed_text(i++, row.reason_code);
    decision.borrowed_text(i++, row.available_fields);
    decision.borrowed_text(i++, row.required_fields);
    decision.borrowed_text(i++, row.missing_required_fields);
    decision.borrowed_text(i++, row.missing_capability_rule_ids);
    decision.borrowed_text(i++, row.cost_treatment);
    decision.borrowed_text(i++, row.context_treatment);
    decision.borrowed_text(i++, row.provenance_treatment);
    decision.borrowed_text(i++, row.source_table);
    decision.borrowed_text(i++, row.source_key);
    decision.integer(i++, row.start_ns);
    decision.integer(i++, row.end_ns);
    decision.integer(i++, row.duration_ns);
    decision.run();

    for (const EvidenceRolePlacementRow& item : row.placements) {
      int j = 1;
      placement.borrowed_text(j++, row.decision_id);
      placement.integer(j++, item.placement_order);
      placement.borrowed_text(j++, item.placement_kind);
      placement.borrowed_text(j++, item.placement_id);
      if (item.owner_id.empty()) {
        placement.null(j++);
      } else {
        placement.borrowed_text(j++, item.owner_id);
      }
      placement.borrowed_text(j++, item.relation_name);
      placement.borrowed_text(j++, item.support_state);
      placement.borrowed_text(j++, item.reason_code);
      placement.run();
    }
    for (const EvidenceRoleIssueRow& item : row.issues) {
      issue.text(1, "role-issue-" + std::to_string(issue_index++));
      issue.borrowed_text(2, row.decision_id);
      issue.borrowed_text(3, item.code);
      issue.borrowed_text(4, item.support_state);
      issue.borrowed_text(5, item.related_ids);
      issue.run();
    }
  }
}

void insert_protected_intervals(
    Db& db, const std::vector<ProtectedIntervalSqlRow>& rows) {
  Stmt stmt(db.get(),
            "INSERT INTO traceloom_protected_interval VALUES "
            "(?,?,?,?,?,?,?,?,?,?,?,?,?)");
  for (const ProtectedIntervalSqlRow& row : rows) {
    int i = 1;
    stmt.text(i++, row.protected_interval_id);
    stmt.integer(i++, row.db_idx);
    stmt.integer(i++, row.device_id);
    stmt.text(i++, row.kind);
    stmt.text(i++, row.boundary_policy);
    stmt.text(i++, row.first_anchor_id);
    stmt.text(i++, row.last_anchor_id);
    if (row.replay_unit_id.empty()) {
      stmt.null(i++);
    } else {
      stmt.text(i++, row.replay_unit_id);
    }
    stmt.integer(i++, row.evidence_source_ref_id);
    stmt.integer(i++, row.start_ns);
    stmt.integer(i++, row.end_ns);
    stmt.text(i++, row.support_state);
    stmt.text(i++, row.reason_code);
    stmt.run();
  }
}

void insert_replay_unit_anchors(
    Db& db, const std::vector<ReplayUnitAnchorSqlRow>& rows) {
  Stmt stmt(db.get(),
            "INSERT INTO traceloom_replay_unit_anchor VALUES (?,?,?,?)");
  for (const ReplayUnitAnchorSqlRow& row : rows) {
    stmt.text(1, row.replay_unit_id);
    stmt.text(2, row.anchor_id);
    stmt.integer(3, row.db_idx);
    stmt.integer(4, row.device_id);
    stmt.run();
  }
}

void create_indexes_and_views(Db& db) {
  db.exec(R"SQL(
CREATE UNIQUE INDEX IF NOT EXISTS idx_traceloom_evidence_role_decision_event
  ON traceloom_evidence_role_decision(db_idx, event_id);
CREATE INDEX IF NOT EXISTS idx_traceloom_evidence_role_decision_filter
  ON traceloom_evidence_role_decision(
    db_idx, input_provider_scope, final_role, support_state);
CREATE INDEX IF NOT EXISTS idx_traceloom_evidence_role_decision_rule
  ON traceloom_evidence_role_decision(policy_id, rule_id, support_state);
CREATE INDEX IF NOT EXISTS idx_traceloom_evidence_role_decision_source
  ON traceloom_evidence_role_decision(source_table, source_key, db_idx);
CREATE INDEX IF NOT EXISTS idx_traceloom_evidence_role_placement_member
  ON traceloom_evidence_role_placement(
    placement_kind, placement_id, decision_id);
CREATE INDEX IF NOT EXISTS idx_traceloom_evidence_role_placement_owner
  ON traceloom_evidence_role_placement(
    owner_id, placement_kind, decision_id);
CREATE INDEX IF NOT EXISTS idx_traceloom_replay_unit_anchor_anchor
  ON traceloom_replay_unit_anchor(anchor_id, db_idx, device_id);
CREATE INDEX IF NOT EXISTS idx_traceloom_evidence_role_issue_code
  ON traceloom_evidence_role_issue(code, support_state, decision_id);
CREATE INDEX IF NOT EXISTS idx_traceloom_protected_interval_range
  ON traceloom_protected_interval(
    db_idx, device_id, start_ns, end_ns, protected_interval_id);

DROP VIEW IF EXISTS traceloom_v_evidence_role_decision;
CREATE VIEW traceloom_v_evidence_role_decision AS
SELECT d.*, e.dur_us, e.symbol, e.label, e.raw_label, e.op_type,
       e.compute_task_type, e.task_type,
       s.source_ordinal, s.source_role,
       json_extract(s.raw_json, '$.source_path') AS source_path,
       s.raw_json AS source_raw_json
FROM traceloom_evidence_role_decision d
JOIN traceloom_event e
  ON e.event_id = d.event_id AND e.db_idx = d.db_idx
 AND e.device_id = d.device_id
LEFT JOIN traceloom_event_source s
  ON s.event_id = d.event_id AND s.db_idx = d.db_idx
 AND s.device_id = d.device_id
 AND s.source_ordinal = 0;

DROP VIEW IF EXISTS traceloom_v_evidence_role_placement;
CREATE VIEW traceloom_v_evidence_role_placement AS
SELECT d.db_idx, d.device_id, d.decision_id, d.event_id,
       d.input_provider_scope, d.policy_id, d.rule_id, d.rule_class,
       d.policy_role, d.final_role, d.support_state AS decision_support_state,
       d.reason_code AS decision_reason_code, d.duration_ns,
       p.placement_order, p.placement_kind, p.placement_id, p.owner_id,
       p.relation_name, p.support_state AS placement_support_state,
       p.reason_code AS placement_reason_code,
       d.source_table, d.source_key
FROM traceloom_evidence_role_decision d
JOIN traceloom_evidence_role_placement p
  ON p.decision_id = d.decision_id;

DROP VIEW IF EXISTS traceloom_v_evidence_role_cost_coverage;
CREATE VIEW traceloom_v_evidence_role_cost_coverage AS
SELECT db_idx, input_provider_scope, policy_id, policy_version,
       final_role, support_state, COUNT(*) AS event_count,
       SUM(duration_ns) AS retained_duration_ns,
       SUM(CASE WHEN effective_structural_participation = 'identity'
                THEN duration_ns ELSE 0 END) AS identity_duration_ns,
       SUM(CASE WHEN effective_structural_participation <> 'identity'
                THEN duration_ns ELSE 0 END) AS non_identity_duration_ns
FROM traceloom_evidence_role_decision
GROUP BY db_idx, input_provider_scope, policy_id, policy_version,
         final_role, support_state;

DROP VIEW IF EXISTS traceloom_v_evidence_role_structure;
CREATE VIEW traceloom_v_evidence_role_structure AS
      SELECT d.decision_id, d.event_id, d.final_role, p.placement_kind,
       p.placement_id, n.node_id, n.occurrence_idx, n.view_name,
       n.coverage_kind
FROM traceloom_evidence_role_decision d
JOIN traceloom_evidence_role_placement p
  ON p.decision_id = d.decision_id AND p.placement_kind = 'anchor'
JOIN traceloom_viz_node_anchor n
  ON n.anchor_id = p.placement_id
 AND n.db_idx = d.db_idx AND n.device_id = d.device_id
UNION ALL
SELECT d.decision_id, d.event_id, d.final_role, p.placement_kind,
       p.placement_id, n.node_id, n.occurrence_idx, n.view_name,
       n.coverage_kind
FROM traceloom_evidence_role_decision d
JOIN traceloom_evidence_role_placement p
  ON p.decision_id = d.decision_id
 AND p.placement_kind = 'auxiliary_link'
JOIN traceloom_viz_node_anchor n
  ON n.anchor_id = p.owner_id
 AND n.db_idx = d.db_idx AND n.device_id = d.device_id
UNION ALL
SELECT d.decision_id, d.event_id, d.final_role, p.placement_kind,
       p.placement_id, n.node_id, n.occurrence_idx, n.view_name,
       n.coverage_kind
FROM traceloom_evidence_role_decision d
JOIN traceloom_evidence_role_placement p
  ON p.decision_id = d.decision_id
 AND p.placement_kind = 'graph_body_member'
JOIN traceloom_v_node_graph_body_member n
  ON n.member_id = p.placement_id
 AND n.db_idx = d.db_idx AND n.device_id = d.device_id
UNION ALL
SELECT d.decision_id, d.event_id, d.final_role, p.placement_kind,
       p.placement_id, n.node_id, n.occurrence_idx, n.view_name,
       n.coverage_kind
FROM traceloom_evidence_role_decision d
JOIN traceloom_evidence_role_placement p
  ON p.decision_id = d.decision_id
 AND p.placement_kind = 'replay_unit'
JOIN traceloom_replay_unit_anchor r
  ON r.replay_unit_id = p.placement_id
 AND r.db_idx = d.db_idx AND r.device_id = d.device_id
JOIN traceloom_viz_node_anchor n
  ON n.anchor_id = r.anchor_id
 AND n.db_idx = d.db_idx AND n.device_id = d.device_id
UNION ALL
SELECT d.decision_id, d.event_id, d.final_role, p.placement_kind,
       p.placement_id, n.node_id, n.occurrence_idx, n.view_name,
       n.coverage_kind
FROM traceloom_evidence_role_decision d
JOIN traceloom_evidence_role_placement p
  ON p.decision_id = d.decision_id
 AND p.placement_kind = 'protected_interval'
JOIN traceloom_protected_interval i
  ON i.protected_interval_id = p.placement_id
 AND i.db_idx = d.db_idx AND i.device_id = d.device_id
JOIN traceloom_replay_unit_anchor r
  ON r.replay_unit_id = i.replay_unit_id
 AND r.db_idx = d.db_idx AND r.device_id = d.device_id
JOIN traceloom_viz_node_anchor n
  ON n.anchor_id = r.anchor_id
 AND n.db_idx = d.db_idx AND n.device_id = d.device_id;
)SQL");
}

}  // namespace

void write_evidence_role_sql_rows(
    const std::string& sqlite_path,
    const SignalClassificationRuleset& ruleset,
    const EvidenceRoleSqlRows& rows,
    bool timing_diagnostics) {
  Db db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    // Policy, decisions, direct placements, protected intervals, and the
    // compact replay-to-anchor bridge share one transaction so no published
    // audit path can observe a mixed generation.
    auto phase_start = std::chrono::steady_clock::now();
    const auto emit_phase = [&](const char* name) {
      const auto now = std::chrono::steady_clock::now();
      if (timing_diagnostics) {
        std::cerr << "timing evidence_role_" << name << "_ms="
                  << std::chrono::duration<double, std::milli>(now - phase_start)
                         .count()
                  << "\n";
      }
      phase_start = now;
    };
    create_schema(db);
    db.exec("DELETE FROM traceloom_evidence_role_issue");
    db.exec("DELETE FROM traceloom_evidence_role_placement");
    db.exec("DELETE FROM traceloom_evidence_role_decision");
    db.exec("DELETE FROM traceloom_replay_unit_anchor");
    db.exec("DELETE FROM traceloom_protected_interval");
    db.exec("DELETE FROM traceloom_evidence_role_rule");
    db.exec("DELETE FROM traceloom_evidence_role_policy");
    emit_phase("schema_clear");
    insert_policy_and_rules(db, ruleset);
    emit_phase("policy_write");
    insert_decisions(db, rows.decisions);
    emit_phase("decision_write");
    insert_replay_unit_anchors(db, rows.replay_unit_anchors);
    insert_protected_intervals(db, rows.protected_intervals);
    emit_phase("compact_relation_write");
    create_indexes_and_views(db);
    emit_phase("indexes_views");
    db.exec("COMMIT");
    emit_phase("commit");
  } catch (...) {
    try {
      db.exec("ROLLBACK");
    } catch (...) {
    }
    throw;
  }
}

}  // namespace traceloom::compat::detail

#endif
