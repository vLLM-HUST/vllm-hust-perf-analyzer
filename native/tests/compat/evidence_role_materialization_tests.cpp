#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

class TempDatabase {
 public:
  TempDatabase() {
    const auto now =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = (std::filesystem::temp_directory_path() /
             ("traceloom_evidence_role_materialization_" +
              std::to_string(now) + ".db"))
                .string();
  }

  TempDatabase(const TempDatabase&) = delete;
  TempDatabase& operator=(const TempDatabase&) = delete;

  ~TempDatabase() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

int scalar_int(const std::string& path, const std::string& sql) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  if (rc != SQLITE_OK) {
    const std::string message =
        db == nullptr ? "cannot open test database" : sqlite3_errmsg(db);
    if (db != nullptr) {
      sqlite3_close(db);
    }
    throw std::runtime_error(message);
  }

  sqlite3_stmt* statement = nullptr;
  rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr);
  if (rc != SQLITE_OK || statement == nullptr) {
    const std::string message = sqlite3_errmsg(db);
    if (statement != nullptr) {
      sqlite3_finalize(statement);
    }
    sqlite3_close(db);
    throw std::runtime_error(message);
  }
  rc = sqlite3_step(statement);
  if (rc != SQLITE_ROW) {
    const std::string message = sqlite3_errmsg(db);
    sqlite3_finalize(statement);
    sqlite3_close(db);
    throw std::runtime_error(message);
  }
  const int value = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  sqlite3_close(db);
  return value;
}

std::string scalar_text(const std::string& path, const std::string& sql) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  if (rc != SQLITE_OK) {
    const std::string message =
        db == nullptr ? "cannot open test database" : sqlite3_errmsg(db);
    if (db != nullptr) {
      sqlite3_close(db);
    }
    throw std::runtime_error(message);
  }

  sqlite3_stmt* statement = nullptr;
  rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr);
  if (rc != SQLITE_OK || statement == nullptr) {
    const std::string message = sqlite3_errmsg(db);
    if (statement != nullptr) {
      sqlite3_finalize(statement);
    }
    sqlite3_close(db);
    throw std::runtime_error(message);
  }
  rc = sqlite3_step(statement);
  if (rc != SQLITE_ROW) {
    const std::string message = sqlite3_errmsg(db);
    sqlite3_finalize(statement);
    sqlite3_close(db);
    throw std::runtime_error(message);
  }
  const unsigned char* raw = sqlite3_column_text(statement, 0);
  const std::string value =
      raw == nullptr ? std::string() : reinterpret_cast<const char*>(raw);
  sqlite3_finalize(statement);
  sqlite3_close(db);
  return value;
}

void exec_sql(const std::string& path, const std::string& sql) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db,
                           SQLITE_OPEN_READWRITE, nullptr);
  if (rc != SQLITE_OK) {
    const std::string message =
        db == nullptr ? "cannot open test database" : sqlite3_errmsg(db);
    if (db != nullptr) {
      sqlite3_close(db);
    }
    throw std::runtime_error(message);
  }
  char* error = nullptr;
  rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
  if (rc != SQLITE_OK) {
    const std::string message =
        error == nullptr ? sqlite3_errmsg(db) : std::string(error);
    sqlite3_free(error);
    sqlite3_close(db);
    throw std::runtime_error(message);
  }
  sqlite3_close(db);
}

traceloom::FlatAnchorBuildConfig default_anchor_config() {
  traceloom::FlatAnchorBuildConfig config;
  config.filter_auxiliary_task_anchors = true;
  config.classification_rules =
      traceloom::load_default_signal_classification_ruleset();
  return config;
}

