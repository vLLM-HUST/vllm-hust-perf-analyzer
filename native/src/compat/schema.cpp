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

const CompatTableSchema& anchor_host_api_summary_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_anchor_host_api_summary",
      {
          {"interval_id", CompatColumnType::kText, false},
          {"api_family", CompatColumnType::kText, false},
          {"call_count", CompatColumnType::kInteger, false},
          {"distinct_api_name_count", CompatColumnType::kInteger, false},
          {"scheduled_call_us", CompatColumnType::kReal, false},
          {"scheduled_overlap_us", CompatColumnType::kReal, false},
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

const CompatTableSchema& event_reconciliation_policy_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_event_reconciliation_policy",
      {
          {"policy_id", CompatColumnType::kText, false},
          {"policy_version", CompatColumnType::kText, false},
          {"manifest_schema", CompatColumnType::kText, false},
          {"source_manifest", CompatColumnType::kText, false},
          {"manifest_sha256", CompatColumnType::kText, false},
          {"unmatched_behavior", CompatColumnType::kText, false},
          {"description", CompatColumnType::kText, false},
      },
  };
  return schema;
}

const CompatTableSchema& event_reconciliation_rule_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_event_reconciliation_rule",
      {
          {"policy_id", CompatColumnType::kText, false},
          {"policy_version", CompatColumnType::kText, false},
          {"rule_id", CompatColumnType::kText, false},
          {"priority", CompatColumnType::kInteger, false},
          {"provider_scope", CompatColumnType::kText, false},
          {"source_domain", CompatColumnType::kText, false},
          {"task_type", CompatColumnType::kText, false},
          {"generic_context_id", CompatColumnType::kInteger, false},
          {"concrete_context_id", CompatColumnType::kInteger, false},
          {"min_contained_fraction", CompatColumnType::kReal, false},
          {"rule_origin", CompatColumnType::kText, false},
          {"rule_origin_sha256", CompatColumnType::kText, false},
          {"source_line", CompatColumnType::kInteger, false},
          {"note", CompatColumnType::kText, false},
      },
  };
  return schema;
}

const CompatTableSchema& event_reconciliation_decision_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_event_reconciliation_decision",
      {
          {"decision_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"policy_id", CompatColumnType::kText, false},
          {"policy_version", CompatColumnType::kText, false},
          {"rule_id", CompatColumnType::kText, false},
          {"status", CompatColumnType::kText, false},
          {"reason_code", CompatColumnType::kText, false},
          {"canonical_event_id", CompatColumnType::kText, true},
          {"envelope_event_id", CompatColumnType::kText, true},
          {"canonical_anchor_id", CompatColumnType::kText, true},
          {"canonical_start_ns", CompatColumnType::kInteger, true},
          {"canonical_end_ns", CompatColumnType::kInteger, true},
          {"contained_fraction", CompatColumnType::kReal, true},
          {"member_count", CompatColumnType::kInteger, false},
      },
  };
  return schema;
}

const CompatTableSchema& event_reconciliation_member_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_event_reconciliation_member",
      {
          {"decision_id", CompatColumnType::kText, false},
          {"member_order", CompatColumnType::kInteger, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"event_id", CompatColumnType::kText, false},
          {"task_id", CompatColumnType::kInteger, false},
          {"source_path", CompatColumnType::kText, false},
          {"source_table", CompatColumnType::kText, false},
          {"source_key", CompatColumnType::kText, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"stream_id", CompatColumnType::kInteger, false},
          {"raw_task_id", CompatColumnType::kInteger, false},
          {"raw_global_task_id", CompatColumnType::kInteger, true},
          {"raw_connection_id", CompatColumnType::kInteger, true},
          {"raw_context_id", CompatColumnType::kInteger, true},
          {"member_role", CompatColumnType::kText, false},
          {"contributes_timing", CompatColumnType::kInteger, false},
          {"contributes_symbol", CompatColumnType::kInteger, false},
          {"contributes_cost", CompatColumnType::kInteger, false},
          {"retained_as_normalized_evidence", CompatColumnType::kInteger,
           false},
      },
  };
  return schema;
}

