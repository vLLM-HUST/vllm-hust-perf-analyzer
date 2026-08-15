#include "traceloom/compat/schema.h"

namespace traceloom::compat {

const CompatTableSchema& viz_node_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_viz_node",
      {
          {"node_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"view_name", CompatColumnType::kText, false},
          {"local_node_id", CompatColumnType::kText, false},
          {"path", CompatColumnType::kText, true},
          {"node_type", CompatColumnType::kText, true},
          {"kind", CompatColumnType::kText, true},
          {"symbol", CompatColumnType::kText, true},
          {"label", CompatColumnType::kText, true},
          {"category", CompatColumnType::kText, true},
          {"depth", CompatColumnType::kInteger, true},
          {"level", CompatColumnType::kInteger, true},
          {"repeat_label", CompatColumnType::kText, true},
          {"repeat_count", CompatColumnType::kInteger, true},
          {"occurrence_count", CompatColumnType::kInteger, true},
          {"anchor_count", CompatColumnType::kInteger, true},
          {"anchors_per_occurrence", CompatColumnType::kReal, true},
          {"first_anchor_idx", CompatColumnType::kInteger, true},
          {"last_anchor_idx", CompatColumnType::kInteger, true},
          {"compute_us", CompatColumnType::kReal, true},
          {"comm_us", CompatColumnType::kReal, true},
          {"idle_us", CompatColumnType::kReal, true},
          {"total_us", CompatColumnType::kReal, true},
          {"avg_compute_us", CompatColumnType::kReal, true},
          {"avg_comm_us", CompatColumnType::kReal, true},
          {"avg_idle_us", CompatColumnType::kReal, true},
          {"avg_total_us", CompatColumnType::kReal, true},
          {"self_us", CompatColumnType::kReal, true},
          {"aux_events", CompatColumnType::kReal, true},
          {"aux_us", CompatColumnType::kReal, true},
          {"raw_json", CompatColumnType::kText, true},
      },
  };
  return schema;
}

const CompatTableSchema& viz_edge_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_viz_edge",
      {
          {"parent_node_id", CompatColumnType::kText, false},
          {"child_node_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"view_name", CompatColumnType::kText, false},
          {"edge_order", CompatColumnType::kInteger, false},
          {"edge_kind", CompatColumnType::kText, true},
          {"raw_json", CompatColumnType::kText, true},
      },
  };
  return schema;
}

const CompatTableSchema& viz_node_anchor_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_viz_node_anchor",
      {
          {"node_id", CompatColumnType::kText, false},
          {"anchor_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"view_name", CompatColumnType::kText, false},
          {"occurrence_idx", CompatColumnType::kInteger, false},
          {"anchor_order", CompatColumnType::kInteger, false},
          {"coverage_kind", CompatColumnType::kText, false},
          {"repeat_context", CompatColumnType::kText, true},
          {"compute_us", CompatColumnType::kReal, false},
          {"comm_us", CompatColumnType::kReal, false},
          {"idle_us", CompatColumnType::kReal, false},
          {"total_us", CompatColumnType::kReal, false},
          {"self_us", CompatColumnType::kReal, false},
          {"aux_events", CompatColumnType::kReal, false},
          {"aux_us", CompatColumnType::kReal, false},
      },
  };
  return schema;
}

const CompatTableSchema& anchor_primary_node_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_anchor_primary_node",
      {
          {"anchor_id", CompatColumnType::kText, false},
          {"node_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"view_name", CompatColumnType::kText, false},
          {"reason", CompatColumnType::kText, false},
      },
  };
  return schema;
}

const CompatTableSchema& loop_node_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_loop_node",
      {
          {"node_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"view_name", CompatColumnType::kText, false},
          {"loop_rank", CompatColumnType::kInteger, true},
          {"repeat_label", CompatColumnType::kText, true},
          {"repeat_count", CompatColumnType::kInteger, true},
          {"occurrence_count", CompatColumnType::kInteger, true},
          {"anchor_count", CompatColumnType::kInteger, true},
          {"total_us", CompatColumnType::kReal, true},
          {"avg_total_us", CompatColumnType::kReal, true},
          {"compute_us", CompatColumnType::kReal, true},
          {"comm_us", CompatColumnType::kReal, true},
          {"idle_us", CompatColumnType::kReal, true},
          {"loop_total_pct", CompatColumnType::kReal, true},
          {"raw_json", CompatColumnType::kText, true},
      },
  };
  return schema;
}

