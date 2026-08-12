#include "traceloom/compat/schema.h"

#include <cctype>
#include <stdexcept>

namespace traceloom::compat {

namespace {

bool is_safe_identifier(const std::string& value) {
  if (value.empty()) {
    return false;
  }
  const unsigned char first = static_cast<unsigned char>(value[0]);
  if (!(std::isalpha(first) || value[0] == '_')) {
    return false;
  }
  for (const char ch : value) {
    const unsigned char current = static_cast<unsigned char>(ch);
    if (!(std::isalnum(current) || ch == '_')) {
      return false;
    }
  }
  return true;
}

void require_safe_identifier(const std::string& value, const char* what) {
  if (!is_safe_identifier(value)) {
    throw std::invalid_argument(std::string("invalid compatibility ") + what +
                                " identifier: " + value);
  }
}

}  // namespace

const char* compat_column_type_name(CompatColumnType type) {
  switch (type) {
    case CompatColumnType::kInteger:
      return "integer";
    case CompatColumnType::kReal:
      return "real";
    case CompatColumnType::kText:
      return "text";
  }
  return "text";
}

const char* sqlite_column_type_name(CompatColumnType type) {
  switch (type) {
    case CompatColumnType::kInteger:
      return "INTEGER";
    case CompatColumnType::kReal:
      return "REAL";
    case CompatColumnType::kText:
      return "TEXT";
  }
  return "TEXT";
}

const CompatTableSchema& metadata_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_metadata",
      {
          {"key", CompatColumnType::kText, false},
          {"value", CompatColumnType::kText, false},
      },
  };
  return schema;
}

const CompatTableSchema& event_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_event",
      {
          {"event_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"step_idx", CompatColumnType::kInteger, false},
          {"source_table", CompatColumnType::kText, false},
          {"source_key", CompatColumnType::kText, false},
          {"stream_id", CompatColumnType::kInteger, true},
          {"start_ns", CompatColumnType::kInteger, true},
          {"end_ns", CompatColumnType::kInteger, true},
          {"dur_us", CompatColumnType::kReal, true},
          {"category", CompatColumnType::kText, true},
          {"role", CompatColumnType::kText, true},
          {"semantic_role", CompatColumnType::kText, true},
          {"semantic_role_reason", CompatColumnType::kText, true},
          {"symbol", CompatColumnType::kText, true},
          {"label", CompatColumnType::kText, true},
          {"raw_label", CompatColumnType::kText, true},
          {"op_type", CompatColumnType::kText, true},
          {"compute_task_type", CompatColumnType::kText, true},
          {"family", CompatColumnType::kText, true},
          {"task_type", CompatColumnType::kText, true},
          {"raw_json", CompatColumnType::kText, true},
      },
  };
  return schema;
}

const CompatTableSchema& event_source_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_event_source",
      {
          {"event_id", CompatColumnType::kText, false},
          {"source_ordinal", CompatColumnType::kInteger, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"source_table", CompatColumnType::kText, false},
          {"source_key", CompatColumnType::kText, false},
          {"source_role", CompatColumnType::kText, true},
          {"raw_json", CompatColumnType::kText, true},
      },
  };
  return schema;
}

const CompatTableSchema& runtime_call_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_runtime_call",
      {
          {"runtime_call_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"provider", CompatColumnType::kText, false},
          {"clock_domain", CompatColumnType::kText, false},
          {"source_table", CompatColumnType::kText, false},
          {"source_key", CompatColumnType::kText, false},
          {"start_ns", CompatColumnType::kInteger, false},
          {"end_ns", CompatColumnType::kInteger, false},
          {"dur_us", CompatColumnType::kReal, false},
          {"api_name", CompatColumnType::kText, true},
          {"api_type", CompatColumnType::kText, true},
          {"process_id", CompatColumnType::kText, true},
          {"thread_id", CompatColumnType::kText, true},
          {"global_tid", CompatColumnType::kText, true},
          {"context_id", CompatColumnType::kText, true},
          {"device_id", CompatColumnType::kText, true},
          {"correlation_id", CompatColumnType::kText, true},
          {"match_policy", CompatColumnType::kText, false},
          {"raw_json", CompatColumnType::kText, true},
      },
  };
  return schema;
}

