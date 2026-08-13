#include "traceloom/compat/sidecar_writer.h"

#include <stdexcept>
#include <string>

#include "sqlite_support.h"

namespace traceloom::compat {
namespace {

void finish_row(SqliteStmt& stmt, const char* failure) {
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string(failure) + ": " +
                             sqlite3_errmsg(stmt.db()));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

}  // namespace

void replace_symbol_normalization_rows(
    const std::string& sqlite_path,
    const SymbolNormalizationSqlRows& rows) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  materialize_compatibility_schema(
      sqlite_path,
      {symbol_normalization_policy_table_schema(),
       symbol_normalization_rule_table_schema(),
       anchor_symbol_normalization_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    db.exec("DELETE FROM traceloom_anchor_symbol_normalization");
    db.exec("DELETE FROM traceloom_symbol_normalization_rule");
    db.exec("DELETE FROM traceloom_symbol_normalization_policy");

    SqliteStmt policy_stmt(
        db.get(),
        "INSERT INTO traceloom_symbol_normalization_policy ("
        "policy_id, policy_version, policy_kind, source_manifest, "
        "manifest_sha256, description"
        ") VALUES (?, ?, ?, ?, ?, ?)");
    for (const SymbolNormalizationPolicySqlRow& row : rows.policies) {
      bind_text(policy_stmt, 1, row.policy_id);
      bind_text(policy_stmt, 2, row.policy_version);
      bind_text(policy_stmt, 3, row.policy_kind);
      bind_text(policy_stmt, 4, row.source_manifest);
      bind_text(policy_stmt, 5, row.manifest_sha256);
      bind_text(policy_stmt, 6, row.description);
      finish_row(policy_stmt,
                 "failed to insert symbol normalization policy row");
    }

    SqliteStmt rule_stmt(
        db.get(),
        "INSERT INTO traceloom_symbol_normalization_rule ("
        "policy_id, policy_version, rule_id, precedence, provider_scope, "
        "source_domain, match_mode, match_expression, structural_symbol, "
        "required_fields, rule_origin, rule_origin_sha256, source_line, "
        "description"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const SymbolNormalizationRuleSqlRow& row : rows.rules) {
      bind_text(rule_stmt, 1, row.policy_id);
      bind_text(rule_stmt, 2, row.policy_version);
      bind_text(rule_stmt, 3, row.rule_id);
      bind_int64(rule_stmt, 4, row.precedence);
      bind_text(rule_stmt, 5, row.provider_scope);
      bind_text(rule_stmt, 6, row.source_domain);
      bind_text(rule_stmt, 7, row.match_mode);
      bind_text(rule_stmt, 8, row.match_expression);
      bind_text(rule_stmt, 9, row.structural_symbol);
      bind_text(rule_stmt, 10, row.required_fields);
      bind_text(rule_stmt, 11, row.rule_origin);
      bind_text(rule_stmt, 12, row.rule_origin_sha256);
      bind_int64(rule_stmt, 13,
                 static_cast<std::int64_t>(row.source_line));
      bind_text(rule_stmt, 14, row.description);
      finish_row(rule_stmt,
                 "failed to insert symbol normalization rule row");
    }

    SqliteStmt decision_stmt(
        db.get(),
        "INSERT INTO traceloom_anchor_symbol_normalization ("
        "anchor_id, db_idx, device_id, anchor_idx, event_id, source_path, "
        "source_table, source_key, observed_symbol, observed_symbol_source, "
        "structural_symbol, policy_id, policy_version, rule_id, outcome, "
        "candidate_rule_ids, reason_code"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const AnchorSymbolNormalizationSqlRow& row : rows.decisions) {
      bind_text(decision_stmt, 1, row.anchor_id);
      bind_int64(decision_stmt, 2, row.db_idx);
      bind_int64(decision_stmt, 3, row.device_id);
      bind_int64(decision_stmt, 4, row.anchor_idx);
      bind_nullable_text(decision_stmt, 5, row.event_id);
      bind_text(decision_stmt, 6, row.source_path);
      bind_text(decision_stmt, 7, row.source_table);
      bind_text(decision_stmt, 8, row.source_key);
      bind_nullable_text(decision_stmt, 9, row.observed_symbol);
      bind_text(decision_stmt, 10, row.observed_symbol_source);
      bind_nullable_text(decision_stmt, 11, row.structural_symbol);
      bind_text(decision_stmt, 12, row.policy_id);
      bind_text(decision_stmt, 13, row.policy_version);
      bind_text(decision_stmt, 14, row.rule_id);
      bind_text(decision_stmt, 15, row.outcome);
      bind_nullable_text(decision_stmt, 16, row.candidate_rule_ids);
      bind_text(decision_stmt, 17, row.reason_code);
      finish_row(decision_stmt,
                 "failed to insert anchor symbol normalization row");
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
