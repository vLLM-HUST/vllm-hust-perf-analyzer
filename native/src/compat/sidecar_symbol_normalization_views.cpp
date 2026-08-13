#include "sidecar_views.h"

#include "sqlite_support.h"

namespace traceloom::compat {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)

void materialize_symbol_normalization_views(SqliteDb& db) {
  db.exec("DROP VIEW IF EXISTS traceloom_v_symbol_variant_cost");
  db.exec("DROP VIEW IF EXISTS traceloom_v_symbol_normalization_placement");
  db.exec("DROP VIEW IF EXISTS traceloom_v_anchor_symbol_lineage");

  db.exec(
      "CREATE VIEW traceloom_v_anchor_symbol_lineage AS SELECT "
      "d.*, r.precedence, r.provider_scope, r.source_domain, r.match_mode, "
      "r.match_expression, r.required_fields, r.description AS "
      "rule_description, a.role AS anchor_role, a.start_ns, a.end_ns, "
      "a.dur_us "
      "FROM traceloom_anchor_symbol_normalization d "
      "JOIN traceloom_symbol_normalization_rule r ON r.policy_id = "
      "d.policy_id AND r.policy_version = d.policy_version AND r.rule_id = "
      "d.rule_id JOIN traceloom_anchor a ON a.anchor_id = d.anchor_id AND "
      "a.db_idx = d.db_idx AND a.device_id = d.device_id");

  db.exec(
      "CREATE VIEW traceloom_v_symbol_normalization_placement AS SELECT "
      "d.anchor_id, d.db_idx, d.device_id, d.anchor_idx, d.event_id, "
      "d.observed_symbol, d.observed_symbol_source, d.structural_symbol, "
      "d.policy_id, d.policy_version, d.rule_id, d.outcome, d.reason_code, "
      "d.candidate_rule_ids, "
      "d.source_path, d.source_table, d.source_key, na.node_id, "
      "n.local_node_id, na.view_name, na.occurrence_idx, na.anchor_order, "
      "na.coverage_kind, na.repeat_context, a.dur_us AS anchor_dur_us "
      "FROM traceloom_anchor_symbol_normalization d JOIN traceloom_anchor a "
      "ON a.anchor_id = d.anchor_id AND a.db_idx = d.db_idx AND a.device_id "
      "= d.device_id JOIN traceloom_viz_node_anchor na ON na.anchor_id = "
      "d.anchor_id AND na.db_idx = d.db_idx AND na.device_id = d.device_id "
      "JOIN traceloom_viz_node n ON n.node_id = na.node_id AND n.db_idx = "
      "na.db_idx AND n.device_id = na.device_id AND n.view_name = "
      "na.view_name");

  db.exec(
      "CREATE VIEW traceloom_v_symbol_variant_cost AS SELECT "
      "db_idx, device_id, view_name, node_id, local_node_id, anchor_order, "
      "structural_symbol, observed_symbol, observed_symbol_source, rule_id, "
      "outcome, COUNT(*) AS occurrence_count, "
      "ROUND(SUM(anchor_dur_us), 3) AS total_us, "
      "ROUND(AVG(anchor_dur_us), 3) AS avg_us, "
      "ROUND(MIN(anchor_dur_us), 3) AS min_us, "
      "ROUND(MAX(anchor_dur_us), 3) AS max_us "
      "FROM traceloom_v_symbol_normalization_placement WHERE coverage_kind = "
      "'self' GROUP BY db_idx, device_id, view_name, node_id, local_node_id, "
      "anchor_order, structural_symbol, observed_symbol, "
      "observed_symbol_source, rule_id, outcome");
}

#endif

}  // namespace traceloom::compat