const CompatTableSchema& device_work_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_device_work",
      {
          {"device_work_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"provider", CompatColumnType::kText, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"work_kind", CompatColumnType::kText, false},
          {"event_id", CompatColumnType::kText, true},
          {"task_id", CompatColumnType::kText, true},
          {"graph_launch_occurrence_id", CompatColumnType::kInteger, true},
          {"source_table", CompatColumnType::kText, false},
          {"source_key", CompatColumnType::kText, false},
          {"start_ns", CompatColumnType::kInteger, false},
          {"end_ns", CompatColumnType::kInteger, false},
          {"dur_us", CompatColumnType::kReal, false},
          {"symbol", CompatColumnType::kText, true},
          {"raw_json", CompatColumnType::kText, true},
      },
  };
  return schema;
}

const CompatTableSchema& runtime_device_relation_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_runtime_device_relation",
      {
          {"relation_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"runtime_call_id", CompatColumnType::kText, true},
          {"device_work_id", CompatColumnType::kText, true},
          {"relation_kind", CompatColumnType::kText, false},
          {"match_policy", CompatColumnType::kText, false},
          {"evidence_level", CompatColumnType::kText, false},
          {"support_state", CompatColumnType::kText, false},
          {"cardinality", CompatColumnType::kText, false},
          {"runtime_candidate_count", CompatColumnType::kInteger, false},
          {"device_candidate_count", CompatColumnType::kInteger, false},
          {"correlation_id", CompatColumnType::kText, true},
          {"raw_json", CompatColumnType::kText, true},
      },
  };
  return schema;
}

const CompatTableSchema& anchor_runtime_relation_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_anchor_runtime_relation",
      {
          {"anchor_id", CompatColumnType::kText, false},
          {"relation_id", CompatColumnType::kText, false},
          {"runtime_call_id", CompatColumnType::kText, true},
          {"device_work_id", CompatColumnType::kText, false},
          {"endpoint_kind", CompatColumnType::kText, false},
      },
  };
  return schema;
}

const CompatTableSchema& anchor_host_interval_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_anchor_host_interval",
      {
          {"interval_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"left_anchor_id", CompatColumnType::kText, false},
          {"right_anchor_id", CompatColumnType::kText, false},
          {"left_runtime_call_id", CompatColumnType::kText, true},
          {"right_runtime_call_id", CompatColumnType::kText, true},
          {"left_endpoint_count", CompatColumnType::kInteger, false},
          {"right_endpoint_count", CompatColumnType::kInteger, false},
          {"provider", CompatColumnType::kText, true},
          {"clock_domain", CompatColumnType::kText, true},
          {"host_start_ns", CompatColumnType::kInteger, true},
          {"host_end_ns", CompatColumnType::kInteger, true},
          {"scope_policy", CompatColumnType::kText, false},
          {"process_id", CompatColumnType::kText, true},
          {"thread_id", CompatColumnType::kText, true},
          {"support_state", CompatColumnType::kText, false},
      },
  };
  return schema;
}

const CompatTableSchema& anchor_host_activity_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_anchor_host_activity",
      {
          {"interval_id", CompatColumnType::kText, false},
          {"runtime_call_id", CompatColumnType::kText, false},
          {"observed_order", CompatColumnType::kInteger, false},
      },
  };
  return schema;
}

const CompatTableSchema& anchor_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_anchor",
      {
          {"anchor_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"anchor_idx", CompatColumnType::kInteger, false},
          {"event_id", CompatColumnType::kText, false},
          {"step_idx", CompatColumnType::kInteger, false},
          {"symbol", CompatColumnType::kText, true},
          {"role", CompatColumnType::kText, true},
          {"label", CompatColumnType::kText, true},
          {"family", CompatColumnType::kText, true},
          {"start_ns", CompatColumnType::kInteger, true},
          {"end_ns", CompatColumnType::kInteger, true},
          {"dur_us", CompatColumnType::kReal, true},
      },
  };
  return schema;
}

const CompatTableSchema& anchor_aux_slot_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_anchor_aux_slot",
      {
          {"anchor_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"anchor_idx", CompatColumnType::kInteger, false},
          {"anchor_step_idx", CompatColumnType::kInteger, false},
          {"aux_start_step_idx", CompatColumnType::kInteger, true},
          {"aux_end_step_idx", CompatColumnType::kInteger, true},
          {"aux_event_count", CompatColumnType::kInteger, true},
          {"aux_dur_us", CompatColumnType::kReal, true},
          {"raw_json", CompatColumnType::kText, true},
      },
  };
  return schema;
}

