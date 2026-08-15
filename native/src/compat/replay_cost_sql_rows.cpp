#include "traceloom/compat/replay_cost_sql_rows.h"

#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "traceloom/analysis/replay_internal_cost_map.h"
#include "traceloom/compat/timeline_rows.h"

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
#include <sqlite3.h>
#endif

namespace traceloom::compat {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
namespace {

class Db {
public:
  explicit Db(const std::string &path) {
    if (sqlite3_open_v2(path.c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        nullptr) != SQLITE_OK) {
      const std::string message = db_ ? sqlite3_errmsg(db_) : "open failed";
      if (db_)
        sqlite3_close(db_);
      db_ = nullptr;
      throw std::runtime_error("failed to open replay cost database: " +
                               message);
    }
  }
  ~Db() {
    if (db_)
      sqlite3_close(db_);
  }
  Db(const Db &) = delete;
  Db &operator=(const Db &) = delete;
  sqlite3 *get() const { return db_; }
  void exec(const std::string &sql) {
    char *error = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
      const std::string message = error ? error : sqlite3_errmsg(db_);
      sqlite3_free(error);
      throw std::runtime_error("failed to materialize replay cost SQL: " +
                               message);
    }
  }

private:
  sqlite3 *db_ = nullptr;
};

class Stmt {
public:
  Stmt(sqlite3 *db, const char *sql) : db_(db) {
    if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
      throw std::runtime_error("failed to prepare replay cost SQL: " +
                               std::string(sqlite3_errmsg(db)));
    }
  }
  ~Stmt() {
    if (stmt_)
      sqlite3_finalize(stmt_);
  }
  sqlite3_stmt *get() const { return stmt_; }
  sqlite3 *db() const { return db_; }
  void text(int index, const std::string &value) {
    if (sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT) !=
        SQLITE_OK)
      fail("text bind");
  }
  void integer(int index, std::int64_t value) {
    if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK)
      fail("integer bind");
  }
  void boolean(int index, bool value) { integer(index, value ? 1 : 0); }
  void run() {
    if (sqlite3_step(stmt_) != SQLITE_DONE)
      fail("insert");
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
  }

private:
  [[noreturn]] void fail(const char *operation) {
    throw std::runtime_error(std::string("replay cost ") + operation +
                             " failed: " + sqlite3_errmsg(db_));
  }
  sqlite3 *db_ = nullptr;
  sqlite3_stmt *stmt_ = nullptr;
};

std::string join_reason_codes(const std::vector<std::string> &reasons) {
  std::string out;
  for (std::size_t i = 0; i < reasons.size(); ++i) {
    if (i)
      out += ',';
    out += reasons[i];
  }
  return out;
}

std::string symbol_value(const NativeIr &ir, SymbolId id) {
  return id.valid() && id.value() < ir.symbols.size() ? ir.symbols.value(id)
                                                      : std::string();
}

template <typename IdType>
std::int64_t id_value_or_minus_one(IdType id) {
  return id.valid() ? static_cast<std::int64_t>(id.value()) : -1;
}

std::uint32_t unit_device_id(const NativeIr &ir,
                             const ReplayUnitCostBlock &unit) {
  if (unit.replay_unit_id.valid() &&
      unit.replay_unit_id.value() < ir.replay_units.size()) {
    const ReplayUnitRow &row = ir.replay_units.row(unit.replay_unit_id);
    if (row.launch_trace_event_id.valid() &&
        row.launch_trace_event_id.value() < ir.trace_events.size()) {
      return ir.trace_events.row(row.launch_trace_event_id).device_id;
    }
  }
  return 0;
}

using AggregateKey =
    std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
               std::uint32_t, std::uint32_t, std::uint32_t>;

AggregateKey aggregate_key(const ReplayAlignedCostAggregateRow &row) {
  return {row.graph_template_id.value(),
          row.device_id,
          static_cast<std::uint32_t>(row.slot_role),
          row.replay_body_template_id.value(),
          row.stream_id,
          row.within_stream_position,
          row.identity_symbol_id.value()};
}

AggregateKey aggregate_key(const ReplayInternalCostMapResult &result,
                           const ReplayMemberCostRow &row) {
  return {result.units.at(row.replay_unit_id.value()).graph_template_id.value(),
          row.device_id,
          static_cast<std::uint32_t>(row.slot_role),
          row.replay_body_template_id.value(),
          row.stream_id,
          row.within_stream_position,
          row.identity_symbol_id.value()};
}

