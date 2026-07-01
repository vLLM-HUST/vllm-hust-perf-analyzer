#include "traceloom/compat/native_sidecar_materializer.h"
#include "traceloom/testing/test_util.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace traceloom;

std::string temp_db_path() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("traceloom_native_compat_materializer_" + std::to_string(now) + ".db");
  return path.string();
}

int run_scalar_int(const std::string& path, const std::string& sql) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);

  sqlite3_stmt* raw_stmt = nullptr;
  rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw_stmt, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);
  rc = sqlite3_step(raw_stmt);
  traceloom::testing::require(rc == SQLITE_ROW);
  const int value = sqlite3_column_int(raw_stmt, 0);
  rc = sqlite3_step(raw_stmt);
  traceloom::testing::require(rc == SQLITE_DONE);
  sqlite3_finalize(raw_stmt);
  sqlite3_close(db);
  return value;
}

std::string run_scalar_text(const std::string& path, const std::string& sql) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);

  sqlite3_stmt* raw_stmt = nullptr;
  rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw_stmt, nullptr);
  traceloom::testing::require(rc == SQLITE_OK);
  rc = sqlite3_step(raw_stmt);
  traceloom::testing::require(rc == SQLITE_ROW);
  const unsigned char* raw_text = sqlite3_column_text(raw_stmt, 0);
  const std::string value =
      raw_text == nullptr ? "" : reinterpret_cast<const char*>(raw_text);
  rc = sqlite3_step(raw_stmt);
  traceloom::testing::require(rc == SQLITE_DONE);
  sqlite3_finalize(raw_stmt);
  sqlite3_close(db);
  return value;
}

