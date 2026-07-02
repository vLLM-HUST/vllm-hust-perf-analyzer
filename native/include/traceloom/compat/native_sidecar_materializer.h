#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

struct NativeCompatibilitySidecarOptions {
  std::uint32_t db_idx = 0;
  std::string source_kind = "native_ir";
  std::string source_path;
  bool materialize_report_views = true;
  bool materialize_collective_tags = true;
  bool materialize_aux_attribution = true;
  std::string collective_run_name;
  std::string collective_db_name;
  std::uint32_t collective_expected_world_size = 0;
  bool materialize_grammar_report_tree = true;
  std::size_t grammar_worker_count = 1;
  std::size_t grammar_target_nodes_per_chunk = 4096;
  std::size_t grammar_full_discovery_cap = 50000;
  bool timing_diagnostics = false;
};

void write_basic_native_compatibility_sidecar(
    const std::string& sqlite_path,
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options =
        NativeCompatibilitySidecarOptions{});

NodeCoverageSqlRows build_native_loop_tree_node_coverage_rows(
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options =
        NativeCompatibilitySidecarOptions{});

}  // namespace traceloom::compat
