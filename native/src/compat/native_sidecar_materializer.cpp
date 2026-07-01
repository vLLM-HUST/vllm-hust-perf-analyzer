#include "traceloom/compat/native_sidecar_materializer.h"

#include <string>
#include <vector>

#include "traceloom/compat/anchor_sequence_rows.h"
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
  replace_event_rows(sqlite_path, build_timeline_sql_rows(ir, options.db_idx));
  replace_anchor_rows(sqlite_path,
                      build_anchor_sequence_sql_rows(ir, options.db_idx));
  replace_node_coverage_rows(
      sqlite_path,
      build_native_report_tree_node_coverage_sql_rows(ir, options.db_idx));
  replace_semantic_tree_rows(
      sqlite_path, build_native_report_tree_semantic_sql_rows(ir, options.db_idx));
  if (options.materialize_report_views) {
    materialize_report_compatibility_views(sqlite_path);
  }
}

}  // namespace traceloom::compat