void create_schema(Db &db) {
  db.exec(R"SQL(
CREATE TABLE IF NOT EXISTS traceloom_replay_cost_unit (
  cost_unit_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL,
  replay_unit_id INTEGER NOT NULL, graph_template_id INTEGER NOT NULL,
  launch_member_count INTEGER NOT NULL, resolved_launch_count INTEGER NOT NULL,
  support_status TEXT NOT NULL, reason_codes TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS traceloom_replay_cost_launch (
  launch_id TEXT NOT NULL, cost_unit_id TEXT NOT NULL, db_idx INTEGER NOT NULL,
  device_id INTEGER NOT NULL, member_order INTEGER NOT NULL,
  graph_launch_occurrence_id INTEGER NOT NULL, composition_slot_id INTEGER NOT NULL,
  slot_role TEXT NOT NULL, slot_order INTEGER NOT NULL,
  replay_body_template_id INTEGER NOT NULL, body_id INTEGER NOT NULL,
  support_status TEXT NOT NULL, reason_code TEXT NOT NULL,
  member_count INTEGER NOT NULL, task_sum_ns INTEGER NOT NULL,
  busy_union_ns INTEGER NOT NULL, envelope_ns INTEGER NOT NULL,
  compute_ns INTEGER NOT NULL, communication_ns INTEGER NOT NULL,
  data_move_ns INTEGER NOT NULL, replay_unit_id INTEGER NOT NULL,
  graph_template_id INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS traceloom_replay_cost_stream (
  launch_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL,
  stream_id INTEGER NOT NULL, lane_ordinal INTEGER NOT NULL,
  lane_consistent INTEGER NOT NULL, member_count INTEGER NOT NULL,
  task_sum_ns INTEGER NOT NULL, busy_union_ns INTEGER NOT NULL,
  compute_ns INTEGER NOT NULL, communication_ns INTEGER NOT NULL,
  data_move_ns INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS traceloom_replay_cost_member (
  member_id TEXT NOT NULL, launch_id TEXT NOT NULL, cost_unit_id TEXT NOT NULL,
  db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL,
  composition_slot_id INTEGER NOT NULL, slot_role TEXT NOT NULL,
  slot_order INTEGER NOT NULL, replay_body_template_id INTEGER NOT NULL,
  body_id INTEGER NOT NULL, stream_id INTEGER NOT NULL,
  lane_ordinal INTEGER NOT NULL, task_ordinal INTEGER NOT NULL,
  kind TEXT NOT NULL, event_id TEXT NOT NULL, identity TEXT NOT NULL,
  raw_task_id INTEGER NOT NULL, start_ns INTEGER NOT NULL, end_ns INTEGER NOT NULL,
  duration_ns INTEGER NOT NULL, relative_start_ns INTEGER NOT NULL,
  relative_end_ns INTEGER NOT NULL, scheduled_work_share_ppm INTEGER NOT NULL,
  scheduled_work_share_supported INTEGER NOT NULL,
  scheduled_work_denominator_body_task_sum_ns INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS traceloom_replay_cost_aggregate (
  aggregate_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL,
  graph_template_id INTEGER NOT NULL, slot_role TEXT NOT NULL,
  aggregation_scope TEXT NOT NULL, replay_body_template_id INTEGER NOT NULL,
  stream_id INTEGER NOT NULL, task_ordinal INTEGER NOT NULL, identity TEXT NOT NULL,
  kind TEXT NOT NULL, member_occurrence_count INTEGER NOT NULL,
  replay_unit_count INTEGER NOT NULL, launch_member_count INTEGER NOT NULL,
  kind_consistent INTEGER NOT NULL, lane_consistent INTEGER NOT NULL,
  distribution_supported INTEGER NOT NULL, duration_p25_ns INTEGER NOT NULL,
  duration_median_ns INTEGER NOT NULL, duration_p75_ns INTEGER NOT NULL,
  scheduled_work_share_ppm INTEGER NOT NULL,
  scheduled_work_share_supported INTEGER NOT NULL,
  scheduled_work_denominator_body_task_sum_ns INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS traceloom_replay_cost_aggregate_member (
  aggregate_id TEXT NOT NULL, member_id TEXT NOT NULL, db_idx INTEGER NOT NULL,
  device_id INTEGER NOT NULL, contributor_order INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS traceloom_replay_cost_issue (
  issue_id TEXT NOT NULL, db_idx INTEGER NOT NULL, device_id INTEGER NOT NULL,
  code TEXT NOT NULL, replay_unit_id INTEGER NOT NULL, launch_id TEXT,
  detail TEXT NOT NULL);
)SQL");
}

void create_indexes_and_views(Db &db) {
  db.exec(R"SQL(
CREATE INDEX IF NOT EXISTS idx_traceloom_replay_cost_member_event
  ON traceloom_replay_cost_member(event_id, db_idx, device_id);
CREATE INDEX IF NOT EXISTS idx_traceloom_replay_cost_member_launch
  ON traceloom_replay_cost_member(launch_id, db_idx, device_id, lane_ordinal, task_ordinal);
CREATE INDEX IF NOT EXISTS idx_traceloom_replay_cost_aggregate_hotspot
  ON traceloom_replay_cost_aggregate(db_idx, device_id, duration_median_ns DESC);
CREATE INDEX IF NOT EXISTS idx_traceloom_replay_cost_contributor
  ON traceloom_replay_cost_aggregate_member(aggregate_id, contributor_order);
DROP VIEW IF EXISTS traceloom_v_node_replay_cost_member;
CREATE VIEW traceloom_v_node_replay_cost_member AS
SELECT
  g.node_id, g.occurrence_idx, g.view_name, g.coverage_kind,
  g.node_anchor_id, g.node_member_order, g.node_slot_order,
  c.*, g.source_table, g.source_row_id, g.graph_node_id,
  g.original_graph_node_id, g.evidence_level
FROM traceloom_v_node_graph_body_member g
JOIN traceloom_replay_cost_member c
 ON c.member_id = g.member_id
 AND c.db_idx = g.db_idx
 AND c.device_id = g.device_id
WHERE g.coverage_kind = 'self';
)SQL");
}

}  // namespace
#endif

void replace_replay_cost_rows(const std::string &sqlite_path,
                              const NativeIr &ir, std::uint32_t db_idx) {
  replace_replay_cost_rows(sqlite_path, ir,
                           build_replay_internal_cost_map(ir), db_idx);
}

void replace_replay_cost_rows(
    const std::string& sqlite_path,
    const NativeIr& ir,
    const ReplayInternalCostMapResult& result,
    std::uint32_t db_idx) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  Db db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    create_schema(db);
    for (const char *table :
         {"traceloom_replay_cost_aggregate_member",
          "traceloom_replay_cost_aggregate", "traceloom_replay_cost_member",
          "traceloom_replay_cost_stream", "traceloom_replay_cost_launch",
          "traceloom_replay_cost_issue", "traceloom_replay_cost_unit"}) {
      db.exec(std::string("DELETE FROM ") + table);
    }

    Stmt unit_stmt(
        db.get(),
        "INSERT INTO traceloom_replay_cost_unit VALUES (?,?,?,?,?,?,?,?,?)");
    for (const ReplayUnitCostBlock &unit : result.units) {
      const std::string id =
          "replay-cost-unit-" + std::to_string(unit.replay_unit_id.value());
      unit_stmt.text(1, id);
      unit_stmt.integer(2, db_idx);
      unit_stmt.integer(3, unit_device_id(ir, unit));
      unit_stmt.integer(4, unit.replay_unit_id.value());
      unit_stmt.integer(5, unit.graph_template_id.value());
      unit_stmt.integer(6, unit.launch_member_count);
      unit_stmt.integer(7, unit.resolved_launch_count);
      unit_stmt.text(
          8, unit.supported
                 ? "supported"
                 : (unit.resolved_launch_count ? "partial" : "unsupported"));
      unit_stmt.text(9, join_reason_codes(unit.unit_reason_codes));
      unit_stmt.run();
    }

    Stmt launch_stmt(db.get(),
                     "INSERT INTO traceloom_replay_cost_launch VALUES "
                     "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    Stmt stream_stmt(db.get(), "INSERT INTO traceloom_replay_cost_stream "
                               "VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const ReplayUnitCostBlock &unit : result.units) {
      const std::string unit_id =
          "replay-cost-unit-" + std::to_string(unit.replay_unit_id.value());
      for (const ReplayLaunchMemberCostRow &launch : unit.launch_members) {
        const std::string launch_id =
            "graph-launch-" +
            std::to_string(launch.replay_unit_launch_member_id.value());
        std::uint32_t device_id = unit_device_id(ir, unit);
        if (launch.graph_launch_occurrence_id.valid() &&
            launch.graph_launch_occurrence_id.value() <
                ir.graph_launch_occurrences.size())
          device_id =
              ir.graph_launch_occurrences.row(launch.graph_launch_occurrence_id)
                  .device_id;
        int i = 1;
        launch_stmt.text(i++, launch_id);
        launch_stmt.text(i++, unit_id);
        launch_stmt.integer(i++, db_idx);
        launch_stmt.integer(i++, device_id);
        launch_stmt.integer(i++, launch.member_order);
        launch_stmt.integer(
            i++, id_value_or_minus_one(launch.graph_launch_occurrence_id));
        launch_stmt.integer(
            i++, id_value_or_minus_one(launch.replay_composition_slot_id));
        launch_stmt.text(
            i++, replay_internal_cost_map_slot_role_name(launch.slot_role));
        launch_stmt.integer(i++, launch.slot_order);
        launch_stmt.integer(
            i++, id_value_or_minus_one(launch.replay_body_template_id));
        launch_stmt.integer(
            i++, id_value_or_minus_one(launch.graph_launch_body_id));
        launch_stmt.text(i++, launch.supported ? "supported" : "unsupported");
        launch_stmt.text(i++, launch.reason_code);
        launch_stmt.integer(i++, launch.member_count);
        launch_stmt.integer(i++, launch.task_sum_ns);
        launch_stmt.integer(i++, launch.busy_union_ns);
        launch_stmt.integer(i++, launch.envelope_ns);
        launch_stmt.integer(i++, launch.compute_ns);
        launch_stmt.integer(i++, launch.communication_ns);
        launch_stmt.integer(i++, launch.data_move_ns);
        // Keep the owning replay/template identity directly queryable at the
        // launch lens instead of requiring an implicit unit-row lookup.
        launch_stmt.integer(i++, unit.replay_unit_id.value());
        launch_stmt.integer(i++, unit.graph_template_id.value());
        launch_stmt.run();
        for (const ReplayStreamCostRow &stream : launch.streams) {
          int j = 1;
          stream_stmt.text(j++, launch_id);
          stream_stmt.integer(j++, db_idx);
          stream_stmt.integer(j++, device_id);
          stream_stmt.integer(j++, stream.stream_id);
          stream_stmt.integer(j++, stream.lane_ordinal);
          stream_stmt.boolean(j++, stream.lane_consistent);
          stream_stmt.integer(j++, stream.member_count);
          stream_stmt.integer(j++, stream.task_sum_ns);
          stream_stmt.integer(j++, stream.busy_union_ns);
          stream_stmt.integer(j++, stream.compute_ns);
          stream_stmt.integer(j++, stream.communication_ns);
          stream_stmt.integer(j++, stream.data_move_ns);
          stream_stmt.run();
        }
      }
    }

    Stmt member_stmt(db.get(),
                     "INSERT INTO traceloom_replay_cost_member VALUES "
                     "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const ReplayMemberCostRow &member : result.members) {
      int i = 1;
      member_stmt.text(
          i++, "graph-body-member-" +
                   std::to_string(member.graph_launch_body_member_id.value()));
      member_stmt.text(
          i++, "graph-launch-" +
                   std::to_string(member.replay_unit_launch_member_id.value()));
      member_stmt.text(i++, "replay-cost-unit-" +
                                std::to_string(member.replay_unit_id.value()));
      member_stmt.integer(i++, db_idx);
      member_stmt.integer(i++, member.device_id);
      member_stmt.integer(i++, member.replay_composition_slot_id.value());
      member_stmt.text(
          i++, replay_internal_cost_map_slot_role_name(member.slot_role));
      member_stmt.integer(i++, member.slot_order);
      member_stmt.integer(i++, member.replay_body_template_id.value());
      member_stmt.integer(i++, member.graph_launch_body_id.value());
      member_stmt.integer(i++, member.stream_id);
      member_stmt.integer(i++, member.lane_ordinal);
      member_stmt.integer(i++, member.within_stream_position);
      member_stmt.text(i++,
                       replay_internal_cost_map_member_kind_name(member.kind));
      member_stmt.text(i++, trace_event_compat_id(member.trace_event_id));
      member_stmt.text(i++, symbol_value(ir, member.identity_symbol_id));
      member_stmt.integer(i++, member.raw_task_id);
      member_stmt.integer(i++, member.start_ns);
      member_stmt.integer(i++, member.end_ns);
      member_stmt.integer(i++, member.duration_ns);
      member_stmt.integer(i++, member.relative_start_ns);
      member_stmt.integer(i++, member.relative_end_ns);
      member_stmt.integer(i++, member.scheduled_work_share_ppm);
      member_stmt.boolean(i++, member.scheduled_work_share_supported);
      member_stmt.integer(i++,
                          member.scheduled_work_denominator_body_task_sum_ns);
      member_stmt.run();
    }

    std::map<AggregateKey, std::string> aggregate_ids;
    Stmt aggregate_stmt(db.get(),
                        "INSERT INTO traceloom_replay_cost_aggregate VALUES "
                        "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (std::size_t n = 0; n < result.aggregates.size(); ++n) {
      const auto &row = result.aggregates[n];
      const std::string id = "replay-cost-aggregate-" + std::to_string(n);
      aggregate_ids.emplace(aggregate_key(row), id);
      int i = 1;
      aggregate_stmt.text(i++, id);
      aggregate_stmt.integer(i++, db_idx);
      aggregate_stmt.integer(i++, row.device_id);
      aggregate_stmt.integer(i++, row.graph_template_id.value());
      aggregate_stmt.text(
          i++, replay_internal_cost_map_slot_role_name(row.slot_role));
      aggregate_stmt.text(i++, replay_internal_cost_map_aggregation_scope_name(
                                   row.aggregation_scope));
      aggregate_stmt.integer(i++, row.replay_body_template_id.value());
      aggregate_stmt.integer(i++, row.stream_id);
      aggregate_stmt.integer(i++, row.within_stream_position);
      aggregate_stmt.text(i++, symbol_value(ir, row.identity_symbol_id));
      aggregate_stmt.text(i++,
                          replay_internal_cost_map_member_kind_name(row.kind));
      aggregate_stmt.integer(i++, row.member_occurrence_count);
      aggregate_stmt.integer(i++, row.replay_unit_count);
      aggregate_stmt.integer(i++, row.launch_member_count);
      aggregate_stmt.boolean(i++, row.kind_consistent);
      aggregate_stmt.boolean(i++, row.lane_consistent);
      aggregate_stmt.boolean(i++, row.distribution_supported);
      aggregate_stmt.integer(i++, row.duration_p25_ns);
      aggregate_stmt.integer(i++, row.duration_median_ns);
      aggregate_stmt.integer(i++, row.duration_p75_ns);
      aggregate_stmt.integer(i++, row.scheduled_work_share_ppm);
      aggregate_stmt.boolean(i++, row.scheduled_work_share_supported);
      aggregate_stmt.integer(i++,
                             row.scheduled_work_denominator_body_task_sum_ns);
      aggregate_stmt.run();
    }
    Stmt contributor_stmt(db.get(),
                          "INSERT INTO traceloom_replay_cost_aggregate_member "
                          "VALUES (?,?,?,?,?)");
    std::map<std::string, std::uint32_t> contributor_order;
    for (const ReplayMemberCostRow &member : result.members) {
      if (!member.identity_symbol_id.valid())
        continue;
      const auto found = aggregate_ids.find(aggregate_key(result, member));
      if (found == aggregate_ids.end())
        throw std::logic_error("replay aggregate contributor has no aggregate");
      contributor_stmt.text(1, found->second);
      contributor_stmt.text(
          2, "graph-body-member-" +
                 std::to_string(member.graph_launch_body_member_id.value()));
      contributor_stmt.integer(3, db_idx);
      contributor_stmt.integer(4, member.device_id);
      contributor_stmt.integer(5, contributor_order[found->second]++);
      contributor_stmt.run();
    }
    Stmt issue_stmt(
        db.get(),
        "INSERT INTO traceloom_replay_cost_issue VALUES (?,?,?,?,?,?,?)");
    for (std::size_t n = 0; n < result.issues.size(); ++n) {
      const auto &issue = result.issues[n];
      const std::uint32_t device =
          issue.replay_unit_id.valid() &&
                  issue.replay_unit_id.value() < result.units.size()
              ? unit_device_id(ir, result.units[issue.replay_unit_id.value()])
              : 0;
      issue_stmt.text(1, "replay-cost-issue-" + std::to_string(n));
      issue_stmt.integer(2, db_idx);
      issue_stmt.integer(3, device);
      issue_stmt.text(4, issue.code);
      issue_stmt.integer(5, id_value_or_minus_one(issue.replay_unit_id));
      if (issue.replay_unit_launch_member_id.valid())
        issue_stmt.text(
            6, "graph-launch-" +
                   std::to_string(issue.replay_unit_launch_member_id.value()));
      else
        sqlite3_bind_null(issue_stmt.get(), 6);
      issue_stmt.text(7, issue.detail);
      issue_stmt.run();
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
  (void)db_idx;
  throw std::runtime_error("replay cost SQL requires SQLite support");
#endif
}

}  // namespace traceloom::compat
