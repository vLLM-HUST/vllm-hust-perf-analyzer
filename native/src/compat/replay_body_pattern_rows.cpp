#include "traceloom/compat/replay_body_pattern_rows.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "sqlite_support.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/pattern/grammar_modes.h"

namespace traceloom::compat {
namespace {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)

void finish_row(SqliteStmt& stmt, const char* context) {
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string(context) + ": " +
                             sqlite3_errmsg(stmt.db()));
  }
  sqlite3_reset(stmt.get());
  sqlite3_clear_bindings(stmt.get());
}

std::string join_codes(const std::vector<std::string>& codes) {
  std::string out;
  for (const std::string& code : codes) {
    if (!out.empty()) {
      out += ',';
    }
    out += code;
  }
  return out;
}

const char* node_kind_name(StructuralNodeKind kind) noexcept {
  switch (kind) {
    case StructuralNodeKind::kSeq:
      return "seq";
    case StructuralNodeKind::kRepeat:
      return "repeat";
    case StructuralNodeKind::kAtom:
      return "atom";
  }
  return "atom";
}

std::string domain_id(std::size_t index) {
  return "replay-body-domain-" + std::to_string(index);
}

std::string pattern_id(const std::string& domain,
                       StructuralNodeDefId id) {
  return domain + "-pattern-" + std::to_string(id.value());
}

std::string occurrence_id(const std::string& domain,
                          StructuralNodeOccurrenceId id) {
  return domain + "-occurrence-" + std::to_string(id.value());
}

std::string position_id(const std::string& domain, std::size_t ordinal) {
  return domain + "-position-" + std::to_string(ordinal);
}

std::string aggregate_id(std::size_t index) {
  return "replay-cost-aggregate-" + std::to_string(index);
}

std::int64_t sql_integer(std::uint64_t value, const char* field) {
  if (value > static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max())) {
    throw std::overflow_error(std::string(field) +
                              " exceeds SQLite INTEGER range");
  }
  return static_cast<std::int64_t>(value);
}

std::uint64_t checked_add(std::uint64_t lhs,
                          std::uint64_t rhs,
                          const char* field) {
  if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
    throw std::overflow_error(std::string(field) + " overflow");
  }
  return lhs + rhs;
}

struct CostPrefixes {
  std::vector<std::uint64_t> p25;
  std::vector<std::uint64_t> median;
  std::vector<std::uint64_t> p75;
  std::vector<std::uint64_t> share;
  std::vector<std::uint64_t> unsupported_share_count;
};

CostPrefixes build_cost_prefixes(
    const ReplayInternalCostMapResult& replay_cost,
    const ReplayBodyPatternDomain& domain) {
  CostPrefixes out;
  const std::size_t count = domain.aggregate_indices.size();
  out.p25.assign(count + 1, 0);
  out.median.assign(count + 1, 0);
  out.p75.assign(count + 1, 0);
  out.share.assign(count + 1, 0);
  out.unsupported_share_count.assign(count + 1, 0);
  for (std::size_t index = 0; index < count; ++index) {
    const ReplayAlignedCostAggregateRow& row =
        replay_cost.aggregates.at(domain.aggregate_indices[index]);
    out.p25[index + 1] =
        checked_add(out.p25[index], row.duration_p25_ns, "p25 prefix");
    out.median[index + 1] = checked_add(
        out.median[index], row.duration_median_ns, "median prefix");
    out.p75[index + 1] =
        checked_add(out.p75[index], row.duration_p75_ns, "p75 prefix");
    out.share[index + 1] = checked_add(
        out.share[index], row.scheduled_work_share_ppm, "share prefix");
    out.unsupported_share_count[index + 1] =
        out.unsupported_share_count[index] +
        (row.scheduled_work_share_supported ? 0 : 1);
  }
  return out;
}

