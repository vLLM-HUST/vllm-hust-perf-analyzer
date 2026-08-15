#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/analysis/flat_anchor_builder.h"
#include "traceloom/analysis/structural_occurrence_graph.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/ir/native_ir.h"

namespace traceloom::compat {

std::vector<StructuralProjectionToken>
build_structural_projection_tokens_from_native_ir(const NativeIr& ir);

std::vector<StructuralProjectionToken>
build_structural_projection_tokens_from_native_ir(
    const NativeIr& ir,
    FlatAnchorBuildConfig config);

struct NativeStructuralDevicePartition {
  std::uint32_t device_id = 0;
  std::vector<StructuralProjectionToken> tokens;
};

// Partitions the structural token stream by observed device_id, preserving
// deterministic token-table order within each device. Each partition is an
// independent device-local sequence; no cross-device ordering is invented.
std::vector<NativeStructuralDevicePartition>
partition_structural_projection_tokens_by_device(const NativeIr& ir);

std::vector<NativeStructuralDevicePartition>
partition_structural_projection_tokens_by_device(
    const NativeIr& ir,
    FlatAnchorBuildConfig config);

NodeCoverageSqlRows build_structural_node_coverage_sql_rows(
    const StructuralOccurrenceGraph& graph,
    const std::vector<StructuralProjectionToken>& tokens,
    std::uint32_t db_idx = 0,
    std::string view_name = "native_report_tree",
    bool scope_node_ids_by_device = false);

NodeCoverageSqlRows build_structural_node_coverage_sql_rows(
    const StructuralOccurrenceGraph& graph,
    const std::vector<StructuralProjectionToken>& tokens,
    const AuxAttributionSqlRows& aux_rows,
    std::uint32_t db_idx = 0,
    std::string view_name = "native_report_tree",
    bool scope_node_ids_by_device = false);

NodeCoverageSqlRows build_native_structural_node_coverage_sql_rows(
    const NativeIr& ir,
    std::uint32_t db_idx = 0,
    std::string view_name = "native_report_tree");

LoopTreeSqlRows split_loop_tree_sql_rows(const NodeCoverageSqlRows& rows);

NodeAnchorCoverageSqlRows split_node_anchor_coverage_sql_rows(
    const NodeCoverageSqlRows& rows);

SemanticTreeSqlRows build_structural_semantic_sql_rows(
    const StructuralOccurrenceGraph& graph,
    const std::vector<StructuralProjectionToken>& tokens,
    std::uint32_t db_idx = 0,
    std::string tree_id = "native-report-tree",
    std::string view_name = "anchor_tree",
    bool scope_node_ids_by_device = false);

SemanticTreeSqlRows build_structural_semantic_sql_rows(
    const StructuralOccurrenceGraph& graph,
    const std::vector<StructuralProjectionToken>& tokens,
    const AuxAttributionSqlRows& aux_rows,
    std::uint32_t db_idx = 0,
    std::string tree_id = "native-report-tree",
    std::string view_name = "anchor_tree",
    bool scope_node_ids_by_device = false);

SemanticTreeSqlRows build_native_structural_semantic_sql_rows(
    const NativeIr& ir,
    std::uint32_t db_idx = 0,
    std::string tree_id = "native-report-tree",
    std::string view_name = "anchor_tree");

std::vector<SemanticTreeHeaderSqlRow> split_semantic_tree_catalog_sql_rows(
    const SemanticTreeSqlRows& rows);

SemanticGraphSqlRows split_semantic_graph_sql_rows(
    const SemanticTreeSqlRows& rows);

}  // namespace traceloom::compat
