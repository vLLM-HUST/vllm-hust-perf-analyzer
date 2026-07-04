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
           "(5, 'direct_copy_kernel'), "
           "(6, 'fused_qwen35_gdn_decode_packed_kernel'), "
           "(7, 'fused_qwen35_gdn_post_norm_kernel'), "
           "(8, '_qwen35_flat_kv_decode_stage1_b1_direct'), "
           "(9, '_qwen35_dense_kv_decode_stage2_b1_direct'), "
           "(10, 'my_custom_decode_kernel');"
           "CREATE TABLE HIPOPS_0("
           "BeginNs INTEGER, EndNs INTEGER, dev_id INTEGER, "
           "queue_id INTEGER, Name INTEGER, _Index INTEGER);"
           "INSERT INTO HIPOPS_0(BeginNs, EndNs, dev_id, queue_id, Name, "
           "_Index) VALUES "
           "(100, 200, 0, 7, 1, 10), "
           "(210, 250, 0, 7, 2, 11), "
           "(260, 270, 0, 7, 3, 12), "
           "(280, 300, 0, 7, 4, 13), "
           "(310, 340, 0, 7, 6, 14), "
           "(350, 370, 0, 7, 7, 15), "
           "(380, 410, 0, 7, 8, 16), "
           "(420, 450, 0, 7, 9, 17), "
           "(460, 490, 0, 7, 10, 18);"
           "CREATE TABLE HIPCOPY_0("
           "BeginNs INTEGER, EndNs INTEGER, dev_id INTEGER, "
           "queue_id INTEGER, Kind INTEGER, _Index INTEGER, Bytes INTEGER, "
           "MemoryType INTEGER);"
           "INSERT INTO HIPCOPY_0(BeginNs, EndNs, dev_id, queue_id, Kind, "
           "_Index, Bytes, MemoryType) VALUES "
           "(500, 510, 0, 9, 1, 20, 4096, 2);");

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
  require(ir.trace_events.size() == 10,
          "adapter did not load HIPOPS/HIPCOPY trace events");
  require(ir.tasks.size() == 10, "adapter did not load HIPOPS/HIPCOPY tasks");
  require(ir.streams.size() == 2, "adapter did not normalize Hygon streams");

  require(has_task_op_type(ir, "FlashAttention"),
          "flash_fwd kernel was not lifted to FlashAttention");
  require(has_task_op_type(ir, "MatMul"),
          "Tensile GEMM kernel was not lifted to MatMul");
  require(has_task_op_type(ir, "MambaScan"),
          "Mamba scan kernel was not lifted to MambaScan");
  require(has_task_op_type(ir, "Qwen35GDNDecode"),
          "Qwen35 GDN decode kernel was not lifted to an anchor label");
  require(has_task_op_type(ir, "Qwen35GDNPostNorm"),
          "Qwen35 GDN post-norm kernel was not lifted to an anchor label");
  require(has_task_op_type(ir, "Qwen35KVDecodeStage1"),
          "Qwen35 flat KV decode stage1 was not lifted to an anchor label");
  require(has_task_op_type(ir, "Qwen35KVDecodeStage2"),
          "Qwen35 dense KV decode stage2 was not lifted to an anchor label");
  require(has_task_op_type(ir, "HygonKernel"),
          "unclassified Hygon kernel did not default to an anchor label");
  require(has_task_op_type(ir, "HygonAux:GemmEpilogue"),
          "Tensile PostGSU kernel was not classified as Hygon auxiliary");
  require(has_task_op_type(ir, "DataMove"),
          "HIPCOPY row was not classified as DataMove");
  require(has_task_type(ir, "HIP_KERNEL_AUX"),
          "auxiliary Hygon kernels did not receive HIP_KERNEL_AUX task type");

  FlatAnchorBuildConfig anchor_config;
  anchor_config.filter_auxiliary_task_anchors = true;
  const FlatAnchorBuildStats stats = build_flat_anchors(ir, anchor_config);
  require(stats.device_event_anchors == 8,
          "semantic anchor filtering should keep only compute Hygon anchors");
  require(ir.anchors.size() == 8, "unexpected Hygon anchor count");
  require(count_anchor_label(ir, "FlashAttention") == 1,
          "FlashAttention anchor missing");
  require(count_anchor_label(ir, "MatMul") == 1, "MatMul anchor missing");
  require(count_anchor_label(ir, "MambaScan") == 1, "MambaScan anchor missing");
  require(count_anchor_label(ir, "Qwen35GDNDecode") == 1,
          "Qwen35GDNDecode anchor missing");
  require(count_anchor_label(ir, "Qwen35GDNPostNorm") == 1,
          "Qwen35GDNPostNorm anchor missing");
  require(count_anchor_label(ir, "Qwen35KVDecodeStage1") == 1,
          "Qwen35KVDecodeStage1 anchor missing");
  require(count_anchor_label(ir, "Qwen35KVDecodeStage2") == 1,
          "Qwen35KVDecodeStage2 anchor missing");
  require(count_anchor_label(ir, "HygonKernel") == 1,
          "default HygonKernel anchor missing");
  require(count_anchor_label(ir, "HygonAux:GemmEpilogue") == 0,
          "auxiliary Hygon kernel became an anchor");
  require(count_anchor_label(ir, "DataMove") == 0,
          "HIPCOPY DataMove became a compute anchor");

  std::remove(db_path.c_str());
  return 0;
}
