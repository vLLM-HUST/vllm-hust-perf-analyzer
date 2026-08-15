#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "traceloom/ir/native_ir.h"

namespace traceloom {

struct AscendSplitSQLiteTableInfo {
  std::string db_path;
  std::string table_name;
  std::string create_sql;
  std::uint64_t row_count = 0;
};

bool ascend_sqlite_has_usable_task_table(const std::string& db_path);
bool looks_like_ascend_split_sqlite_profile(const std::string& profile_dir);
std::vector<AscendSplitSQLiteTableInfo>
inventory_ascend_split_sqlite_profile(const std::string& profile_dir);

struct AscendSQLiteAdapterOptions {
  std::string db_path;
  std::string source_kind = "ascend_sqlite";
  std::size_t thread_count = 1;
  bool timing_diagnostics = false;
};

class AscendSQLiteAdapter final {
 public:
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_ADAPTERS)
  explicit AscendSQLiteAdapter(AscendSQLiteAdapterOptions options);
  explicit AscendSQLiteAdapter(std::string db_path,
                               std::string source_kind = "ascend_sqlite");

  NativeIr load() const;
#else
  explicit AscendSQLiteAdapter(AscendSQLiteAdapterOptions options)
      : options_(std::move(options)) {}
  explicit AscendSQLiteAdapter(std::string db_path,
                               std::string source_kind = "ascend_sqlite")
      : options_(AscendSQLiteAdapterOptions{std::move(db_path),
                                            std::move(source_kind)}) {}

  NativeIr load() const {
    throw std::runtime_error(
        "AscendSQLiteAdapter is unavailable: SQLite3 support was not built");
  }
#endif

 private:
  AscendSQLiteAdapterOptions options_;
};

}  // namespace traceloom