const CompatTableSchema& aux_link_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_aux_link",
      {
          {"anchor_id", CompatColumnType::kText, false},
          {"aux_event_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"aux_order", CompatColumnType::kInteger, false},
          {"aux_step_idx", CompatColumnType::kInteger, false},
          {"link_type", CompatColumnType::kText, false},
          {"reason", CompatColumnType::kText, true},
          {"aux_kind", CompatColumnType::kText, true},
          {"aux_dur_us", CompatColumnType::kReal, true},
          {"raw_json", CompatColumnType::kText, true},
      },
  };
  return schema;
}

const CompatTableSchema& cuda_graph_replay_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_cuda_graph_replay",
      {
          {"graph_event_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"graph_provider", CompatColumnType::kText, true},
          {"graph_kind", CompatColumnType::kText, true},
          {"graph_event_idx", CompatColumnType::kInteger, false},
          {"event_id", CompatColumnType::kText, false},
          {"step_idx", CompatColumnType::kInteger, false},
          {"stream_id", CompatColumnType::kInteger, true},
          {"correlation_id", CompatColumnType::kText, true},
          {"graph_id", CompatColumnType::kText, true},
          {"graph_exec_id", CompatColumnType::kText, true},
          {"context_id", CompatColumnType::kText, true},
          {"start_ns", CompatColumnType::kInteger, true},
          {"end_ns", CompatColumnType::kInteger, true},
          {"dur_us", CompatColumnType::kReal, true},
          {"enclosed_event_count", CompatColumnType::kInteger, true},
          {"enclosed_event_us", CompatColumnType::kReal, true},
          {"enclosed_kernel_count", CompatColumnType::kInteger, true},
          {"enclosed_kernel_us", CompatColumnType::kReal, true},
          {"raw_json", CompatColumnType::kText, true},
      },
  };
  return schema;
}

const CompatTableSchema& cuda_graph_envelope_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_cuda_graph_envelope",
      {
          {"envelope_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"graph_provider", CompatColumnType::kText, true},
          {"graph_kind", CompatColumnType::kText, true},
          {"envelope_idx", CompatColumnType::kInteger, false},
          {"graph_event_id", CompatColumnType::kText, false},
          {"child_event_id", CompatColumnType::kText, false},
          {"graph_step_idx", CompatColumnType::kInteger, false},
          {"child_step_idx", CompatColumnType::kInteger, false},
          {"relation", CompatColumnType::kText, false},
          {"stream_relation", CompatColumnType::kText, true},
          {"graph_id", CompatColumnType::kText, true},
          {"graph_exec_id", CompatColumnType::kText, true},
          {"graph_correlation_id", CompatColumnType::kText, true},
          {"graph_start_ns", CompatColumnType::kInteger, true},
          {"graph_end_ns", CompatColumnType::kInteger, true},
          {"child_start_ns", CompatColumnType::kInteger, true},
          {"child_end_ns", CompatColumnType::kInteger, true},
          {"start_offset_us", CompatColumnType::kReal, true},
          {"end_offset_us", CompatColumnType::kReal, true},
          {"child_dur_us", CompatColumnType::kReal, true},
          {"raw_json", CompatColumnType::kText, true},
      },
  };
  return schema;
}

const CompatTableSchema& aclgraph_reconstruction_region_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_aclgraph_reconstruction_region",
      {
          {"region_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"graph_provider", CompatColumnType::kText, false},
          {"candidate_id", CompatColumnType::kText, false},
          {"region_order", CompatColumnType::kInteger, false},
          {"status", CompatColumnType::kText, false},
          {"boundary_policy", CompatColumnType::kText, false},
          {"order_policy", CompatColumnType::kText, false},
          {"identity_policy", CompatColumnType::kText, false},
          {"shape_policy", CompatColumnType::kText, false},
          {"first_launch_occurrence_id", CompatColumnType::kInteger, false},
          {"last_launch_occurrence_id", CompatColumnType::kInteger, false},
          {"observed_launch_count", CompatColumnType::kInteger, false},
          {"expected_launch_count", CompatColumnType::kInteger, false},
          {"start_ns", CompatColumnType::kInteger, false},
          {"end_ns", CompatColumnType::kInteger, false},
          {"dur_us", CompatColumnType::kReal, false},
          {"raw_json", CompatColumnType::kText, true},
      },
  };
  return schema;
}

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

