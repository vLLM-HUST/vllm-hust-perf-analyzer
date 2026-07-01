#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/ir/native_ir.h"
#include "traceloom/report/report_tree.h"

namespace traceloom::compat {

std::vector<ReportToken> build_report_tokens_from_native_ir(
    const NativeIr& ir);

NodeCoverageSqlRows build_report_tree_node_coverage_sql_rows(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    std::uint32_t db_idx = 0,
    std::string view_name = "native_report_tree");

NodeCoverageSqlRows build_report_tree_node_coverage_sql_rows(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    const AuxAttributionSqlRows& aux_rows,
    std::uint32_t db_idx = 0,
    std::string view_name = "native_report_tree");

NodeCoverageSqlRows build_native_report_tree_node_coverage_sql_rows(
    const NativeIr& ir,
    std::uint32_t db_idx = 0,
    std::string view_name = "native_report_tree");

SemanticTreeSqlRows build_report_tree_semantic_sql_rows(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    std::uint32_t db_idx = 0,
    std::string tree_id = "native-report-tree",
    std::string view_name = "anchor_tree");

SemanticTreeSqlRows build_report_tree_semantic_sql_rows(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    const AuxAttributionSqlRows& aux_rows,
    std::uint32_t db_idx = 0,
    std::string tree_id = "native-report-tree",
    std::string view_name = "anchor_tree");

SemanticTreeSqlRows build_native_report_tree_semantic_sql_rows(
    const NativeIr& ir,
    std::uint32_t db_idx = 0,
    std::string tree_id = "native-report-tree",
    std::string view_name = "anchor_tree");

}  // namespace traceloom::compat
