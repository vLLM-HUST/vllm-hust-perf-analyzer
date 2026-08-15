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
}

}  // namespace

int main() {
  require_unplaced_auxiliary_is_auditable();
  require_communication_replacement_lineage();
  return 0;
}
