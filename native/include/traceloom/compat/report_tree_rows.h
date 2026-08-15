#pragma once

#include "traceloom/compat/structural_projection_rows.h"
#include "traceloom/report/report_tree.h"

namespace traceloom::compat {

using NativeReportDevicePartition = NativeStructuralDevicePartition;

std::vector<ReportToken> build_report_tokens_from_native_ir(
    const NativeIr& ir);

std::vector<ReportToken> build_report_tokens_from_native_ir(
    const NativeIr& ir,
    FlatAnchorBuildConfig config);

std::vector<NativeReportDevicePartition> partition_report_tokens_by_device(
    const NativeIr& ir);

std::vector<NativeReportDevicePartition> partition_report_tokens_by_device(
    const NativeIr& ir,
    FlatAnchorBuildConfig config);

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

}  // namespace traceloom::compat
