#include "traceloom/compat/report_tree_rows.h"

#include <utility>

namespace traceloom::compat {

std::vector<ReportToken> build_report_tokens_from_native_ir(
    const NativeIr& ir) {
  return build_structural_projection_tokens_from_native_ir(ir);
}

std::vector<ReportToken> build_report_tokens_from_native_ir(
    const NativeIr& ir,
    FlatAnchorBuildConfig config) {
  return build_structural_projection_tokens_from_native_ir(
      ir, std::move(config));
}

std::vector<NativeReportDevicePartition> partition_report_tokens_by_device(
    const NativeIr& ir) {
  return partition_structural_projection_tokens_by_device(ir);
}

std::vector<NativeReportDevicePartition> partition_report_tokens_by_device(
    const NativeIr& ir,
    FlatAnchorBuildConfig config) {
  return partition_structural_projection_tokens_by_device(
      ir, std::move(config));
}

NodeCoverageSqlRows build_report_tree_node_coverage_sql_rows(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    std::uint32_t db_idx,
    std::string view_name,
    bool scope_node_ids_by_device) {
  return build_structural_node_coverage_sql_rows(
      tree, tokens, db_idx, std::move(view_name), scope_node_ids_by_device);
}

NodeCoverageSqlRows build_report_tree_node_coverage_sql_rows(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    const AuxAttributionSqlRows& aux_rows,
    std::uint32_t db_idx,
    std::string view_name,
    bool scope_node_ids_by_device) {
  return build_structural_node_coverage_sql_rows(
      tree, tokens, aux_rows, db_idx, std::move(view_name),
      scope_node_ids_by_device);
}

NodeCoverageSqlRows build_native_report_tree_node_coverage_sql_rows(
    const NativeIr& ir,
    std::uint32_t db_idx,
    std::string view_name) {
  return build_native_structural_node_coverage_sql_rows(
      ir, db_idx, std::move(view_name));
}

SemanticTreeSqlRows build_report_tree_semantic_sql_rows(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    std::uint32_t db_idx,
    std::string tree_id,
    std::string view_name,
    bool scope_node_ids_by_device) {
  return build_structural_semantic_sql_rows(
      tree, tokens, db_idx, std::move(tree_id), std::move(view_name),
      scope_node_ids_by_device);
}

SemanticTreeSqlRows build_report_tree_semantic_sql_rows(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    const AuxAttributionSqlRows& aux_rows,
    std::uint32_t db_idx,
    std::string tree_id,
    std::string view_name,
    bool scope_node_ids_by_device) {
  return build_structural_semantic_sql_rows(
      tree, tokens, aux_rows, db_idx, std::move(tree_id),
      std::move(view_name), scope_node_ids_by_device);
}

SemanticTreeSqlRows build_native_report_tree_semantic_sql_rows(
    const NativeIr& ir,
    std::uint32_t db_idx,
    std::string tree_id,
    std::string view_name) {
  return build_native_structural_semantic_sql_rows(
      ir, db_idx, std::move(tree_id), std::move(view_name));
}

}  // namespace traceloom::compat