std::uint64_t range_sum(const std::vector<std::uint64_t>& prefix,
                        std::size_t begin,
                        std::size_t end) {
  if (begin > end || end >= prefix.size()) {
    throw std::out_of_range("replay-body occurrence span is out of range");
  }
  return prefix[end] - prefix[begin];
}

std::vector<std::uint32_t> definition_span_sizes(
    const StructuralOccurrenceGraph& graph) {
  std::vector<std::uint32_t> sizes(graph.node_defs.size(), 0);
  std::vector<bool> seen(graph.node_defs.size(), false);
  for (const StructuralNodeOccurrence& occurrence : graph.occurrences) {
    if (!occurrence.node_def_id.valid() ||
        occurrence.node_def_id.value() >= sizes.size() ||
        occurrence.token_end_ordinal < occurrence.token_start_ordinal) {
      throw std::invalid_argument(
          "replay-body occurrence graph contains an invalid span");
    }
    const std::uint32_t size =
        occurrence.token_end_ordinal - occurrence.token_start_ordinal;
    const std::size_t index = occurrence.node_def_id.value();
    if (seen[index] && sizes[index] != size) {
      throw std::invalid_argument(
          "replay-body pattern definition has inconsistent span sizes");
    }
    sizes[index] = size;
    seen[index] = true;
  }
  return sizes;
}

void create_indexes_and_views(SqliteDb& db) {
  db.exec(R"SQL(
CREATE UNIQUE INDEX IF NOT EXISTS idx_traceloom_replay_body_domain_key
  ON traceloom_replay_body_pattern_domain(
    db_idx, device_id, graph_template_id, slot_role, aggregation_scope,
    replay_body_template_id, stream_id);
CREATE INDEX IF NOT EXISTS idx_traceloom_replay_body_pattern_kind
  ON traceloom_replay_body_pattern_definition(
    domain_id, node_kind, repeat_count, pattern_id);
CREATE INDEX IF NOT EXISTS idx_traceloom_replay_body_occurrence_pattern
  ON traceloom_replay_body_pattern_occurrence(
    pattern_id, occurrence_index);
CREATE INDEX IF NOT EXISTS idx_traceloom_replay_body_occurrence_parent
  ON traceloom_replay_body_pattern_occurrence(
    parent_occurrence_id, edge_order);
CREATE INDEX IF NOT EXISTS idx_traceloom_replay_body_occurrence_span
  ON traceloom_replay_body_pattern_occurrence(
    domain_id, position_start, position_end_exclusive);
CREATE UNIQUE INDEX IF NOT EXISTS idx_traceloom_replay_body_position_coordinate
  ON traceloom_replay_body_position(domain_id, position_ordinal);
CREATE INDEX IF NOT EXISTS idx_traceloom_replay_body_position_aggregate
  ON traceloom_replay_body_position(aggregate_id);

DROP VIEW IF EXISTS traceloom_v_replay_body_pattern;
CREATE VIEW traceloom_v_replay_body_pattern AS
SELECT
  d.run_id, d.graph_template_id, d.slot_role, d.aggregation_scope,
  d.replay_body_template_id, d.stream_id, d.support_status,
  p.*,
  s.min_occurrence_median_sum_ns,
  s.avg_occurrence_median_sum_ns,
  s.max_occurrence_median_sum_ns
FROM traceloom_replay_body_pattern_definition p
JOIN traceloom_replay_body_pattern_domain d
  ON d.domain_id = p.domain_id
LEFT JOIN (
  SELECT pattern_id,
         MIN(duration_median_sum_ns) AS min_occurrence_median_sum_ns,
         AVG(duration_median_sum_ns) AS avg_occurrence_median_sum_ns,
         MAX(duration_median_sum_ns) AS max_occurrence_median_sum_ns
  FROM traceloom_replay_body_pattern_occurrence
  GROUP BY pattern_id
) s ON s.pattern_id = p.pattern_id;

DROP VIEW IF EXISTS traceloom_v_replay_body_pattern_position;
CREATE VIEW traceloom_v_replay_body_pattern_position AS
SELECT
  o.occurrence_id, o.pattern_id, o.parent_occurrence_id,
  o.occurrence_index, o.repeat_iteration, o.position_start,
  o.position_end_exclusive, p.*
FROM traceloom_replay_body_pattern_occurrence o
JOIN traceloom_replay_body_position p
  ON p.domain_id = o.domain_id
 AND p.position_ordinal >= o.position_start
 AND p.position_ordinal < o.position_end_exclusive;

DROP VIEW IF EXISTS traceloom_v_replay_body_pattern_member;
CREATE VIEW traceloom_v_replay_body_pattern_member AS
SELECT
  p.occurrence_id, p.pattern_id, p.parent_occurrence_id,
  p.occurrence_index, p.repeat_iteration, p.domain_id,
  p.position_ordinal, p.aggregate_id,
  a.member_id, a.contributor_order,
  m.launch_id, m.cost_unit_id, m.event_id, m.identity, m.kind,
  m.duration_ns, m.start_ns, m.end_ns, m.raw_task_id,
  m.composition_slot_id, m.slot_order, m.body_id, m.task_ordinal,
  m.db_idx, m.device_id
FROM traceloom_v_replay_body_pattern_position p
JOIN traceloom_replay_cost_aggregate_member a
  ON a.aggregate_id = p.aggregate_id
 AND a.db_idx = p.db_idx
 AND a.device_id = p.device_id
JOIN traceloom_replay_cost_member m
  ON m.member_id = a.member_id
 AND m.db_idx = a.db_idx
 AND m.device_id = a.device_id;

DROP VIEW IF EXISTS traceloom_v_replay_body_pattern_source_locator;
CREATE VIEW traceloom_v_replay_body_pattern_source_locator AS
SELECT
  m.occurrence_id, m.pattern_id, m.domain_id, m.position_ordinal,
  m.aggregate_id, m.member_id, m.event_id, m.identity, m.kind,
  m.duration_ns, s.source_ordinal, s.source_table, s.source_key,
  s.source_role, s.raw_json, m.db_idx, m.device_id
FROM traceloom_v_replay_body_pattern_member m
JOIN traceloom_event_source s
  ON s.event_id = m.event_id
 AND s.db_idx = m.db_idx
 AND s.device_id = m.device_id;
)SQL");
}

