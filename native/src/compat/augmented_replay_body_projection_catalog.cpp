#include "augmented_replay_body_projection_catalog.h"

#include <string>
#include <vector>

#include "sidecar_sqlite_utils.h"

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
namespace traceloom::compat::detail {
namespace {

void insert_values(sqlite3* db,
                   const std::string& table,
                   const std::vector<std::vector<std::string>>& rows,
                   const std::string& context) {
  for (const std::vector<std::string>& row : rows) {
    std::string sql = "INSERT INTO " + table + " VALUES(";
    for (std::size_t index = 0; index < row.size(); ++index) {
      if (index != 0) {
        sql += ',';
      }
      sql += quote_literal(row[index]);
    }
    sql += ')';
    sqlite_exec(db, sql, context);
  }
}

}  // namespace

void materialize_replay_body_projection_catalog(sqlite3* db) {
  insert_values(
      db, "traceloom_analysis_surface",
      {
          {"replay_body_pattern_run",
           "traceloom_replay_body_pattern_run",
           "one recursive replay-body recovery run",
           "audit support, exact source relation, and typed rejection before "
           "using body patterns",
           "SELECT * FROM traceloom_replay_body_pattern_run ORDER BY db_idx;"},
          {"replay_body_pattern_domain",
           "traceloom_replay_body_pattern_domain",
           "one exact replay-body stream coordinate domain",
           "select a supported graph template, body template, and stream "
           "without inventing cross-stream order",
           "SELECT * FROM traceloom_replay_body_pattern_domain ORDER BY "
           "support_status DESC, position_count DESC, domain_id;"},
          {"replay_body_pattern",
           "traceloom_v_replay_body_pattern",
           "one recursive pattern definition in one replay-body domain",
           "read full-body replay structure and occurrence cost populations",
           "SELECT * FROM traceloom_v_replay_body_pattern WHERE domain_id = "
           ":domain_id ORDER BY loop_depth, display_depth, pattern_id;"},
          {"replay_body_pattern_occurrence",
           "traceloom_replay_body_pattern_occurrence",
           "one realized replay-body pattern occurrence",
           "compare or select exact repeated full-body regions by Position "
           "span and scheduled-cost statistics",
           "SELECT * FROM traceloom_replay_body_pattern_occurrence WHERE "
           "pattern_id = :pattern_id ORDER BY occurrence_index;"},
          {"replay_body_position",
           "traceloom_replay_body_position",
           "one aligned replay-body Position",
           "inspect the exact operator identity, kind, cost distribution, and "
           "aggregate coordinate at native replay-body resolution",
           "SELECT * FROM traceloom_replay_body_position WHERE domain_id = "
           ":domain_id ORDER BY position_ordinal;"},
          {"replay_body_pattern_member",
           "traceloom_v_replay_body_pattern_member",
           "one exact replay member covered by one pattern occurrence",
           "drill from recursive full-body structure to every contributing "
           "launch member and normalized event",
           "SELECT * FROM traceloom_v_replay_body_pattern_member WHERE "
           "occurrence_id = :occurrence_id ORDER BY position_ordinal, "
           "contributor_order;"},
          {"replay_body_pattern_source",
           "traceloom_v_replay_body_pattern_source_locator",
           "one exact replay pattern member-to-raw-source locator",
           "audit a selected full-body pattern occurrence against embedded "
           "profiler evidence",
           "SELECT * FROM traceloom_v_replay_body_pattern_source_locator "
           "WHERE occurrence_id = :occurrence_id ORDER BY position_ordinal, "
           "member_id, source_ordinal;"},
      },
      "failed to insert replay-body analysis surface");

  insert_values(
      db, "traceloom_projection_recipe",
      {
          {"replay_body_domains", "45", "replay_body_domain",
           "candidate_scopes", "folded", "device",
           "grammar_support_and_size", "(none)",
           "select an exact supported replay-body stream domain",
           "SELECT domain_id, db_idx, device_id, graph_template_id, "
           "replay_body_template_id, stream_id, position_count, "
           "grammar_description_element_count, grammar_description_ratio "
           "FROM traceloom_replay_body_pattern_domain WHERE support_status "
           "= 'supported' ORDER BY position_count DESC, domain_id;"},
          {"replay_body_patterns", "46", "replay_body_pattern",
           "definitions", "folded", "device",
           "occurrence_cost_population", ":domain_id",
           "inspect recursive pattern definitions inside one exact replay "
           "body stream",
           "SELECT * FROM traceloom_v_replay_body_pattern WHERE domain_id = "
           ":domain_id ORDER BY loop_depth, display_depth, pattern_id;"},
          {"replay_body_pattern_occurrences", "47",
           "replay_body_pattern", "one_or_all_occurrences", "realized",
           "device", "occurrence_cost", ":pattern_id",
           "compare exact Position spans and costs for one recovered pattern",
           "SELECT * FROM traceloom_replay_body_pattern_occurrence WHERE "
           "pattern_id = :pattern_id ORDER BY occurrence_index;"},
          {"replay_body_pattern_positions", "48",
           "replay_body_pattern_occurrence", "one_occurrence", "native",
           "device", "position_cost_distribution", ":occurrence_id",
           "expand one pattern occurrence to exact aligned replay Positions",
           "SELECT * FROM traceloom_v_replay_body_pattern_position WHERE "
           "occurrence_id = :occurrence_id ORDER BY position_ordinal;"},
          {"replay_body_pattern_members", "49",
           "replay_body_pattern_occurrence", "one_occurrence", "raw",
           "device", "member_cost_and_lineage", ":occurrence_id",
           "drill one occurrence to every contributing replay member and "
           "a normalized event coordinate for source audit",
           "SELECT * FROM traceloom_v_replay_body_pattern_member "
           "WHERE occurrence_id = :occurrence_id ORDER BY position_ordinal, "
           "contributor_order, member_id;"},
      },
      "failed to insert replay-body projection recipe");

  insert_values(
      db, "traceloom_projection_parameter",
      {
          {"replay_body_patterns", "10", "domain_id", "TEXT", "0",
           "replay_body_domain_id", "traceloom_replay_body_pattern_domain",
           "domain_id", "selected exact replay-body stream domain"},
          {"replay_body_pattern_occurrences", "10", "pattern_id", "TEXT",
           "0", "replay_body_pattern_id",
           "traceloom_replay_body_pattern_definition", "pattern_id",
           "selected recursive replay-body pattern"},
          {"replay_body_pattern_positions", "10", "occurrence_id", "TEXT",
           "0", "replay_body_pattern_occurrence_id",
           "traceloom_replay_body_pattern_occurrence", "occurrence_id",
           "selected realized replay-body pattern occurrence"},
          {"replay_body_pattern_members", "10", "occurrence_id", "TEXT",
           "0", "replay_body_pattern_occurrence_id",
           "traceloom_replay_body_pattern_occurrence", "occurrence_id",
           "selected realized replay-body pattern occurrence"},
      },
      "failed to insert replay-body projection parameter");

  insert_values(
      db, "traceloom_projection_coordinate",
      {
          {"replay_body_domains", "10", "domain_id",
           "replay_body_domain_id", "reusable replay-body stream domain"},
          {"replay_body_patterns", "10", "pattern_id",
           "replay_body_pattern_id", "reusable recursive pattern"},
          {"replay_body_patterns", "20", "domain_id",
           "replay_body_domain_id", "owning replay-body stream domain"},
          {"replay_body_pattern_occurrences", "10", "occurrence_id",
           "replay_body_pattern_occurrence_id",
           "reusable realized pattern occurrence"},
          {"replay_body_pattern_occurrences", "20", "pattern_id",
           "replay_body_pattern_id", "owning recursive pattern"},
          {"replay_body_pattern_occurrences", "30", "domain_id",
           "replay_body_domain_id", "owning replay-body stream domain"},
          {"replay_body_pattern_positions", "10", "aggregate_id",
           "replay_cost_aggregate_id", "exact aligned replay cost row"},
          {"replay_body_pattern_positions", "20", "occurrence_id",
           "replay_body_pattern_occurrence_id",
           "owning realized pattern occurrence"},
          {"replay_body_pattern_positions", "30", "domain_id",
           "replay_body_domain_id", "owning replay-body stream domain"},
          {"replay_body_pattern_members", "10", "member_id",
           "replay_cost_member_id", "exact contributing replay member"},
          {"replay_body_pattern_members", "20", "event_id", "event_id",
           "normalized event with raw source lineage"},
          {"replay_body_pattern_members", "30", "occurrence_id",
           "replay_body_pattern_occurrence_id",
           "owning realized pattern occurrence"},
          {"replay_body_pattern_members", "40", "domain_id",
           "replay_body_domain_id", "owning replay-body stream domain"},
      },
      "failed to insert replay-body projection coordinate");
}

}  // namespace traceloom::compat::detail
#endif