void require_unplaced_auxiliary_is_auditable() {
  using namespace traceloom;
  NativeIr ir;
  const SourceRefId source = ir.source_refs.append(
      "ascend_sqlite_hot_path", "memory", "TASK", 0);
  const SymbolId ai_core = ir.symbols.intern("AI_CORE");
  const SymbolId wait = ir.symbols.intern("EVENT_WAIT");
  const SymbolId matmul = ir.symbols.intern("MatMulV2");
  const SymbolId future = ir.symbols.intern("FutureFusedKernel");
  const auto append_task = [&](std::uint64_t row_id, std::int64_t start_ns,
                               SymbolId task_type, SymbolId op) {
    const TraceEventId event = ir.trace_events.append(
        source, row_id, 0, 3, start_ns, start_ns + 10,
        op.valid() ? op : task_type);
    ir.tasks.append(source, event, row_id, row_id, -1, task_type, op, op,
                    op.valid() ? task_type : SymbolId::invalid(),
                    SymbolId::invalid());
  };
  append_task(1, 0, wait, SymbolId::invalid());
  append_task(2, 20, ai_core, matmul);
  append_task(3, 40, ai_core, future);
  append_task(4, 60, wait, SymbolId::invalid());

  FlatAnchorBuildConfig config = default_anchor_config();
  build_flat_anchors(ir, config);

  TempDatabase database;
  compat::NativeCompatibilitySidecarOptions options;
  options.source_kind = "ascend_sqlite_hot_path";
  options.source_path = "memory";
  options.evidence_role_config = config;
  options.evidence_role_policy_id =
      config.classification_rules.metadata().policy_id;
  options.evidence_role_policy_version =
      config.classification_rules.metadata().policy_version;
  options.evidence_role_manifest_sha256 =
      config.classification_rules.metadata().manifest_sha256;
  compat::write_basic_native_compatibility_sidecar(database.path(), ir,
                                                    options);

  traceloom::testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM traceloom_evidence_role_decision "
                 "WHERE event_id = 'event-3' AND final_role = 'auxiliary' "
                 "AND support_state = 'retained_unplaced' AND reason_code = "
                 "'omitted_event_without_auxiliary_link'") == 1);
  traceloom::testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM traceloom_evidence_role_issue "
                 "WHERE decision_id = 'role-decision-3' AND code = "
                 "'omitted_event_without_auxiliary_link' AND support_state = "
                 "'retained_unplaced'") == 1);
  traceloom::testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM traceloom_aux_link "
                 "WHERE aux_event_id = 'event-3'") == 0);
}

void require_communication_replacement_lineage() {
  using namespace traceloom;
  NativeIr ir;
  const SourceRefId task_source = ir.source_refs.append(
      "ascend_sqlite_hot_path", "memory", "TASK", 0);
  const SourceRefId comm_source = ir.source_refs.append(
      "ascend_sqlite_hot_path", "memory", "COMMUNICATION_OP", 0);
  const SymbolId kernel = ir.symbols.intern("KERNEL_AIVEC");
  const SymbolId task_name = ir.symbols.intern("hcom_allReduce_");
  const SymbolId all_reduce = ir.symbols.intern("AllReduce");

  const TraceEventId task_event = ir.trace_events.append(
      task_source, 1, 0, 415, 1000, 2000, task_name);
  ir.tasks.append(task_source, task_event, 1, 1, 77, kernel, task_name,
                  task_name, kernel, task_name);
  const TraceEventId comm_event = ir.trace_events.append(
      comm_source, 2, 0, 415, 1000, 2000, all_reduce);
  ir.communication_ops.append(comm_source, comm_event, 77, 9, 1, 1,
                              all_reduce);

  FlatAnchorBuildConfig config = default_anchor_config();
  config.skip_tasks_covered_by_communication_ops = true;
  build_flat_anchors(ir, config);
  traceloom::testing::require(ir.anchors.size() == 1);
  traceloom::testing::require(
      ir.anchors.rows().front().trace_event_id == comm_event);

  TempDatabase database;
  compat::NativeCompatibilitySidecarOptions options;
  options.source_kind = "ascend_sqlite_hot_path";
  options.source_path = "memory";
  options.evidence_role_config = config;
  compat::write_basic_native_compatibility_sidecar(database.path(), ir,
                                                    options);

  traceloom::testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM traceloom_evidence_role_decision "
                 "WHERE event_id = 'event-0' AND final_role = 'anchor' AND "
                 "support_state = 'supported' AND reason_code = "
                 "'represented_by_communication_anchor'") == 1);
  traceloom::testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM traceloom_evidence_role_placement "
                 "WHERE decision_id = 'role-decision-0' AND placement_kind = "
                 "'anchor' AND placement_id = 'anchor-0' AND reason_code = "
                 "'represented_by_communication_anchor'") == 1);
  traceloom::testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM traceloom_evidence_role_issue "
                 "WHERE decision_id = 'role-decision-0'") == 0);
  traceloom::testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM traceloom_aux_link "
                 "WHERE aux_event_id = 'event-0'") == 0);
  traceloom::testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM "
                 "traceloom_v_replay_region_annotation_status WHERE "
                 "support_state = 'unavailable' AND reason_code = "
                 "'no_exact_replay_regions'") == 1);
  traceloom::testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM "
                 "traceloom_v_flattened_execution_timeline WHERE item_kind "
                 "= 'mainline_anchor' AND replay_region_state = "
                 "'unavailable'") == 1);
}