NativeIr build_collective_repeat_ir() {
  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "collective", "TASK", 0);
  const SymbolId ai_core = ir.symbols.intern("AI_CORE");
  const SymbolId matmul = ir.symbols.intern("MatMul");
  const SymbolId all_reduce = ir.symbols.intern("HcclAllReduce");

  std::vector<AnchorId> anchors;
  for (std::uint32_t idx = 0; idx < 4; ++idx) {
    const bool collective = idx >= 2;
    const SymbolId symbol = collective ? all_reduce : matmul;
    const std::int64_t start_ns = 1000 + static_cast<std::int64_t>(idx) * 1000;
    const TraceEventId event =
        ir.trace_events.append(source, idx, 0, 5, start_ns, start_ns + 500,
                               symbol);
    ir.tasks.append(source, event, idx + 1, 9000 + idx,
                    collective ? 700 : -1, ai_core, symbol, symbol, ai_core,
                    collective ? all_reduce : SymbolId::invalid());
    anchors.push_back(ir.anchors.append(
        source, event, ReplayUnitId::invalid(),
        collective ? AnchorKind::kCommunication : AnchorKind::kDeviceEvent,
        symbol, 0, 5, start_ns, start_ns + 500));
  }
  for (std::uint32_t idx = 0; idx < anchors.size(); ++idx) {
    const AnchorRow& anchor = ir.anchors.row(anchors[idx]);
    ir.tokens.append(anchors[idx], anchor.symbol_id, 0, idx, anchor.start_ns,
                     anchor.end_ns);
  }
  return ir;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  NativeIr ir;
  const SourceRefId source =
      ir.source_refs.append("fixture", "memory", "TASK", 0);
  const SymbolId task_type = ir.symbols.intern("AI_CORE");
  const SymbolId op_name = ir.symbols.intern("MatMul");
  const SymbolId op_type = ir.symbols.intern("Cube");

  const TraceEventId event =
      ir.trace_events.append(source, 12, 0, 5, 1000, 3000, task_type);
  ir.tasks.append(source, event, 77, 9001, -1, task_type, op_name, op_type,
                  task_type, SymbolId::invalid());
  const AnchorId anchor =
      ir.anchors.append(source, event, ReplayUnitId::invalid(),
                        AnchorKind::kDeviceEvent, op_type, 0, 5, 1000, 3000);
  ir.tokens.append(anchor, op_type, 0, 0, 1000, 3000);

  const std::string db_path = temp_db_path();
  compat::NativeCompatibilitySidecarOptions options;
  options.db_idx = 2;
  options.source_kind = "fixture";
  options.source_path = "memory";
  compat::write_basic_native_compatibility_sidecar(db_path, ir, options);

  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_metadata") == 6);
  require(run_scalar_text(db_path,
                          "SELECT value FROM traceloom_metadata "
                          "WHERE key = 'native_compatibility_materializer'") ==
          "basic_native_ir_v1");
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_event") == 1);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_event_source") == 1);
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_anchor") ==
          1);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM traceloom_anchor_cost_breakdown") == 1);
  require(run_scalar_int(db_path, "SELECT COUNT(*) FROM traceloom_viz_node") ==
          2);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_viz_node_anchor") ==
          2);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_anchor_primary_node") == 1);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_semantic_tree") == 1);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM traceloom_semantic_node") == 2);
  require(run_scalar_text(db_path,
                          "SELECT symbol FROM traceloom_event "
                          "WHERE event_id = 'event-0'") == "Cube");
  require(run_scalar_text(db_path,
                          "SELECT event_id FROM traceloom_anchor "
                          "WHERE anchor_id = 'anchor-0'") == "event-0");
  require(run_scalar_int(db_path,
                         "SELECT CAST(total_us * 1000 AS INTEGER) FROM "
                         "traceloom_anchor_cost_breakdown "
                         "WHERE anchor_idx = 1") == 2000);
  require(run_scalar_text(db_path,
                          "SELECT anchor_kind FROM "
                          "traceloom_anchor_cost_breakdown "
                          "WHERE anchor_idx = 1") == "exec");
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM traceloom_event_source s "
              "LEFT JOIN traceloom_event e ON e.event_id = s.event_id "
              "WHERE e.event_id IS NULL") == 0);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM traceloom_anchor a "
              "LEFT JOIN traceloom_event e ON e.event_id = a.event_id "
              "WHERE e.event_id IS NULL") == 0);
  require(run_scalar_int(
              db_path,
              "SELECT COUNT(*) FROM sqlite_master "
              "WHERE type = 'view' AND name = 'traceloom_v_tree_node'") == 1);
  require(run_scalar_int(db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_collective_global_link") == 0);

  std::remove(db_path.c_str());

  const std::string collective_db_path = temp_db_path();
  compat::NativeCompatibilitySidecarOptions collective_options;
  collective_options.db_idx = 3;
  collective_options.source_kind = "fixture";
  collective_options.source_path = "collective-smoke";
  collective_options.collective_run_name = "collective smoke";
  collective_options.collective_db_name = "db00.traceloom_augmented.db";
  collective_options.collective_expected_world_size = 1;
  compat::write_basic_native_compatibility_sidecar(
      collective_db_path, build_collective_repeat_ir(), collective_options);

  require(run_scalar_int(collective_db_path,
                         "SELECT COUNT(*) FROM "
                         "traceloom_collective_global_link") == 2);
  require(run_scalar_text(collective_db_path,
                          "SELECT op_type FROM "
                          "traceloom_collective_global_link "
                          "ORDER BY idx_in_occurrence LIMIT 1") ==
          "allReduce");
  require(run_scalar_text(collective_db_path,
                          "SELECT validation_status FROM "
                          "traceloom_collective_global_link "
                          "ORDER BY idx_in_occurrence LIMIT 1") ==
          "complete");
  require(run_scalar_text(collective_db_path,
                          "SELECT candidate_collective_key FROM "
                          "traceloom_collective_global_link "
                          "ORDER BY idx_in_occurrence LIMIT 1")
              .find("collective_smoke:LP_M001_01_") == 0);

  std::remove(collective_db_path.c_str());
  return 0;
}