#endif

}  // namespace

void replace_replay_body_pattern_rows(
    const std::string& sqlite_path,
    const NativeIr& ir,
    const ReplayInternalCostMapResult& replay_cost,
    std::uint32_t db_idx,
    const ReplayBodyPatternConfig& config) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  const ReplayBodyPatternResult result =
      build_replay_body_patterns(ir, replay_cost, config);
  materialize_compatibility_schema(
      sqlite_path,
      {replay_body_pattern_run_table_schema(),
       replay_body_pattern_domain_table_schema(),
       replay_body_pattern_definition_table_schema(),
       replay_body_pattern_occurrence_table_schema(),
       replay_body_position_table_schema(),
       replay_body_pattern_issue_table_schema()});

  SqliteDb db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    for (const char* table :
         {"traceloom_replay_body_pattern_issue",
          "traceloom_replay_body_position",
          "traceloom_replay_body_pattern_occurrence",
          "traceloom_replay_body_pattern_definition",
          "traceloom_replay_body_pattern_domain",
          "traceloom_replay_body_pattern_run"}) {
      db.exec(std::string("DELETE FROM ") + table);
    }

    const std::string run_id = "replay-body-pattern-run-" +
                               std::to_string(db_idx);
    std::uint64_t position_count = 0;
    for (const ReplayBodyPatternDomain& domain : result.domains) {
      position_count = checked_add(position_count,
                                   domain.aggregate_indices.size(),
                                   "replay-body run position count");
    }
    const std::string run_status =
        result.supported_domain_count == 0
            ? "unsupported"
            : (result.rejected_domain_count == 0 ? "supported" : "partial");
    SqliteStmt run_stmt(
        db.get(),
        "INSERT INTO traceloom_replay_body_pattern_run VALUES "
        "(?,?,?,?,?,?,?,?,?)");
    bind_text(run_stmt, 1, run_id);
    bind_int64(run_stmt, 2, db_idx);
    bind_text(run_stmt, 3, run_status);
    bind_text(run_stmt, 4, join_codes(result.result_reason_codes));
    bind_text(run_stmt, 5, grammar_algorithm_mode_name(config.grammar_mode));
    bind_text(run_stmt, 6, "traceloom_replay_cost_aggregate");
    bind_int64(run_stmt, 7, sql_integer(position_count, "position_count"));
    bind_int64(run_stmt, 8, result.supported_domain_count);
    bind_int64(run_stmt, 9, result.rejected_domain_count);
    finish_row(run_stmt, "failed to insert replay-body pattern run");

    SqliteStmt domain_stmt(
        db.get(),
        "INSERT INTO traceloom_replay_body_pattern_domain VALUES "
        "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    SqliteStmt definition_stmt(
        db.get(),
        "INSERT INTO traceloom_replay_body_pattern_definition VALUES "
        "(?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    SqliteStmt occurrence_stmt(
        db.get(),
        "INSERT INTO traceloom_replay_body_pattern_occurrence VALUES "
        "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    SqliteStmt position_stmt(
        db.get(),
        "INSERT INTO traceloom_replay_body_position VALUES "
        "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    SqliteStmt issue_stmt(
        db.get(),
        "INSERT INTO traceloom_replay_body_pattern_issue VALUES "
        "(?,?,?,?,?,?,?)");

    std::size_t issue_index = 0;
    for (const std::string& code : result.result_reason_codes) {
      bind_text(issue_stmt, 1,
                "replay-body-pattern-issue-" +
                    std::to_string(issue_index++));
      bind_text(issue_stmt, 2, run_id);
      bind_null(issue_stmt, 3);
      bind_int64(issue_stmt, 4, db_idx);
      bind_null(issue_stmt, 5);
      bind_text(issue_stmt, 6, code);
      bind_text(issue_stmt, 7,
                "replay-body pattern recovery has no supported domain");
      finish_row(issue_stmt, "failed to insert replay-body global issue");
    }

    for (std::size_t domain_index = 0;
         domain_index < result.domains.size(); ++domain_index) {
      const ReplayBodyPatternDomain& domain = result.domains[domain_index];
      const std::string domain_name = domain_id(domain_index);
      const std::size_t description_elements =
          domain.grammar_live_node_count + domain.grammar_macro_def_count;
      const double description_ratio =
          description_elements == 0
              ? 0.0
              : static_cast<double>(domain.aggregate_indices.size()) /
                    static_cast<double>(description_elements);
      int column = 1;
      bind_text(domain_stmt, column++, domain_name);
      bind_text(domain_stmt, column++, run_id);
      bind_int64(domain_stmt, column++, db_idx);
      bind_int64(domain_stmt, column++, domain.key.device_id);
      bind_int64(domain_stmt, column++, domain.key.graph_template_id.value());
      bind_text(domain_stmt, column++,
                replay_internal_cost_map_slot_role_name(domain.key.slot_role));
      bind_text(domain_stmt, column++,
                replay_internal_cost_map_aggregation_scope_name(
                    domain.key.aggregation_scope));
      bind_int64(domain_stmt, column++,
                 domain.key.replay_body_template_id.value());
      bind_int64(domain_stmt, column++, domain.key.stream_id);
      bind_text(domain_stmt, column++,
                replay_body_pattern_support_status_name(
                    domain.support_status));
      bind_text(domain_stmt, column++, domain.reason_code);
      bind_int64(domain_stmt, column++, domain.aggregate_indices.size());
      bind_text(domain_stmt, column++,
                grammar_algorithm_mode_name(domain.grammar_mode));
      bind_text(domain_stmt, column++,
                grammar_engine_stop_reason_name(domain.grammar_stop_reason));
      bind_int64(domain_stmt, column++, domain.grammar_live_node_count);
      bind_int64(domain_stmt, column++, domain.grammar_macro_def_count);
      bind_int64(domain_stmt, column++, description_elements);
      bind_double(domain_stmt, column++, description_ratio);
      bind_int64(domain_stmt, column++, domain.grammar_step_count);
      bind_int64(domain_stmt, column++, domain.graph.node_defs.size());
      bind_int64(domain_stmt, column++, domain.graph.occurrences.size());
      finish_row(domain_stmt, "failed to insert replay-body domain");

      for (std::size_t ordinal = 0;
           ordinal < domain.aggregate_indices.size(); ++ordinal) {
        const std::size_t aggregate_index = domain.aggregate_indices[ordinal];
        const ReplayAlignedCostAggregateRow& row =
            replay_cost.aggregates.at(aggregate_index);
        const std::string identity =
            row.identity_symbol_id.valid() &&
                    row.identity_symbol_id.value() < ir.symbols.size()
                ? ir.symbols.value(row.identity_symbol_id)
                : std::string();
        int i = 1;
        bind_text(position_stmt, i++, position_id(domain_name, ordinal));
        bind_text(position_stmt, i++, domain_name);
        bind_int64(position_stmt, i++, db_idx);
        bind_int64(position_stmt, i++, domain.key.device_id);
        bind_int64(position_stmt, i++, ordinal);
        bind_text(position_stmt, i++, aggregate_id(aggregate_index));
        bind_text(position_stmt, i++, identity);
        bind_text(position_stmt, i++,
                  replay_internal_cost_map_member_kind_name(row.kind));
        bind_int64(position_stmt, i++, row.member_occurrence_count);
        bind_int64(position_stmt, i++, row.replay_unit_count);
        bind_int64(position_stmt, i++, row.launch_member_count);
        bind_int64(position_stmt, i++,
                   sql_integer(row.duration_p25_ns, "duration_p25_ns"));
        bind_int64(position_stmt, i++,
                   sql_integer(row.duration_median_ns,
                               "duration_median_ns"));
        bind_int64(position_stmt, i++,
                   sql_integer(row.duration_p75_ns, "duration_p75_ns"));
        bind_int64(position_stmt, i++,
                   sql_integer(row.scheduled_work_share_ppm,
                               "scheduled_work_share_ppm"));
        bind_int64(position_stmt, i++,
                   row.scheduled_work_share_supported ? 1 : 0);
        finish_row(position_stmt, "failed to insert replay-body Position");
      }

      if (domain.support_status !=
          ReplayBodyPatternSupportStatus::kSupported) {
        continue;
      }
      const std::vector<std::uint32_t> span_sizes =
          definition_span_sizes(domain.graph);
      for (const StructuralNodeDef& def : domain.graph.node_defs) {
        int i = 1;
        bind_text(definition_stmt, i++, pattern_id(domain_name, def.id));
        bind_text(definition_stmt, i++, domain_name);
        bind_int64(definition_stmt, i++, db_idx);
        bind_int64(definition_stmt, i++, domain.key.device_id);
        bind_text(definition_stmt, i++, def.local_node_id);
        bind_text(definition_stmt, i++, node_kind_name(def.kind));
        bind_text(definition_stmt, i++, def.display_op);
        bind_text(definition_stmt, i++, def.display_category);
        if (def.repeat_count == 0) {
          bind_null(definition_stmt, i++);
        } else {
          bind_int64(definition_stmt, i++, def.repeat_count);
        }
        bind_int64(definition_stmt, i++, def.display_depth);
        bind_int64(definition_stmt, i++, def.loop_depth);
        bind_int64(definition_stmt, i++,
                   structural_occurrence_count_for_def(domain.graph, def.id));
        bind_int64(definition_stmt, i++, span_sizes.at(def.id.value()));
        bind_text(definition_stmt, i++, def.visibility_reason);
        finish_row(definition_stmt,
                   "failed to insert replay-body pattern definition");
      }

      const CostPrefixes prefixes = build_cost_prefixes(replay_cost, domain);
      for (const StructuralNodeOccurrence& occurrence :
           domain.graph.occurrences) {
        const std::size_t begin = occurrence.token_start_ordinal;
        const std::size_t end = occurrence.token_end_ordinal;
        int i = 1;
        bind_text(occurrence_stmt, i++,
                  occurrence_id(domain_name, occurrence.id));
        bind_text(occurrence_stmt, i++,
                  pattern_id(domain_name, occurrence.node_def_id));
        bind_text(occurrence_stmt, i++, domain_name);
        if (occurrence.parent_occurrence_id.valid()) {
          bind_text(occurrence_stmt, i++,
                    occurrence_id(domain_name,
                                  occurrence.parent_occurrence_id));
        } else {
          bind_null(occurrence_stmt, i++);
        }
        bind_int64(occurrence_stmt, i++, db_idx);
        bind_int64(occurrence_stmt, i++, domain.key.device_id);
        bind_int64(occurrence_stmt, i++,
                   occurrence.occurrence_index_for_def);
        bind_int64(occurrence_stmt, i++, occurrence.edge_order);
        bind_int64(occurrence_stmt, i++, occurrence.repeat_iteration);
        bind_int64(occurrence_stmt, i++, begin);
        bind_int64(occurrence_stmt, i++, end);
        bind_int64(occurrence_stmt, i++, end - begin);
        bind_int64(occurrence_stmt, i++,
                   sql_integer(range_sum(prefixes.p25, begin, end),
                               "occurrence p25 sum"));
        bind_int64(occurrence_stmt, i++,
                   sql_integer(range_sum(prefixes.median, begin, end),
                               "occurrence median sum"));
        bind_int64(occurrence_stmt, i++,
                   sql_integer(range_sum(prefixes.p75, begin, end),
                               "occurrence p75 sum"));
        bind_int64(occurrence_stmt, i++,
                   sql_integer(range_sum(prefixes.share, begin, end),
                               "occurrence share sum"));
        bind_int64(
            occurrence_stmt, i++,
            range_sum(prefixes.unsupported_share_count, begin, end) == 0
                ? 1
                : 0);
        finish_row(occurrence_stmt,
                   "failed to insert replay-body pattern occurrence");
      }
    }

    for (const ReplayBodyPatternIssue& issue : result.issues) {
      const bool has_domain = issue.domain_index < result.domains.size();
      bind_text(issue_stmt, 1,
                "replay-body-pattern-issue-" +
                    std::to_string(issue_index++));
      bind_text(issue_stmt, 2, run_id);
      if (has_domain) {
        bind_text(issue_stmt, 3, domain_id(issue.domain_index));
      } else {
        bind_null(issue_stmt, 3);
      }
      bind_int64(issue_stmt, 4, db_idx);
      if (has_domain) {
        bind_int64(issue_stmt, 5,
                   result.domains[issue.domain_index].key.device_id);
      } else {
        bind_null(issue_stmt, 5);
      }
      bind_text(issue_stmt, 6, issue.code);
      bind_text(issue_stmt, 7, issue.detail);
      finish_row(issue_stmt, "failed to insert replay-body pattern issue");
    }

    create_indexes_and_views(db);
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
  (void)replay_cost;
  (void)db_idx;
  (void)config;
  throw std::runtime_error("replay-body pattern SQL requires SQLite support");
#endif
}

}  // namespace traceloom::compat