const CompatTableSchema& collective_global_link_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_collective_global_link",
      {
          {"candidate_collective_key", CompatColumnType::kText, false},
          {"db_name", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"member_id", CompatColumnType::kText, false},
          {"pair_id", CompatColumnType::kText, false},
          {"local_node_id", CompatColumnType::kText, false},
          {"occurrence_idx", CompatColumnType::kInteger, false},
          {"idx_in_occurrence", CompatColumnType::kInteger, false},
          {"op_type", CompatColumnType::kText, false},
          {"anchor_id", CompatColumnType::kText, false},
          {"event_id", CompatColumnType::kText, false},
          {"source_table", CompatColumnType::kText, true},
          {"source_key", CompatColumnType::kText, true},
          {"connection_id", CompatColumnType::kText, true},
          {"op_id", CompatColumnType::kText, true},
          {"start_ns", CompatColumnType::kInteger, true},
          {"end_ns", CompatColumnType::kInteger, true},
          {"dur_us", CompatColumnType::kReal, true},
          {"validation_status", CompatColumnType::kText, true},
          {"confidence", CompatColumnType::kReal, true},
      },
  };
  return schema;
}

const CompatTableSchema& global_collective_summary_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_global_collective_summary",
      {
          {"candidate_collective_key", CompatColumnType::kText, false},
          {"pair_id", CompatColumnType::kText, false},
          {"occurrence_idx", CompatColumnType::kInteger, false},
          {"op_type", CompatColumnType::kText, false},
          {"idx_in_occurrence", CompatColumnType::kInteger, false},
          {"member_count", CompatColumnType::kInteger, false},
          {"expected_world_size", CompatColumnType::kInteger, false},
          {"start_skew_us", CompatColumnType::kReal, true},
          {"duration_skew_us", CompatColumnType::kReal, true},
          {"connection_ids", CompatColumnType::kText, true},
          {"op_ids", CompatColumnType::kText, true},
          {"members", CompatColumnType::kText, true},
          {"missing_members", CompatColumnType::kText, true},
          {"validation_status", CompatColumnType::kText, true},
          {"confidence", CompatColumnType::kReal, true},
      },
  };
  return schema;
}

const CompatTableSchema& global_collective_member_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_global_collective_member",
      {
          {"candidate_collective_key", CompatColumnType::kText, false},
          {"db_name", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"member_id", CompatColumnType::kText, false},
          {"pair_id", CompatColumnType::kText, false},
          {"local_node_id", CompatColumnType::kText, false},
          {"occurrence_idx", CompatColumnType::kInteger, false},
          {"idx_in_occurrence", CompatColumnType::kInteger, false},
          {"op_type", CompatColumnType::kText, false},
          {"anchor_id", CompatColumnType::kText, false},
          {"event_id", CompatColumnType::kText, false},
          {"source_table", CompatColumnType::kText, true},
          {"source_key", CompatColumnType::kText, true},
          {"connection_id", CompatColumnType::kText, true},
          {"op_id", CompatColumnType::kText, true},
          {"start_ns", CompatColumnType::kInteger, true},
          {"end_ns", CompatColumnType::kInteger, true},
          {"dur_us", CompatColumnType::kReal, true},
          {"validation_status", CompatColumnType::kText, true},
          {"confidence", CompatColumnType::kReal, true},
      },
  };
  return schema;
}

const CompatTableSchema& anchor_cost_breakdown_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_anchor_cost_breakdown",
      {
          {"anchor_idx", CompatColumnType::kInteger, false},
          {"symbol", CompatColumnType::kText, false},
          {"anchor_kind", CompatColumnType::kText, false},
          {"total_us", CompatColumnType::kReal, false},
          {"self_us", CompatColumnType::kReal, false},
          {"aux_us", CompatColumnType::kReal, false},
          {"graph_child_us", CompatColumnType::kReal, false},
          {"residual_us", CompatColumnType::kReal, false},
          {"raw_child_task_count", CompatColumnType::kInteger, false},
          {"top_ops", CompatColumnType::kText, false},
          {"diagnostic_flags", CompatColumnType::kText, false},
      },
  };
  return schema;
}

const CompatTableSchema& run_metadata_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_run_metadata",
      {
          {"run_id", CompatColumnType::kText, false},
          {"analysis_status", CompatColumnType::kText, false},
          {"span_start_ns", CompatColumnType::kInteger, true},
          {"span_end_ns", CompatColumnType::kInteger, true},
          {"contract_version", CompatColumnType::kText, false},
          {"semantic_rules_version", CompatColumnType::kText, false},
          {"semantic_rules_sha256", CompatColumnType::kText, false},
          {"attribution_rule_version", CompatColumnType::kText, false},
          {"host_api_rules_version", CompatColumnType::kText, false},
          {"host_api_rules_sha256", CompatColumnType::kText, false},
          {"collection_status", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"source_kind", CompatColumnType::kText, false},
          {"source_path", CompatColumnType::kText, false},
          {"metadata_json", CompatColumnType::kText, false},
      },
  };
  return schema;
}

