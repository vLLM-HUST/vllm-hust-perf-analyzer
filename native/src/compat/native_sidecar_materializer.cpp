#include "traceloom/compat/native_sidecar_materializer.h"

#include <string>
#include <vector>

#include "traceloom/compat/anchor_sequence_rows.h"
#include "traceloom/compat/aux_attribution_rows.h"
#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/compat/report_tree_rows.h"
#include "traceloom/compat/timeline_rows.h"

namespace traceloom::compat {

void write_basic_native_compatibility_sidecar(
    const std::string& sqlite_path,
    const NativeIr& ir,
    const NativeCompatibilitySidecarOptions& options) {
  std::vector<MetadataSqlRow> metadata{
      {"traceloom_schema_version", "augmented_db_v1"},
      {"native_compatibility_materializer", "basic_native_ir_v1"},
      {"source_kind", options.source_kind},
      {"source_path", options.source_path},
      {"trace_event_count", std::to_string(ir.trace_events.size())},
      {"anchor_count", std::to_string(ir.anchors.size())},
  };

  replace_metadata_rows(sqlite_path, metadata);
  const EventSqlRows event_rows = build_timeline_sql_rows(ir, options.db_idx);
  replace_timeline_rows(sqlite_path,
                        split_timeline_event_sql_rows(event_rows));
  replace_event_source_rows(sqlite_path,
                            split_source_lineage_sql_rows(event_rows));
  replace_anchor_rows(sqlite_path,
                      build_anchor_sequence_sql_rows(ir, options.db_idx));
  replace_aux_attribution_rows(sqlite_path,
                               build_aux_attribution_sql_rows(ir,
                                                              options.db_idx));
  const NodeCoverageSqlRows node_rows =
      build_native_report_tree_node_coverage_sql_rows(ir, options.db_idx);
  replace_loop_tree_rows(sqlite_path, split_loop_tree_sql_rows(node_rows));
  replace_node_anchor_coverage_rows(
      sqlite_path, split_node_anchor_coverage_sql_rows(node_rows));

  const SemanticTreeSqlRows semantic_rows =
      build_native_report_tree_semantic_sql_rows(ir, options.db_idx);
  replace_semantic_tree_catalog_rows(
      sqlite_path, split_semantic_tree_catalog_sql_rows(semantic_rows));
  replace_semantic_graph_rows(sqlite_path,
                              split_semantic_graph_sql_rows(semantic_rows));
  if (options.materialize_report_views) {
    materialize_report_compatibility_views(sqlite_path);
  }
}

}  // namespace traceloom::compat
