#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/report/report_tree.h"

namespace traceloom::compat {

std::vector<ReportToken> build_report_tokens_from_native_ir(
    const NativeIr& ir);

struct NativeReportDevicePartition {
  std::uint32_t device_id = 0;
  std::vector<ReportToken> tokens;
};

// Partitions the report token stream by observed device_id, preserving the
// deterministic token-table order within each device. One partition exists
// per device that owns at least one report token, ordered by device_id. This
// never invents cross-device ordering: a multi-device profiler DB yields one
// independent linear anchor sequence per device instead of a combined tree.
std::vector<NativeReportDevicePartition> partition_report_tokens_by_device(
    const NativeIr& ir);

// Attributes each replay unit to the device that owns its anchor bounds.
// Every present bound must be in range, and when both bounds are present
// they must own anchors on the same device; malformed units fail closed
// (throw) instead of being misattributed. Units with no valid bound are
// left unclaimed so callers can decide the unattributable fallback.
std::map<ReplayUnitId::value_type, std::uint32_t> replay_unit_device_map(
    const NativeIr& ir);

NodeCoverageSqlRows build_report_tree_node_coverage_sql_rows(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    std::uint32_t db_idx = 0,
    std::string view_name = "native_report_tree",
    bool scope_node_ids_by_device = false);

NodeCoverageSqlRows build_report_tree_node_coverage_sql_rows(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    const AuxAttributionSqlRows& aux_rows,
    std::uint32_t db_idx = 0,
    std::string view_name = "native_report_tree",
    bool scope_node_ids_by_device = false);

NodeCoverageSqlRows build_native_report_tree_node_coverage_sql_rows(
    const NativeIr& ir,
    std::uint32_t db_idx = 0,
    std::string view_name = "native_report_tree");

LoopTreeSqlRows split_loop_tree_sql_rows(const NodeCoverageSqlRows& rows);

NodeAnchorCoverageSqlRows split_node_anchor_coverage_sql_rows(
    const NodeCoverageSqlRows& rows);

SemanticTreeSqlRows build_report_tree_semantic_sql_rows(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    std::uint32_t db_idx = 0,
    std::string tree_id = "native-report-tree",
    std::string view_name = "anchor_tree",
    bool scope_node_ids_by_device = false);

SemanticTreeSqlRows build_report_tree_semantic_sql_rows(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    const AuxAttributionSqlRows& aux_rows,
    std::uint32_t db_idx = 0,
    std::string tree_id = "native-report-tree",
    std::string view_name = "anchor_tree",
    bool scope_node_ids_by_device = false);

SemanticTreeSqlRows build_native_report_tree_semantic_sql_rows(
    const NativeIr& ir,
    std::uint32_t db_idx = 0,
    std::string tree_id = "native-report-tree",
    std::string view_name = "anchor_tree");

std::vector<SemanticTreeHeaderSqlRow> split_semantic_tree_catalog_sql_rows(
    const SemanticTreeSqlRows& rows);

SemanticGraphSqlRows split_semantic_graph_sql_rows(
    const SemanticTreeSqlRows& rows);

}  // namespace traceloom::compat
