#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

namespace {

int scalar(const std::string& path, const std::string& sql) {
  sqlite3* db = nullptr;
  traceloom::testing::require(
      sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) ==
      SQLITE_OK);
  sqlite3_stmt* stmt = nullptr;
  traceloom::testing::require(
      sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK);
  traceloom::testing::require(sqlite3_step(stmt) == SQLITE_ROW);
  const int value = sqlite3_column_int(stmt, 0);
  traceloom::testing::require(sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return value;
}

std::string temporary_db() {
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return (std::filesystem::temp_directory_path() /
          ("traceloom_event_reconciliation_" + std::to_string(stamp) +
           ".db"))
      .string();
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("ascend", "memory", "TASK", 0);
  const SymbolId mix_aiv = ir.symbols.intern("KERNEL_MIX_AIV");
  const SymbolId reduce_all = ir.symbols.intern("ReduceAll");
  const TraceEventId envelope =
      ir.trace_events.append(source, 1, 0, 46, 100, 140, mix_aiv);
  const TraceEventId detail =
      ir.trace_events.append(source, 2, 0, 46, 105, 135, mix_aiv);
  ir.tasks.append(source, envelope, 7, 31, 500, mix_aiv,
                  SymbolId::invalid(), SymbolId::invalid(),
                  SymbolId::invalid(), SymbolId::invalid(), -1,
                  SymbolId::invalid(), 4294967295LL);
  ir.tasks.append(source, detail, 7, 9, 500, mix_aiv,
                  SymbolId::invalid(), reduce_all, SymbolId::invalid(),
                  SymbolId::invalid(), -1, SymbolId::invalid(), 0);
  FlatAnchorBuildConfig config;
  config.filter_auxiliary_task_anchors = true;
  const FlatAnchorBuildStats stats = build_flat_anchors(ir, config);
  require(stats.reconciled_event_groups == 1);
  require(ir.anchors.size() == 1);

  const std::string path = temporary_db();
  compat::NativeCompatibilitySidecarOptions options;
  options.source_kind = "ascend";
  options.source_path = "memory";
  options.evidence_role_config = config;
  compat::write_basic_native_compatibility_sidecar(path, ir, options);
  require(scalar(path,
                 "SELECT COUNT(*) FROM "
                 "traceloom_event_reconciliation_policy") == 1);
  require(scalar(path,
                 "SELECT COUNT(*) FROM "
                 "traceloom_event_reconciliation_rule") == 2);
  require(scalar(path,
                 "SELECT COUNT(*) FROM "
                 "traceloom_event_reconciliation_decision WHERE status = "
                 "'reconciled' AND canonical_anchor_id = 'anchor-0'") == 1);
  require(scalar(path,
                 "SELECT COUNT(*) FROM "
                 "traceloom_v_event_reconciliation") == 2);
  require(scalar(path,
                 "SELECT COUNT(*) FROM traceloom_aux_link l JOIN "
                 "traceloom_event_reconciliation_member m ON "
                 "m.event_id = l.aux_event_id WHERE m.member_role = "
                 "'timing_envelope'") == 0);
  require(scalar(path,
                 "SELECT COUNT(*) FROM "
                 "traceloom_evidence_role_issue WHERE code = "
                 "'identity_event_without_anchor'") == 0);
  std::remove(path.c_str());

  NativeIr fused;
  const SourceRefId fused_task_source =
      fused.source_refs.append(
          "ascend", "profile/device_0/sqlite/ascend_task.db", "TASK", 0);
  const SourceRefId fused_comm_source = fused.source_refs.append(
      "ascend", "profile/device_0/sqlite/hccl_single_device.db",
      "COMMUNICATION_OP", 0);
  const SymbolId fused_task_type = fused.symbols.intern("KERNEL_MIX_AIC");
  const SymbolId fused_op_type = fused.symbols.intern("MatmulAllReduce");
  const SymbolId fused_comm_name =
      fused.symbols.intern("MatmulAllReduceMc2AicpuKernel_fixture");
  const TraceEventId fused_task_event = fused.trace_events.append(
      fused_task_source, 1, 0, 47, 100, 200, fused_task_type);
  const TraceEventId fused_comm_event = fused.trace_events.append(
      fused_comm_source, 2, 0, 116, 120, 180, fused_comm_name);
  fused.tasks.append(fused_task_source, fused_task_event, 1, 1, 101,
                     fused_task_type, fused_op_type, fused_op_type,
                     SymbolId::invalid(), SymbolId::invalid());
  fused.communication_ops.append(fused_comm_source, fused_comm_event, 100, 1,
                                 1, 1, fused_comm_name);
  const FlatAnchorBuildStats fused_stats = build_flat_anchors(fused);
  require(fused_stats.reconciled_event_groups == 1);
  require(fused_stats.suppressed_duplicate_observations == 1);
  require(fused.anchors.size() == 1);
  require(fused.anchors.row(AnchorId(0)).trace_event_id == fused_task_event);

  const std::string fused_path = temporary_db();
  compat::NativeCompatibilitySidecarOptions fused_options;
  fused_options.source_kind = "ascend";
  fused_options.source_path = "profile.db";
  compat::write_basic_native_compatibility_sidecar(fused_path, fused,
                                                    fused_options);
  require(scalar(
              fused_path,
              "SELECT COUNT(*) FROM traceloom_event_reconciliation_rule "
              "WHERE source_domain = 'task+communication_op' AND "
              "task_op_type = 'MatmulAllReduce' AND "
              "communication_op_name_prefix = "
              "'MatmulAllReduceMc2AicpuKernel_'") == 1);
  require(scalar(
              fused_path,
              "SELECT COUNT(*) FROM traceloom_v_event_reconciliation "
              "WHERE status = 'reconciled' AND member_role = "
              "'provider_detail' AND source_table = 'COMMUNICATION_OP' AND "
              "source_domain = 'communication_op' AND task_id IS NULL AND "
              "communication_op_id = 0") == 1);
  require(scalar(fused_path,
                 "SELECT COUNT(*) FROM traceloom_aux_link WHERE "
                 "aux_event_id = 'event-1'") == 0);
  std::remove(fused_path.c_str());
  return 0;
}
