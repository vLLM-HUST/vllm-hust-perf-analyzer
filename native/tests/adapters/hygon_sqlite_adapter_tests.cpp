#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "traceloom/adapters/hygon_sqlite_adapter.h"
#include "traceloom/analysis/flat_anchor_builder.h"

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

std::string temp_db_path(const char* suffix) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("traceloom_hygon_sqlite_adapter_" +
                                      std::to_string(now) + suffix + ".db");
  return path.string();
}

void exec_sql(sqlite3* db, const char* sql) {
  char* error = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
  if (rc != SQLITE_OK) {
    std::cerr << "failed to execute SQL: "
              << (error == nullptr ? "unknown" : error) << '\n';
    sqlite3_free(error);
    sqlite3_close(db);
    std::exit(1);
  }
}

void create_minimal_hygon_db(const std::string& path) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db,
                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  require(rc == SQLITE_OK, "failed to create temporary Hygon sqlite DB");

  exec_sql(db,
           "CREATE TABLE STR_TABLE(STR_ID INTEGER PRIMARY KEY, STR_NAME TEXT);"
           "INSERT INTO STR_TABLE(STR_ID, STR_NAME) VALUES "
           "(1, 'flash_fwd_kernel_bf16'), "
           "(2, 'Cijk_Alik_Bljk_BBH_MT256x256x16_MI250'), "
           "(3, 'Cijk_B_PostGSU'), "
           "(4, 'chunk_local_cumsum_scalar_kernel'), "
           "(5, 'direct_copy_kernel');"
           "CREATE TABLE HIPOPS_0("
           "BeginNs INTEGER, EndNs INTEGER, dev_id INTEGER, "
           "queue_id INTEGER, Name INTEGER, _Index INTEGER);"
           "INSERT INTO HIPOPS_0(BeginNs, EndNs, dev_id, queue_id, Name, "
           "_Index) VALUES "
           "(100, 200, 0, 7, 1, 10), "
           "(210, 250, 0, 7, 2, 10), "
           "(260, 270, 0, 7, 3, 12), "
           "(280, 300, 0, 7, 4, 13);"
           "CREATE TABLE HIPCOPY_0("
           "BeginNs INTEGER, EndNs INTEGER, dev_id INTEGER, "
           "queue_id INTEGER, Kind INTEGER, _Index INTEGER, Bytes INTEGER, "
           "MemoryType INTEGER);"
           "INSERT INTO HIPCOPY_0(BeginNs, EndNs, dev_id, queue_id, Kind, "
           "_Index, Bytes, MemoryType) VALUES "
           "(305, 315, 0, 9, 1, 20, 4096, 2);");

  sqlite3_close(db);
}

bool has_task_op_type(const traceloom::NativeIr& ir, const std::string& label) {
  for (const traceloom::TaskRow& task : ir.tasks.rows()) {
    if (task.op_type_symbol_id.valid() &&
        ir.symbols.value(task.op_type_symbol_id) == label) {
      return true;
    }
  }
  return false;
}

bool has_task_type(const traceloom::NativeIr& ir, const std::string& label) {
  for (const traceloom::TaskRow& task : ir.tasks.rows()) {
    if (task.task_type_symbol_id.valid() &&
        ir.symbols.value(task.task_type_symbol_id) == label) {
      return true;
    }
  }
  return false;
}

std::size_t count_anchor_label(const traceloom::NativeIr& ir,
                               const std::string& label) {
  std::size_t count = 0;
  for (const traceloom::AnchorRow& anchor : ir.anchors.rows()) {
    if (anchor.symbol_id.valid() &&
        ir.symbols.value(anchor.symbol_id) == label) {
      ++count;
    }
  }
  return count;
}

}  // namespace

