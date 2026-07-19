#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "traceloom/adapters/source_adapter.h"

namespace traceloom {

struct CudaNsightSQLiteAdapterOptions {
  std::string db_path;
  std::string source_kind = "cuda_nsys_sqlite";
  std::size_t thread_count = 1;
  bool timing_diagnostics = false;
};

// Describes the deliberately small first native CUDA adapter boundary. Kernel
// rows and StringIds name resolution are supported. Other CUPTI activity tables
// are reported here rather than being silently presented as imported evidence.
struct CudaNsightSQLiteInventory {
  bool has_kernel_table = false;
  bool has_string_ids_table = false;
  std::uint64_t kernel_row_count = 0;
  std::vector<std::string> missing_required_kernel_columns;
  std::vector<std::string> unsupported_activity_tables;
};

CudaNsightSQLiteInventory inspect_cuda_nsys_sqlite_profile(
    const std::string& db_path);
bool looks_like_cuda_nsys_sqlite_profile(const std::string& db_path);

class CudaNsightSQLiteAdapter final : public SourceAdapter {
 public:
#if defined(TRACELOOM_NATIVE_HAS_ASCEND_SQLITE)
  explicit CudaNsightSQLiteAdapter(CudaNsightSQLiteAdapterOptions options);
  explicit CudaNsightSQLiteAdapter(
      std::string db_path, std::string source_kind = "cuda_nsys_sqlite");

  NativeIr load() const override;
#else
  explicit CudaNsightSQLiteAdapter(CudaNsightSQLiteAdapterOptions options)
      : options_(std::move(options)) {}
  explicit CudaNsightSQLiteAdapter(
      std::string db_path, std::string source_kind = "cuda_nsys_sqlite")
      : options_(CudaNsightSQLiteAdapterOptions{std::move(db_path),
                                                std::move(source_kind)}) {}

  NativeIr load() const override {
    throw std::runtime_error(
        "CudaNsightSQLiteAdapter is unavailable: SQLite3 support was not "
        "built");
  }
#endif

 private:
  CudaNsightSQLiteAdapterOptions options_;
};

}  // namespace traceloom
