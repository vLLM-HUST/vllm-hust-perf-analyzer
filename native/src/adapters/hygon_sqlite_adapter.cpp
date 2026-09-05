#include "traceloom/adapters/hygon_sqlite_adapter.h"

#include "sqlite_profile_reader.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace traceloom {
namespace {

using SqliteDb = sqlite_profile_detail::ReadOnlyDatabase;
using SqliteStmt = sqlite_profile_detail::Statement;
using sqlite_profile_detail::quote_identifier;

class Stopwatch {
 public:
  Stopwatch() : start_(Clock::now()) {}

  double elapsed_ms() const {
    const auto elapsed = Clock::now() - start_;
    return std::chrono::duration<double, std::milli>(elapsed).count();
  }

 private:
  using Clock = std::chrono::steady_clock;
  Clock::time_point start_;
};

template <typename Fn>
double time_stage(Fn&& fn) {
  const Stopwatch stopwatch;
  fn();
  return stopwatch.elapsed_ms();
}

struct HygonLoadTiming {
  double sqlite_open_ms = 0.0;
  double inventory_ms = 0.0;
  double strings_ms = 0.0;
  double hipops_ms = 0.0;
  double hipcopy_ms = 0.0;
};

void print_load_timing(const HygonLoadTiming& timing) {
  std::cerr << "timing load_sqlite_open_ms=" << timing.sqlite_open_ms << "\n";
  std::cerr << "timing load_inventory_ms=" << timing.inventory_ms << "\n";
  std::cerr << "timing load_hygon_strings_ms=" << timing.strings_ms << "\n";
  std::cerr << "timing load_hygon_hipops_ms=" << timing.hipops_ms << "\n";
  std::cerr << "timing load_hygon_hipcopy_ms=" << timing.hipcopy_ms << "\n";
}

bool file_exists(const std::string& path) {
  std::ifstream input(path);
  return input.good();
}

std::int64_t sqlite_i64(sqlite3_stmt* stmt, int column,
                        std::int64_t fallback = 0) {
  return sqlite_profile_detail::read_i64(stmt, column, fallback);
}

std::uint32_t sqlite_u32(sqlite3_stmt* stmt, int column) {
  return sqlite_profile_detail::read_u32(stmt, column);
}

std::uint64_t sqlite_u64(sqlite3_stmt* stmt, int column) {
  return sqlite_profile_detail::read_u64(stmt, column);
}

std::string sqlite_text(sqlite3_stmt* stmt, int column) {
  return sqlite_profile_detail::read_text(stmt, column);
}

bool starts_with(const std::string& value, const std::string& prefix) {
  return value.rfind(prefix, 0) == 0;
}

std::string lower_ascii(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

bool contains_any(const std::string& text,
                  const std::vector<std::string>& needles) {
  for (const std::string& needle : needles) {
    if (text.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> table_names(SqliteDb& db) {
  SqliteStmt stmt(db.get(),
                  "SELECT name FROM sqlite_master WHERE type='table' "
                  "ORDER BY name");
  std::vector<std::string> out;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      out.push_back(sqlite_text(stmt.get(), 0));
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    throw std::runtime_error("failed to read Hygon SQLite inventory: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  return out;
}

std::uint64_t table_row_count(SqliteDb& db, const std::string& table_name) {
  SqliteStmt stmt(db.get(),
                  "SELECT COUNT(*) FROM " + quote_identifier(table_name));
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_ROW) {
    if (rc == SQLITE_DONE) {
      return 0;
    }
    throw std::runtime_error("failed to count Hygon table rows: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  return sqlite_u64(stmt.get(), 0);
}

std::unordered_map<std::int64_t, std::string> load_hygon_strings(
    SqliteDb& db, const std::vector<std::string>& tables) {
  if (std::find(tables.begin(), tables.end(), "STR_TABLE") == tables.end()) {
    return {};
  }
  SqliteStmt stmt(db.get(), "SELECT STR_ID, STR_NAME FROM STR_TABLE");
  std::unordered_map<std::int64_t, std::string> out;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      out.emplace(sqlite_i64(stmt.get(), 0, -1), sqlite_text(stmt.get(), 1));
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    throw std::runtime_error("failed to load Hygon STR_TABLE: " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
  return out;
}

struct LiftedKernel {
  bool anchor = false;
  std::string label;
  std::string detail;
};

LiftedKernel lifted_anchor(std::string label, std::string detail) {
  return LiftedKernel{true, std::move(label), std::move(detail)};
}

LiftedKernel lifted_aux(std::string label, std::string detail) {
  return LiftedKernel{false, "HygonAux:" + std::move(label), std::move(detail)};
}

LiftedKernel lift_hygon_kernel_label(const std::string& raw_label) {
  const std::string low = lower_ascii(raw_label);
  if (low.find("cijk_b_postgsu") != std::string::npos) {
    return lifted_aux("GemmEpilogue", "TensilePostGSU");
  }
  if (low.find("cijk_alik_bljk_bbh") != std::string::npos ||
      low.find("cijk_alik_bjlk_sb") != std::string::npos ||
      low.find("cijk_ailk_bljk_i8ii_") != std::string::npos) {
    std::smatch match;
    const std::regex mt_pattern("_mt([0-9]+x[0-9]+x[0-9]+)");
    const std::string detail = std::regex_search(low, match, mt_pattern)
                                   ? "TensileGEMM[" + match.str(1) + "]"
                                   : "TensileGEMM";
    return lifted_anchor("MatMul", detail);
  }

  if (low.find("qwen35_gdn_decode_kernel") != std::string::npos) {
    return lifted_anchor("MambaDeltaRule", "native gated_delta_rule");
  }
  if (low.find("qwen35_gdn_qk_state_kernel") != std::string::npos) {
    return lifted_aux("MambaStatePrep", "native qk state preparation");
  }

  if (low.find("flash_fwd_kernel") != std::string::npos) {
    return lifted_anchor("FlashAttention", "flash_fwd");
  }
  if (low.find("kernel_unified_attention") != std::string::npos) {
    return lifted_anchor("Attention", "unified_attention");
  }
  if (low.find("reshape_and_cache_kernel_flash") != std::string::npos) {
    return lifted_anchor("KVCacheUpdate", "reshape_and_cache_flash");
  }
  if (low.find("rotary_kernel") != std::string::npos ||
      low.find("_triton_mrope_forward") != std::string::npos) {
    return lifted_anchor("Rope", "rotary/mrope");
  }
  if (contains_any(low, {"layer_norm_fwd_kernel", "l2norm_fwd_kernel2",
                         "vectorized_layer_norm_kernel"})) {
    return lifted_anchor("Norm", "layer/l2 norm");
  }
  if (low.find("triton_red_fused__to_copy_add_mean_mul_pow_rsqrt_0") !=
      std::string::npos) {
    return lifted_anchor("RmsNorm", "triton fused rmsnorm");
  }
  if (low.find("act_and_mul_kernel") != std::string::npos ||
      low.find("gelucudakernelimpl") != std::string::npos) {
    return lifted_anchor("Activation", "silu/gelu gate");
  }
  if (low.find("_causal_conv1d_fwd_kernel") != std::string::npos ||
      low.find("_causal_conv1d_update_kernel") != std::string::npos) {
    return lifted_anchor("MambaConv", "causal_conv1d");
  }
  if (low.find("chunk_scaled_dot_kkt_fwd_kernel") != std::string::npos) {
    return lifted_anchor("MambaChunk", "scaled_dot_kkt");
  }
  if (low.find("chunk_gated_delta_rule_fwd_kernel") != std::string::npos ||
      low.find("fused_recurrent_gated_delta_rule") != std::string::npos) {
    return lifted_anchor("MambaDeltaRule", "gated_delta_rule");
  }
  if (low.find("chunk_fwd_kernel_o") != std::string::npos) {
    return lifted_anchor("MambaChunkOut", "chunk_fwd_o");
  }
  if (low.find("chunk_local_cumsum_scalar_kernel") != std::string::npos) {
    return lifted_anchor("MambaScan", "local_cumsum");
  }
  if (low.find("merge_16x16_to_64x64_inverse_kernel") != std::string::npos) {
    return lifted_anchor("MambaLayout", "merge_inverse");
  }
  if (low.find("recompute_w_u_fwd_kernel") != std::string::npos) {
    return lifted_anchor("MambaRecompute", "recompute_w_u");
  }
  if (low.find("reduce_segments") != std::string::npos) {
    return lifted_anchor("MambaSegmentReduce", "reduce_segments");
  }
  if (low.find("fused_gdn_gating_kernel") != std::string::npos) {
    return lifted_anchor("MambaGate", "fused_gdn_gating");
  }

  if (low.find("_zero_kv_blocks_kernel") != std::string::npos) {
    return lifted_aux("KVCacheInit", "KVCacheInit");
  }
  if (contains_any(low, {"fillfunctor", "fill_reverse_indices"})) {
    return lifted_aux("Fill", "Fill");
  }
  if (contains_any(low, {"direct_copy_kernel", "copy_kernel_cuda",
                         "catarraybatchedcopy", "tensorcopy"})) {
    return lifted_aux("Copy", "Copy");
  }
  if (contains_any(low, {"index_elementwise_kernel", "indexselectsmallindex",
                         "vectorized_gather_kernel", "scatter_gather"})) {
    return lifted_aux("Index", "Index");
  }
  if (contains_any(low, {"masked_fill_kernel", "where_kernel_impl",
                         "compare_scalar_kernel", "bitwise_not_kernel"})) {
    return lifted_aux("Mask", "Mask");
  }
  if (contains_any(low, {"distribution_elementwise_grid_stride_kernel",
                         "distribution_nullary_kernel"})) {
    return lifted_aux("Random", "Random");
  }
  if (contains_any(low, {"rocprim::detail::single_scan_kernel",
                         "rocprim::detail::lookback_scan_kernel",
                         "rocprim::detail::init_lookback_scan_state_kernel"})) {
    return lifted_aux("ScanHelper", "ScanHelper");
  }
  if (contains_any(low, {"binaryfunctor", "aunaryfunctor", "bunaryfunctor",
                         "cudafunctor", "divfunctor", "mulfunctor",
                         "pow_tensor_tensor_kernel", "reciprocal_kernel_cuda",
                         "sigmoid_kernel_cuda"})) {
    return lifted_aux("Pointwise", "Pointwise");
  }
  if (contains_any(low, {"arange_cuda_out", "linspace_cuda_out",
                         "launch_clamp_scalar"})) {
    return lifted_aux("Range", "Range");
  }
  if (contains_any(low, {"argmaxops", "sum_functor", "reduceop"})) {
    return lifted_aux("ReductionHelper", "ReductionHelper");
  }
  return lifted_aux("Unknown", "unclassified");
}

using StreamKey = std::pair<std::uint32_t, std::uint64_t>;

void intern_stream(NativeIr& ir, std::map<StreamKey, StreamId>& streams,
                   SourceRefId source_ref, std::uint32_t device_id,
                   std::uint64_t raw_stream_id) {
  const StreamKey key{device_id, raw_stream_id};
  if (streams.find(key) != streams.end()) {
    return;
  }
  streams.emplace(key, ir.streams.append(source_ref, device_id, raw_stream_id));
}

void append_task_event(NativeIr& ir, SourceRefId source_ref,
                       std::uint64_t source_row_id, std::uint32_t device_id,
                       std::uint32_t stream_id, std::int64_t start_ns,
                       std::int64_t end_ns, std::uint64_t raw_task_id,
                       std::int64_t raw_global_task_id,
                       const std::string& raw_label,
                       const std::string& task_type, const std::string& op_name,
                       const std::string& op_type,
                       SymbolId comm_name_symbol_id = SymbolId::invalid()) {
  if (end_ns <= start_ns) {
    return;
  }
  const SymbolId raw_symbol = ir.symbols.intern(raw_label);
  const TraceEventId event =
      ir.trace_events.append(source_ref, source_row_id, device_id, stream_id,
                             start_ns, end_ns, raw_symbol);
  const SymbolId task_type_symbol = ir.symbols.intern(task_type);
  const SymbolId op_name_symbol =
      op_name.empty() ? SymbolId::invalid() : ir.symbols.intern(op_name);
  const SymbolId op_type_symbol =
      op_type.empty() ? SymbolId::invalid() : ir.symbols.intern(op_type);
  ir.tasks.append(source_ref, event, raw_task_id, raw_global_task_id, -1,
                  task_type_symbol, op_name_symbol, op_type_symbol,
                  task_type_symbol, comm_name_symbol_id);
}

void load_hipops_table(
    SqliteDb& db, NativeIr& ir, std::map<StreamKey, StreamId>& streams,
    SourceRefId source_ref, const std::string& table_name,
    const std::unordered_map<std::int64_t, std::string>& strings) {
  // _Index identifies a launch, not a row: one graph launch owns many
  // kernel/copy records. Preserve the literal SQLite rowid for raw audit.
  SqliteStmt stmt(db.get(),
                  "SELECT BeginNs, EndNs, dev_id, queue_id, Name, _Index, rowid "
                  "FROM \"" +
                      table_name +
                      "\" WHERE BeginNs IS NOT NULL AND EndNs IS NOT NULL "
                      "AND EndNs > BeginNs ORDER BY BeginNs, EndNs, _Index");
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      const std::int64_t start_ns = sqlite_i64(stmt.get(), 0);
      const std::int64_t end_ns = sqlite_i64(stmt.get(), 1);
      const std::uint32_t device_id = sqlite_u32(stmt.get(), 2);
      const std::uint32_t queue_id = sqlite_u32(stmt.get(), 3);
      const std::int64_t name_id = sqlite_i64(stmt.get(), 4, -1);
      const std::uint64_t index = sqlite_u64(stmt.get(), 5);
      const auto found = strings.find(name_id);
      const std::string raw_label =
          found == strings.end() ? "hip_kernel_" + std::to_string(name_id)
                                 : found->second;
      const LiftedKernel lifted = lift_hygon_kernel_label(raw_label);
      intern_stream(ir, streams, source_ref, device_id, queue_id);
      append_task_event(ir, source_ref, sqlite_u64(stmt.get(), 6), device_id,
                        queue_id, start_ns,
                        end_ns, index, static_cast<std::int64_t>(index),
                        raw_label,
                        "HIP_KERNEL", raw_label,
                        // Small kernels are executable structure, not noise.
                        // Keep their exact identity instead of collapsing every
                        // cast/multiply/reduction into one generic family.
                        lifted.anchor ? lifted.label : raw_label);
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    throw std::runtime_error("failed to load Hygon HIPOPS table " + table_name +
                             ": " + std::string(sqlite3_errmsg(stmt.db())));
  }
}

void load_hipcopy_table(SqliteDb& db, NativeIr& ir,
                        std::map<StreamKey, StreamId>& streams,
                        SourceRefId source_ref, const std::string& table_name) {
  // _Index identifies a launch, not a row: one graph launch owns many
  // kernel/copy records. Preserve the literal SQLite rowid for raw audit.
  SqliteStmt stmt(db.get(),
                  "SELECT BeginNs, EndNs, dev_id, queue_id, Kind, _Index, "
                  "Bytes, MemoryType, rowid "
                  "FROM \"" +
                      table_name +
                      "\" WHERE BeginNs IS NOT NULL AND EndNs IS NOT NULL "
                      "AND EndNs > BeginNs ORDER BY BeginNs, EndNs, _Index");
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      const std::int64_t start_ns = sqlite_i64(stmt.get(), 0);
      const std::int64_t end_ns = sqlite_i64(stmt.get(), 1);
      const std::uint32_t device_id = sqlite_u32(stmt.get(), 2);
      const std::uint32_t queue_id = sqlite_u32(stmt.get(), 3);
      const std::int64_t kind = sqlite_i64(stmt.get(), 4, -1);
      const std::uint64_t index = sqlite_u64(stmt.get(), 5);
      const std::uint64_t bytes = sqlite_u64(stmt.get(), 6);
      const std::int64_t memory_type = sqlite_i64(stmt.get(), 7, -1);
      const std::string label = "hip_copy kind=" + std::to_string(kind) +
                                " memory_type=" + std::to_string(memory_type) +
                                " bytes=" + std::to_string(bytes);
      intern_stream(ir, streams, source_ref, device_id, queue_id);
      const SymbolId comm_symbol = ir.symbols.intern(label);
      append_task_event(ir, source_ref, sqlite_u64(stmt.get(), 8), device_id,
                        queue_id, start_ns,
                        end_ns, index, static_cast<std::int64_t>(index), label,
                        "MEMCPY", label, "DataMove", comm_symbol);
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    throw std::runtime_error("failed to load Hygon HIPCOPY table " +
                             table_name + ": " +
                             std::string(sqlite3_errmsg(stmt.db())));
  }
}

}  // namespace

bool looks_like_hygon_sqlite_profile(const std::string& db_path) {
  if (!file_exists(db_path)) {
    return false;
  }
  try {
    SqliteDb db(db_path);
    const std::vector<std::string> tables = table_names(db);
    for (const std::string& table : tables) {
      if ((starts_with(table, "HIPOPS_") || starts_with(table, "HIPCOPY_") ||
           starts_with(table, "HIP_")) &&
          table_row_count(db, table) > 0) {
        return true;
      }
    }
  } catch (const std::exception&) {
    return false;
  }
  return false;
}

HygonSQLiteAdapter::HygonSQLiteAdapter(HygonSQLiteAdapterOptions options)
    : options_(std::move(options)) {}

HygonSQLiteAdapter::HygonSQLiteAdapter(std::string db_path,
                                       std::string source_kind)
    : options_(HygonSQLiteAdapterOptions{std::move(db_path),
                                         std::move(source_kind)}) {}

NativeIr HygonSQLiteAdapter::load() const {
  if (options_.db_path.empty()) {
    throw std::invalid_argument("Hygon SQLite DB path is empty");
  }
  if (!file_exists(options_.db_path)) {
    throw std::invalid_argument("Hygon SQLite DB does not exist: " +
                                options_.db_path);
  }

  HygonLoadTiming timing;
  const Stopwatch sqlite_open_watch;
  SqliteDb db(options_.db_path);
  timing.sqlite_open_ms = sqlite_open_watch.elapsed_ms();
  NativeIr ir;

  std::vector<std::string> tables;
  std::unordered_map<std::string, SourceRefId> table_refs;
  timing.inventory_ms = time_stage([&]() {
    tables = table_names(db);
    for (const std::string& table : tables) {
      table_refs.emplace(
          table, ir.source_refs.append(options_.source_kind, options_.db_path,
                                       table, 0));
    }
    if (tables.empty()) {
      ir.source_refs.append(options_.source_kind, options_.db_path,
                            "sqlite_schema", 0);
    }
  });

  std::unordered_map<std::int64_t, std::string> strings;
  timing.strings_ms =
      time_stage([&]() { strings = load_hygon_strings(db, tables); });

  std::map<StreamKey, StreamId> streams;
  timing.hipops_ms = time_stage([&]() {
    for (const std::string& table : tables) {
      if (!starts_with(table, "HIPOPS_")) {
        continue;
      }
      load_hipops_table(db, ir, streams, table_refs.at(table), table, strings);
    }
  });
  timing.hipcopy_ms = time_stage([&]() {
    for (const std::string& table : tables) {
      if (!starts_with(table, "HIPCOPY_")) {
        continue;
      }
      load_hipcopy_table(db, ir, streams, table_refs.at(table), table);
    }
  });

  if (options_.timing_diagnostics) {
    print_load_timing(timing);
  }
  return ir;
}

}  // namespace traceloom
