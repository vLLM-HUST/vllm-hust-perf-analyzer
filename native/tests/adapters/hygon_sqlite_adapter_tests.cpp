#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

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
  require(has_task_op_type(ir, "Cijk_B_PostGSU"),
          "Tensile epilogue must retain its concrete identity");
  require(has_task_op_type(ir, "DataMove"),
          "HIPCOPY row was not classified as DataMove");
  require(has_task_type(ir, "HIP_KERNEL"),
          "executable Hygon kernels must use HIP_KERNEL");

  FlatAnchorBuildConfig anchor_config;
  anchor_config.filter_auxiliary_task_anchors = true;
  const FlatAnchorBuildStats stats = build_flat_anchors(ir, anchor_config);
  require(stats.device_event_anchors == 4,
          "all HIPOPS kernels must participate in structure");
  require(ir.anchors.size() == 4, "unexpected Hygon anchor count");
  require(count_anchor_label(ir, "FlashAttention") == 1,
          "FlashAttention anchor missing");
  require(count_anchor_label(ir, "MatMul") == 1, "MatMul anchor missing");
  require(count_anchor_label(ir, "MambaScan") == 1, "MambaScan anchor missing");
  require(count_anchor_label(ir, "Cijk_B_PostGSU") == 1,
          "GEMM epilogue must be a structural anchor");
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
  require(has_task_op_type(gdn_ir, "MambaStatePrep"),
          "native QK preparation must retain concrete identity");
  build_flat_anchors(gdn_ir, anchor_config);
  require(count_anchor_label(gdn_ir, "MambaDeltaRule") == 1 &&
              count_anchor_label(gdn_ir, "MambaStatePrep") == 1,
          "GDN preparation must participate alongside the core");

  require(sqlite3_open(db_path.c_str(), &updated_db) == SQLITE_OK,
          "failed to reopen pointwise fixture");
  exec_sql(updated_db,
           "UPDATE STR_TABLE SET STR_NAME='kernel<MulFunctor<float>>' WHERE STR_ID=3;"
           "UPDATE STR_TABLE SET STR_NAME='kernel<DivFunctor<float>>' WHERE STR_ID=4;");
  sqlite3_close(updated_db);
  NativeIr pointwise_ir = adapter.load();
  const auto pointwise_stats = build_flat_anchors(pointwise_ir, anchor_config);
  require(pointwise_stats.device_event_anchors == 4 &&
              pointwise_stats.unknown_anchor_task_events == 0,
          "small HIP kernels must be explicit anchors, not unknown fallback");
  require(count_anchor_label(pointwise_ir, "kernel<MulFunctor<float>>") == 1 &&
              count_anchor_label(pointwise_ir, "kernel<DivFunctor<float>>") == 1,
          "distinct small kernels must not collapse to one Pointwise token");

  const std::vector<std::pair<std::string, std::string>> native_cases = {
      {"Cijk_Alik_Bljk_SB_MT16x16x32_SE_AMAS3_BW_ISA936", "MatMul"},
      {"Cijk_Ailk_Bljk_SB_MT32x16x16_SE_AMAS3_BW_ISA936", "MatMul"},
      {"void ck_tile::kentry<256, 1, ck_tile::Smoothquant<ck_tile::SmoothquantPipelineTorch<problem>>>(args)", "SmoothQuant"},
      {"scale_tile(int const*, float const*, float const*, unsigned short*, int)", "Int8GemmEpilogue"},
      {"silu_mul_tile(unsigned short const*, unsigned short const*, unsigned short*, int)", "SiluMul"},
      {"void norm_kernel<256, true>(unsigned short const*, unsigned short const*, unsigned short const*, unsigned short const*, unsigned short*, unsigned short*, int, float, float)", "QwenRmsNorm"},
      {"void norm_kernel<256, true>(unsigned short const*, unsigned short const*, unsigned short const*, unsigned short const*, unsigned short*, unsigned short*, int, float, float, int)", "QwenRmsNorm"},
      {"void norm_kernel<64, false>(unsigned short const*, unsigned short const*, unsigned short const*, unsigned short const*, unsigned short*, unsigned short*, int, float, float)", "RmsNormOptionalGate"},
      {"scale_tile(float*, int)", "scale_tile(float*, int)"},
      {"qwen_rope_bf16_pair(unsigned short const*, unsigned short const*, unsigned short const*, unsigned short const*, unsigned short*, unsigned short*, int, int, int, int)", "QwenRoPE"},
      {"qwen_rope_bf16_pair(float*)", "qwen_rope_bf16_pair(float*)"},
      {"silu_mul_tile(float*, float*, float*, int)", "silu_mul_tile(float*, float*, float*, int)"},
      {"void norm_kernel<256, true>(float*)", "void norm_kernel<256, true>(float*)"},
      {"other::SmoothquantPipelineTorch<float>()", "other::SmoothquantPipelineTorch<float>()"},
  };
  for (const auto& entry : native_cases) {
    require(sqlite3_open(db_path.c_str(), &updated_db) == SQLITE_OK,
            "failed to reopen native leaf fixture");
    const std::string sql = "UPDATE STR_TABLE SET STR_NAME='" + entry.first + "' WHERE STR_ID=1;";
    exec_sql(updated_db, sql.c_str());
    sqlite3_close(updated_db);
    NativeIr leaf_ir = adapter.load();
    require(has_task_op_type(leaf_ir, entry.second), "native leaf label mismatch");
    bool retained_raw_name = false;
    for (const auto& task : leaf_ir.tasks.rows()) {
      if (task.op_name_symbol_id.valid() &&
          leaf_ir.symbols.value(task.op_name_symbol_id) == entry.first) {
        retained_raw_name = true;
      }
    }
    require(retained_raw_name, "native leaf labeling erased the literal symbol");
    const auto leaf_stats = build_flat_anchors(leaf_ir, anchor_config);
    require(leaf_stats.device_event_anchors == 4 &&
                count_anchor_label(leaf_ir, entry.second) == (entry.second == "MatMul" ? 2 : 1),
            "native labeling changed event coverage");
  }

  std::remove(db_path.c_str());
  return 0;
}