const CompatTableSchema& semantic_tree_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_semantic_tree",
      {
          {"tree_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"view_name", CompatColumnType::kText, false},
          {"tree_kind", CompatColumnType::kText, false},
          {"stem", CompatColumnType::kText, true},
          {"root_node_id", CompatColumnType::kText, true},
          {"schema_version", CompatColumnType::kText, true},
          {"semantic_projection", CompatColumnType::kText, true},
          {"macro_discovery", CompatColumnType::kText, true},
          {"readable_macro_mode", CompatColumnType::kText, true},
          {"auxiliary_attribution", CompatColumnType::kText, true},
          {"raw_json", CompatColumnType::kText, true},
      },
  };
  return schema;
}

const CompatTableSchema& semantic_node_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_semantic_node",
      {
          {"node_id", CompatColumnType::kText, false},
          {"tree_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"view_name", CompatColumnType::kText, false},
          {"tree_kind", CompatColumnType::kText, false},
          {"local_node_id", CompatColumnType::kText, false},
          {"parent_node_id", CompatColumnType::kText, true},
          {"parent_local_node_id", CompatColumnType::kText, true},
          {"preorder_idx", CompatColumnType::kInteger, false},
          {"sibling_order", CompatColumnType::kInteger, false},
          {"path", CompatColumnType::kText, true},
          {"depth", CompatColumnType::kInteger, true},
          {"display_depth", CompatColumnType::kInteger, true},
          {"loop_depth", CompatColumnType::kInteger, true},
          {"node_type", CompatColumnType::kText, true},
          {"semantic_kind", CompatColumnType::kText, true},
          {"symbol", CompatColumnType::kText, true},
          {"label", CompatColumnType::kText, true},
          {"category", CompatColumnType::kText, true},
          {"repeat_count", CompatColumnType::kInteger, true},
          {"occurrence_count", CompatColumnType::kInteger, true},
          {"anchor_count", CompatColumnType::kInteger, true},
          {"first_anchor_idx", CompatColumnType::kInteger, true},
          {"last_anchor_idx", CompatColumnType::kInteger, true},
          {"start_ns", CompatColumnType::kInteger, true},
          {"end_ns", CompatColumnType::kInteger, true},
          {"compute_us", CompatColumnType::kReal, true},
          {"comm_us", CompatColumnType::kReal, true},
          {"idle_us", CompatColumnType::kReal, true},
          {"total_us", CompatColumnType::kReal, true},
          {"avg_compute_us", CompatColumnType::kReal, true},
          {"avg_comm_us", CompatColumnType::kReal, true},
          {"avg_idle_us", CompatColumnType::kReal, true},
          {"avg_total_us", CompatColumnType::kReal, true},
          {"self_us", CompatColumnType::kReal, true},
          {"aux_event_count", CompatColumnType::kReal, true},
          {"aux_us", CompatColumnType::kReal, true},
          {"hidden_aux_event_count", CompatColumnType::kReal, true},
          {"hidden_aux_us", CompatColumnType::kReal, true},
          {"raw_json", CompatColumnType::kText, true},
      },
  };
  return schema;
}

const CompatTableSchema& semantic_edge_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_semantic_edge",
      {
          {"parent_node_id", CompatColumnType::kText, false},
          {"child_node_id", CompatColumnType::kText, false},
          {"tree_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"view_name", CompatColumnType::kText, false},
          {"tree_kind", CompatColumnType::kText, false},
          {"edge_order", CompatColumnType::kInteger, false},
          {"edge_kind", CompatColumnType::kText, true},
          {"raw_json", CompatColumnType::kText, true},
      },
  };
  return schema;
}

}  // namespace traceloom::compat