int main() {
  using namespace traceloom;

  const std::string db_path = temp_db_path("_ok");
  create_minimal_hygon_db(db_path);
  require(looks_like_hygon_sqlite_profile(db_path),
          "Hygon profile sniffing did not recognize HIPOPS/HIPCOPY DB");

  HygonSQLiteAdapterOptions options;
  options.db_path = db_path;
  options.source_kind = "hygon_sqlite_test";
  const HygonSQLiteAdapter adapter(std::move(options));
  NativeIr ir = adapter.load();

  require(!ir.source_refs.empty(), "adapter did not emit SourceRef rows");
  require(ir.source_refs.row(SourceRefId(0)).source_kind == "hygon_sqlite_test",
          "SourceRef source_kind mismatch");
  require(ir.trace_events.size() == 5,
          "adapter did not load HIPOPS/HIPCOPY trace events");
  require(ir.tasks.size() == 5, "adapter did not load HIPOPS/HIPCOPY tasks");
  require(ir.streams.size() == 2, "adapter did not normalize Hygon streams");
  require(ir.trace_events.rows()[0].source_row_id == 1 &&
              ir.trace_events.rows()[1].source_row_id == 2 &&
              ir.trace_events.rows()[4].source_row_id == 1,
          "raw source locators must be table rowids, not repeated launch indices");

  require(has_task_op_type(ir, "FlashAttention"),
          "flash_fwd kernel was not lifted to FlashAttention");
  require(has_task_op_type(ir, "MatMul"),
          "Tensile GEMM kernel was not lifted to MatMul");
  require(has_task_op_type(ir, "MambaScan"),
          "Mamba scan kernel was not lifted to MambaScan");
  require(has_task_op_type(ir, "HygonAux:GemmEpilogue"),
          "Tensile PostGSU kernel was not classified as Hygon auxiliary");
  require(has_task_op_type(ir, "DataMove"),
          "HIPCOPY row was not classified as DataMove");
  require(has_task_type(ir, "HIP_KERNEL_AUX"),
          "auxiliary Hygon kernels did not receive HIP_KERNEL_AUX task type");

  FlatAnchorBuildConfig anchor_config;
  anchor_config.filter_auxiliary_task_anchors = true;
  const FlatAnchorBuildStats stats = build_flat_anchors(ir, anchor_config);
  require(stats.device_event_anchors == 3,
          "semantic anchor filtering should keep only compute Hygon anchors");
  require(ir.anchors.size() == 3, "unexpected Hygon anchor count");
  require(count_anchor_label(ir, "FlashAttention") == 1,
          "FlashAttention anchor missing");
  require(count_anchor_label(ir, "MatMul") == 1, "MatMul anchor missing");
  require(count_anchor_label(ir, "MambaScan") == 1, "MambaScan anchor missing");
  require(count_anchor_label(ir, "HygonAux:GemmEpilogue") == 0,
          "auxiliary Hygon kernel became an anchor");
  require(count_anchor_label(ir, "DataMove") == 0,
          "HIPCOPY DataMove became a compute anchor");

  // Dynamic INT8 Tensile uses another operand/type prefix on gfx936.
  sqlite3* updated_db = nullptr;
  require(sqlite3_open(db_path.c_str(), &updated_db) == SQLITE_OK,
          "failed to reopen fixture");
  exec_sql(updated_db,
           "UPDATE STR_TABLE SET STR_NAME = "
           "'Cijk_Ailk_Bljk_I8II_BH_MT128x64x64_SE_AMAS3_BW_ISA936' "
           "WHERE STR_ID = 2;");
  sqlite3_close(updated_db);
  NativeIr int8_ir = adapter.load();
  require(has_task_op_type(int8_ir, "MatMul"),
          "INT8 Tensile GEMM must remain a MatMul anchor");

  require(sqlite3_open(db_path.c_str(), &updated_db) == SQLITE_OK,
          "failed to reopen native GDN fixture");
  exec_sql(updated_db,
           "UPDATE STR_TABLE SET STR_NAME = "
           "'void qwen35_gdn_decode_kernel<hip_bfloat16>(...)' WHERE STR_ID=1;"
           "UPDATE STR_TABLE SET STR_NAME = "
           "'qwen35_gdn_qk_state_kernel(...)' WHERE STR_ID=3;");
  sqlite3_close(updated_db);
  NativeIr gdn_ir = adapter.load();
  require(has_task_op_type(gdn_ir, "MambaDeltaRule"),
          "native GDN core must be recognized");
  require(has_task_op_type(gdn_ir, "HygonAux:MambaStatePrep"),
          "native QK preparation must stay auxiliary");
  build_flat_anchors(gdn_ir, anchor_config);
  require(count_anchor_label(gdn_ir, "MambaDeltaRule") == 1 &&
              count_anchor_label(gdn_ir, "HygonAux:MambaStatePrep") == 0,
          "GDN classification must preserve anchor/auxiliary roles");

  std::remove(db_path.c_str());
  return 0;
}