const CompatTableSchema& device_interval_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_device_interval",
      {
          {"interval_id", CompatColumnType::kText, false},
          {"run_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"interval_order", CompatColumnType::kInteger, false},
          {"start_ns", CompatColumnType::kInteger, false},
          {"end_ns", CompatColumnType::kInteger, false},
          {"duration_ns", CompatColumnType::kInteger, false},
          {"duration_us", CompatColumnType::kReal, false},
          {"interval_kind", CompatColumnType::kText, false},
          {"source_count", CompatColumnType::kInteger, false},
          {"clock_domain", CompatColumnType::kText, false},
          {"contract_version", CompatColumnType::kText, false},
          {"semantic_rules_version", CompatColumnType::kText, false},
          {"attribution_rule_version", CompatColumnType::kText, false},
      },
  };
  return schema;
}

const CompatTableSchema& stream_state_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_stream_state",
      {
          {"state_id", CompatColumnType::kText, false},
          {"run_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"stream_id", CompatColumnType::kInteger, false},
          {"state_order", CompatColumnType::kInteger, false},
          {"start_ns", CompatColumnType::kInteger, false},
          {"end_ns", CompatColumnType::kInteger, false},
          {"duration_ns", CompatColumnType::kInteger, false},
          {"duration_us", CompatColumnType::kReal, false},
          {"state", CompatColumnType::kText, false},
          {"source_count", CompatColumnType::kInteger, false},
          {"stream_universe_kind", CompatColumnType::kText, false},
          {"stream_universe_size", CompatColumnType::kInteger, false},
          {"observed_stream_count", CompatColumnType::kInteger, false},
          {"observed_universe_scan_complete", CompatColumnType::kInteger,
           false},
          {"collection_status", CompatColumnType::kText, false},
          {"clock_domain", CompatColumnType::kText, false},
          {"contract_version", CompatColumnType::kText, false},
          {"semantic_rules_version", CompatColumnType::kText, false},
          {"attribution_rule_version", CompatColumnType::kText, false},
      },
  };
  return schema;
}

const CompatTableSchema& idle_explanation_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_idle_explanation",
      {
          {"idle_explanation_id", CompatColumnType::kText, false},
          {"run_id", CompatColumnType::kText, false},
          {"gap_interval_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"explanation_order", CompatColumnType::kInteger, false},
          {"start_ns", CompatColumnType::kInteger, false},
          {"end_ns", CompatColumnType::kInteger, false},
          {"duration_ns", CompatColumnType::kInteger, false},
          {"duration_us", CompatColumnType::kReal, false},
          {"category", CompatColumnType::kText, false},
          {"evidence_level", CompatColumnType::kText, false},
          {"evidence_relation", CompatColumnType::kText, false},
          {"alignment_status", CompatColumnType::kText, false},
          {"collection_status", CompatColumnType::kText, false},
          {"reason", CompatColumnType::kText, false},
          {"source_count", CompatColumnType::kInteger, false},
          {"clock_domain", CompatColumnType::kText, false},
          {"contract_version", CompatColumnType::kText, false},
          {"semantic_rules_version", CompatColumnType::kText, false},
          {"attribution_rule_version", CompatColumnType::kText, false},
      },
  };
  return schema;
}

const CompatTableSchema& evidence_link_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_evidence_link",
      {
          {"owner_kind", CompatColumnType::kText, false},
          {"owner_id", CompatColumnType::kText, false},
          {"evidence_ordinal", CompatColumnType::kInteger, false},
          {"source_kind", CompatColumnType::kText, false},
          {"source_table", CompatColumnType::kText, false},
          {"source_key", CompatColumnType::kText, false},
          {"relation", CompatColumnType::kText, false},
          {"evidence_level", CompatColumnType::kText, false},
          {"overlap_start_ns", CompatColumnType::kInteger, true},
          {"overlap_end_ns", CompatColumnType::kInteger, true},
          {"stream_id", CompatColumnType::kInteger, true},
          {"state", CompatColumnType::kText, true},
          {"trace_event_id", CompatColumnType::kText, false},
          {"matched_rule_id", CompatColumnType::kText, true},
      },
  };
  return schema;
}

