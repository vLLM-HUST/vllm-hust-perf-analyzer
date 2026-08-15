#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/analysis/structural_occurrence_graph.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

struct NativeCompatibilitySidecarOptions {
  std::uint32_t db_idx = 0;
  std::string source_kind = "native_ir";
  std::string source_path;
  bool materialize_structural_views = true;
  bool materialize_collective_tags = true;
  bool materialize_aux_attribution = true;
  std::string collective_run_name;
  std::string collective_db_name;
  std::uint32_t collective_expected_world_size = 0;
  bool materialize_grammar_structural_projection = true;
  std::size_t grammar_worker_count = 1;
  std::size_t grammar_target_nodes_per_chunk = 4096;
  std::size_t grammar_full_discovery_cap = 50000;
  bool timing_diagnostics = false;
  std::string artifact_kind = "compatibility_database";
  bool source_embedded = false;
  std::string source_sha256;
  std::uint64_t source_size_bytes = 0;
  std::string evidence_role_policy_id;
  std::string evidence_role_policy_version;
  std::string evidence_role_manifest_sha256;
  FlatAnchorBuildConfig evidence_role_config;
};

struct NativeDeviceStructuralProjection {
  std::uint32_t device_id = 0;
  std::vector<StructuralProjectionToken> tokens;
  StructuralOccurrenceGraph graph;
};

void write_basic_native_compatibility_sidecar(
    const std::string& sqlite_path,
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options =
        NativeCompatibilitySidecarOptions{});

// Creates a new SQLite artifact by snapshotting a regular input profiler DB,
// then materializing TraceLoom relations into that snapshot. The input is
// opened read-only and is never modified. Publication is atomic: the output
// path is replaced only after the complete queryable database timeline is ready.
void write_queryable_database_timeline(
    const std::string& output_path,
    const std::string& source_sqlite_path,
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options =
        NativeCompatibilitySidecarOptions{});

// Multi-file form used by split profiler layouts. Every input database is
// opened read-only and copied into collision-free raw tables in the one
// output artifact. traceloom_raw_source_database and traceloom_raw_table map
// original paths/table names to their embedded names.
void write_queryable_database_timeline(
    const std::string& output_path,
    const std::vector<std::string>& source_sqlite_paths,
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options =
        NativeCompatibilitySidecarOptions{});

// Builds one independently recovered structural occurrence graph per observed
// device. A multi-device IR yields one deterministic device-local sequence per
// device and never invents a cross-device graph.
std::vector<NativeDeviceStructuralProjection>
build_native_device_structural_projections(
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options =
        NativeCompatibilitySidecarOptions{});

// Returns per-device Loop Tree rows for every observed device. Each row
// carries its true device_id; node ids are device-scoped when the IR spans
// multiple devices so SQL joins stay unambiguous.
NodeCoverageSqlRows build_native_loop_tree_node_coverage_rows(
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options =
        NativeCompatibilitySidecarOptions{});

}  // namespace traceloom::compat
