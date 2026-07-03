#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

#include "traceloom/adapters/source_adapter.h"

namespace traceloom {

struct AscendSQLiteAdapterOptions {
  std::string db_path;
  std::string source_kind = "ascend_sqlite";
  std::size_t thread_count = 1;
  bool timing_diagnostics = false;
};

class AscendSQLiteAdapter final : public SourceAdapter {
 public:
#if defined(TRACELOOM_NATIVE_HAS_ASCEND_SQLITE)
  explicit AscendSQLiteAdapter(AscendSQLiteAdapterOptions options);
  explicit AscendSQLiteAdapter(std::string db_path,
                               std::string source_kind = "ascend_sqlite");

  NativeIr load() const override;
#else
  explicit AscendSQLiteAdapter(AscendSQLiteAdapterOptions options)
      : options_(std::move(options)) {}
  explicit AscendSQLiteAdapter(std::string db_path,
                               std::string source_kind = "ascend_sqlite")
      : options_(AscendSQLiteAdapterOptions{std::move(db_path),
                                            std::move(source_kind)}) {}

  NativeIr load() const override {
    throw std::runtime_error(
        "AscendSQLiteAdapter is unavailable: SQLite3 support was not built");
  }
#endif

 private:
  AscendSQLiteAdapterOptions options_;
};

}  // namespace traceloom