const CompatTableSchema& anchor_idle_explanation_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_anchor_idle_explanation",
      {
          {"anchor_id", CompatColumnType::kText, false},
          {"run_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"anchor_idx", CompatColumnType::kInteger, false},
          {"category", CompatColumnType::kText, false},
          {"evidence_level", CompatColumnType::kText, false},
          {"slice_count", CompatColumnType::kInteger, false},
          {"duration_ns", CompatColumnType::kInteger, false},
          {"duration_us", CompatColumnType::kReal, false},
      },
  };
  return schema;
}

const CompatTableSchema& node_idle_explanation_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_node_idle_explanation",
      {
          {"node_id", CompatColumnType::kText, false},
          {"run_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"view_name", CompatColumnType::kText, false},
          {"category", CompatColumnType::kText, false},
          {"evidence_level", CompatColumnType::kText, false},
          {"slice_count", CompatColumnType::kInteger, false},
          {"duration_ns", CompatColumnType::kInteger, false},
          {"duration_us", CompatColumnType::kReal, false},
      },
  };
  return schema;
}

const CompatTableSchema& graph_launch_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_graph_launch",
      {
          {"launch_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"graph_provider", CompatColumnType::kText, false},
          {"graph_event_id", CompatColumnType::kText, false},
          {"anchor_id", CompatColumnType::kText, true},
          {"replay_unit_id", CompatColumnType::kInteger, false},
          {"graph_template_id", CompatColumnType::kInteger, false},
          {"graph_launch_occurrence_id", CompatColumnType::kInteger, false},
          {"replay_body_template_id", CompatColumnType::kInteger, false},
          {"body_id", CompatColumnType::kInteger, false},
          {"member_order", CompatColumnType::kInteger, false},
          {"slot_order", CompatColumnType::kInteger, true},
          {"correlation_id", CompatColumnType::kText, true},
          {"match_policy", CompatColumnType::kText, true},
          {"association_policy", CompatColumnType::kText, true},
          {"start_ns", CompatColumnType::kInteger, true},
          {"end_ns", CompatColumnType::kInteger, true},
          {"dur_us", CompatColumnType::kReal, true},
          {"evidence_level", CompatColumnType::kText, false},
      },
  };
  return schema;
}

const CompatTableSchema& graph_body_member_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_graph_body_member",
      {
          {"member_id", CompatColumnType::kText, false},
          {"launch_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"graph_provider", CompatColumnType::kText, false},
          {"graph_event_id", CompatColumnType::kText, false},
          {"replay_unit_id", CompatColumnType::kInteger, false},
          {"graph_template_id", CompatColumnType::kInteger, false},
          {"graph_launch_occurrence_id", CompatColumnType::kInteger, false},
          {"body_id", CompatColumnType::kInteger, false},
          {"replay_body_template_id", CompatColumnType::kInteger, false},
          {"member_order", CompatColumnType::kInteger, false},
          {"slot_order", CompatColumnType::kInteger, true},
          {"lane_ordinal", CompatColumnType::kInteger, false},
          {"task_ordinal", CompatColumnType::kInteger, false},
          {"kind", CompatColumnType::kText, false},
          {"event_id", CompatColumnType::kText, false},
          {"task_id", CompatColumnType::kInteger, false},
          {"source_table", CompatColumnType::kText, true},
          {"source_row_id", CompatColumnType::kInteger, true},
          {"raw_task_id", CompatColumnType::kInteger, true},
          {"start_ns", CompatColumnType::kInteger, true},
          {"end_ns", CompatColumnType::kInteger, true},
          {"dur_us", CompatColumnType::kReal, true},
          {"correlation_id", CompatColumnType::kText, true},
          {"graph_node_id", CompatColumnType::kInteger, true},
          {"original_graph_node_id", CompatColumnType::kInteger, true},
          {"match_policy", CompatColumnType::kText, true},
          {"association_policy", CompatColumnType::kText, true},
          {"evidence_level", CompatColumnType::kText, false},
      },
  };
  return schema;
}