traceloom::NativeIr build_replay_annotated_timeline_ir() {
  using namespace traceloom;
  NativeIr ir;
  const SourceRefId task_source = ir.source_refs.append(
      "ascend_sqlite_hot_path", "timeline", "TASK", 0);
  const SourceRefId comm_source = ir.source_refs.append(
      "ascend_sqlite_hot_path", "timeline", "COMMUNICATION_OP", 0);
  const SourceRefId graph_source = ir.source_refs.append(
      "ascend_sqlite_hot_path", "timeline", "ACLGRAPH_REPLAY_UNIT", 0);
  const SymbolId ai_core = ir.symbols.intern("AI_CORE");
  const SymbolId all_reduce = ir.symbols.intern("AllReduce");
  const SymbolId all_gather = ir.symbols.intern("AllGather");
  const SymbolId aiv_sync = ir.symbols.intern("hccl_aiv_sync");
  const SymbolId graph_symbol = ir.symbols.intern("ACLH");

  const TraceEventId comm_before_event = ir.trace_events.append(
      comm_source, 1, 0, 5, 800, 900, all_reduce);
  ir.communication_ops.append(comm_source, comm_before_event, 10, 1, 0, 1,
                              all_reduce);
  const TraceEventId graph_event = ir.trace_events.append(
      graph_source, 2, 0, 5, 1000, 2000, graph_symbol);
  const TraceEventId member_event = ir.trace_events.append(
      task_source, 3, 0, 5, 1100, 1200, aiv_sync);
  const TaskId member_task = ir.tasks.append(
      task_source, member_event, 3, 3, -1, ai_core, aiv_sync, aiv_sync,
      ai_core, SymbolId::invalid());
  const TraceEventId comm_after_event = ir.trace_events.append(
      comm_source, 4, 0, 5, 2100, 2200, all_gather);
  ir.communication_ops.append(comm_source, comm_after_event, 11, 2, 0, 1,
                              all_gather);

  const GraphLaunchOccurrenceId occurrence =
      ir.graph_launch_occurrences.append(
          graph_source, graph_source, 0, 2, 100, 9001, -1,
          StreamId::invalid(), StreamId::invalid(),
          CapturedGraphInstanceId::invalid(), TaskId::invalid(),
          TaskId::invalid(), TaskId::invalid(), 1000, 2000, 0,
          GraphLaunchMatchPolicy::kNotifyCompletionAdjacent);
  const ReplayBodyTemplateId body = ir.replay_body_templates.append(
      graph_source, 17, aiv_sync, 1, 0, 1,
      ReplayBodyTopologyPolicy::kSingleModelStream);
  const ReplayCompositionCandidateId composition =
      ir.replay_composition_candidates.append(
          graph_source, 0, occurrence, occurrence, 1, 0, 1, 1, 0, 22,
          ReplayCompositionIdentityPolicy::kGraphConnection,
          ReplayCompositionOrderPolicy::kHostSubmissionOrder,
          ReplayCompositionShapePolicy::kSingleGraph,
          ReplayCompositionBoundaryPolicy::kDirectObservedGraphLaunch);
  const ReplayCompositionSlotId slot = ir.replay_composition_slots.append(
      composition, 0, CapturedGraphInstanceId::invalid(),
      GraphSlotTemplateId::invalid(), body, ReplayCompositionSlotRole::kHead,
      9001);
  const ReplayCompositionRegionId region =
      ir.replay_composition_regions.append(
          composition, 0, occurrence, occurrence, 1000, 2000, 1, 1,
          ReplayCompositionRegionStatus::kRecognizedCompletePattern);
  ir.replay_composition_region_members.append(region, 0, occurrence, 0);
  const GraphLaunchBodyId body_id = ir.graph_launch_bodies.append(
      occurrence, body, member_task, member_task, 1, 0, 1);
  ir.graph_launch_body_members.append(
      body_id, member_task, 0, 0,
      GraphLaunchBodyMemberRow::Kind::kCompute);
  const GraphTemplateId graph_template =
      ir.graph_templates.append(graph_source, 33, 1);
  const ReplayUnitId replay_unit = ir.replay_units.append(
      graph_template, graph_source, AnchorId::invalid(), AnchorId::invalid(),
      graph_event, region);
  const ReplayUnitLaunchMemberId launch_member =
      ir.replay_unit_launch_members.append(replay_unit, 0, occurrence, slot);

  const AnchorId comm_before = ir.anchors.append(
      comm_source, comm_before_event, ReplayUnitId::invalid(),
      AnchorKind::kCommunication, all_reduce, 0, 5, 800, 900);
  const AnchorId position = ir.anchors.append(
      graph_source, TraceEventId::invalid(), replay_unit,
      AnchorKind::kGraphReplayUnit, graph_symbol, 0, 5, 1000, 2000,
      launch_member);
  const AnchorId comm_after = ir.anchors.append(
      comm_source, comm_after_event, ReplayUnitId::invalid(),
      AnchorKind::kCommunication, all_gather, 0, 5, 2100, 2200);
  ir.tokens.append(comm_before, all_reduce, 0, 0, 800, 900);
  const TokenId position_token =
      ir.tokens.append(position, graph_symbol, 0, 1, 1000, 2000);
  ir.tokens.append(comm_after, all_gather, 0, 2, 2100, 2200);
  ir.replay_units.set_anchor_bounds(replay_unit, position, position);
  ir.protected_intervals.append(
      ProtectedIntervalKind::kGraphReplayUnit, BoundaryPolicy::kNoCross,
      position_token, position_token, position, position, graph_source);
  return ir;
}