const CompatTableSchema& symbol_normalization_policy_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_symbol_normalization_policy",
      {
          {"policy_id", CompatColumnType::kText, false},
          {"policy_version", CompatColumnType::kText, false},
          {"policy_kind", CompatColumnType::kText, false},
          {"source_manifest", CompatColumnType::kText, false},
          {"manifest_sha256", CompatColumnType::kText, false},
          {"description", CompatColumnType::kText, false},
      },
  };
  return schema;
}

const CompatTableSchema& symbol_normalization_rule_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_symbol_normalization_rule",
      {
          {"policy_id", CompatColumnType::kText, false},
          {"policy_version", CompatColumnType::kText, false},
          {"rule_id", CompatColumnType::kText, false},
          {"precedence", CompatColumnType::kInteger, false},
          {"provider_scope", CompatColumnType::kText, false},
          {"source_domain", CompatColumnType::kText, false},
          {"match_mode", CompatColumnType::kText, false},
          {"match_expression", CompatColumnType::kText, false},
          {"structural_symbol", CompatColumnType::kText, false},
          {"required_fields", CompatColumnType::kText, false},
          {"rule_origin", CompatColumnType::kText, false},
          {"rule_origin_sha256", CompatColumnType::kText, false},
          {"source_line", CompatColumnType::kInteger, false},
          {"description", CompatColumnType::kText, false},
      },
  };
  return schema;
}

const CompatTableSchema& anchor_symbol_normalization_table_schema() {
  static const CompatTableSchema schema{
      "traceloom_anchor_symbol_normalization",
      {
          {"anchor_id", CompatColumnType::kText, false},
          {"db_idx", CompatColumnType::kInteger, false},
          {"device_id", CompatColumnType::kInteger, false},
          {"anchor_idx", CompatColumnType::kInteger, false},
          {"event_id", CompatColumnType::kText, true},
          {"source_path", CompatColumnType::kText, false},
          {"source_table", CompatColumnType::kText, false},
          {"source_key", CompatColumnType::kText, false},
          {"observed_symbol", CompatColumnType::kText, true},
          {"observed_symbol_source", CompatColumnType::kText, false},
          {"structural_symbol", CompatColumnType::kText, true},
          {"policy_id", CompatColumnType::kText, false},
          {"policy_version", CompatColumnType::kText, false},
          {"rule_id", CompatColumnType::kText, false},
          {"candidate_rule_ids", CompatColumnType::kText, true},
          {"outcome", CompatColumnType::kText, false},
          {"reason_code", CompatColumnType::kText, false},
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
      anchor_host_api_summary_table_schema(),
      anchor_table_schema(),
      event_reconciliation_policy_table_schema(),
      event_reconciliation_rule_table_schema(),
      event_reconciliation_decision_table_schema(),
      event_reconciliation_member_table_schema(),
      symbol_normalization_policy_table_schema(),
      symbol_normalization_rule_table_schema(),
      anchor_symbol_normalization_table_schema(),
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
      replay_body_pattern_run_table_schema(),
      replay_body_pattern_domain_table_schema(),
      replay_body_pattern_definition_table_schema(),
      replay_body_pattern_occurrence_table_schema(),
      replay_body_position_table_schema(),
      replay_body_position_refinement_table_schema(),
      replay_body_position_member_table_schema(),
      replay_body_pattern_issue_table_schema(),
      viz_node_table_schema(),
      viz_edge_table_schema(),
      viz_node_anchor_table_schema(),
      anchor_primary_node_table_schema(),
      loop_node_table_schema(),
      semantic_tree_table_schema(),
      semantic_node_table_schema(),
      semantic_edge_table_schema(),
      position_refinement_table_schema(),
      position_occurrence_table_schema(),
      position_member_table_schema(),
      collective_global_link_table_schema(),
      anchor_cost_breakdown_table_schema(),
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
