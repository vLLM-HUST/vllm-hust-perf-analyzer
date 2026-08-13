#include "traceloom/compat/evidence_role_sql_rows.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/aux_attribution_rows.h"
#include "traceloom/compat/timeline_rows.h"

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
#include <sqlite3.h>
#endif

namespace traceloom::compat {

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
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

struct ReplayMembership {
  ReplayUnitId replay_unit_id;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  bool exact = false;
};

struct PlacementRow {
  std::string decision_id;
  std::uint32_t placement_order = 0;
  std::string placement_kind;
  std::string placement_id;
  std::string owner_id;
  std::string relation_name;
  std::string support_state = "supported";
  std::string reason_code;
};

struct IssueRow {
  std::string decision_id;
  std::string code;
  std::string support_state;
  std::string related_ids;
};

struct ProtectedIntervalSqlRow {
  std::string protected_interval_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string kind;
  std::string boundary_policy;
  std::string first_anchor_id;
  std::string last_anchor_id;
  std::string replay_unit_id;
  std::uint32_t evidence_source_ref_id = 0;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::string support_state;
  std::string reason_code;
};

struct DecisionRow {
  std::string decision_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::int64_t task_id = -1;
  std::string event_id;
  std::uint32_t source_ref_id = 0;
  std::string source_domain;
  std::string input_provider_scope;
  std::string policy_id;
  std::string policy_version;
  std::string manifest_sha256;
  std::string policy_role;
  std::string final_role;
  std::string rule_id;
  std::string rule_class;
  bool matched_rule = false;
  std::int32_t priority = 0;
  std::uint64_t declaration_order = 0;
  std::string policy_structural_participation;
  std::string effective_structural_participation;
  std::string support_state;
  std::string reason_code;
  std::string available_fields;
  std::string required_fields;
  std::string missing_required_fields;
  std::string missing_capability_rule_ids;
  std::string cost_treatment;
  std::string context_treatment;
  std::string provenance_treatment;
  std::string source_table;
  std::string source_key;
  std::int64_t start_ns = 0;
  std::int64_t end_ns = 0;
  std::int64_t duration_ns = 0;
  std::vector<PlacementRow> placements;
  std::vector<IssueRow> issues;
};

std::string join_strings(const std::set<std::string>& values) {
  std::string out;
  for (const std::string& value : values) {
    if (!out.empty()) {
      out += ',';
    }
    out += value;
  }
  return out;
}

std::string replay_unit_id_text(ReplayUnitId id) {
  return "replay-unit-" + std::to_string(id.value());
}

std::string protected_interval_id_text(ProtectedIntervalId id) {
  return "protected-interval-" + std::to_string(id.value());
}

std::string graph_body_member_id_text(GraphLaunchBodyMemberId id) {
  return "graph-body-member-" + std::to_string(id.value());
}

const char* protected_interval_kind_name(ProtectedIntervalKind kind) {
  switch (kind) {
    case ProtectedIntervalKind::kGraphReplayUnit:
      return "graph_replay_unit";
    case ProtectedIntervalKind::kUserWindow:
      return "user_window";
    case ProtectedIntervalKind::kUnknown:
      return "unknown";
  }
  return "unknown";
}

const char* boundary_policy_name(BoundaryPolicy policy) {
  switch (policy) {
    case BoundaryPolicy::kNoCross:
      return "no_cross";
    case BoundaryPolicy::kAllowEnclosing:
      return "allow_enclosing";
    case BoundaryPolicy::kBlockAnyOverlap:
      return "block_any_overlap";
  }
  return "unknown";
}

std::string symbol_text(const NativeIr& ir, SymbolId id) {
  return id.valid() ? ir.symbols.value(id) : std::string();
}

bool task_type_is_explicitly_skipped(const NativeIr& ir,
                                     const TaskRow& task,
                                     const FlatAnchorBuildConfig& config) {
  if (!task.task_type_symbol_id.valid()) {
    return false;
  }
  const std::string& task_type = ir.symbols.value(task.task_type_symbol_id);
  return std::find(config.skipped_task_type_symbols.begin(),
                   config.skipped_task_type_symbols.end(),
                   task_type) != config.skipped_task_type_symbols.end();
}

std::uint64_t connection_key(std::uint32_t device_id,
                             std::int64_t connection_id) {
  return (static_cast<std::uint64_t>(device_id) << 32u) ^
         (static_cast<std::uint64_t>(connection_id) & 0xffffffffu);
}

bool overlaps(std::int64_t lhs_start, std::int64_t lhs_end,
              std::int64_t rhs_start, std::int64_t rhs_end) {
  return lhs_start <= rhs_end && lhs_end >= rhs_start;
}

std::vector<ReplayMembership> replay_memberships_for_event(
    const NativeIr& ir,
    const TraceEventRow& event,
    const std::unordered_map<TraceEventId::value_type,
                             std::set<ReplayUnitId::value_type>>&
        exact_replays_by_event) {
  std::map<ReplayUnitId::value_type, ReplayMembership> memberships;
  const auto exact = exact_replays_by_event.find(event.id.value());
  if (exact != exact_replays_by_event.end()) {
    for (const ReplayUnitId::value_type value : exact->second) {
      const ReplayUnitRow& replay = ir.replay_units.row(ReplayUnitId(value));
      const TraceEventRow& replay_event =
          ir.trace_events.row(replay.launch_trace_event_id);
      memberships.emplace(
          value, ReplayMembership{replay.id, replay_event.start_ns,
                                  replay_event.end_ns, true});
    }
  }
  for (const ReplayUnitRow& replay : ir.replay_units.rows()) {
    if (!replay.launch_trace_event_id.valid() ||
        replay.launch_trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument(
          "evidence-role SQL: ReplayUnit launch event is out of range");
    }
    const TraceEventRow& replay_event =
        ir.trace_events.row(replay.launch_trace_event_id);
    if (replay.launch_trace_event_id == event.id ||
        (event.device_id == replay_event.device_id &&
         event.start_ns >= replay_event.start_ns &&
         event.end_ns <= replay_event.end_ns)) {
      const bool exact_membership = replay.replay_composition_region_id.valid();
      auto inserted = memberships.emplace(
          replay.id.value(),
          ReplayMembership{replay.id, replay_event.start_ns,
                           replay_event.end_ns, exact_membership});
      inserted.first->second.exact =
          inserted.first->second.exact || exact_membership;
    }
  }
  std::vector<ReplayMembership> result;
  for (const auto& item : memberships) {
    result.push_back(item.second);
  }
  return result;
}

void add_placement(DecisionRow& row, std::string kind, std::string id,
                   std::string owner, std::string relation,
                   std::string reason,
                   std::string support = "supported") {
  PlacementRow placement;
  placement.decision_id = row.decision_id;
  placement.placement_order =
      static_cast<std::uint32_t>(row.placements.size());
  placement.placement_kind = std::move(kind);
  placement.placement_id = std::move(id);
  placement.owner_id = std::move(owner);
  placement.relation_name = std::move(relation);
  placement.support_state = std::move(support);
  placement.reason_code = std::move(reason);
  row.placements.push_back(std::move(placement));
}

void add_issue(DecisionRow& row, std::string code, std::string support,
               std::string related) {
  row.issues.push_back(
      IssueRow{row.decision_id, std::move(code), std::move(support),
               std::move(related)});
}

void apply_policy_decision(DecisionRow& row,
                           const SignalClassificationDecision& decision) {
  row.policy_role = signal_role_name(decision.role);
  row.final_role = row.policy_role;
  row.rule_id = decision.rule_id;
  row.rule_class = decision.matched_rule ? "positive_policy" : "fallback";
  row.matched_rule = decision.matched_rule;
  row.priority = decision.priority;
  row.declaration_order = decision.declaration_order;
  row.policy_structural_participation =
      signal_structural_participation_name(decision.structural_participation);
  row.effective_structural_participation =
      row.policy_structural_participation;
  row.support_state = decision.support_state;
  row.reason_code = decision.reason_code;
  row.available_fields = decision.available_fields;
  row.required_fields = decision.required_fields;
  row.missing_required_fields = decision.missing_required_fields;
  row.missing_capability_rule_ids =
      decision.missing_capability_rule_ids;
  row.cost_treatment = signal_cost_treatment_name(decision.cost_treatment);
  row.context_treatment =
      signal_context_treatment_name(decision.context_treatment);
  row.provenance_treatment =
      signal_provenance_treatment_name(decision.provenance_treatment);
}

void apply_system_decision(DecisionRow& row, const std::string& role,
                           const std::string& participation,
                           const std::string& rule_id,
                           const std::string& rule_class,
                           const std::string& support_state,
                           const std::string& reason_code,
                           const std::string& required_fields) {
  row.final_role = role;
  row.rule_id = rule_id;
  row.rule_class = rule_class;
  row.matched_rule = true;
  row.priority = 0;
  row.declaration_order = 0;
  row.effective_structural_participation = participation;
  row.support_state = support_state;
  row.reason_code = reason_code;
  row.required_fields = required_fields;
  row.cost_treatment = "retained_for_attribution";
  row.context_treatment = "retained";
  row.provenance_treatment = "retained";
}

std::vector<DecisionRow> build_decisions(
    const NativeIr& ir,
    const FlatAnchorBuildConfig& config,
    std::uint32_t db_idx,
    bool materialize_aux_attribution) {
  std::unordered_map<TraceEventId::value_type, const TaskRow*> task_by_event;
  for (const TaskRow& task : ir.tasks.rows()) {
    if (!task.trace_event_id.valid() ||
        task.trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument(
          "evidence-role SQL: TaskRow trace_event_id is out of range");
    }
    if (!task_by_event.emplace(task.trace_event_id.value(), &task).second) {
      throw std::invalid_argument(
          "evidence-role SQL: multiple tasks reference one normalized event");
    }
  }

  std::unordered_map<TraceEventId::value_type,
                     std::vector<const CommunicationOpRow*>>
      comm_by_event;
  std::unordered_map<std::uint64_t,
                     std::vector<const CommunicationOpRow*>>
      comm_by_connection;
  for (const CommunicationOpRow& comm : ir.communication_ops.rows()) {
    if (!comm.trace_event_id.valid() ||
        comm.trace_event_id.value() >= ir.trace_events.size()) {
      throw std::invalid_argument(
          "evidence-role SQL: communication event is out of range");
    }
    comm_by_event[comm.trace_event_id.value()].push_back(&comm);
    const TraceEventRow& event = ir.trace_events.row(comm.trace_event_id);
    comm_by_connection[connection_key(event.device_id, comm.raw_connection_id)]
        .push_back(&comm);
  }

  std::unordered_map<TaskId::value_type,
                     std::vector<GraphLaunchBodyMemberId>>
      body_members_by_task;
  std::unordered_map<GraphLaunchOccurrenceId::value_type,
                     std::set<ReplayUnitId::value_type>>
      replay_units_by_occurrence;
  for (const ReplayUnitLaunchMemberRow& member :
       ir.replay_unit_launch_members.rows()) {
    if (!member.replay_unit_id.valid() ||
        member.replay_unit_id.value() >= ir.replay_units.size() ||
        !member.graph_launch_occurrence_id.valid() ||
        member.graph_launch_occurrence_id.value() >=
            ir.graph_launch_occurrences.size()) {
      throw std::invalid_argument(
          "evidence-role SQL: replay launch membership is out of range");
    }
    replay_units_by_occurrence[member.graph_launch_occurrence_id.value()]
        .insert(member.replay_unit_id.value());
  }
  std::unordered_map<TraceEventId::value_type,
                     std::set<ReplayUnitId::value_type>>
      exact_replays_by_event;
  for (const GraphLaunchBodyMemberRow& member :
       ir.graph_launch_body_members.rows()) {
    if (!member.task_id.valid() || member.task_id.value() >= ir.tasks.size() ||
        !member.graph_launch_body_id.valid() ||
        member.graph_launch_body_id.value() >= ir.graph_launch_bodies.size()) {
      throw std::invalid_argument(
          "evidence-role SQL: graph body membership is out of range");
    }
    body_members_by_task[member.task_id.value()].push_back(member.id);
    const GraphLaunchBodyRow& body =
        ir.graph_launch_bodies.row(member.graph_launch_body_id);
    const auto replay_found = replay_units_by_occurrence.find(
        body.graph_launch_occurrence_id.value());
    if (replay_found == replay_units_by_occurrence.end()) {
      continue;
    }
    const TaskRow& task = ir.tasks.row(member.task_id);
    exact_replays_by_event[task.trace_event_id.value()].insert(
        replay_found->second.begin(), replay_found->second.end());
  }

  std::unordered_map<TraceEventId::value_type, std::vector<AnchorId>>
      anchors_by_event;
  std::unordered_map<ReplayUnitId::value_type, std::vector<AnchorId>>
      anchors_by_replay;
  for (const AnchorRow& anchor : ir.anchors.rows()) {
    if (anchor.trace_event_id.valid()) {
      anchors_by_event[anchor.trace_event_id.value()].push_back(anchor.id);
    }
    if (anchor.replay_unit_id.valid()) {
      anchors_by_replay[anchor.replay_unit_id.value()].push_back(anchor.id);
    }
  }

  std::unordered_map<ReplayUnitId::value_type, ProtectedIntervalId>
      protected_by_replay;
  for (const ReplayUnitRow& replay : ir.replay_units.rows()) {
    if (!replay.first_anchor_id.valid() || !replay.last_anchor_id.valid()) {
      continue;
    }
    for (const ProtectedIntervalRow& interval :
         ir.protected_intervals.rows()) {
      if (interval.first_anchor_id == replay.first_anchor_id &&
          interval.last_anchor_id == replay.last_anchor_id) {
        if (!protected_by_replay.emplace(replay.id.value(), interval.id)
                 .second) {
          throw std::invalid_argument(
              "evidence-role SQL: replay maps to multiple protected intervals");
        }
      }
    }
  }

  std::unordered_map<TraceEventId::value_type,
                     std::vector<const AuxLinkSqlRow*>>
      aux_by_event;
  AuxAttributionSqlRows aux_rows;
  if (materialize_aux_attribution) {
    aux_rows = build_aux_attribution_sql_rows(ir, db_idx);
    for (const AuxLinkSqlRow& link : aux_rows.aux_links) {
      const std::string prefix = "event-";
      if (link.aux_event_id.rfind(prefix, 0) != 0) {
        throw std::invalid_argument(
            "evidence-role SQL: malformed auxiliary event id");
      }
      const auto event_value = static_cast<TraceEventId::value_type>(
          std::stoul(link.aux_event_id.substr(prefix.size())));
      aux_by_event[event_value].push_back(&link);
    }
  }

  std::vector<DecisionRow> rows;
  rows.reserve(ir.trace_events.size());
  for (const TraceEventRow& event : ir.trace_events.rows()) {
    if (!event.source_ref_id.valid() ||
        event.source_ref_id.value() >= ir.source_refs.size()) {
      throw std::invalid_argument(
          "evidence-role SQL: event source_ref_id is out of range");
    }
    const SourceRefRow& source = ir.source_refs.row(event.source_ref_id);
    DecisionRow row;
    row.decision_id = "role-decision-" + std::to_string(event.id.value());
    row.db_idx = db_idx;
    row.device_id = event.device_id;
    row.event_id = trace_event_compat_id(event.id);
    row.source_ref_id = event.source_ref_id.value();
    row.policy_id = config.classification_rules.metadata().policy_id;
    row.policy_version =
        config.classification_rules.metadata().policy_version;
    row.manifest_sha256 =
        config.classification_rules.metadata().manifest_sha256;
    row.source_table = source.table_name;
    row.source_key = std::to_string(event.source_row_id);
    row.start_ns = event.start_ns;
    row.end_ns = event.end_ns;
    row.duration_ns = std::max<std::int64_t>(0, event.end_ns - event.start_ns);
    add_placement(row, "normalized_event", row.event_id, "",
                  "traceloom_event", "normalized_event_retained");

    const auto task_found = task_by_event.find(event.id.value());
    const TaskRow* task =
        task_found == task_by_event.end() ? nullptr : task_found->second;
    if (task != nullptr) {
      row.task_id = task->id.value();
      row.source_domain = "task";
      const SignalClassificationInput input =
          signal_classification_input_for_task(ir, *task);
      row.input_provider_scope = input.provider_scope;
      const SignalClassificationDecision policy_decision =
          config.classification_rules.decide(input);
      apply_policy_decision(row, policy_decision);
      if (!policy_decision.missing_required_fields.empty()) {
        add_issue(
            row, "missing_required_capability", policy_decision.support_state,
            policy_decision.missing_required_fields + ":" +
                policy_decision.missing_capability_rule_ids);
      }
    } else {
      row.input_provider_scope = "any";
      row.policy_structural_participation = "not_applicable";
      row.available_fields = "provider_scope,source_domain";
    }

    const std::vector<ReplayMembership> replay_memberships =
        replay_memberships_for_event(ir, event, exact_replays_by_event);
    bool communication_covered =
        comm_by_event.find(event.id.value()) != comm_by_event.end();
    if (!communication_covered && task != nullptr &&
        task->raw_connection_id >= 0) {
      const auto found = comm_by_connection.find(
          connection_key(event.device_id, task->raw_connection_id));
      if (found != comm_by_connection.end()) {
        for (const CommunicationOpRow* comm : found->second) {
          const TraceEventRow& comm_event =
              ir.trace_events.row(comm->trace_event_id);
          if (overlaps(event.start_ns, event.end_ns, comm_event.start_ns,
                       comm_event.end_ns)) {
            communication_covered = true;
          }
        }
      }
    }

    const bool skip_replay_covered =
        config.skip_tasks_covered_by_replay_units ||
        config.skip_events_covered_by_replay_units;
    if (!replay_memberships.empty() &&
        (task == nullptr || skip_replay_covered)) {
      const bool conflict = replay_memberships.size() > 1;
      apply_system_decision(
          row, "protected_boundary", "atomic_boundary",
          "system.protected_composite", "protected_membership",
          conflict ? "conflict" : "supported",
          conflict ? "multiple_protected_composites"
                   : "provider_composite_membership",
          "replay_unit_membership");
      std::set<std::string> replay_ids;
      for (const ReplayMembership& membership : replay_memberships) {
        replay_ids.insert(replay_unit_id_text(membership.replay_unit_id));
        add_placement(row, "replay_unit",
                      replay_unit_id_text(membership.replay_unit_id), "",
                      "traceloom_graph_launch",
                      membership.exact ? "exact_composite_membership"
                                       : "typed_composite_membership");
        const auto interval =
            protected_by_replay.find(membership.replay_unit_id.value());
        if (interval != protected_by_replay.end()) {
          add_placement(row, "protected_interval",
                        protected_interval_id_text(interval->second),
                        replay_unit_id_text(membership.replay_unit_id),
                        "traceloom_protected_interval",
                        "generic_discovery_no_cross_boundary");
        }
        const auto replay_anchors =
            anchors_by_replay.find(membership.replay_unit_id.value());
        if (replay_anchors != anchors_by_replay.end()) {
          for (const AnchorId anchor : replay_anchors->second) {
            add_placement(row, "anchor", anchor_compat_id(anchor),
                          replay_unit_id_text(membership.replay_unit_id),
                          "traceloom_anchor",
                          "protected_composite_anchor");
          }
        }
      }
      if (conflict) {
        add_issue(row, "multiple_protected_composites", "conflict",
                  join_strings(replay_ids));
      }
    } else if (task != nullptr &&
               task_type_is_explicitly_skipped(ir, *task, config)) {
      apply_system_decision(row, "auxiliary", "excluded",
                            "system.analysis_config_exclusion",
                            "analysis_config", "supported",
                            "explicit_analysis_config_exclusion",
                            "task_type");
    } else if (communication_covered &&
               (task == nullptr ||
                config.skip_tasks_covered_by_communication_ops)) {
      apply_system_decision(row, "anchor", "identity",
                            "system.communication_anchor",
                            "provider_relation", "supported",
                            "represented_by_communication_anchor",
                            "communication_membership");
    } else if (task != nullptr &&
               !config.filter_auxiliary_task_anchors) {
      apply_system_decision(row, "anchor", "identity",
                            "system.unfiltered_task_anchor",
                            "analysis_config", "supported",
                            "auxiliary_filter_disabled", "task_event");
    } else if (task == nullptr) {
      const bool has_direct_anchor =
          anchors_by_event.find(event.id.value()) != anchors_by_event.end();
      if (has_direct_anchor) {
        row.source_domain = "event";
        apply_system_decision(row, "anchor", "identity",
                              "system.existing_event_anchor",
                              "structural_membership", "supported",
                              "existing_anchor_membership", "anchor");
      } else {
        row.source_domain = "event";
        apply_system_decision(row, "unsupported", "not_applicable",
                              "system.unsupported_event_kind", "unsupported",
                              "unsupported", "event_not_projection_candidate",
                              "normalized_event");
        add_issue(row, "event_not_projection_candidate", "unsupported",
                  row.event_id);
      }
    }

    const auto direct_anchors = anchors_by_event.find(event.id.value());
    if (direct_anchors != anchors_by_event.end()) {
      if (direct_anchors->second.size() > 1) {
        row.support_state = "conflict";
        row.reason_code = "multiple_direct_anchors";
        std::set<std::string> ids;
        for (const AnchorId anchor : direct_anchors->second) {
          ids.insert(anchor_compat_id(anchor));
        }
        add_issue(row, "multiple_direct_anchors", "conflict",
                  join_strings(ids));
      }
      for (const AnchorId anchor : direct_anchors->second) {
        add_placement(row, "anchor", anchor_compat_id(anchor), "",
                      "traceloom_anchor", "direct_event_anchor");
      }
    }

    if (task != nullptr) {
      const auto members = body_members_by_task.find(task->id.value());
      if (members != body_members_by_task.end()) {
        if (members->second.size() > 1) {
          row.support_state = "conflict";
          row.reason_code = "multiple_graph_body_memberships";
          std::set<std::string> ids;
          for (const GraphLaunchBodyMemberId member : members->second) {
            ids.insert(graph_body_member_id_text(member));
          }
          add_issue(row, "multiple_graph_body_memberships", "conflict",
                    join_strings(ids));
        }
        for (const GraphLaunchBodyMemberId member : members->second) {
          add_placement(row, "graph_body_member",
                        graph_body_member_id_text(member), "",
                        "traceloom_graph_body_member",
                        "exact_graph_body_membership");
        }
      }
    }

    const auto aux = aux_by_event.find(event.id.value());
    if (aux != aux_by_event.end()) {
      for (const AuxLinkSqlRow* link : aux->second) {
        add_placement(row, "auxiliary_link",
                      link->anchor_id + "/aux-" +
                          std::to_string(link->aux_order),
                      link->anchor_id, "traceloom_aux_link",
                      "retained_between_anchor_evidence");
      }
    }

    const bool identity_role = row.final_role == "anchor" ||
                               row.final_role == "unknown_anchor";
    const bool excluded_role = row.final_role == "auxiliary" ||
                               row.final_role == "transparent";
    const bool has_identity_placement = std::any_of(
        row.placements.begin(), row.placements.end(),
        [](const PlacementRow& placement) {
          return placement.placement_kind == "anchor" ||
                 placement.placement_kind == "graph_body_member";
        });
    const bool has_aux_placement = std::any_of(
        row.placements.begin(), row.placements.end(),
        [](const PlacementRow& placement) {
          return placement.placement_kind == "auxiliary_link";
        });
    if (identity_role && !has_identity_placement &&
        row.final_role != "protected_boundary") {
      row.support_state = "orphan";
      row.reason_code = "identity_event_without_anchor";
      add_issue(row, "identity_event_without_anchor", "orphan", row.event_id);
    } else if (excluded_role && materialize_aux_attribution &&
               !has_aux_placement) {
      row.support_state = "retained_unplaced";
      row.reason_code = "omitted_event_without_auxiliary_link";
      add_issue(row, "omitted_event_without_auxiliary_link",
                "retained_unplaced", row.event_id);
    }

    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<ProtectedIntervalSqlRow> build_protected_intervals(
    const NativeIr& ir, std::uint32_t db_idx) {
  std::map<std::pair<AnchorId::value_type, AnchorId::value_type>, ReplayUnitId>
      replay_by_bounds;
  for (const ReplayUnitRow& replay : ir.replay_units.rows()) {
    if (replay.first_anchor_id.valid() && replay.last_anchor_id.valid()) {
      const auto inserted = replay_by_bounds.emplace(
          std::make_pair(replay.first_anchor_id.value(),
                         replay.last_anchor_id.value()),
          replay.id);
      if (!inserted.second) {
        throw std::invalid_argument(
            "evidence-role SQL: multiple replay units share anchor bounds");
      }
    }
  }
  std::vector<ProtectedIntervalSqlRow> rows;
  rows.reserve(ir.protected_intervals.size());
  for (const ProtectedIntervalRow& interval : ir.protected_intervals.rows()) {
    if (!interval.first_anchor_id.valid() ||
        interval.first_anchor_id.value() >= ir.anchors.size() ||
        !interval.last_anchor_id.valid() ||
        interval.last_anchor_id.value() >= ir.anchors.size() ||
        !interval.evidence_source_ref_id.valid() ||
        interval.evidence_source_ref_id.value() >= ir.source_refs.size()) {
      throw std::invalid_argument(
          "evidence-role SQL: protected interval identity is out of range");
    }
    const AnchorRow& first = ir.anchors.row(interval.first_anchor_id);
    const AnchorRow& last = ir.anchors.row(interval.last_anchor_id);
    ProtectedIntervalSqlRow row;
    row.protected_interval_id = protected_interval_id_text(interval.id);
    row.db_idx = db_idx;
    row.device_id = first.device_id;
    row.kind = protected_interval_kind_name(interval.kind);
    row.boundary_policy = boundary_policy_name(interval.boundary_policy);
    row.first_anchor_id = anchor_compat_id(interval.first_anchor_id);
    row.last_anchor_id = anchor_compat_id(interval.last_anchor_id);
    row.evidence_source_ref_id = interval.evidence_source_ref_id.value();
    row.start_ns = first.start_ns;
    row.end_ns = last.end_ns;
    const auto replay = replay_by_bounds.find(
        {interval.first_anchor_id.value(), interval.last_anchor_id.value()});
    if (replay != replay_by_bounds.end()) {
      row.replay_unit_id = replay_unit_id_text(replay->second);
      row.support_state = "supported";
      row.reason_code = "exact_replay_anchor_bounds";
    } else {
      row.support_state = "typed_open";
      row.reason_code = "protected_bounds_without_replay_unit";
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

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

void insert_decisions(Db& db, const std::vector<DecisionRow>& rows) {
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
  for (const DecisionRow& row : rows) {
    int i = 1;
    decision.text(i++, row.decision_id);
    decision.integer(i++, row.db_idx);
    decision.integer(i++, row.device_id);
    if (row.task_id < 0) {
      decision.null(i++);
    } else {
      decision.integer(i++, row.task_id);
    }
    decision.text(i++, row.event_id);
    decision.integer(i++, row.source_ref_id);
    decision.text(i++, row.source_domain);
    decision.text(i++, row.input_provider_scope);
    decision.text(i++, row.policy_id);
    decision.text(i++, row.policy_version);
    decision.text(i++, row.manifest_sha256);
    if (row.policy_role.empty()) {
      decision.null(i++);
    } else {
      decision.text(i++, row.policy_role);
    }
    decision.text(i++, row.final_role);
    decision.text(i++, row.rule_id);
    decision.text(i++, row.rule_class);
    decision.boolean(i++, row.matched_rule);
    decision.integer(i++, row.priority);
    decision.integer(i++, static_cast<std::int64_t>(row.declaration_order));
    decision.text(i++, row.policy_structural_participation);
    decision.text(i++, row.effective_structural_participation);
    decision.text(i++, row.support_state);
    decision.text(i++, row.reason_code);
    decision.text(i++, row.available_fields);
    decision.text(i++, row.required_fields);
    decision.text(i++, row.missing_required_fields);
    decision.text(i++, row.missing_capability_rule_ids);
    decision.text(i++, row.cost_treatment);
    decision.text(i++, row.context_treatment);
    decision.text(i++, row.provenance_treatment);
    decision.text(i++, row.source_table);
    decision.text(i++, row.source_key);
    decision.integer(i++, row.start_ns);
    decision.integer(i++, row.end_ns);
    decision.integer(i++, row.duration_ns);
    decision.run();

    for (const PlacementRow& item : row.placements) {
      int j = 1;
      placement.text(j++, item.decision_id);
      placement.integer(j++, item.placement_order);
      placement.text(j++, item.placement_kind);
      placement.text(j++, item.placement_id);
      if (item.owner_id.empty()) {
        placement.null(j++);
      } else {
        placement.text(j++, item.owner_id);
      }
      placement.text(j++, item.relation_name);
      placement.text(j++, item.support_state);
      placement.text(j++, item.reason_code);
      placement.run();
    }
    for (const IssueRow& item : row.issues) {
      issue.text(1, "role-issue-" + std::to_string(issue_index++));
      issue.text(2, item.decision_id);
      issue.text(3, item.code);
      issue.text(4, item.support_state);
      issue.text(5, item.related_ids);
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
JOIN traceloom_v_node_graph_body_member n
  ON n.replay_unit_id = CAST(substr(p.placement_id, 13) AS INTEGER)
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
JOIN traceloom_v_node_graph_body_member n
  ON n.replay_unit_id = CAST(substr(i.replay_unit_id, 13) AS INTEGER)
 AND n.db_idx = d.db_idx AND n.device_id = d.device_id;
)SQL");
}

}  // namespace
#endif

void replace_evidence_role_sql_rows(
    const std::string& sqlite_path,
    const NativeIr& ir,
    FlatAnchorBuildConfig config,
    std::uint32_t db_idx,
    bool materialize_aux_attribution) {
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
  if (config.classification_rules.rules().empty()) {
    config.classification_rules = load_default_signal_classification_ruleset();
  }
  if (!config.classification_overrides.empty()) {
    config.classification_rules = override_signal_classification_ruleset(
        config.classification_rules, config.classification_overrides);
  }
  const std::vector<DecisionRow> rows =
      build_decisions(ir, config, db_idx, materialize_aux_attribution);
  const std::vector<ProtectedIntervalSqlRow> protected_rows =
      build_protected_intervals(ir, db_idx);
  Db db(sqlite_path);
  db.exec("BEGIN IMMEDIATE");
  try {
    create_schema(db);
    db.exec("DELETE FROM traceloom_evidence_role_issue");
    db.exec("DELETE FROM traceloom_evidence_role_placement");
    db.exec("DELETE FROM traceloom_evidence_role_decision");
    db.exec("DELETE FROM traceloom_protected_interval");
    db.exec("DELETE FROM traceloom_evidence_role_rule");
    db.exec("DELETE FROM traceloom_evidence_role_policy");
    insert_policy_and_rules(db, config.classification_rules);
    insert_decisions(db, rows);
    insert_protected_intervals(db, protected_rows);
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
  (void)config;
  (void)db_idx;
  (void)materialize_aux_attribution;
  throw std::runtime_error("evidence-role SQL requires SQLite support");
#endif
}

}  // namespace traceloom::compat
