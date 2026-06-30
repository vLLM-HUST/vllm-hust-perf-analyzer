#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>

#include "traceloom/analysis/native_pipeline.h"
#include "traceloom/core/string_table.h"

namespace traceloom {

struct NativeResultJsonOptions {
  std::string source_kind = "unknown";
  std::string source_path;
  std::size_t thread_count = 1;
  std::size_t top_candidate_limit = 16;
  double load_source_adapter_ms = 0.0;
  double materialization_ms = 0.0;
};

void write_native_result_json(std::ostream& out,
                              const SymbolTable& symbols,
                              const NativePipelineResult& result,
                              const NativeResultJsonOptions& options);

}  // namespace traceloom
