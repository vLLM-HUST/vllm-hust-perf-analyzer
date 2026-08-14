#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/ir/native_ir.h"

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
namespace traceloom::compat::detail {

struct RawSourceDatabase {
  std::string source_id;
  std::uint32_t source_ordinal = 0;
  std::string source_path;
  std::string embedded_mode;
  std::uint64_t size_bytes = 0;
  std::string sha256;
};

struct RawTableCopy {
  std::string source_id;
  std::string source_path;
  std::string source_table;
  std::string embedded_table_name;
  std::string source_rowid_column;
  std::uint64_t row_count = 0;
};

struct RawPackagingResult {
  std::vector<RawSourceDatabase> sources;
  std::vector<RawTableCopy> tables;
};

void materialize_augmented_catalog(const std::string& path,
                                   const RawPackagingResult& packaging,
                                   const NativeIr& ir);

}  // namespace traceloom::compat::detail
#endif
