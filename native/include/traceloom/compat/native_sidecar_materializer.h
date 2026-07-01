#pragma once

#include <cstdint>
#include <string>

#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

struct NativeCompatibilitySidecarOptions {
  std::uint32_t db_idx = 0;
  std::string source_kind = "native_ir";
  std::string source_path;
  bool materialize_report_views = true;
  bool materialize_collective_tags = true;
  std::string collective_run_name;
  std::string collective_db_name;
  std::uint32_t collective_expected_world_size = 0;
};

void write_basic_native_compatibility_sidecar(
    const std::string& sqlite_path,
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options =
        NativeCompatibilitySidecarOptions{});

}  // namespace traceloom::compat