void require_replay_is_a_flat_timeline_annotation() {
  using namespace traceloom;
  TempDatabase database;
  compat::NativeCompatibilitySidecarOptions options;
  options.source_kind = "ascend_sqlite_hot_path";
  options.source_path = "timeline";
  options.evidence_role_config = default_anchor_config();
  compat::write_basic_native_compatibility_sidecar(
      database.path(), build_replay_annotated_timeline_ir(), options);

  testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM "
                 "traceloom_v_replay_region_annotation_status WHERE "
                 "replay_region_count = 1 AND support_state = 'supported' "
                 "AND reason_code = "
                 "'ordered_disjoint_exact_replay_regions'") == 1);
  testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM "
                 "traceloom_v_annotated_anchor_timeline") == 3);
  testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM "
                 "traceloom_v_annotated_anchor_timeline WHERE "
                 "replay_region_state = 'inside_exact_replay' AND "
                 "replay_pattern_id = 0 AND replay_occurrence_id = "
                 "'replay-unit-0' AND replay_position_id = "
                 "'graph-launch-0' AND replay_position_order = 0") == 1);
  testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM "
                 "traceloom_v_annotated_anchor_timeline WHERE source_table "
                 "= 'COMMUNICATION_OP' AND replay_region_state = "
                 "'outside_exact_replay'") == 2);
  testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM "
                 "traceloom_v_flattened_execution_timeline") == 3);
  testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM "
                 "traceloom_v_flattened_execution_timeline WHERE item_kind "
                 "= 'position_member' AND symbol = 'hccl_aiv_sync' AND "
                 "source_table = 'TASK' AND position_anchor_id = "
                 "'anchor-1' AND replay_occurrence_id = 'replay-unit-0'") ==
      1);
  testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM "
                 "traceloom_v_flattened_execution_timeline WHERE "
                 "timeline_item_id = 'anchor:anchor-1'") == 0);
  testing::require(
      scalar_text(database.path(),
                  "SELECT group_concat(symbol, ',') FROM (SELECT symbol "
                  "FROM traceloom_v_flattened_execution_timeline ORDER BY "
                  "timeline_order)") ==
      "AllReduce,hccl_aiv_sync,AllGather");
  testing::require(
      scalar_text(database.path(),
                  "SELECT DISTINCT duration_aggregation_semantics FROM "
                  "traceloom_v_flattened_execution_timeline") ==
      "flat_items_may_overlap_do_not_sum_without_cost_lens");
}

void require_replay_annotation_fails_closed_on_overlapping_regions() {
  using namespace traceloom;
  TempDatabase database;
  compat::NativeCompatibilitySidecarOptions options;
  options.source_kind = "ascend_sqlite_hot_path";
  options.source_path = "timeline";
  options.evidence_role_config = default_anchor_config();
  compat::write_basic_native_compatibility_sidecar(
      database.path(), build_replay_annotated_timeline_ir(), options);
  exec_sql(
      database.path(),
      "INSERT INTO traceloom_protected_interval SELECT "
      "'test-overlapping-region',db_idx,device_id,kind,boundary_policy,"
      "first_anchor_id,last_anchor_id,replay_unit_id,evidence_source_ref_id,"
      "start_ns,end_ns,support_state,reason_code FROM "
      "traceloom_protected_interval WHERE kind = 'graph_replay_unit' LIMIT 1");

  testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM "
                 "traceloom_v_replay_region_annotation_status WHERE "
                 "overlap_count = 1 AND support_state = 'unsupported' AND "
                 "reason_code = 'overlapping_replay_regions'") == 1);
  testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM "
                 "traceloom_v_annotated_anchor_timeline") == 3);
  testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM "
                 "traceloom_v_annotated_anchor_timeline WHERE "
                 "replay_region_state = 'unsupported' AND "
                 "protected_interval_id IS NULL") == 3);
  testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM "
                 "traceloom_v_flattened_execution_timeline WHERE item_kind "
                 "= 'position_member'") == 0);
  testing::require(
      scalar_int(database.path(),
                 "SELECT COUNT(*) FROM "
                 "traceloom_v_flattened_execution_timeline WHERE "
                 "timeline_item_id = 'anchor:anchor-1' AND "
                 "flattening_action = 'unexpanded_position_anchor'") == 1);
}

}  // namespace

int main() {
  require_unplaced_auxiliary_is_auditable();
  require_communication_replacement_lineage();
  require_replay_is_a_flat_timeline_annotation();
  require_replay_annotation_fails_closed_on_overlapping_regions();
  return 0;
}
