#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

#include "traceloom/ir/native_ir.h"

namespace traceloom {

struct HygonSQLiteAdapterOptions {
  std::string db_path;
  std::string source_kind = "hygon_sqlite";
  std::size_t thread_count = 1;
  bool timing_diagnostics = false;
};

bool looks_like_hygon_sqlite_profile(const std::string& db_path);

class HygonSQLiteAdapter final {
 public:
#if defined(TRACELOOM_NATIVE_HAS_SQLITE_ADAPTERS)
  explicit HygonSQLiteAdapter(HygonSQLiteAdapterOptions options);
  explicit HygonSQLiteAdapter(std::string db_path,
                              std::string source_kind = "hygon_sqlite");

  NativeIr load() const;
#else
  explicit HygonSQLiteAdapter(HygonSQLiteAdapterOptions options)
      : options_(std::move(options)) {}
  explicit HygonSQLiteAdapter(std::string db_path,
                              std::string source_kind = "hygon_sqlite")
      : options_(HygonSQLiteAdapterOptions{std::move(db_path),
                                           std::move(source_kind)}) {}

  NativeIr load() const {
    throw std::runtime_error(
        "HygonSQLiteAdapter is unavailable: SQLite3 support was not built");
  }
#endif

 private:
  HygonSQLiteAdapterOptions options_;
};

}  // namespace traceloom
