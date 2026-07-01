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

std::vector<CompatTableSchema> compatibility_table_schemas() {
  return {
      event_table_schema(),
      anchor_table_schema(),
      anchor_aux_slot_table_schema(),
      aux_link_table_schema(),
      cuda_graph_replay_table_schema(),
      cuda_graph_envelope_table_schema(),
      viz_node_table_schema(),
      viz_edge_table_schema(),
      viz_node_anchor_table_schema(),
      semantic_tree_table_schema(),
      semantic_node_table_schema(),
      anchor_cost_breakdown_table_schema(),
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