const CompatTableSchema& replay_cost_unit_table_schema() {
  static const CompatTableSchema s{
      "traceloom_replay_cost_unit",
      {{"cost_unit_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"replay_unit_id", CompatColumnType::kInteger, false},
       {"graph_template_id", CompatColumnType::kInteger, false},
       {"launch_member_count", CompatColumnType::kInteger, false},
       {"resolved_launch_count", CompatColumnType::kInteger, false},
       {"support_status", CompatColumnType::kText, false},
       {"reason_codes", CompatColumnType::kText, false}}};
  return s;
}
const CompatTableSchema& replay_cost_launch_table_schema() {
  static const CompatTableSchema s{
      "traceloom_replay_cost_launch",
      {{"launch_id", CompatColumnType::kText, false},
       {"cost_unit_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"member_order", CompatColumnType::kInteger, false},
       {"graph_launch_occurrence_id", CompatColumnType::kInteger, false},
       {"composition_slot_id", CompatColumnType::kInteger, false},
       {"slot_role", CompatColumnType::kText, false},
       {"slot_order", CompatColumnType::kInteger, false},
       {"replay_body_template_id", CompatColumnType::kInteger, false},
       {"body_id", CompatColumnType::kInteger, false},
       {"support_status", CompatColumnType::kText, false},
       {"reason_code", CompatColumnType::kText, false},
       {"member_count", CompatColumnType::kInteger, false},
       {"task_sum_ns", CompatColumnType::kInteger, false},
       {"busy_union_ns", CompatColumnType::kInteger, false},
       {"envelope_ns", CompatColumnType::kInteger, false},
       {"compute_ns", CompatColumnType::kInteger, false},
       {"communication_ns", CompatColumnType::kInteger, false},
       {"data_move_ns", CompatColumnType::kInteger, false},
       {"replay_unit_id", CompatColumnType::kInteger, false},
       {"graph_template_id", CompatColumnType::kInteger, false}}};
  return s;
}
const CompatTableSchema& replay_cost_stream_table_schema() {
  static const CompatTableSchema s{
      "traceloom_replay_cost_stream",
      {{"launch_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"stream_id", CompatColumnType::kInteger, false},
       {"lane_ordinal", CompatColumnType::kInteger, false},
       {"lane_consistent", CompatColumnType::kInteger, false},
       {"member_count", CompatColumnType::kInteger, false},
       {"task_sum_ns", CompatColumnType::kInteger, false},
       {"busy_union_ns", CompatColumnType::kInteger, false},
       {"compute_ns", CompatColumnType::kInteger, false},
       {"communication_ns", CompatColumnType::kInteger, false},
       {"data_move_ns", CompatColumnType::kInteger, false}}};
  return s;
}
const CompatTableSchema& replay_cost_member_table_schema() {
  static const CompatTableSchema s{
      "traceloom_replay_cost_member",
      {{"member_id", CompatColumnType::kText, false},
       {"launch_id", CompatColumnType::kText, false},
       {"cost_unit_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"composition_slot_id", CompatColumnType::kInteger, false},
       {"slot_role", CompatColumnType::kText, false},
       {"slot_order", CompatColumnType::kInteger, false},
       {"replay_body_template_id", CompatColumnType::kInteger, false},
       {"body_id", CompatColumnType::kInteger, false},
       {"stream_id", CompatColumnType::kInteger, false},
       {"lane_ordinal", CompatColumnType::kInteger, false},
       {"task_ordinal", CompatColumnType::kInteger, false},
       {"kind", CompatColumnType::kText, false},
       {"event_id", CompatColumnType::kText, false},
       {"identity", CompatColumnType::kText, false},
       {"raw_task_id", CompatColumnType::kInteger, false},
       {"start_ns", CompatColumnType::kInteger, false},
       {"end_ns", CompatColumnType::kInteger, false},
       {"duration_ns", CompatColumnType::kInteger, false},
       {"relative_start_ns", CompatColumnType::kInteger, false},
       {"relative_end_ns", CompatColumnType::kInteger, false},
       {"scheduled_work_share_ppm", CompatColumnType::kInteger, false},
       {"scheduled_work_share_supported", CompatColumnType::kInteger, false},
       {"scheduled_work_denominator_body_task_sum_ns",
        CompatColumnType::kInteger, false}}};
  return s;
}
const CompatTableSchema& replay_cost_aggregate_table_schema() {
  static const CompatTableSchema s{
      "traceloom_replay_cost_aggregate",
      {{"aggregate_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"graph_template_id", CompatColumnType::kInteger, false},
       {"slot_role", CompatColumnType::kText, false},
       {"aggregation_scope", CompatColumnType::kText, false},
       {"replay_body_template_id", CompatColumnType::kInteger, false},
       {"stream_id", CompatColumnType::kInteger, false},
       {"task_ordinal", CompatColumnType::kInteger, false},
       {"identity", CompatColumnType::kText, false},
       {"kind", CompatColumnType::kText, false},
       {"member_occurrence_count", CompatColumnType::kInteger, false},
       {"replay_unit_count", CompatColumnType::kInteger, false},
       {"launch_member_count", CompatColumnType::kInteger, false},
       {"kind_consistent", CompatColumnType::kInteger, false},
       {"lane_consistent", CompatColumnType::kInteger, false},
       {"distribution_supported", CompatColumnType::kInteger, false},
       {"duration_p25_ns", CompatColumnType::kInteger, false},
       {"duration_median_ns", CompatColumnType::kInteger, false},
       {"duration_p75_ns", CompatColumnType::kInteger, false},
       {"scheduled_work_share_ppm", CompatColumnType::kInteger, false},
       {"scheduled_work_share_supported", CompatColumnType::kInteger, false},
       {"scheduled_work_denominator_body_task_sum_ns",
        CompatColumnType::kInteger, false}}};
  return s;
}
const CompatTableSchema& replay_cost_aggregate_member_table_schema() {
  static const CompatTableSchema s{
      "traceloom_replay_cost_aggregate_member",
      {{"aggregate_id", CompatColumnType::kText, false},
       {"member_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"contributor_order", CompatColumnType::kInteger, false}}};
  return s;
}
const CompatTableSchema& replay_cost_issue_table_schema() {
  static const CompatTableSchema s{
      "traceloom_replay_cost_issue",
      {{"issue_id", CompatColumnType::kText, false},
       {"db_idx", CompatColumnType::kInteger, false},
       {"device_id", CompatColumnType::kInteger, false},
       {"code", CompatColumnType::kText, false},
       {"replay_unit_id", CompatColumnType::kInteger, false},
       {"launch_id", CompatColumnType::kText, true},
       {"detail", CompatColumnType::kText, false}}};
  return s;
}

std::vector<CompatTableSchema> compatibility_table_schemas() {
  return {
      metadata_table_schema(),
      event_table_schema(),
      event_source_table_schema(),
      runtime_call_table_schema(),
      device_work_table_schema(),
      runtime_device_relation_table_schema(),
      anchor_runtime_relation_table_schema(),
      anchor_host_interval_table_schema(),
      anchor_host_activity_table_schema(),
      anchor_table_schema(),
      anchor_aux_slot_table_schema(),
      aux_link_table_schema(),
      cuda_graph_replay_table_schema(),
      cuda_graph_envelope_table_schema(),
      aclgraph_reconstruction_region_table_schema(),
      graph_launch_table_schema(),
      graph_body_member_table_schema(),
      replay_cost_unit_table_schema(),
      replay_cost_launch_table_schema(),
      replay_cost_stream_table_schema(),
      replay_cost_member_table_schema(),
      replay_cost_aggregate_table_schema(),
      replay_cost_aggregate_member_table_schema(),
      replay_cost_issue_table_schema(),
      viz_node_table_schema(),
      viz_edge_table_schema(),
      viz_node_anchor_table_schema(),
      anchor_primary_node_table_schema(),
      loop_node_table_schema(),
      semantic_tree_table_schema(),
      semantic_node_table_schema(),
      semantic_edge_table_schema(),
      collective_global_link_table_schema(),
      anchor_cost_breakdown_table_schema(),
      run_metadata_table_schema(),
      device_interval_table_schema(),
      stream_state_table_schema(),
      idle_explanation_table_schema(),
      evidence_link_table_schema(),
      anchor_idle_explanation_table_schema(),
      node_idle_explanation_table_schema(),
  };
}

std::vector<CompatTableSchema> global_collective_table_schemas() {
  return {
      global_collective_summary_table_schema(),
      global_collective_member_table_schema(),
  };
}

std::vector<std::string> column_names(const CompatTableSchema& schema) {
  std::vector<std::string> names;
  names.reserve(schema.columns.size());
  for (const CompatColumnSchema& column : schema.columns) {
    names.push_back(column.name);
  }
  return names;
}

std::string sqlite_create_table_sql(const CompatTableSchema& schema) {
  require_safe_identifier(schema.name, "table");
  if (schema.columns.empty()) {
    throw std::invalid_argument("compatibility table schema has no columns");
  }

  std::string sql = "CREATE TABLE IF NOT EXISTS ";
  sql += schema.name;
  sql += " (";
  for (std::size_t index = 0; index < schema.columns.size(); ++index) {
    const CompatColumnSchema& column = schema.columns[index];
    require_safe_identifier(column.name, "column");
    if (index != 0) {
      sql += ", ";
    }
    sql += column.name;
    sql += " ";
    sql += sqlite_column_type_name(column.type);
    if (!column.nullable) {
      sql += " NOT NULL";
    }
  }
  sql += ")";
  return sql;
}

}  // namespace traceloom::compat
