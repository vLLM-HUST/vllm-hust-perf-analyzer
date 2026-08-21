#pragma once

#include <string>
#include <vector>

namespace traceloom::compat {

enum class CompatColumnType {
  kInteger,
  kReal,
  kText,
};

struct CompatColumnSchema {
  std::string name;
  CompatColumnType type = CompatColumnType::kText;
  bool nullable = false;
};

struct CompatTableSchema {
  std::string name;
  std::vector<CompatColumnSchema> columns;
};

const char* compat_column_type_name(CompatColumnType type);
const char* sqlite_column_type_name(CompatColumnType type);

const CompatTableSchema& metadata_table_schema();
const CompatTableSchema& event_table_schema();
const CompatTableSchema& event_source_table_schema();
const CompatTableSchema& runtime_call_table_schema();
const CompatTableSchema& device_work_table_schema();
const CompatTableSchema& runtime_device_relation_table_schema();
const CompatTableSchema& anchor_runtime_relation_table_schema();
const CompatTableSchema& anchor_host_interval_table_schema();
const CompatTableSchema& anchor_host_activity_table_schema();
const CompatTableSchema& anchor_host_api_summary_table_schema();
const CompatTableSchema& anchor_table_schema();
const CompatTableSchema& event_reconciliation_policy_table_schema();
const CompatTableSchema& event_reconciliation_rule_table_schema();
const CompatTableSchema& event_reconciliation_decision_table_schema();
const CompatTableSchema& event_reconciliation_member_table_schema();
const CompatTableSchema& symbol_normalization_policy_table_schema();
const CompatTableSchema& symbol_normalization_rule_table_schema();
const CompatTableSchema& anchor_symbol_normalization_table_schema();
const CompatTableSchema& anchor_aux_slot_table_schema();
const CompatTableSchema& aux_link_table_schema();
const CompatTableSchema& cuda_graph_replay_table_schema();
const CompatTableSchema& cuda_graph_envelope_table_schema();
const CompatTableSchema& aclgraph_reconstruction_region_table_schema();
const CompatTableSchema& graph_launch_table_schema();
const CompatTableSchema& graph_body_member_table_schema();
const CompatTableSchema& replay_cost_unit_table_schema();
const CompatTableSchema& replay_cost_launch_table_schema();
const CompatTableSchema& replay_cost_stream_table_schema();
const CompatTableSchema& replay_cost_member_table_schema();
const CompatTableSchema& replay_cost_aggregate_table_schema();
const CompatTableSchema& replay_cost_aggregate_member_table_schema();
const CompatTableSchema& replay_cost_issue_table_schema();
const CompatTableSchema& replay_body_pattern_run_table_schema();
const CompatTableSchema& replay_body_pattern_domain_table_schema();
const CompatTableSchema& replay_body_pattern_definition_table_schema();
const CompatTableSchema& replay_body_pattern_occurrence_table_schema();
const CompatTableSchema& replay_body_position_table_schema();
const CompatTableSchema& replay_body_position_refinement_table_schema();
const CompatTableSchema& replay_body_position_member_table_schema();
const CompatTableSchema& replay_body_pattern_issue_table_schema();
const CompatTableSchema& viz_node_table_schema();
const CompatTableSchema& viz_edge_table_schema();
const CompatTableSchema& viz_node_anchor_table_schema();
const CompatTableSchema& anchor_primary_node_table_schema();
const CompatTableSchema& loop_node_table_schema();
const CompatTableSchema& semantic_tree_table_schema();
const CompatTableSchema& semantic_node_table_schema();
const CompatTableSchema& semantic_edge_table_schema();
const CompatTableSchema& position_refinement_table_schema();
const CompatTableSchema& position_occurrence_table_schema();
const CompatTableSchema& position_member_table_schema();
const CompatTableSchema& collective_global_link_table_schema();
const CompatTableSchema& global_collective_summary_table_schema();
const CompatTableSchema& global_collective_member_table_schema();
const CompatTableSchema& anchor_cost_breakdown_table_schema();
std::vector<CompatTableSchema> compatibility_table_schemas();
std::vector<CompatTableSchema> global_collective_table_schemas();

std::vector<std::string> column_names(const CompatTableSchema& schema);
std::string sqlite_create_table_sql(const CompatTableSchema& schema);

}  // namespace traceloom::compat
