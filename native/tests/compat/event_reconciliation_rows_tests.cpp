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
                 "traceloom_event_reconciliation_rule") == 1);
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
  return 0;
}
